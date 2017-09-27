/**********************************************************
 * Copyright 2012-2015 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

/*
 * svga3d_dx.h --
 *
 *       SVGA 3d hardware definitions for DX10 support.
 */

#ifndef _SVGA3D_DX_H_
#define _SVGA3D_DX_H_

#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_VMCORE
#include "includeCheck.h"

#include "svga3d_limits.h"

#define SVGA3D_INPUT_MIN               0
#define SVGA3D_INPUT_PER_VERTEX_DATA   0
#define SVGA3D_INPUT_PER_INSTANCE_DATA 1
#define SVGA3D_INPUT_MAX               2
typedef uint32 SVGA3dInputClassification;

#define SVGA3D_RESOURCE_TYPE_MIN      1
#define SVGA3D_RESOURCE_BUFFER        1
#define SVGA3D_RESOURCE_TEXTURE1D     2
#define SVGA3D_RESOURCE_TEXTURE2D     3
#define SVGA3D_RESOURCE_TEXTURE3D     4
#define SVGA3D_RESOURCE_TEXTURECUBE   5
#define SVGA3D_RESOURCE_TYPE_DX10_MAX 6
#define SVGA3D_RESOURCE_BUFFEREX      6
#define SVGA3D_RESOURCE_TYPE_MAX      7
typedef uint32 SVGA3dResourceType;

#define SVGA3D_DEPTH_WRITE_MASK_ZERO   0
#define SVGA3D_DEPTH_WRITE_MASK_ALL    1
typedef uint8 SVGA3dDepthWriteMask;

#define SVGA3D_FILTER_MIP_LINEAR  (1 << 0)
#define SVGA3D_FILTER_MAG_LINEAR  (1 << 2)
#define SVGA3D_FILTER_MIN_LINEAR  (1 << 4)
#define SVGA3D_FILTER_ANISOTROPIC (1 << 6)
#define SVGA3D_FILTER_COMPARE     (1 << 7)
typedef uint32 SVGA3dFilter;

#define SVGA3D_CULL_INVALID 0
#define SVGA3D_CULL_MIN     1
#define SVGA3D_CULL_NONE    1
#define SVGA3D_CULL_FRONT   2
#define SVGA3D_CULL_BACK    3
#define SVGA3D_CULL_MAX     4
typedef uint8 SVGA3dCullMode;

#define SVGA3D_COMPARISON_INVALID         0
#define SVGA3D_COMPARISON_MIN             1
#define SVGA3D_COMPARISON_NEVER           1
#define SVGA3D_COMPARISON_LESS            2
#define SVGA3D_COMPARISON_EQUAL           3
#define SVGA3D_COMPARISON_LESS_EQUAL      4
#define SVGA3D_COMPARISON_GREATER         5
#define SVGA3D_COMPARISON_NOT_EQUAL       6
#define SVGA3D_COMPARISON_GREATER_EQUAL   7
#define SVGA3D_COMPARISON_ALWAYS          8
#define SVGA3D_COMPARISON_MAX             9
typedef uint8 SVGA3dComparisonFunc;

#define SVGA3D_DX_MAX_VERTEXBUFFERS 32
#define SVGA3D_DX_MAX_SOTARGETS 4
#define SVGA3D_DX_MAX_SRVIEWS 128
#define SVGA3D_DX_MAX_CONSTBUFFERS 16
#define SVGA3D_DX_MAX_SAMPLERS 16

/* Id limits */
static const uint32 SVGA3dBlendObjectCountPerContext = 4096;
static const uint32 SVGA3dDepthStencilObjectCountPerContext = 4096;

typedef uint32 SVGA3dSurfaceId;
typedef uint32 SVGA3dShaderResourceViewId;
typedef uint32 SVGA3dRenderTargetViewId;
typedef uint32 SVGA3dDepthStencilViewId;

typedef uint32 SVGA3dShaderId;
typedef uint32 SVGA3dElementLayoutId;
typedef uint32 SVGA3dSamplerId;
typedef uint32 SVGA3dBlendStateId;
typedef uint32 SVGA3dDepthStencilStateId;
typedef uint32 SVGA3dRasterizerStateId;
typedef uint32 SVGA3dQueryId;
typedef uint32 SVGA3dStreamOutputId;

typedef union {
   struct {
      float r;
      float g;
      float b;
      float a;
   };

   float value[4];
} SVGA3dRGBAFloat;

