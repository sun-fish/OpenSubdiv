//
//     Copyright (C) Pixar. All rights reserved.
//
//     This license governs use of the accompanying software. If you
//     use the software, you accept this license. If you do not accept
//     the license, do not use the software.
//
//     1. Definitions
//     The terms "reproduce," "reproduction," "derivative works," and
//     "distribution" have the same meaning here as under U.S.
//     copyright law.  A "contribution" is the original software, or
//     any additions or changes to the software.
//     A "contributor" is any person or entity that distributes its
//     contribution under this license.
//     "Licensed patents" are a contributor's patent claims that read
//     directly on its contribution.
//
//     2. Grant of Rights
//     (A) Copyright Grant- Subject to the terms of this license,
//     including the license conditions and limitations in section 3,
//     each contributor grants you a non-exclusive, worldwide,
//     royalty-free copyright license to reproduce its contribution,
//     prepare derivative works of its contribution, and distribute
//     its contribution or any derivative works that you create.
//     (B) Patent Grant- Subject to the terms of this license,
//     including the license conditions and limitations in section 3,
//     each contributor grants you a non-exclusive, worldwide,
//     royalty-free license under its licensed patents to make, have
//     made, use, sell, offer for sale, import, and/or otherwise
//     dispose of its contribution in the software or derivative works
//     of the contribution in the software.
//
//     3. Conditions and Limitations
//     (A) No Trademark License- This license does not grant you
//     rights to use any contributor's name, logo, or trademarks.
//     (B) If you bring a patent claim against any contributor over
//     patents that you claim are infringed by the software, your
//     patent license from such contributor to the software ends
//     automatically.
//     (C) If you distribute any portion of the software, you must
//     retain all copyright, patent, trademark, and attribution
//     notices that are present in the software.
//     (D) If you distribute any portion of the software in source
//     code form, you may do so only under this license by including a
//     complete copy of this license with your distribution. If you
//     distribute any portion of the software in compiled or object
//     code form, you may only do so under a license that complies
//     with this license.
//     (E) The software is licensed "as-is." You bear the risk of
//     using it. The contributors give no express warranties,

#if defined(__APPLE__)
    #include "TargetConditionals.h"
    #if TARGET_OS_IPHONE or TARGET_IPHONE_SIMULATOR
        #include <OpenGLES/ES2/gl.h>
    #else
        #include <OpenGL/gl3.h>
    #endif
#elif defined(ANDROID)
    #include <GLES2/gl2.h>
#else
    #if defined(_WIN32)
        #include <windows.h>
    #endif
    #include <GL/glew.h>
#endif

#include "../far/dispatcher.h"
#include "../far/loopSubdivisionTables.h"
#include "../osd/glDrawRegistry.h"
#include "../osd/glDrawContext.h"

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

OsdGLDrawContext::OsdGLDrawContext() :
    patchIndexBuffer(0), ptexCoordinateTextureBuffer(0), fvarDataTextureBuffer(0),
    vertexTextureBuffer(0), vertexValenceTextureBuffer(0), quadOffsetTextureBuffer(0)
{
}

OsdGLDrawContext::~OsdGLDrawContext()
{
    glDeleteBuffers(1, &patchIndexBuffer);
    glDeleteTextures(1, &vertexTextureBuffer);
    glDeleteTextures(1, &vertexValenceTextureBuffer);
    glDeleteTextures(1, &quadOffsetTextureBuffer);
    glDeleteTextures(1, &ptexCoordinateTextureBuffer);
    glDeleteTextures(1, &fvarDataTextureBuffer);
}

OsdGLDrawContext *
OsdGLDrawContext::Create(FarMesh<OsdVertex> const *farMesh, bool requireFVarData)
{
    FarPatchTables const * patchTables = farMesh->GetPatchTables();
    if (patchTables)
        return Create(patchTables, requireFVarData);

    // XXX: allocateUniform will be retired once uniform patches are
    //      integrated into patcharray.
    OsdGLDrawContext * instance = new OsdGLDrawContext();
    if (instance->allocateUniform(farMesh, requireFVarData))
        return instance;

    delete instance;
    return NULL;
}

