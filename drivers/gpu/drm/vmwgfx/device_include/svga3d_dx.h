/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**********************************************************
 * Copyright 2012-2019 VMware, Inc.
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

#define SVGA3D_COLOR_WRITE_ENABLE_RED     (1 << 0)
#define SVGA3D_COLOR_WRITE_ENABLE_GREEN   (1 << 1)
#define SVGA3D_COLOR_WRITE_ENABLE_BLUE    (1 << 2)
#define SVGA3D_COLOR_WRITE_ENABLE_ALPHA   (1 << 3)
#define SVGA3D_COLOR_WRITE_ENABLE_ALL     (SVGA3D_COLOR_WRITE_ENABLE_RED |   \
                                           SVGA3D_COLOR_WRITE_ENABLE_GREEN | \
                                           SVGA3D_COLOR_WRITE_ENABLE_BLUE |  \
                                           SVGA3D_COLOR_WRITE_ENABLE_ALPHA)
typedef uint8 SVGA3dColorWriteEnable;

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

/*
 * SVGA3D_MULTISAMPLE_RAST_DISABLE disables MSAA for all primitives.
 * SVGA3D_MULTISAMPLE_RAST_DISABLE_LINE, which is supported in SM41,
 * disables MSAA for lines only.
 */
#define SVGA3D_MULTISAMPLE_RAST_DISABLE        0
#define SVGA3D_MULTISAMPLE_RAST_ENABLE         1
#define SVGA3D_MULTISAMPLE_RAST_DX_MAX         1
#define SVGA3D_MULTISAMPLE_RAST_DISABLE_LINE   2
#define SVGA3D_MULTISAMPLE_RAST_MAX            2
typedef uint8 SVGA3dMultisampleRastEnable;

#define SVGA3D_DX_MAX_VERTEXBUFFERS 32
#define SVGA3D_DX_MAX_VERTEXINPUTREGISTERS 16
#define SVGA3D_DX_SM41_MAX_VERTEXINPUTREGISTERS 32
#define SVGA3D_DX_MAX_SOTARGETS 4
#define SVGA3D_DX_MAX_SRVIEWS 128
#define SVGA3D_DX_MAX_CONSTBUFFERS 16
#define SVGA3D_DX_MAX_SAMPLERS 16
#define SVGA3D_DX_MAX_CLASS_INSTANCES 253

#define SVGA3D_DX_MAX_CONSTBUF_BINDING_SIZE (4096 * 4 * (uint32)sizeof(uint32))

typedef uint32 SVGA3dShaderResourceViewId;
typedef uint32 SVGA3dRenderTargetViewId;
typedef uint32 SVGA3dDepthStencilViewId;
typedef uint32 SVGA3dUAViewId;

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

typedef union {
   struct {
      uint32 r;
      uint32 g;
      uint32 b;
      uint32 a;
   };

   uint32 value[4];
} SVGA3dRGBAUint32;

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