typedef
#include "vmware_pack_begin.h"
struct {
   uint32 cid;
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGAOTableDXContextEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineContext {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineContext;   /* SVGA_3D_CMD_DX_DEFINE_CONTEXT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyContext {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyContext;   /* SVGA_3D_CMD_DX_DESTROY_CONTEXT */

/*
 * Bind a DX context.
 *
 * validContents should be set to 0 for new contexts,
 * and 1 if this is an old context which is getting paged
 * back on to the device.
 *
 * For new contexts, it is recommended that the driver
 * issue commands to initialize all interesting state
 * prior to rendering.
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBindContext {
   uint32 cid;
   SVGAMobId mobid;
   uint32 validContents;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBindContext;   /* SVGA_3D_CMD_DX_BIND_CONTEXT */

/*
 * Readback a DX context.
 * (Request that the device flush the contents back into guest memory.)
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXReadbackContext {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXReadbackContext;   /* SVGA_3D_CMD_DX_READBACK_CONTEXT */

/*
 * Invalidate a guest-backed context.
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXInvalidateContext {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXInvalidateContext;   /* SVGA_3D_CMD_DX_INVALIDATE_CONTEXT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dReplyFormatData {
   uint32 formatSupport;
   uint32 msaa2xQualityLevels:5;
   uint32 msaa4xQualityLevels:5;
   uint32 msaa8xQualityLevels:5;
   uint32 msaa16xQualityLevels:5;
   uint32 msaa32xQualityLevels:5;
   uint32 pad:7;
}
#include "vmware_pack_end.h"
SVGA3dReplyFormatData;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetSingleConstantBuffer {
   uint32 slot;
   SVGA3dShaderType type;
   SVGA3dSurfaceId sid;
   uint32 offsetInBytes;
   uint32 sizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetSingleConstantBuffer;
/* SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetShaderResources {
   uint32 startView;
   SVGA3dShaderType type;

   /*
    * Followed by a variable number of SVGA3dShaderResourceViewId's.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetShaderResources; /* SVGA_3D_CMD_DX_SET_SHADER_RESOURCES */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetShader {
   SVGA3dShaderId shaderId;
   SVGA3dShaderType type;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetShader; /* SVGA_3D_CMD_DX_SET_SHADER */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetSamplers {
   uint32 startSampler;
   SVGA3dShaderType type;

   /*
    * Followed by a variable number of SVGA3dSamplerId's.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetSamplers; /* SVGA_3D_CMD_DX_SET_SAMPLERS */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDraw {
   uint32 vertexCount;
   uint32 startVertexLocation;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDraw; /* SVGA_3D_CMD_DX_DRAW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDrawIndexed {
   uint32 indexCount;
   uint32 startIndexLocation;
   int32  baseVertexLocation;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDrawIndexed; /* SVGA_3D_CMD_DX_DRAW_INDEXED */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDrawInstanced {
   uint32 vertexCountPerInstance;
   uint32 instanceCount;
   uint32 startVertexLocation;
   uint32 startInstanceLocation;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDrawInstanced; /* SVGA_3D_CMD_DX_DRAW_INSTANCED */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDrawIndexedInstanced {
   uint32 indexCountPerInstance;
   uint32 instanceCount;
   uint32 startIndexLocation;
   int32  baseVertexLocation;
   uint32 startInstanceLocation;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDrawIndexedInstanced; /* SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDrawAuto {
   uint32 pad0;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDrawAuto; /* SVGA_3D_CMD_DX_DRAW_AUTO */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetInputLayout {
   SVGA3dElementLayoutId elementLayoutId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetInputLayout; /* SVGA_3D_CMD_DX_SET_INPUT_LAYOUT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dVertexBuffer {
   SVGA3dSurfaceId sid;
   uint32 stride;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGA3dVertexBuffer;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetVertexBuffers {
   uint32 startBuffer;
   /* Followed by a variable number of SVGA3dVertexBuffer's. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetVertexBuffers; /* SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetIndexBuffer {
   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetIndexBuffer; /* SVGA_3D_CMD_DX_SET_INDEX_BUFFER */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetTopology {
   SVGA3dPrimitiveType topology;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetTopology; /* SVGA_3D_CMD_DX_SET_TOPOLOGY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetRenderTargets {
   SVGA3dDepthStencilViewId depthStencilViewId;
   /* Followed by a variable number of SVGA3dRenderTargetViewId's. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetRenderTargets; /* SVGA_3D_CMD_DX_SET_RENDERTARGETS */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetBlendState {
   SVGA3dBlendStateId blendId;
   float blendFactor[4];
   uint32 sampleMask;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetBlendState; /* SVGA_3D_CMD_DX_SET_BLEND_STATE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetDepthStencilState {
   SVGA3dDepthStencilStateId depthStencilId;
   uint32 stencilRef;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetDepthStencilState; /* SVGA_3D_CMD_DX_SET_DEPTHSTENCIL_STATE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetRasterizerState {
   SVGA3dRasterizerStateId rasterizerId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetRasterizerState; /* SVGA_3D_CMD_DX_SET_RASTERIZER_STATE */

#define SVGA3D_DXQUERY_FLAG_PREDICATEHINT (1 << 0)
typedef uint32 SVGA3dDXQueryFlags;

/*
 * The SVGADXQueryDeviceState and SVGADXQueryDeviceBits are used by the device
 * to track query state transitions, but are not intended to be used by the
 * driver.
 */
#define SVGADX_QDSTATE_INVALID   ((uint8)-1) /* Query has no state */
#define SVGADX_QDSTATE_MIN       0
#define SVGADX_QDSTATE_IDLE      0   /* Query hasn't started yet */
#define SVGADX_QDSTATE_ACTIVE    1   /* Query is actively gathering data */
#define SVGADX_QDSTATE_PENDING   2   /* Query is waiting for results */
#define SVGADX_QDSTATE_FINISHED  3   /* Query has completed */
#define SVGADX_QDSTATE_MAX       4
typedef uint8 SVGADXQueryDeviceState;

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dQueryTypeUint8 type;
   uint16 pad0;
   SVGADXQueryDeviceState state;
   SVGA3dDXQueryFlags flags;
   SVGAMobId mobid;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGACOTableDXQueryEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineQuery {
   SVGA3dQueryId queryId;
   SVGA3dQueryType type;
   SVGA3dDXQueryFlags flags;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineQuery; /* SVGA_3D_CMD_DX_DEFINE_QUERY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyQuery {
   SVGA3dQueryId queryId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyQuery; /* SVGA_3D_CMD_DX_DESTROY_QUERY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBindQuery {
   SVGA3dQueryId queryId;
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBindQuery; /* SVGA_3D_CMD_DX_BIND_QUERY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetQueryOffset {
   SVGA3dQueryId queryId;
   uint32 mobOffset;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetQueryOffset; /* SVGA_3D_CMD_DX_SET_QUERY_OFFSET */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBeginQuery {
   SVGA3dQueryId queryId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBeginQuery; /* SVGA_3D_CMD_DX_QUERY_BEGIN */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXEndQuery {
   SVGA3dQueryId queryId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXEndQuery; /* SVGA_3D_CMD_DX_QUERY_END */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXReadbackQuery {
   SVGA3dQueryId queryId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXReadbackQuery; /* SVGA_3D_CMD_DX_READBACK_QUERY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXMoveQuery {
   SVGA3dQueryId queryId;
   SVGAMobId mobid;
   uint32 mobOffset;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXMoveQuery; /* SVGA_3D_CMD_DX_MOVE_QUERY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBindAllQuery {
   uint32 cid;
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBindAllQuery; /* SVGA_3D_CMD_DX_BIND_ALL_QUERY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXReadbackAllQuery {
   uint32 cid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXReadbackAllQuery; /* SVGA_3D_CMD_DX_READBACK_ALL_QUERY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetPredication {
   SVGA3dQueryId queryId;
   uint32 predicateValue;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetPredication; /* SVGA_3D_CMD_DX_SET_PREDICATION */

typedef
#include "vmware_pack_begin.h"
struct MKS3dDXSOState {
   uint32 offset;       /* Starting offset */
   uint32 intOffset;    /* Internal offset */
   uint32 vertexCount;  /* vertices written */
   uint32 sizeInBytes;  /* max bytes to write */
}
#include "vmware_pack_end.h"
SVGA3dDXSOState;

/* Set the offset field to this value to append SO values to the buffer */
#define SVGA3D_DX_SO_OFFSET_APPEND ((uint32) ~0u)

typedef
#include "vmware_pack_begin.h"
struct SVGA3dSoTarget {
   SVGA3dSurfaceId sid;
   uint32 offset;
   uint32 sizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dSoTarget;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetSOTargets {
   uint32 pad0;
   /* Followed by a variable number of SVGA3dSOTarget's. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetSOTargets; /* SVGA_3D_CMD_DX_SET_SOTARGETS */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dViewport
{
   float x;
   float y;
   float width;
   float height;
   float minDepth;
   float maxDepth;
}
#include "vmware_pack_end.h"
SVGA3dViewport;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetViewports {
   uint32 pad0;
   /* Followed by a variable number of SVGA3dViewport's. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetViewports; /* SVGA_3D_CMD_DX_SET_VIEWPORTS */

#define SVGA3D_DX_MAX_VIEWPORTS  16

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetScissorRects {
   uint32 pad0;
   /* Followed by a variable number of SVGASignedRect's. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetScissorRects; /* SVGA_3D_CMD_DX_SET_SCISSORRECTS */

#define SVGA3D_DX_MAX_SCISSORRECTS  16

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXClearRenderTargetView {
   SVGA3dRenderTargetViewId renderTargetViewId;
   SVGA3dRGBAFloat rgba;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXClearRenderTargetView; /* SVGA_3D_CMD_DX_CLEAR_RENDERTARGET_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXClearDepthStencilView {
   uint16 flags;
   uint16 stencil;
   SVGA3dDepthStencilViewId depthStencilViewId;
   float depth;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXClearDepthStencilView; /* SVGA_3D_CMD_DX_CLEAR_DEPTHSTENCIL_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXPredCopyRegion {
   SVGA3dSurfaceId dstSid;
   uint32 dstSubResource;
   SVGA3dSurfaceId srcSid;
   uint32 srcSubResource;
   SVGA3dCopyBox box;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXPredCopyRegion;
/* SVGA_3D_CMD_DX_PRED_COPY_REGION */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXPredCopy {
   SVGA3dSurfaceId dstSid;
   SVGA3dSurfaceId srcSid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXPredCopy; /* SVGA_3D_CMD_DX_PRED_COPY */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBufferCopy {
   SVGA3dSurfaceId dest;
   SVGA3dSurfaceId src;
   uint32 destX;
   uint32 srcX;
   uint32 width;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBufferCopy;
/* SVGA_3D_CMD_DX_BUFFER_COPY */

typedef uint32 SVGA3dDXStretchBltMode;
#define SVGADX_STRETCHBLT_LINEAR         (1 << 0)
#define SVGADX_STRETCHBLT_FORCE_SRC_SRGB (1 << 1)

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXStretchBlt {
   SVGA3dSurfaceId srcSid;
   uint32 srcSubResource;
   SVGA3dSurfaceId dstSid;
   uint32 destSubResource;
   SVGA3dBox boxSrc;
   SVGA3dBox boxDest;
   SVGA3dDXStretchBltMode mode;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXStretchBlt; /* SVGA_3D_CMD_DX_STRETCHBLT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXGenMips {
   SVGA3dShaderResourceViewId shaderResourceViewId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXGenMips; /* SVGA_3D_CMD_DX_GENMIPS */

/*
 * Defines a resource/DX surface.  Resources share the surfaceId namespace.
 *
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDefineGBSurface_v2 {
   uint32 sid;
   SVGA3dSurfaceFlags surfaceFlags;
   SVGA3dSurfaceFormat format;
   uint32 numMipLevels;
   uint32 multisampleCount;
   SVGA3dTextureFilter autogenFilter;
   SVGA3dSize size;
   uint32 arraySize;
   uint32 pad;
}
#include "vmware_pack_end.h"
SVGA3dCmdDefineGBSurface_v2;   /* SVGA_3D_CMD_DEFINE_GB_SURFACE_V2 */

/*
 * Update a sub-resource in a guest-backed resource.
 * (Inform the device that the guest-contents have been updated.)
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXUpdateSubResource {
   SVGA3dSurfaceId sid;
   uint32 subResource;
   SVGA3dBox box;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXUpdateSubResource;   /* SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE */

/*
 * Readback a subresource in a guest-backed resource.
 * (Request the device to flush the dirty contents into the guest.)
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXReadbackSubResource {
   SVGA3dSurfaceId sid;
   uint32 subResource;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXReadbackSubResource;   /* SVGA_3D_CMD_DX_READBACK_SUBRESOURCE */

/*
 * Invalidate an image in a guest-backed surface.
 * (Notify the device that the contents can be lost.)
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXInvalidateSubResource {
   SVGA3dSurfaceId sid;
   uint32 subResource;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXInvalidateSubResource;   /* SVGA_3D_CMD_DX_INVALIDATE_SUBRESOURCE */


/*
 * Raw byte wise transfer from a buffer surface into another surface
 * of the requested box.
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXTransferFromBuffer {
   SVGA3dSurfaceId srcSid;
   uint32 srcOffset;
   uint32 srcPitch;
   uint32 srcSlicePitch;
   SVGA3dSurfaceId destSid;
   uint32 destSubResource;
   SVGA3dBox destBox;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXTransferFromBuffer;   /* SVGA_3D_CMD_DX_TRANSFER_FROM_BUFFER */


/*
 * Raw byte wise transfer from a buffer surface into another surface
 * of the requested box.  Supported if SVGA3D_DEVCAP_DXCONTEXT is set.
 * The context is implied from the command buffer header.
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXPredTransferFromBuffer {
   SVGA3dSurfaceId srcSid;
   uint32 srcOffset;
   uint32 srcPitch;
   uint32 srcSlicePitch;
   SVGA3dSurfaceId destSid;
   uint32 destSubResource;
   SVGA3dBox destBox;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXPredTransferFromBuffer;
/* SVGA_3D_CMD_DX_PRED_TRANSFER_FROM_BUFFER */


typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSurfaceCopyAndReadback {
   SVGA3dSurfaceId srcSid;
   SVGA3dSurfaceId destSid;
   SVGA3dCopyBox box;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSurfaceCopyAndReadback;
/* SVGA_3D_CMD_DX_SURFACE_COPY_AND_READBACK */


typedef
#include "vmware_pack_begin.h"
struct {
   union {
      struct {
         uint32 firstElement;
         uint32 numElements;
         uint32 pad0;
         uint32 pad1;
      } buffer;
      struct {
         uint32 mostDetailedMip;
         uint32 firstArraySlice;
         uint32 mipLevels;
         uint32 arraySize;
      } tex;
      struct {
         uint32 firstElement;
         uint32 numElements;
         uint32 flags;
         uint32 pad0;
      } bufferex;
   };
}
#include "vmware_pack_end.h"
SVGA3dShaderResourceViewDesc;

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;
   SVGA3dShaderResourceViewDesc desc;
   uint32 pad;
}
#include "vmware_pack_end.h"
SVGACOTableDXSRViewEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineShaderResourceView {
   SVGA3dShaderResourceViewId shaderResourceViewId;

   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;

   SVGA3dShaderResourceViewDesc desc;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineShaderResourceView;
/* SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyShaderResourceView {
   SVGA3dShaderResourceViewId shaderResourceViewId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyShaderResourceView;
/* SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dRenderTargetViewDesc {
   union {
      struct {
         uint32 firstElement;
         uint32 numElements;
      } buffer;
      struct {
         uint32 mipSlice;
         uint32 firstArraySlice;
         uint32 arraySize;
      } tex;                    /* 1d, 2d, cube */
      struct {
         uint32 mipSlice;
         uint32 firstW;
         uint32 wSize;
      } tex3D;
   };
}
#include "vmware_pack_end.h"
SVGA3dRenderTargetViewDesc;

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;
   SVGA3dRenderTargetViewDesc desc;
   uint32 pad[2];
}
#include "vmware_pack_end.h"
SVGACOTableDXRTViewEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineRenderTargetView {
   SVGA3dRenderTargetViewId renderTargetViewId;

   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;

   SVGA3dRenderTargetViewDesc desc;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineRenderTargetView;
/* SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyRenderTargetView {
   SVGA3dRenderTargetViewId renderTargetViewId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyRenderTargetView;
/* SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW */

/*
 */
#define SVGA3D_DXDSVIEW_CREATE_READ_ONLY_DEPTH   0x01
#define SVGA3D_DXDSVIEW_CREATE_READ_ONLY_STENCIL 0x02
#define SVGA3D_DXDSVIEW_CREATE_FLAG_MASK         0x03
typedef uint8 SVGA3DCreateDSViewFlags;

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;
   uint32 mipSlice;
   uint32 firstArraySlice;
   uint32 arraySize;
   SVGA3DCreateDSViewFlags flags;
   uint8 pad0;
   uint16 pad1;
   uint32 pad2;
}
#include "vmware_pack_end.h"
SVGACOTableDXDSViewEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineDepthStencilView {
   SVGA3dDepthStencilViewId depthStencilViewId;

   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;
   uint32 mipSlice;
   uint32 firstArraySlice;
   uint32 arraySize;
   SVGA3DCreateDSViewFlags flags;
   uint8 pad0;
   uint16 pad1;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineDepthStencilView;
/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyDepthStencilView {
   SVGA3dDepthStencilViewId depthStencilViewId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyDepthStencilView;
/* SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dInputElementDesc {
   uint32 inputSlot;
   uint32 alignedByteOffset;
   SVGA3dSurfaceFormat format;
   SVGA3dInputClassification inputSlotClass;
   uint32 instanceDataStepRate;
   uint32 inputRegister;
}
#include "vmware_pack_end.h"
SVGA3dInputElementDesc;

typedef
#include "vmware_pack_begin.h"
struct {
   /*
    * XXX: How many of these can there be?
    */
   uint32 elid;
   uint32 numDescs;
   SVGA3dInputElementDesc desc[32];
   uint32 pad[62];
}
#include "vmware_pack_end.h"
SVGACOTableDXElementLayoutEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineElementLayout {
   SVGA3dElementLayoutId elementLayoutId;
   /* Followed by a variable number of SVGA3dInputElementDesc's. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineElementLayout;
/* SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyElementLayout {
   SVGA3dElementLayoutId elementLayoutId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyElementLayout;
/* SVGA_3D_CMD_DX_DESTROY_ELEMENTLAYOUT */


#define SVGA3D_DX_MAX_RENDER_TARGETS 8

typedef
#include "vmware_pack_begin.h"
struct SVGA3dDXBlendStatePerRT {
      uint8 blendEnable;
      uint8 srcBlend;
      uint8 destBlend;
      uint8 blendOp;
      uint8 srcBlendAlpha;
      uint8 destBlendAlpha;
      uint8 blendOpAlpha;
      uint8 renderTargetWriteMask;
      uint8 logicOpEnable;
      uint8 logicOp;
      uint16 pad0;
}
#include "vmware_pack_end.h"
SVGA3dDXBlendStatePerRT;

typedef
#include "vmware_pack_begin.h"
struct {
   uint8 alphaToCoverageEnable;
   uint8 independentBlendEnable;
   uint16 pad0;
   SVGA3dDXBlendStatePerRT perRT[SVGA3D_MAX_RENDER_TARGETS];
   uint32 pad1[7];
}
#include "vmware_pack_end.h"
SVGACOTableDXBlendStateEntry;

/*
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineBlendState {
   SVGA3dBlendStateId blendId;
   uint8 alphaToCoverageEnable;
   uint8 independentBlendEnable;
   uint16 pad0;
   SVGA3dDXBlendStatePerRT perRT[SVGA3D_MAX_RENDER_TARGETS];
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineBlendState; /* SVGA_3D_CMD_DX_DEFINE_BLEND_STATE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyBlendState {
   SVGA3dBlendStateId blendId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyBlendState; /* SVGA_3D_CMD_DX_DESTROY_BLEND_STATE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint8 depthEnable;
   SVGA3dDepthWriteMask depthWriteMask;
   SVGA3dComparisonFunc depthFunc;
   uint8 stencilEnable;
   uint8 frontEnable;
   uint8 backEnable;
   uint8 stencilReadMask;
   uint8 stencilWriteMask;

   uint8 frontStencilFailOp;
   uint8 frontStencilDepthFailOp;
   uint8 frontStencilPassOp;
   SVGA3dComparisonFunc frontStencilFunc;

   uint8 backStencilFailOp;
   uint8 backStencilDepthFailOp;
   uint8 backStencilPassOp;
   SVGA3dComparisonFunc backStencilFunc;
}
#include "vmware_pack_end.h"
SVGACOTableDXDepthStencilEntry;

/*
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineDepthStencilState {
   SVGA3dDepthStencilStateId depthStencilId;

   uint8 depthEnable;
   SVGA3dDepthWriteMask depthWriteMask;
   SVGA3dComparisonFunc depthFunc;
   uint8 stencilEnable;
   uint8 frontEnable;
   uint8 backEnable;
   uint8 stencilReadMask;
   uint8 stencilWriteMask;

   uint8 frontStencilFailOp;
   uint8 frontStencilDepthFailOp;
   uint8 frontStencilPassOp;
   SVGA3dComparisonFunc frontStencilFunc;

   uint8 backStencilFailOp;
   uint8 backStencilDepthFailOp;
   uint8 backStencilPassOp;
   SVGA3dComparisonFunc backStencilFunc;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineDepthStencilState;
/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyDepthStencilState {
   SVGA3dDepthStencilStateId depthStencilId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyDepthStencilState;
/* SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_STATE */

typedef
#include "vmware_pack_begin.h"
struct {
   uint8 fillMode;
   SVGA3dCullMode cullMode;
   uint8 frontCounterClockwise;
   uint8 provokingVertexLast;
   int32 depthBias;
   float depthBiasClamp;
   float slopeScaledDepthBias;
   uint8 depthClipEnable;
   uint8 scissorEnable;
   uint8 multisampleEnable;
   uint8 antialiasedLineEnable;
   float lineWidth;
   uint8 lineStippleEnable;
   uint8 lineStippleFactor;
   uint16 lineStipplePattern;
   uint32 forcedSampleCount;
}
#include "vmware_pack_end.h"
SVGACOTableDXRasterizerStateEntry;

/*
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineRasterizerState {
   SVGA3dRasterizerStateId rasterizerId;

   uint8 fillMode;
   SVGA3dCullMode cullMode;
   uint8 frontCounterClockwise;
   uint8 provokingVertexLast;
   int32 depthBias;
   float depthBiasClamp;
   float slopeScaledDepthBias;
   uint8 depthClipEnable;
   uint8 scissorEnable;
   uint8 multisampleEnable;
   uint8 antialiasedLineEnable;
   float lineWidth;
   uint8 lineStippleEnable;
   uint8 lineStippleFactor;
   uint16 lineStipplePattern;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineRasterizerState;
/* SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyRasterizerState {
   SVGA3dRasterizerStateId rasterizerId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyRasterizerState;
/* SVGA_3D_CMD_DX_DESTROY_RASTERIZER_STATE */

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dFilter filter;
   uint8 addressU;
   uint8 addressV;
   uint8 addressW;
   uint8 pad0;
   float mipLODBias;
   uint8 maxAnisotropy;
   SVGA3dComparisonFunc comparisonFunc;
   uint16 pad1;
   SVGA3dRGBAFloat borderColor;
   float minLOD;
   float maxLOD;
   uint32 pad2[6];
}
#include "vmware_pack_end.h"
SVGACOTableDXSamplerEntry;

/*
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineSamplerState {
   SVGA3dSamplerId samplerId;
   SVGA3dFilter filter;
   uint8 addressU;
   uint8 addressV;
   uint8 addressW;
   uint8 pad0;
   float mipLODBias;
   uint8 maxAnisotropy;
   SVGA3dComparisonFunc comparisonFunc;
   uint16 pad1;
   SVGA3dRGBAFloat borderColor;
   float minLOD;
   float maxLOD;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineSamplerState; /* SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroySamplerState {
   SVGA3dSamplerId samplerId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroySamplerState; /* SVGA_3D_CMD_DX_DESTROY_SAMPLER_STATE */

/*
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dSignatureEntry {
   uint8 systemValue;
   uint8 reg;                 /* register is a reserved word */
   uint16 mask;
   uint8 registerComponentType;
   uint8 minPrecision;
   uint16 pad0;
}
#include "vmware_pack_end.h"
SVGA3dSignatureEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineShader {
   SVGA3dShaderId shaderId;
   SVGA3dShaderType type;
   uint32 sizeInBytes; /* Number of bytes of shader text. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineShader; /* SVGA_3D_CMD_DX_DEFINE_SHADER */

typedef
#include "vmware_pack_begin.h"
struct SVGACOTableDXShaderEntry {
   SVGA3dShaderType type;
   uint32 sizeInBytes;
   uint32 offsetInBytes;
   SVGAMobId mobid;
   uint32 numInputSignatureEntries;
   uint32 numOutputSignatureEntries;

   uint32 numPatchConstantSignatureEntries;

   uint32 pad;
}
#include "vmware_pack_end.h"
SVGACOTableDXShaderEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyShader {
   SVGA3dShaderId shaderId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyShader; /* SVGA_3D_CMD_DX_DESTROY_SHADER */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBindShader {
   uint32 cid;
   uint32 shid;
   SVGAMobId mobid;
   uint32 offsetInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBindShader;   /* SVGA_3D_CMD_DX_BIND_SHADER */

/*
 * The maximum number of streamout decl's in each streamout entry.
 */
#define SVGA3D_MAX_STREAMOUT_DECLS 64

typedef
#include "vmware_pack_begin.h"
struct SVGA3dStreamOutputDeclarationEntry {
   uint32 outputSlot;
   uint32 registerIndex;
   uint8  registerMask;
   uint8  pad0;
   uint16 pad1;
   uint32 stream;
}
#include "vmware_pack_end.h"
SVGA3dStreamOutputDeclarationEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGAOTableStreamOutputEntry {
   uint32 numOutputStreamEntries;
   SVGA3dStreamOutputDeclarationEntry decl[SVGA3D_MAX_STREAMOUT_DECLS];
   uint32 streamOutputStrideInBytes[SVGA3D_DX_MAX_SOTARGETS];
   uint32 rasterizedStream;
   uint32 pad[250];
}
#include "vmware_pack_end.h"
SVGACOTableDXStreamOutputEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineStreamOutput {
   SVGA3dStreamOutputId soid;
   uint32 numOutputStreamEntries;
   SVGA3dStreamOutputDeclarationEntry decl[SVGA3D_MAX_STREAMOUT_DECLS];
   uint32 streamOutputStrideInBytes[SVGA3D_DX_MAX_SOTARGETS];
   uint32 rasterizedStream;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineStreamOutput; /* SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyStreamOutput {
   SVGA3dStreamOutputId soid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyStreamOutput; /* SVGA_3D_CMD_DX_DESTROY_STREAMOUTPUT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetStreamOutput {
   SVGA3dStreamOutputId soid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetStreamOutput; /* SVGA_3D_CMD_DX_SET_STREAMOUTPUT */

typedef
#include "vmware_pack_begin.h"
struct {
   uint64 value;
   uint32 mobId;
   uint32 mobOffset;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXMobFence64;  /* SVGA_3D_CMD_DX_MOB_FENCE_64 */

/*
 * SVGA3dCmdSetCOTable --
 *
 * This command allows the guest to bind a mob to a context-object table.
 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetCOTable {
   uint32 cid;
   uint32 mobid;
   SVGACOTableType type;
   uint32 validSizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetCOTable; /* SVGA_3D_CMD_DX_SET_COTABLE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXReadbackCOTable {
   uint32 cid;
   SVGACOTableType type;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXReadbackCOTable; /* SVGA_3D_CMD_DX_READBACK_COTABLE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCOTableData {
   uint32 mobid;
}
#include "vmware_pack_end.h"
SVGA3dCOTableData;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dBufferBinding {
   uint32 bufferId;
   uint32 stride;
   uint32 offset;
}
#include "vmware_pack_end.h"
SVGA3dBufferBinding;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dConstantBufferBinding {
   uint32 sid;
   uint32 offsetInBytes;
   uint32 sizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dConstantBufferBinding;

typedef
#include "vmware_pack_begin.h"
struct SVGADXInputAssemblyMobFormat {
   uint32 layoutId;
   SVGA3dBufferBinding vertexBuffers[SVGA3D_DX_MAX_VERTEXBUFFERS];
   uint32 indexBufferSid;
   uint32 pad;
   uint32 indexBufferOffset;
   uint32 indexBufferFormat;
   uint32 topology;
}
#include "vmware_pack_end.h"
SVGADXInputAssemblyMobFormat;

typedef
#include "vmware_pack_begin.h"
struct SVGADXContextMobFormat {
   SVGADXInputAssemblyMobFormat inputAssembly;

   struct {
      uint32 blendStateId;
      uint32 blendFactor[4];
      uint32 sampleMask;
      uint32 depthStencilStateId;
      uint32 stencilRef;
      uint32 rasterizerStateId;
      uint32 depthStencilViewId;
      uint32 renderTargetViewIds[SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS];
      uint32 unorderedAccessViewIds[SVGA3D_MAX_UAVIEWS];
   } renderState;

   struct {
      uint32 targets[SVGA3D_DX_MAX_SOTARGETS];
      uint32 soid;
   } streamOut;
   uint32 pad0[11];

   uint8 numViewports;
   uint8 numScissorRects;
   uint16 pad1[1];

   uint32 pad2[3];

   SVGA3dViewport viewports[SVGA3D_DX_MAX_VIEWPORTS];
   uint32 pad3[32];

   SVGASignedRect scissorRects[SVGA3D_DX_MAX_SCISSORRECTS];
   uint32 pad4[64];

   struct {
      uint32 queryID;
      uint32 value;
   } predication;
   uint32 pad5[2];

   struct {
      uint32 shaderId;
      SVGA3dConstantBufferBinding constantBuffers[SVGA3D_DX_MAX_CONSTBUFFERS];
      uint32 shaderResources[SVGA3D_DX_MAX_SRVIEWS];
      uint32 samplers[SVGA3D_DX_MAX_SAMPLERS];
   } shaderState[SVGA3D_NUM_SHADERTYPE];
   uint32 pad6[26];

   SVGA3dQueryId queryID[SVGA3D_MAX_QUERY];

   SVGA3dCOTableData cotables[SVGA_COTABLE_MAX];
   uint32 pad7[381];
}
#include "vmware_pack_end.h"
SVGADXContextMobFormat;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXTempSetContext {
   uint32 dxcid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXTempSetContext; /* SVGA_3D_CMD_DX_TEMP_SET_CONTEXT */

#endif /* _SVGA3D_DX_H_ */