OsdGLDrawContext *
OsdGLDrawContext::Create(FarPatchTables const *patchTables, bool requireFVarData)
{
    OsdGLDrawContext * instance = new OsdGLDrawContext();
    if (instance->allocate(patchTables, requireFVarData))
        return instance;

    delete instance;
    return NULL;
}

bool
OsdGLDrawContext::SupportsAdaptiveTessellation()
{
// Compile-time check of GL version
#if (defined(GL_ARB_tessellation_shader) or defined(GL_VERSION_4_0)) and defined(GLEW_VERSION_4_0)
    // Run-time check of GL version with GLEW
    if (GLEW_VERSION_4_0) {
        return true;
    }
#endif
    return false;
}

bool
OsdGLDrawContext::allocateUniform(FarMesh<OsdVertex> const *farMesh,
                                  bool requireFVarData)
{
    _isAdaptive = false;

    // XXX: farmesh should have FarDensePatchTable for dense mesh indices.
    //      instead of GetFaceVertices().
    const FarSubdivisionTables<OsdVertex> *tables = farMesh->GetSubdivisionTables();
    int level = tables->GetMaxLevel();
    const std::vector<int> &indices = farMesh->GetFaceVertices(level-1);

    int numIndices = (int)indices.size();

    // Allocate and fill index buffer.
    glGenBuffers(1, &patchIndexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, patchIndexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 numIndices * sizeof(unsigned int), &(indices[0]), GL_STATIC_DRAW);

#if defined(GL_ES_VERSION_2_0)
    // OpenGLES 2 supports only triangle topologies for filled
    // primitives i.e. not QUADS or PATCHES or LINES_ADJACENCY
    // For the convenience of clients build build a triangles
    // index buffer by splitting quads.
    int numQuads = indices.size() / 4;
    int numTrisIndices = numQuads * 6;

    std::vector<short> trisIndices;
    trisIndices.reserve(numTrisIndices);
    for (int i=0; i<numQuads; ++i) {
        const int * quad = &indices[i*4];
        trisIndices.push_back(short(quad[0]));
        trisIndices.push_back(short(quad[1]));
        trisIndices.push_back(short(quad[2]));

        trisIndices.push_back(short(quad[2]));
        trisIndices.push_back(short(quad[3]));
        trisIndices.push_back(short(quad[0]));
    }

    // Allocate and fill triangles index buffer.
    glGenBuffers(1, &patchTrianglesIndexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, patchTrianglesIndexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 numTrisIndices * sizeof(short), &(trisIndices[0]), GL_STATIC_DRAW);
#endif

/*
        OsdPatchArray array;
        array.desc.type = kNonPatch;
        array.desc.loop = dynamic_cast<const FarLoopSubdivisionTables<OsdVertex>*>(tables) != NULL;
        array.firstIndex = 0;
        array.numIndices = numIndices;

        patchArrays.push_back(array);
*/

    // Allocate ptex coordinate buffer
#if defined(GL_ARB_texture_buffer_object) || defined(GL_VERSION_3_1)
    GLuint ptexCoordinateBuffer = 0;
    glGenTextures(1, &ptexCoordinateTextureBuffer);
    glGenBuffers(1, &ptexCoordinateBuffer);
    glBindBuffer(GL_TEXTURE_BUFFER, ptexCoordinateBuffer);

    const std::vector<FarPtexCoord> &ptexCoordinates =
        farMesh->GetPtexCoordinates(level-1);
    int size = (int)ptexCoordinates.size() * sizeof(FarPtexCoord);

    glBufferData(GL_TEXTURE_BUFFER, size, &(ptexCoordinates[0]), GL_STATIC_DRAW);
        
    glBindTexture(GL_TEXTURE_BUFFER, ptexCoordinateTextureBuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32I, ptexCoordinateBuffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glDeleteBuffers(1, &ptexCoordinateBuffer);

#endif

    // Allocate fvar data buffer if requested (for non-adaptive)
    if (requireFVarData) {
#if defined(GL_ARB_texture_buffer_object) || defined(GL_VERSION_3_1)
        GLuint fvarDataBuffer = 0;
        glGenTextures(1, &fvarDataTextureBuffer);
        glGenBuffers(1, &fvarDataBuffer);
        glBindBuffer(GL_TEXTURE_BUFFER, fvarDataBuffer);

        const std::vector<float> &fvarData = farMesh->GetFVarData(level-1);
        int size = (int)fvarData.size() * sizeof(float);

        glBufferData(GL_TEXTURE_BUFFER, size, &(fvarData[0]), GL_STATIC_DRAW);

        glBindTexture(GL_TEXTURE_BUFFER, fvarDataTextureBuffer);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, fvarDataBuffer);
        glDeleteBuffers(1, &fvarDataBuffer);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
#endif
    }

    return true;
}