typedef union {
   struct {
      uint32 cbOffset : 12;
      uint32 cbId     : 4;
      uint32 baseSamp : 4;
      uint32 baseTex  : 7;
      uint32 reserved : 5;
   };
   uint32 value;
} SVGA3dIfaceData;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetShaderIface {
   SVGA3dShaderType type;
   uint32 numClassInstances;
   uint32 index;
   uint32 iface;
   SVGA3dIfaceData data;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetShaderIface; /* SVGA_3D_CMD_DX_SET_SHADER_IFACE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBindShaderIface {
   uint32 cid;
   SVGAMobId mobid;
   uint32 offsetInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBindShaderIface; /* SVGA_3D_CMD_DX_BIND_SHADER_IFACE */

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
struct SVGA3dCmdDXDrawIndexedInstancedIndirect {
   SVGA3dSurfaceId argsBufferSid;
   uint32 byteOffsetForArgs;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDrawIndexedInstancedIndirect;
/* SVGA_3D_CMD_DX_DRAW_INDEXED_INSTANCED_INDIRECT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDrawInstancedIndirect {
   SVGA3dSurfaceId argsBufferSid;
   uint32 byteOffsetForArgs;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDrawInstancedIndirect;
/* SVGA_3D_CMD_DX_DRAW_INSTANCED_INDIRECT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDrawAuto {
   uint32 pad0;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDrawAuto; /* SVGA_3D_CMD_DX_DRAW_AUTO */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDispatch {
   uint32 threadGroupCountX;
   uint32 threadGroupCountY;
   uint32 threadGroupCountZ;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDispatch;
/* SVGA_3D_CMD_DX_DISPATCH */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDispatchIndirect {
   SVGA3dSurfaceId argsBufferSid;
   uint32 byteOffsetForArgs;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDispatchIndirect;
/* SVGA_3D_CMD_DX_DISPATCH_INDIRECT */

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
   uint32 dead;
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
struct SVGA3dCmdDXPredConvertRegion {
   SVGA3dSurfaceId dstSid;
   uint32 dstSubResource;
   SVGA3dBox destBox;
   SVGA3dSurfaceId srcSid;
   uint32 srcSubResource;
   SVGA3dBox srcBox;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXPredConvertRegion; /* SVGA_3D_CMD_DX_PRED_CONVERT_REGION */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXPredConvert {
   SVGA3dSurfaceId dstSid;
   SVGA3dSurfaceId srcSid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXPredConvert; /* SVGA_3D_CMD_DX_PRED_CONVERT */

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

/*
 * Perform a surface copy between a multisample, and a non-multisampled
 * surface.
 */
typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceId dstSid;
   uint32 dstSubResource;
   SVGA3dSurfaceId srcSid;
   uint32 srcSubResource;
   SVGA3dSurfaceFormat copyFormat;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXResolveCopy;               /* SVGA_3D_CMD_DX_RESOLVE_COPY */

/*
 * Perform a predicated surface copy between a multisample, and a
 * non-multisampled surface.
 */
typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceId dstSid;
   uint32 dstSubResource;
   SVGA3dSurfaceId srcSid;
   uint32 srcSubResource;
   SVGA3dSurfaceFormat copyFormat;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXPredResolveCopy;           /* SVGA_3D_CMD_DX_PRED_RESOLVE_COPY */

typedef uint32 SVGA3dDXPresentBltMode;
#define SVGADX_PRESENTBLT_LINEAR           (1 << 0)
#define SVGADX_PRESENTBLT_FORCE_SRC_SRGB   (1 << 1)
#define SVGADX_PRESENTBLT_FORCE_SRC_XRBIAS (1 << 2)
#define SVGADX_PRESENTBLT_MODE_MAX         (1 << 3)

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXPresentBlt {
   SVGA3dSurfaceId srcSid;
   uint32 srcSubResource;
   SVGA3dSurfaceId dstSid;
   uint32 destSubResource;
   SVGA3dBox boxSrc;
   SVGA3dBox boxDest;
   SVGA3dDXPresentBltMode mode;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXPresentBlt; /* SVGA_3D_CMD_DX_PRESENTBLT*/

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXGenMips {
   SVGA3dShaderResourceViewId shaderResourceViewId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXGenMips; /* SVGA_3D_CMD_DX_GENMIPS */

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
 * of the requested box.  Supported if 3d is enabled and SVGA_CAP_DX
 * is set.  This command does not take a context.
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


#define SVGA3D_TRANSFER_TO_BUFFER_READBACK   (1 << 0)
#define SVGA3D_TRANSFER_TO_BUFFER_FLAGS_MASK (1 << 0)
typedef uint32 SVGA3dTransferToBufferFlags;

/*
 * Raw byte wise transfer to a buffer surface from another surface
 * of the requested box.  Supported if SVGA_CAP_DX2 is set.  This
 * command does not take a context.
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXTransferToBuffer {
   SVGA3dSurfaceId srcSid;
   uint32 srcSubResource;
   SVGA3dBox srcBox;
   SVGA3dSurfaceId destSid;
   uint32 destOffset;
   uint32 destPitch;
   uint32 destSlicePitch;
   SVGA3dTransferToBufferFlags flags;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXTransferToBuffer;   /* SVGA_3D_CMD_DX_TRANSFER_TO_BUFFER */


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

/*
 * SVGA_DX_HINT_NONE: Does nothing.
 *
 * SVGA_DX_HINT_PREFETCH_OBJECT:
 * SVGA_DX_HINT_PREEVICT_OBJECT:
 *      Consumes a SVGAObjectRef, and hints that the host should consider
 *      fetching/evicting the specified object.
 *
 *      An id of SVGA3D_INVALID_ID can be used if the guest isn't sure
 *      what object was affected.  (For instance, if the guest knows that
 *      it is about to evict a DXShader, but doesn't know precisely which one,
 *      the device can still use this to help limit it's search, or track
 *      how many page-outs have happened.)
 *
 * SVGA_DX_HINT_PREFETCH_COBJECT:
 * SVGA_DX_HINT_PREEVICT_COBJECT:
 *      Same as the above, except they consume an SVGACObjectRef.
 */
typedef uint32 SVGADXHintId;
#define SVGA_DX_HINT_NONE              0
#define SVGA_DX_HINT_PREFETCH_OBJECT   1
#define SVGA_DX_HINT_PREEVICT_OBJECT   2
#define SVGA_DX_HINT_PREFETCH_COBJECT  3
#define SVGA_DX_HINT_PREEVICT_COBJECT  4
#define SVGA_DX_HINT_MAX               5

typedef
#include "vmware_pack_begin.h"
struct SVGAObjectRef {
   SVGAOTableType type;
   uint32 id;
}
#include "vmware_pack_end.h"
SVGAObjectRef;

typedef
#include "vmware_pack_begin.h"
struct SVGACObjectRef {
   SVGACOTableType type;
   uint32 cid;
   uint32 id;
}
#include "vmware_pack_end.h"
SVGACObjectRef;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXHint {
   SVGADXHintId hintId;

   /*
    * Followed by variable sized data depending on the hintId.
    */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXHint;
/* SVGA_3D_CMD_DX_HINT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBufferUpdate {
   SVGA3dSurfaceId sid;
   uint32 x;
   uint32 width;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBufferUpdate;
/* SVGA_3D_CMD_DX_BUFFER_UPDATE */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetConstantBufferOffset {
   uint32 slot;
   uint32 offsetInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetConstantBufferOffset;

typedef SVGA3dCmdDXSetConstantBufferOffset SVGA3dCmdDXSetVSConstantBufferOffset;
/* SVGA_3D_CMD_DX_SET_VS_CONSTANT_BUFFER_OFFSET */

typedef SVGA3dCmdDXSetConstantBufferOffset SVGA3dCmdDXSetPSConstantBufferOffset;
/* SVGA_3D_CMD_DX_SET_PS_CONSTANT_BUFFER_OFFSET */

typedef SVGA3dCmdDXSetConstantBufferOffset SVGA3dCmdDXSetGSConstantBufferOffset;
/* SVGA_3D_CMD_DX_SET_GS_CONSTANT_BUFFER_OFFSET */

typedef SVGA3dCmdDXSetConstantBufferOffset SVGA3dCmdDXSetHSConstantBufferOffset;
/* SVGA_3D_CMD_DX_SET_HS_CONSTANT_BUFFER_OFFSET */

typedef SVGA3dCmdDXSetConstantBufferOffset SVGA3dCmdDXSetDSConstantBufferOffset;
/* SVGA_3D_CMD_DX_SET_DS_CONSTANT_BUFFER_OFFSET */

typedef SVGA3dCmdDXSetConstantBufferOffset SVGA3dCmdDXSetCSConstantBufferOffset;
/* SVGA_3D_CMD_DX_SET_CS_CONSTANT_BUFFER_OFFSET */


#define SVGA3D_BUFFEREX_SRV_RAW        (1 << 0)
#define SVGA3D_BUFFEREX_SRV_FLAGS_MAX  (1 << 1)
#define SVGA3D_BUFFEREX_SRV_FLAGS_MASK (SVGA3D_BUFFEREX_SRV_FLAGS_MAX - 1)
typedef uint32 SVGA3dBufferExFlags;

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
      } tex; /* 1d, 2d, 3d, cube */
      struct {
         uint32 firstElement;
         uint32 numElements;
         SVGA3dBufferExFlags flags;
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
         uint32 padding0;
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

/*
 * Version 2 needed in order to start validating and using the flags
 * field.  Unfortunately the device wasn't validating or using the
 * flags field and the driver wasn't initializing it in shipped code,
 * so a new version of the command is needed to allow that code to
 * continue to work.
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineDepthStencilView_v2 {
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
SVGA3dCmdDXDefineDepthStencilView_v2;
/* SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW_V2 */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyDepthStencilView {
   SVGA3dDepthStencilViewId depthStencilViewId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyDepthStencilView;
/* SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW */


#define SVGA3D_UABUFFER_RAW     (1 << 0)
#define SVGA3D_UABUFFER_APPEND  (1 << 1)
#define SVGA3D_UABUFFER_COUNTER (1 << 2)
typedef uint32 SVGA3dUABufferFlags;

typedef
#include "vmware_pack_begin.h"
struct {
   union {
      struct {
         uint32 firstElement;
         uint32 numElements;
         SVGA3dUABufferFlags flags;
         uint32 padding0;
         uint32 padding1;
      } buffer;
      struct {
         uint32 mipSlice;
         uint32 firstArraySlice;
         uint32 arraySize;
         uint32 padding0;
         uint32 padding1;
      } tex;  /* 1d, 2d */
      struct {
         uint32 mipSlice;
         uint32 firstW;
         uint32 wSize;
         uint32 padding0;
         uint32 padding1;
      } tex3D;
   };
}
#include "vmware_pack_end.h"
SVGA3dUAViewDesc;

typedef
#include "vmware_pack_begin.h"
struct {
   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;
   SVGA3dUAViewDesc desc;
   uint32 structureCount;
   uint32 pad[7];
}
#include "vmware_pack_end.h"
SVGACOTableDXUAViewEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineUAView {
   SVGA3dUAViewId uaViewId;

   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;

   SVGA3dUAViewDesc desc;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineUAView;
/* SVGA_3D_CMD_DX_DEFINE_UA_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDestroyUAView {
   SVGA3dUAViewId uaViewId;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDestroyUAView;
/* SVGA_3D_CMD_DX_DESTROY_UA_VIEW */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXClearUAViewUint {
   SVGA3dUAViewId uaViewId;
   SVGA3dRGBAUint32 value;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXClearUAViewUint;
/* SVGA_3D_CMD_DX_CLEAR_UA_VIEW_UINT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXClearUAViewFloat {
   SVGA3dUAViewId uaViewId;
   SVGA3dRGBAFloat value;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXClearUAViewFloat;
/* SVGA_3D_CMD_DX_CLEAR_UA_VIEW_FLOAT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXCopyStructureCount {
   SVGA3dUAViewId srcUAViewId;
   SVGA3dSurfaceId destSid;
   uint32 destByteOffset;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXCopyStructureCount;
/* SVGA_3D_CMD_DX_COPY_STRUCTURE_COUNT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetStructureCount {
   SVGA3dUAViewId uaViewId;
   uint32 structureCount;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetStructureCount;
/* SVGA_3D_CMD_DX_SET_STRUCTURE_COUNT */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetUAViews {
   uint32 uavSpliceIndex;
   /* Followed by a variable number of SVGA3dUAViewId's. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetUAViews; /* SVGA_3D_CMD_DX_SET_UA_VIEWS */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXSetCSUAViews {
   uint32 startIndex;
   /* Followed by a variable number of SVGA3dUAViewId's. */
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetCSUAViews; /* SVGA_3D_CMD_DX_SET_CS_UA_VIEWS */

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
   uint32 elid;
   uint32 numDescs;
   SVGA3dInputElementDesc descs[32];
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
      SVGA3dColorWriteEnable renderTargetWriteMask;
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
   SVGA3dMultisampleRastEnable multisampleEnable;
   uint8 antialiasedLineEnable;
   float lineWidth;
   uint8 lineStippleEnable;
   uint8 lineStippleFactor;
   uint16 lineStipplePattern;
   uint8 forcedSampleCount;
   uint8 mustBeZero[3];
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
   SVGA3dMultisampleRastEnable multisampleEnable;
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


#define SVGADX_SIGNATURE_SEMANTIC_NAME_UNDEFINED                          0
#define SVGADX_SIGNATURE_SEMANTIC_NAME_POSITION                           1
#define SVGADX_SIGNATURE_SEMANTIC_NAME_CLIP_DISTANCE                      2
#define SVGADX_SIGNATURE_SEMANTIC_NAME_CULL_DISTANCE                      3
#define SVGADX_SIGNATURE_SEMANTIC_NAME_RENDER_TARGET_ARRAY_INDEX          4
#define SVGADX_SIGNATURE_SEMANTIC_NAME_VIEWPORT_ARRAY_INDEX               5
#define SVGADX_SIGNATURE_SEMANTIC_NAME_VERTEX_ID                          6
#define SVGADX_SIGNATURE_SEMANTIC_NAME_PRIMITIVE_ID                       7
#define SVGADX_SIGNATURE_SEMANTIC_NAME_INSTANCE_ID                        8
#define SVGADX_SIGNATURE_SEMANTIC_NAME_IS_FRONT_FACE                      9
#define SVGADX_SIGNATURE_SEMANTIC_NAME_SAMPLE_INDEX                       10
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR  11
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_QUAD_V_EQ_0_EDGE_TESSFACTOR  12
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_QUAD_U_EQ_1_EDGE_TESSFACTOR  13
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_QUAD_V_EQ_1_EDGE_TESSFACTOR  14
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_QUAD_U_INSIDE_TESSFACTOR     15
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_QUAD_V_INSIDE_TESSFACTOR     16
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR   17
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_TRI_V_EQ_0_EDGE_TESSFACTOR   18
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_TRI_W_EQ_0_EDGE_TESSFACTOR   19
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_TRI_INSIDE_TESSFACTOR        20
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_LINE_DETAIL_TESSFACTOR       21
#define SVGADX_SIGNATURE_SEMANTIC_NAME_FINAL_LINE_DENSITY_TESSFACTOR      22
#define SVGADX_SIGNATURE_SEMANTIC_NAME_MAX                                23
typedef uint32 SVGA3dDXSignatureSemanticName;

#define SVGADX_SIGNATURE_REGISTER_COMPONENT_UNKNOWN 0
typedef uint32 SVGA3dDXSignatureRegisterComponentType;

#define SVGADX_SIGNATURE_MIN_PRECISION_DEFAULT 0
typedef uint32 SVGA3dDXSignatureMinPrecision;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dDXSignatureEntry {
   uint32 registerIndex;
   SVGA3dDXSignatureSemanticName semanticName;
   uint32 mask; /* Lower 4 bits represent X, Y, Z, W channels */
   SVGA3dDXSignatureRegisterComponentType componentType;
   SVGA3dDXSignatureMinPrecision minPrecision;
}
#include "vmware_pack_end.h"
SVGA3dDXShaderSignatureEntry;

#define SVGADX_SIGNATURE_HEADER_VERSION_0 0x08a92d12

/*
 * The SVGA3dDXSignatureHeader structure is added after the shader
 * body in the mob that is bound to the shader.  It is followed by the
 * specified number of SVGA3dDXSignatureEntry structures for each of
 * the three types of signatures in the order (input, output, patch
 * constants).
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dDXSignatureHeader {
   uint32 headerVersion;
   uint32 numInputSignatures;
   uint32 numOutputSignatures;
   uint32 numPatchConstantSignatures;
}
#include "vmware_pack_end.h"
SVGA3dDXShaderSignatureHeader;

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
   uint32 pad[4];
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

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBindAllShader {
   uint32 cid;
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBindAllShader;   /* SVGA_3D_CMD_DX_BIND_ALL_SHADER */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXCondBindAllShader {
   uint32 cid;
   SVGAMobId testMobid;
   SVGAMobId mobid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXCondBindAllShader;   /* SVGA_3D_CMD_DX_COND_BIND_ALL_SHADER */

/*
 * The maximum number of streamout decl's in each streamout entry.
 */
#define SVGA3D_MAX_DX10_STREAMOUT_DECLS 64
#define SVGA3D_MAX_STREAMOUT_DECLS 512

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
   SVGA3dStreamOutputDeclarationEntry decl[SVGA3D_MAX_DX10_STREAMOUT_DECLS];
   uint32 streamOutputStrideInBytes[SVGA3D_DX_MAX_SOTARGETS];
   uint32 rasterizedStream;
   uint32 numOutputStreamStrides;
   uint32 mobid;
   uint32 offsetInBytes;
   uint8 usesMob;
   uint8 pad0;
   uint16 pad1;
   uint32 pad2[246];
}
#include "vmware_pack_end.h"
SVGACOTableDXStreamOutputEntry;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineStreamOutput {
   SVGA3dStreamOutputId soid;
   uint32 numOutputStreamEntries;
   SVGA3dStreamOutputDeclarationEntry decl[SVGA3D_MAX_DX10_STREAMOUT_DECLS];
   uint32 streamOutputStrideInBytes[SVGA3D_DX_MAX_SOTARGETS];
   uint32 rasterizedStream;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineStreamOutput; /* SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT */

/*
 * Version 2 needed in order to start validating and using the
 * rasterizedStream field.  Unfortunately the device wasn't validating
 * or using this field and the driver wasn't initializing it in shipped
 * code, so a new version of the command is needed to allow that code
 * to continue to work.  Also added new numOutputStreamStrides field.
 */

#define SVGA3D_DX_SO_NO_RASTERIZED_STREAM 0xFFFFFFFF

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXDefineStreamOutputWithMob {
   SVGA3dStreamOutputId soid;
   uint32 numOutputStreamEntries;
   uint32 numOutputStreamStrides;
   uint32 streamOutputStrideInBytes[SVGA3D_DX_MAX_SOTARGETS];
   uint32 rasterizedStream;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXDefineStreamOutputWithMob;
/* SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT_WITH_MOB */

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXBindStreamOutput {
   SVGA3dStreamOutputId soid;
   uint32 mobid;
   uint32 offsetInBytes;
   uint32 sizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXBindStreamOutput; /* SVGA_3D_CMD_DX_BIND_STREAMOUTPUT */

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
struct SVGA3dCmdDXSetMinLOD {
   SVGA3dSurfaceId sid;
   float minLOD;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXSetMinLOD; /* SVGA_3D_CMD_DX_SET_MIN_LOD */

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

/*
 * Guests using SVGA_3D_CMD_DX_GROW_COTABLE are promising that
 * the new COTable contains the same contents as the old one, except possibly
 * for some new invalid entries at the end.
 *
 * If there is an old cotable mob bound, it also has to still be valid.
 *
 * (Otherwise, guests should use the DXSetCOTableBase command.)
 */
typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXGrowCOTable {
   uint32 cid;
   uint32 mobid;
   SVGACOTableType type;
   uint32 validSizeInBytes;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXGrowCOTable; /* SVGA_3D_CMD_DX_GROW_COTABLE */

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
   } renderState;

   uint32 pad0[8];

   struct {
      uint32 targets[SVGA3D_DX_MAX_SOTARGETS];
      uint32 soid;
   } streamOut;

   uint32 pad1[10];

   uint32 uavSpliceIndex;

   uint8 numViewports;
   uint8 numScissorRects;
   uint16 pad2[1];

   uint32 pad3[3];

   SVGA3dViewport viewports[SVGA3D_DX_MAX_VIEWPORTS];
   uint32 pad4[32];

   SVGASignedRect scissorRects[SVGA3D_DX_MAX_SCISSORRECTS];
   uint32 pad5[64];

   struct {
      uint32 queryID;
      uint32 value;
   } predication;

   SVGAMobId shaderIfaceMobid;
   uint32 shaderIfaceOffset;
   struct {
      uint32 shaderId;
      SVGA3dConstantBufferBinding constantBuffers[SVGA3D_DX_MAX_CONSTBUFFERS];
      uint32 shaderResources[SVGA3D_DX_MAX_SRVIEWS];
      uint32 samplers[SVGA3D_DX_MAX_SAMPLERS];
   } shaderState[SVGA3D_NUM_SHADERTYPE];
   uint32 pad6[26];

   SVGA3dQueryId queryID[SVGA3D_MAX_QUERY];

   SVGA3dCOTableData cotables[SVGA_COTABLE_MAX];

   uint32 pad7[64];

   uint32 uaViewIds[SVGA3D_DX11_1_MAX_UAVIEWS];
   uint32 csuaViewIds[SVGA3D_DX11_1_MAX_UAVIEWS];

   uint32 pad8[188];
}
#include "vmware_pack_end.h"
SVGADXContextMobFormat;

/*
 * There is conflicting documentation on max class instances (253 vs 256).  The
 * lower value is the one used throughout the device, but since mob format is
 * more involved to increase if needed, conservatively use the higher one here.
 */
#define SVGA3D_DX_MAX_CLASS_INSTANCES_PADDED 256

typedef
#include "vmware_pack_begin.h"
struct SVGADXShaderIfaceMobFormat {
   struct {
      uint32 numClassInstances;
      uint32 iface[SVGA3D_DX_MAX_CLASS_INSTANCES_PADDED];
      SVGA3dIfaceData data[SVGA3D_DX_MAX_CLASS_INSTANCES_PADDED];
   } shaderIfaceState[SVGA3D_NUM_SHADERTYPE];

   uint32 pad0[1018];
}
#include "vmware_pack_end.h"
SVGADXShaderIfaceMobFormat;

typedef
#include "vmware_pack_begin.h"
struct SVGA3dCmdDXTempSetContext {
   uint32 dxcid;
}
#include "vmware_pack_end.h"
SVGA3dCmdDXTempSetContext; /* SVGA_3D_CMD_DX_TEMP_SET_CONTEXT */

#endif /* _SVGA3D_DX_H_ */