template <typename T>
GLuint createTextureBuffer(T const &data, GLint format)
{
    GLuint buffer = 0, texture = 0;

#if defined(GL_ARB_texture_buffer_object) || defined(GL_VERSION_3_1)
    glGenTextures(1, &texture);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(typename T::value_type),
                 &data[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindTexture(GL_TEXTURE_BUFFER, texture);
    glTexBuffer(GL_TEXTURE_BUFFER, format, buffer);
    glDeleteBuffers(1, &buffer);
#endif

    return texture;
}

bool
OsdGLDrawContext::allocate(FarPatchTables const *patchTables, bool requireFVarData)
{
    _isAdaptive = true;

    ConvertPatchArrays(patchTables->GetAllPatchArrays(), patchArrays, patchTables->GetMaxValence(), 0);

    FarPatchTables::PTable const & ptables = patchTables->GetPatchTable();

    // Allocate and fill index buffer.
    glGenBuffers(1, &patchIndexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, patchIndexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 ptables.size() * sizeof(unsigned int), &ptables[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    FarPatchTables::PtexCoordinateTable const &
        ptexCoordTables = patchTables->GetPtexCoordinatesTable();

    // create ptex coordinate buffer
    if (not ptexCoordTables.empty())
        ptexCoordinateTextureBuffer = createTextureBuffer(ptexCoordTables, GL_RG32I);

    FarPatchTables::FVarDataTable const &
        fvarTables = patchTables->GetFVarDataTable();

    // create fvar data buffer if requested
    if (requireFVarData and not fvarTables.empty())
        fvarDataTextureBuffer = createTextureBuffer(fvarTables, GL_R32F);

    // allocate and initialize additional buffer data

    FarPatchTables::VertexValenceTable const &
        valenceTable = patchTables->GetVertexValenceTable();

    // create vertex valence buffer and vertex texture
    if (not valenceTable.empty()) {
        vertexValenceTextureBuffer = createTextureBuffer(valenceTable, GL_R32I);
        
        // also create vertex texture buffer (will be updated in UpdateVertexTexture())
        glGenTextures(1, &vertexTextureBuffer);
    }

    // create quad offset table buffer
    FarPatchTables::QuadOffsetTable const &
        quadOffsetTable = patchTables->GetQuadOffsetTable();

    if (not quadOffsetTable.empty())
        quadOffsetTextureBuffer = createTextureBuffer(quadOffsetTable, GL_R32I);

    glBindBuffer(GL_TEXTURE_BUFFER, 0);

    return true;
}

void
OsdGLDrawContext::updateVertexTexture(GLuint vbo, int numVertexElements)
{
    glBindTexture(GL_TEXTURE_BUFFER, vertexTextureBuffer);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, vbo);
    glBindTexture(GL_TEXTURE_BUFFER, 0);

    // XXX: consider moving this proc to base class
    // updating num elements in descriptor with new vbo specs
    for (int i = 0; i < (int)patchArrays.size(); ++i) {
        PatchArray &parray = patchArrays[i];
        PatchDescriptor desc = parray.GetDescriptor();
        desc.SetNumElements(numVertexElements);
        parray.SetDescriptor(desc);
    }
}


} // end namespace OPENSUBDIV_VERSION
} // end namespace OpenSubdiv
