/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2015 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2015 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#ifndef __gc_hal_engine_h_
#define __gc_hal_engine_h_

#include "gc_hal_types.h"
#include "gc_hal_enum.h"

#if gcdENABLE_3D
#if gcdENABLE_VG
#include "gc_hal_engine_vg.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef struct _gcoSTREAM *             gcoSTREAM;
typedef struct _gcoVERTEX *             gcoVERTEX;
typedef struct _gcoTEXTURE *            gcoTEXTURE;
typedef struct _gcoINDEX *              gcoINDEX;
typedef struct _gcsVERTEX_ATTRIBUTES *  gcsVERTEX_ATTRIBUTES_PTR;
typedef struct _gcoVERTEXARRAY *        gcoVERTEXARRAY;
typedef struct _gcoBUFOBJ *             gcoBUFOBJ;

#define gcdATTRIBUTE_COUNT              16

typedef enum _gcePROGRAM_STAGE
{
    gcvPROGRAM_STAGE_VERTEX   = 0x0,
    gcvPROGRAM_STAGE_TES      = 0x1,
    gcvPROGRAM_STAGE_TCS      = 0x2,
    gcvPROGRAM_STAGE_GEOMETRY = 0x3,
    gcvPROGRAM_STAGE_FRAGMENT = 0x4,
    gcvPROGRAM_STAGE_COMPUTE  = 0x5,
    gcvPROGRAM_STAGE_OPENCL   = 0x6,
    gcvPROGRAM_STAGE_LAST
}
gcePROGRAM_STAGE;

typedef enum _gcePROGRAM_STAGE_BIT
{
    gcvPROGRAM_STAGE_VERTEX_BIT   = 1 << gcvPROGRAM_STAGE_VERTEX,
    gcvPROGRAM_STAGE_TES_BIT      = 1 << gcvPROGRAM_STAGE_TES,
    gcvPROGRAM_STAGE_TCS_BIT      = 1 << gcvPROGRAM_STAGE_TCS,
    gcvPROGRAM_STAGE_GEOMETRY_BIT = 1 << gcvPROGRAM_STAGE_GEOMETRY,
    gcvPROGRAM_STAGE_FRAGMENT_BIT = 1 << gcvPROGRAM_STAGE_FRAGMENT,
    gcvPROGRAM_STAGE_COMPUTE_BIT  = 1 << gcvPROGRAM_STAGE_COMPUTE,
    gcvPROGRAM_STAGE_OPENCL_BIT   = 1 << gcvPROGRAM_STAGE_OPENCL,
}
gcePROGRAM_STAGE_BIT;


/******************************************************************************\
********************************* gcoHAL Object *********************************
\******************************************************************************/

gceSTATUS
gcoHAL_QueryShaderCaps(
    IN  gcoHAL    Hal,
    OUT gctUINT * UnifiedUniforms,
    OUT gctUINT * VertUniforms,
    OUT gctUINT * FragUniforms,
    OUT gctUINT * Varyings,
    OUT gctUINT * ShaderCoreCount,
    OUT gctUINT * ThreadCount,
    OUT gctUINT * VertInstructionCount,
    OUT gctUINT * FragInstructionCount
    );

gceSTATUS
gcoHAL_QuerySamplerBase(
                        IN  gcoHAL Hal,
                        OUT gctUINT32 * VertexCount,
                        OUT gctINT_PTR VertexBase,
                        OUT gctUINT32 * FragmentCount,
                        OUT gctINT_PTR FragmentBase
                        );

gceSTATUS
gcoHAL_QueryUniformBase(
                        IN  gcoHAL Hal,
                         OUT gctUINT32 * VertexBase,
                         OUT gctUINT32 * FragmentBase
                        );

gceSTATUS
gcoHAL_QueryTextureCaps(
    IN gcoHAL Hal,
    OUT gctUINT * MaxWidth,
    OUT gctUINT * MaxHeight,
    OUT gctUINT * MaxDepth,
    OUT gctBOOL * Cubic,
    OUT gctBOOL * NonPowerOfTwo,
    OUT gctUINT * VertexSamplers,
    OUT gctUINT * PixelSamplers
    );

gceSTATUS
gcoHAL_QueryTextureMaxAniso(
    IN gcoHAL Hal,
    OUT gctUINT * MaxAnisoValue
    );

gceSTATUS
gcoHAL_QueryStreamCaps(
    IN gcoHAL Hal,
    OUT gctUINT32 * MaxAttributes,
    OUT gctUINT32 * MaxStreamSize,
    OUT gctUINT32 * NumberOfStreams,
    OUT gctUINT32 * Alignment
    );

/******************************************************************************\
********************************* gcoSURF Object ********************************
\******************************************************************************/

/*----------------------------------------------------------------------------*/
/*--------------------------------- gcoSURF 3D --------------------------------*/
typedef enum _gceBLIT_FLAG
{
    gcvBLIT_FLAG_SKIP_DEPTH_WRITE   = 0x1,
    gcvBLIT_FLAG_SKIP_STENCIL_WRITE = 0x2,
} gceBLIT_FLAG;

typedef struct _gcsSURF_BLIT_ARGS
{
    gcoSURF     srcSurface;
    gctINT      srcX, srcY, srcZ;
    gctINT      srcWidth, srcHeight, srcDepth;
    gcoSURF     dstSurface;
    gctINT      dstX, dstY, dstZ;
    gctINT      dstWidth, dstHeight, dstDepth;
    gctBOOL     xReverse;
    gctBOOL     yReverse;
    gctBOOL     scissorTest;
    gcsRECT     scissor;
    gctUINT     flags;
}
gcsSURF_BLIT_ARGS;




/* Clear flags. */
typedef enum _gceCLEAR
{
    gcvCLEAR_COLOR              = 0x1,
    gcvCLEAR_DEPTH              = 0x2,
    gcvCLEAR_STENCIL            = 0x4,
    gcvCLEAR_HZ                 = 0x8,
    gcvCLEAR_HAS_VAA            = 0x10,
    gcvCLEAR_WITH_GPU_ONLY      = 0x100,
    gcvCLEAR_WITH_CPU_ONLY      = 0x200,
}
gceCLEAR;

typedef struct _gcsSURF_CLEAR_ARGS
{
    /*
    ** Color to fill the color portion of the framebuffer when clear
    ** is called.
    */
    struct {
    gcuVALUE r;
    gcuVALUE g;
    gcuVALUE b;
    gcuVALUE a;
    /*
    ** Color has multiple value type so we must specify it.
    */
    gceVALUE_TYPE valueType;
    } color;

    gcuVALUE depth;

    gctUINT  stencil;



    /*
    ** stencil bit-wise mask
    */
    gctUINT8 stencilMask;
    /*
    ** Depth Write Mask
    */
    gctBOOL depthMask;
    /*
    ** 4-bit channel Mask: ABGR:MSB->LSB
    */
    gctUINT8 colorMask;
    /*
    ** If ClearRect is NULL, it means full clear
    */
    gcsRECT_PTR clearRect;
    /*
    ** clear flags
    */
    gceCLEAR  flags;

    /*
    ** Offset in surface to cube/array/3D
    */
    gctUINT32 offset;

} gcsSURF_CLEAR_ARGS;


typedef gcsSURF_CLEAR_ARGS* gcsSURF_CLEAR_ARGS_PTR;

typedef struct _gscSURF_BLITDRAW_BLIT
{
    gcoSURF  srcSurface;
    gcoSURF  dstSurface;
    gcsRECT  srcRect;
    gcsRECT  dstRect;
    gceTEXTURE_FILTER  filterMode;
    gctBOOL  xReverse;
    gctBOOL  yReverse;
    gctBOOL  scissorEnabled;
    gcsRECT  scissor;
}gscSURF_BLITDRAW_BLIT;


typedef enum _gceBLITDRAW_TYPE
{
    gcvBLITDRAW_CLEAR = 0,
    gcvBLITDRAW_BLIT  = 1,

    /* last number, not a real type */
    gcvBLITDRAW_NUM_TYPE
 }
gceBLITDRAW_TYPE;


typedef struct _gscSURF_BLITDRAW_ARGS
{
    /* always the fist member */
    gceHAL_ARG_VERSION version;

    union _gcsSURF_BLITDRAW_ARGS_UNION
    {
       struct _gscSURF_BLITDRAW_ARG_v1
       {
            /* Whether it's clear or blit operation, can be extended. */
            gceBLITDRAW_TYPE type;

            union _gscSURF_BLITDRAW_UNION
            {
                gscSURF_BLITDRAW_BLIT blit;

                struct _gscSURF_BLITDRAW_CLEAR
                {
                    gcsSURF_CLEAR_ARGS clearArgs;
                    gcoSURF rtSurface;
                    gcoSURF dsSurface;
                } clear;
            } u;
       } v1;
    } uArgs;
}
gcsSURF_BLITDRAW_ARGS;


typedef struct _gcsSURF_RESOLVE_ARGS
{
    gceHAL_ARG_VERSION version;
    union _gcsSURF_RESOLVE_ARGS_UNION
    {
        struct _gcsSURF_RESOLVE_ARG_v1
        {
            gctBOOL yInverted;
        }v1;
    } uArgs;
}
gcsSURF_RESOLVE_ARGS;


/* CPU Blit with format (including linear <-> tile) conversion*/
gceSTATUS
gcoSURF_BlitCPU(
    gcsSURF_BLIT_ARGS* args
    );


gceSTATUS
gcoSURF_BlitDraw(
    IN gcsSURF_BLITDRAW_ARGS *args
    );
#endif  /* gcdENABLE_3D */



#if gcdENABLE_3D
/* Clear surface function. */
gceSTATUS
gcoSURF_Clear(
    IN gcoSURF Surface,
    IN gcsSURF_CLEAR_ARGS_PTR  clearArg
    );

/* Preserve pixels from source. */
gceSTATUS
gcoSURF_Preserve(
    IN gcoSURF Source,
    IN gcoSURF Dest,
    IN gcsRECT_PTR MaskRect
    );


/* TO BE REMOVED */
    gceSTATUS
    depr_gcoSURF_Resolve(
        IN gcoSURF SrcSurface,
        IN gcoSURF DestSurface,
        IN gctUINT32 DestAddress,
        IN gctPOINTER DestBits,
        IN gctINT DestStride,
        IN gceSURF_TYPE DestType,
        IN gceSURF_FORMAT DestFormat,
        IN gctUINT DestWidth,
        IN gctUINT DestHeight
        );

    gceSTATUS
    depr_gcoSURF_ResolveRect(
        IN gcoSURF SrcSurface,
        IN gcoSURF DestSurface,
        IN gctUINT32 DestAddress,
        IN gctPOINTER DestBits,
        IN gctINT DestStride,
        IN gceSURF_TYPE DestType,
        IN gceSURF_FORMAT DestFormat,
        IN gctUINT DestWidth,
        IN gctUINT DestHeight,
        IN gcsPOINT_PTR SrcOrigin,
        IN gcsPOINT_PTR DestOrigin,
        IN gcsPOINT_PTR RectSize
        );

/* Resample surface. */
gceSTATUS
gcoSURF_Resample(
    IN gcoSURF SrcSurface,
    IN gcoSURF DestSurface
    );

/* Resolve surface. */
gceSTATUS
gcoSURF_Resolve(
    IN gcoSURF SrcSurface,
    IN gcoSURF DestSurface
    );

gceSTATUS
gcoSURF_ResolveEx(
    IN gcoSURF SrcSurface,
    IN gcoSURF DestSurface,
    IN gcsSURF_RESOLVE_ARGS *args
    );


/* Resolve rectangular area of a surface. */
gceSTATUS
gcoSURF_ResolveRect(
    IN gcoSURF SrcSurface,
    IN gcoSURF DestSurface,
    IN gcsPOINT_PTR SrcOrigin,
    IN gcsPOINT_PTR DestOrigin,
    IN gcsPOINT_PTR RectSize
    );

/* Resolve rectangular area of a surface. */
gceSTATUS
gcoSURF_ResolveRectEx(
    IN gcoSURF SrcSurface,
    IN gcoSURF DestSurface,
    IN gcsPOINT_PTR SrcOrigin,
    IN gcsPOINT_PTR DestOrigin,
    IN gcsPOINT_PTR RectSize,
    IN gcsSURF_RESOLVE_ARGS *args
    );


gceSTATUS
gcoSURF_GetResolveAlignment(
    IN gcoSURF Surface,
    OUT gctUINT *originX,
    OUT gctUINT *originY,
    OUT gctUINT *sizeX,
    OUT gctUINT *sizeY
    );

gceSTATUS
gcoSURF_IsHWResolveable(
    IN gcoSURF SrcSurface,
    IN gcoSURF DestSurface,
    IN gcsPOINT_PTR SrcOrigin,
    IN gcsPOINT_PTR DestOrigin,
    IN gcsPOINT_PTR RectSize
    );

/* Set surface resolvability. */
gceSTATUS
gcoSURF_SetResolvability(
    IN gcoSURF Surface,
    IN gctBOOL Resolvable
    );

gceSTATUS
gcoSURF_IsRenderable(
    IN gcoSURF Surface
    );

gceSTATUS
gcoSURF_IsFormatRenderableAsRT(
    IN gcoSURF Surface
    );

gceSTATUS
gcoSURF_SetSharedLock(
    IN gcoSURF Surface,
    IN gctPOINTER sharedLock
    );

gceSTATUS
gcoSURF_GetFence(
    IN gcoSURF Surface
    );

gceSTATUS
gcoBUFOBJ_GetFence(
    IN gcoBUFOBJ bufObj
    );

gceSTATUS
gcoBUFOBJ_WaitFence(
    IN gcoBUFOBJ bufObj
    );

gceSTATUS
gcoBUFOBJ_IsFenceEnabled(
    IN gcoBUFOBJ bufObj
    );

gceSTATUS
gcoSURF_WaitFence(
    IN gcoSURF Surface
    );

gceSTATUS
gcoSTREAM_GetFence(
    IN gcoSTREAM stream
    );

gceSTATUS
gcoSTREAM_WaitFence(
    IN gcoSTREAM stream
    );

gceSTATUS
gcoINDEX_GetFence(
    IN gcoINDEX index
    );

gceSTATUS
gcoINDEX_WaitFence(
    IN gcoINDEX index
    );

gceSTATUS
gcoSURF_3DBlitClearRect(
    IN gcoSURF Surface,
    IN gcsSURF_CLEAR_ARGS_PTR ClearArgs
    );


gceSTATUS
gcoSURF_3DBlitBltRect(
    IN gcoSURF SrcSurf,
    IN gcoSURF DestSurf,
    IN gcsPOINT_PTR SrcOrigin,
    IN gcsPOINT_PTR DestOrigin,
    IN gcsPOINT_PTR RectSize
    );

gceSTATUS
gcoSURF_3DBlitCopy(
    IN gctUINT32 SrcAddress,
    IN gctUINT32 DestAddress,
    IN gctUINT32 Bytes
    );


/******************************************************************************\
******************************** gcoINDEX Object *******************************
\******************************************************************************/
gceSTATUS
gcoINDEX_SetSharedLock(
    IN gcoINDEX Index,
    IN gctPOINTER sharedLock
    );

/* Construct a new gcoINDEX object. */
gceSTATUS
gcoINDEX_Construct(
    IN gcoHAL Hal,
    OUT gcoINDEX * Index
    );

/* Destroy a gcoINDEX object. */
gceSTATUS
gcoINDEX_Destroy(
    IN gcoINDEX Index
    );

/* Lock index in memory. */
gceSTATUS
gcoINDEX_Lock(
    IN gcoINDEX Index,
    OUT gctUINT32 * Address,
    OUT gctPOINTER * Memory
    );

/* Unlock index that was previously locked with gcoINDEX_Lock. */
gceSTATUS
gcoINDEX_Unlock(
    IN gcoINDEX Index
    );

/* Upload index data into the memory. */
gceSTATUS
gcoINDEX_Load(
    IN gcoINDEX Index,
    IN gceINDEX_TYPE IndexType,
    IN gctUINT32 IndexCount,
    IN gctPOINTER IndexBuffer
    );

/* Bind an index object to the hardware. */
gceSTATUS
gcoINDEX_Bind(
    IN gcoINDEX Index,
    IN gceINDEX_TYPE Type
    );

/* Bind an index object to the hardware. */
gceSTATUS
gcoINDEX_BindOffset(
    IN gcoINDEX Index,
    IN gceINDEX_TYPE Type,
    IN gctUINT32 Offset
    );

/* Free existing index buffer. */
gceSTATUS
gcoINDEX_Free(
    IN gcoINDEX Index
    );

/* Upload data into an index buffer. */
gceSTATUS
gcoINDEX_Upload(
    IN gcoINDEX Index,
    IN gctCONST_POINTER Buffer,
    IN gctSIZE_T Bytes
    );

/* Upload data into an index buffer starting at an offset. */
gceSTATUS
gcoINDEX_UploadOffset(
    IN gcoINDEX Index,
    IN gctSIZE_T Offset,
    IN gctCONST_POINTER Buffer,
    IN gctSIZE_T Bytes
    );

/*Merge index2 to index1 from 0, index2 must subset of inex1*/
gceSTATUS
gcoINDEX_Merge(
    IN gcoINDEX Index1,
    IN gcoINDEX Index2
    );

/*check if index buffer is enough for this draw*/
gctBOOL
gcoINDEX_CheckRange(
    IN gcoINDEX Index,
    IN gceINDEX_TYPE Type,
    IN gctINT Count,
    IN gctUINT32  Indices
    );

/* Query the index capabilities. */
gceSTATUS
gcoINDEX_QueryCaps(
    OUT gctBOOL * Index8,
    OUT gctBOOL * Index16,
    OUT gctBOOL * Index32,
    OUT gctUINT * MaxIndex
    );

/* Determine the index range in the current index buffer. */
gceSTATUS
gcoINDEX_GetIndexRange(
    IN gcoINDEX Index,
    IN gceINDEX_TYPE Type,
    IN gctUINT32 Offset,
    IN gctUINT32 Count,
    OUT gctUINT32 * MinimumIndex,
    OUT gctUINT32 * MaximumIndex
    );

/* Dynamic buffer management. */
gceSTATUS
gcoINDEX_SetDynamic(
    IN gcoINDEX Index,
    IN gctSIZE_T Bytes,
    IN gctUINT Buffers
    );

/******************************************************************************\
********************************** gco3D Object *********************************
\******************************************************************************/

/* Blending targets. */
typedef enum _gceBLEND_UNIT
{
    gcvBLEND_SOURCE,
    gcvBLEND_TARGET,
}
gceBLEND_UNIT;

/* Construct a new gco3D object. */
gceSTATUS
gco3D_Construct(
    IN gcoHAL Hal,
    OUT gco3D * Engine
    );

/* Destroy an gco3D object. */
gceSTATUS
gco3D_Destroy(
    IN gco3D Engine
    );

/* Set 3D API type. */
gceSTATUS
gco3D_SetAPI(
    IN gco3D Engine,
    IN gceAPI ApiType
    );

/* Get 3D API type. */
gceSTATUS
gco3D_GetAPI(
    IN gco3D Engine,
    OUT gceAPI * ApiType
    );

/* Set render target. */
gceSTATUS
gco3D_SetTarget(
    IN gco3D Engine,
    IN gcoSURF Surface
    );

/* Unset render target. */
gceSTATUS
gco3D_UnsetTarget(
    IN gco3D Engine,
    IN gcoSURF Surface
    );

gceSTATUS
gco3D_SetTargetEx(
    IN gco3D Engine,
    IN gctUINT32 TargetIndex,
    IN gcoSURF Surface,
    IN gctUINT32 LayerIndex
    );

gceSTATUS
gco3D_UnsetTargetEx(
    IN gco3D Engine,
    IN gctUINT32 TargetIndex,
    IN gcoSURF Surface
    );

gceSTATUS
gco3D_SetTargetOffsetEx(
    IN gco3D Engine,
    IN gctUINT32 TargetIndex,
    IN gctSIZE_T Offset
    );


gceSTATUS
gco3D_SetPSOutputMapping(
    IN gco3D Engine,
    IN gctINT32 * psOutputMapping
    );


/* Set depth buffer. */
gceSTATUS
gco3D_SetDepth(
    IN gco3D Engine,
    IN gcoSURF Surface
    );

gceSTATUS
gco3D_SetDepthBufferOffset(
    IN gco3D Engine,
    IN gctSIZE_T Offset
    );

/* Unset depth buffer. */
gceSTATUS
gco3D_UnsetDepth(
    IN gco3D Engine,
    IN gcoSURF Surface
    );

/* Set viewport. */
gceSTATUS
gco3D_SetViewport(
    IN gco3D Engine,
    IN gctINT32 Left,
    IN gctINT32 Top,
    IN gctINT32 Right,
    IN gctINT32 Bottom
    );

/* Set scissors. */
gceSTATUS
gco3D_SetScissors(
    IN gco3D Engine,
    IN gctINT32 Left,
    IN gctINT32 Top,
    IN gctINT32 Right,
    IN gctINT32 Bottom
    );

/* Set clear color. */
gceSTATUS
gco3D_SetClearColor(
    IN gco3D Engine,
    IN gctUINT8 Red,
    IN gctUINT8 Green,
    IN gctUINT8 Blue,
    IN gctUINT8 Alpha
    );

/* Set fixed point clear color. */
gceSTATUS
gco3D_SetClearColorX(
    IN gco3D Engine,
    IN gctFIXED_POINT Red,
    IN gctFIXED_POINT Green,
    IN gctFIXED_POINT Blue,
    IN gctFIXED_POINT Alpha
    );

/* Set floating point clear color. */
gceSTATUS
gco3D_SetClearColorF(
    IN gco3D Engine,
    IN gctFLOAT Red,
    IN gctFLOAT Green,
    IN gctFLOAT Blue,
    IN gctFLOAT Alpha
    );

/* Set fixed point clear depth. */
gceSTATUS
gco3D_SetClearDepthX(
    IN gco3D Engine,
    IN gctFIXED_POINT Depth
    );

/* Set floating point clear depth. */
gceSTATUS
gco3D_SetClearDepthF(
    IN gco3D Engine,
    IN gctFLOAT Depth
    );

/* Set clear stencil. */
gceSTATUS
gco3D_SetClearStencil(
    IN gco3D Engine,
    IN gctUINT32 Stencil
    );

/* Set shading mode. */
gceSTATUS
gco3D_SetShading(
    IN gco3D Engine,
    IN gceSHADING Shading
    );

/* Set blending mode. */
gceSTATUS
gco3D_EnableBlending(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Set blending function. */
gceSTATUS
gco3D_SetBlendFunction(
    IN gco3D Engine,
    IN gceBLEND_UNIT Unit,
    IN gceBLEND_FUNCTION FunctionRGB,
    IN gceBLEND_FUNCTION FunctionAlpha
    );

/* Set blending mode. */
gceSTATUS
gco3D_SetBlendMode(
    IN gco3D Engine,
    IN gceBLEND_MODE ModeRGB,
    IN gceBLEND_MODE ModeAlpha
    );

/* Set blending color. */
gceSTATUS
gco3D_SetBlendColor(
    IN gco3D Engine,
    IN gctUINT Red,
    IN gctUINT Green,
    IN gctUINT Blue,
    IN gctUINT Alpha
    );

/* Set fixed point blending color. */
gceSTATUS
gco3D_SetBlendColorX(
    IN gco3D Engine,
    IN gctFIXED_POINT Red,
    IN gctFIXED_POINT Green,
    IN gctFIXED_POINT Blue,
    IN gctFIXED_POINT Alpha
    );

/* Set floating point blending color. */
gceSTATUS
gco3D_SetBlendColorF(
    IN gco3D Engine,
    IN gctFLOAT Red,
    IN gctFLOAT Green,
    IN gctFLOAT Blue,
    IN gctFLOAT Alpha
    );

/* Set culling mode. */
gceSTATUS
gco3D_SetCulling(
    IN gco3D Engine,
    IN gceCULL Mode
    );

/* Enable point size */
gceSTATUS
gco3D_SetPointSizeEnable(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Set point sprite */
gceSTATUS
gco3D_SetPointSprite(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Set fill mode. */
gceSTATUS
gco3D_SetFill(
    IN gco3D Engine,
    IN gceFILL Mode
    );

/* Set depth compare mode. */
gceSTATUS
gco3D_SetDepthCompare(
    IN gco3D Engine,
    IN gceCOMPARE Compare
    );

/* Enable depth writing. */
gceSTATUS
gco3D_EnableDepthWrite(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Set depth mode. */
gceSTATUS
gco3D_SetDepthMode(
    IN gco3D Engine,
    IN gceDEPTH_MODE Mode
    );

/* Set depth range. */
gceSTATUS
gco3D_SetDepthRangeX(
    IN gco3D Engine,
    IN gceDEPTH_MODE Mode,
    IN gctFIXED_POINT Near,
    IN gctFIXED_POINT Far
    );

/* Set depth range. */
gceSTATUS
gco3D_SetDepthRangeF(
    IN gco3D Engine,
    IN gceDEPTH_MODE Mode,
    IN gctFLOAT Near,
    IN gctFLOAT Far
    );

/* Set last pixel enable */
gceSTATUS
gco3D_SetLastPixelEnable(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Set depth Bias and Scale */
gceSTATUS
gco3D_SetDepthScaleBiasX(
    IN gco3D Engine,
    IN gctFIXED_POINT DepthScale,
    IN gctFIXED_POINT DepthBias
    );

gceSTATUS
gco3D_SetDepthScaleBiasF(
    IN gco3D Engine,
    IN gctFLOAT DepthScale,
    IN gctFLOAT DepthBias
    );

/* Set depth near and far clipping plane. */
gceSTATUS
gco3D_SetDepthPlaneF(
    IN gco3D Engine,
    IN gctFLOAT Near,
    IN gctFLOAT Far
    );

/* Enable or disable dithering. */
gceSTATUS
gco3D_EnableDither(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Set color write enable bits. */
gceSTATUS
gco3D_SetColorWrite(
    IN gco3D Engine,
    IN gctUINT8 Enable
    );

/* Enable or disable early depth. */
gceSTATUS
gco3D_SetEarlyDepth(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Deprecated: Enable or disable all early depth operations. */
gceSTATUS
gco3D_SetAllEarlyDepthModes(
    IN gco3D Engine,
    IN gctBOOL Disable
    );

/* Enable or disable all early depth operations. */
gceSTATUS
gco3D_SetAllEarlyDepthModesEx(
    IN gco3D Engine,
    IN gctBOOL Disable,
    IN gctBOOL DisableModify,
    IN gctBOOL DisablePassZ
    );

/* Switch dynamic early mode */
gceSTATUS
gco3D_SwitchDynamicEarlyDepthMode(
    IN gco3D Engine
    );

/* Set dynamic early mode */
gceSTATUS
gco3D_DisableDynamicEarlyDepthMode(
    IN gco3D Engine,
    IN gctBOOL Disable
    );

/* Enable or disable depth-only mode. */
gceSTATUS
gco3D_SetDepthOnly(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

typedef struct _gcsSTENCIL_INFO * gcsSTENCIL_INFO_PTR;
typedef struct _gcsSTENCIL_INFO
{
    gceSTENCIL_MODE         mode;

    gctUINT8                maskFront;
    gctUINT8                maskBack;
    gctUINT8                writeMaskFront;
    gctUINT8                writeMaskBack;

    gctUINT8                referenceFront;

    gceCOMPARE              compareFront;
    gceSTENCIL_OPERATION    passFront;
    gceSTENCIL_OPERATION    failFront;
    gceSTENCIL_OPERATION    depthFailFront;

    gctUINT8                referenceBack;
    gceCOMPARE              compareBack;
    gceSTENCIL_OPERATION    passBack;
    gceSTENCIL_OPERATION    failBack;
    gceSTENCIL_OPERATION    depthFailBack;
}
gcsSTENCIL_INFO;

/* Set stencil mode. */
gceSTATUS
gco3D_SetStencilMode(
    IN gco3D Engine,
    IN gceSTENCIL_MODE Mode
    );

/* Set stencil mask. */
gceSTATUS
gco3D_SetStencilMask(
    IN gco3D Engine,
    IN gctUINT8 Mask
    );

/* Set stencil back mask. */
gceSTATUS
gco3D_SetStencilMaskBack(
    IN gco3D Engine,
    IN gctUINT8 Mask
    );

/* Set stencil write mask. */
gceSTATUS
gco3D_SetStencilWriteMask(
    IN gco3D Engine,
    IN gctUINT8 Mask
    );

/* Set stencil back write mask. */
gceSTATUS
gco3D_SetStencilWriteMaskBack(
    IN gco3D Engine,
    IN gctUINT8 Mask
    );

/* Set stencil reference. */
gceSTATUS
gco3D_SetStencilReference(
    IN gco3D Engine,
    IN gctUINT8 Reference,
    IN gctBOOL Front
    );

/* Set stencil compare. */
gceSTATUS
gco3D_SetStencilCompare(
    IN gco3D Engine,
    IN gceSTENCIL_WHERE Where,
    IN gceCOMPARE Compare
    );

/* Set stencil operation on pass. */
gceSTATUS
gco3D_SetStencilPass(
    IN gco3D Engine,
    IN gceSTENCIL_WHERE Where,
    IN gceSTENCIL_OPERATION Operation
    );

/* Set stencil operation on fail. */
gceSTATUS
gco3D_SetStencilFail(
    IN gco3D Engine,
    IN gceSTENCIL_WHERE Where,
    IN gceSTENCIL_OPERATION Operation
    );

/* Set stencil operation on depth fail. */
gceSTATUS
gco3D_SetStencilDepthFail(
    IN gco3D Engine,
    IN gceSTENCIL_WHERE Where,
    IN gceSTENCIL_OPERATION Operation
    );

/* Set all stencil states in one blow. */
gceSTATUS
gco3D_SetStencilAll(
    IN gco3D Engine,
    IN gcsSTENCIL_INFO_PTR Info
    );

typedef struct _gcsALPHA_INFO * gcsALPHA_INFO_PTR;
typedef struct _gcsALPHA_INFO
{
    /* Alpha test states. */
    gctBOOL                 test;
    gceCOMPARE              compare;
    gctUINT8                reference;
    gctFLOAT                floatReference;

    /* Alpha blending states. */
    gctBOOL                 blend;

    gceBLEND_FUNCTION       srcFuncColor;
    gceBLEND_FUNCTION       srcFuncAlpha;
    gceBLEND_FUNCTION       trgFuncColor;
    gceBLEND_FUNCTION       trgFuncAlpha;

    gceBLEND_MODE           modeColor;
    gceBLEND_MODE           modeAlpha;

    gctUINT32               color;
}
gcsALPHA_INFO;

/* Enable or disable alpha test. */
gceSTATUS
gco3D_SetAlphaTest(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Set alpha test compare. */
gceSTATUS
gco3D_SetAlphaCompare(
    IN gco3D Engine,
    IN gceCOMPARE Compare
    );

/* Set alpha test reference in unsigned integer. */
gceSTATUS
gco3D_SetAlphaReference(
    IN gco3D Engine,
    IN gctUINT8 Reference,
    IN gctFLOAT FloatReference
    );

/* Set alpha test reference in fixed point. */
gceSTATUS
gco3D_SetAlphaReferenceX(
    IN gco3D Engine,
    IN gctFIXED_POINT Reference
    );

/* Set alpha test reference in floating point. */
gceSTATUS
gco3D_SetAlphaReferenceF(
    IN gco3D Engine,
    IN gctFLOAT Reference
    );

/* Enable/Disable anti-alias line. */
gceSTATUS
gco3D_SetAntiAliasLine(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Set texture slot for anti-alias line. */
gceSTATUS
gco3D_SetAALineTexSlot(
    IN gco3D Engine,
    IN gctUINT TexSlot
    );

/* Set anti-alias line width scale. */
gceSTATUS
gco3D_SetAALineWidth(
    IN gco3D Engine,
    IN gctFLOAT Width
    );

/* Draw a number of primitives. */
gceSTATUS
gco3D_DrawPrimitives(
    IN gco3D Engine,
    IN gcePRIMITIVE Type,
    IN gctSIZE_T StartVertex,
    IN gctSIZE_T PrimitiveCount
    );

gceSTATUS
gco3D_DrawInstancedPrimitives(
    IN gco3D Engine,
    IN gcePRIMITIVE Type,
    IN gctBOOL DrawIndex,
    IN gctSIZE_T StartVertex,
    IN gctSIZE_T StartIndex,
    IN gctSIZE_T PrimitiveCount,
    IN gctSIZE_T VertexCount,
    IN gctBOOL SpilitDraw,
    IN gctSIZE_T SpilitCount,
    IN gcePRIMITIVE SpilitType,
    IN gctSIZE_T InstanceCount
    );

gceSTATUS
gco3D_DrawPrimitivesCount(
    IN gco3D Engine,
    IN gcePRIMITIVE Type,
    IN gctINT* StartVertex,
    IN gctSIZE_T* VertexCount,
    IN gctSIZE_T PrimitiveCount
    );


/* Draw a number of primitives using offsets. */
gceSTATUS
gco3D_DrawPrimitivesOffset(
    IN gco3D Engine,
    IN gcePRIMITIVE Type,
    IN gctINT32 StartOffset,
    IN gctSIZE_T PrimitiveCount
    );

/* Draw a number of indexed primitives. */
gceSTATUS
gco3D_DrawIndexedPrimitives(
    IN gco3D Engine,
    IN gcePRIMITIVE Type,
    IN gctSIZE_T BaseVertex,
    IN gctSIZE_T StartIndex,
    IN gctSIZE_T PrimitiveCount,
    IN gctBOOL SpilitDraw,
    IN gctSIZE_T SpilitCount,
    IN gcePRIMITIVE SpilitType
    );

/* Draw a number of indexed primitives using offsets. */
gceSTATUS
gco3D_DrawIndexedPrimitivesOffset(
    IN gco3D Engine,
    IN gcePRIMITIVE Type,
    IN gctINT32 BaseOffset,
    IN gctINT32 StartOffset,
    IN gctSIZE_T PrimitiveCount
    );

/* Draw a element from pattern */
gceSTATUS
gco3D_DrawPattern(
    IN gco3D Engine,
    IN gcsFAST_FLUSH_PTR FastFlushInfo
    );

/* Enable or disable anti-aliasing. */
gceSTATUS
gco3D_SetAntiAlias(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

/* Write data into the command buffer. */
gceSTATUS
gco3D_WriteBuffer(
    IN gco3D Engine,
    IN gctCONST_POINTER Data,
    IN gctSIZE_T Bytes,
    IN gctBOOL Aligned
    );

/* Send sempahore and stall until sempahore is signalled. */
gceSTATUS
gco3D_Semaphore(
    IN gco3D Engine,
    IN gceWHERE From,
    IN gceWHERE To,
    IN gceHOW How);

/* Explicitly flush shader L1 cache */
gceSTATUS
gco3D_FlushSHL1Cache(
    IN gco3D Engine
    );

/* Set the subpixels center. */
gceSTATUS
gco3D_SetCentroids(
    IN gco3D Engine,
    IN gctUINT32 Index,
    IN gctPOINTER Centroids
    );

gceSTATUS
gco3D_SetLogicOp(
    IN gco3D Engine,
    IN gctUINT8 Rop
    );

gceSTATUS
gco3D_SetOQ(
    IN gco3D Engine,
    INOUT gctPOINTER * Result,
    IN gctBOOL Enable
    );

gceSTATUS
gco3D_GetOQ(
    IN gco3D Engine,
    IN gctPOINTER Result,
    OUT gctINT64 * Logical
    );

gceSTATUS
gco3D_DeleteOQ(
    IN gco3D Engine,
    INOUT gctPOINTER Result
    );

gceSTATUS
gco3D_SetColorOutCount(
    IN gco3D Engine,
    IN gctUINT32 ColorOutCount
    );

gceSTATUS
gco3D_Set3DEngine(
    IN gco3D Engine
    );

gceSTATUS
gco3D_UnSet3DEngine(
    IN gco3D Engine
    );

gceSTATUS
gco3D_Get3DEngine(
    OUT gco3D * Engine
    );


/* OCL thread walker information. */
typedef struct _gcsTHREAD_WALKER_INFO * gcsTHREAD_WALKER_INFO_PTR;
typedef struct _gcsTHREAD_WALKER_INFO
{
    gctUINT32   dimensions;
    gctUINT32   traverseOrder;
    gctUINT32   enableSwathX;
    gctUINT32   enableSwathY;
    gctUINT32   enableSwathZ;
    gctUINT32   swathSizeX;
    gctUINT32   swathSizeY;
    gctUINT32   swathSizeZ;
    gctUINT32   valueOrder;

    gctUINT32   globalSizeX;
    gctUINT32   globalOffsetX;
    gctUINT32   globalSizeY;
    gctUINT32   globalOffsetY;
    gctUINT32   globalSizeZ;
    gctUINT32   globalOffsetZ;

    gctUINT32   workGroupSizeX;
    gctUINT32   workGroupCountX;
    gctUINT32   workGroupSizeY;
    gctUINT32   workGroupCountY;
    gctUINT32   workGroupSizeZ;
    gctUINT32   workGroupCountZ;

    gctUINT32   threadAllocation;
}
gcsTHREAD_WALKER_INFO;

/* Start OCL thread walker. */
gceSTATUS
gco3D_InvokeThreadWalker(
    IN gco3D Engine,
    IN gcsTHREAD_WALKER_INFO_PTR Info
    );

gceSTATUS
gco3D_GetClosestRenderFormat(
    IN gco3D Engine,
    IN gceSURF_FORMAT InFormat,
    OUT gceSURF_FORMAT* OutFormat
    );

/* Set w clip and w plane limit value. */
gceSTATUS
gco3D_SetWClipEnable(
    IN gco3D Engine,
    IN gctBOOL Enable
    );

gceSTATUS
gco3D_GetWClipEnable(
    IN gco3D Engine,
    OUT gctBOOL * Enable
    );

gceSTATUS
gco3D_SetWPlaneLimitF(
    IN gco3D Engine,
    IN gctFLOAT Value
    );

gceSTATUS
gco3D_SetWPlaneLimitX(
    IN gco3D Engine,
    IN gctFIXED_POINT Value
    );

gceSTATUS
gco3D_SetWPlaneLimit(
        IN gco3D Engine,
        IN gctFLOAT Value
        );

gceSTATUS
gco3D_PrimitiveRestart(
    IN gco3D Engine,
    IN gctBOOL PrimitiveRestart);

#if gcdSTREAM_OUT_BUFFER

gceSTATUS
gco3D_QueryStreamOut(
    IN gco3D Engine,
    IN gctUINT32 OriginalIndexAddress,
    IN gctUINT32 OriginalIndexOffset,
    IN gctUINT32 OriginalIndexCount,
    OUT gctBOOL_PTR Found
    );

gceSTATUS
gco3D_StartStreamOut(
    IN gco3D Engine,
    IN gctINT StreamOutStatus,
    IN gctUINT32 IndexAddress,
    IN gctUINT32 IndexOffset,
    IN gctUINT32 IndexCount
    );

gceSTATUS
gco3D_StopStreamOut(
    IN gco3D Engine
    );

gceSTATUS
gco3D_ReplayStreamOut(
    IN gco3D Engine,
    IN gctUINT32 IndexAddress,
    IN gctUINT32 IndexOffset,
    IN gctUINT32 IndexCount
    );

gceSTATUS
gco3D_EndStreamOut(
    IN gco3D Engine
    );

#endif

/*----------------------------------------------------------------------------*/
/*-------------------------- gco3D Fragment Processor ------------------------*/

/* Set the fragment processor configuration. */
gceSTATUS
gco3D_SetFragmentConfiguration(
    IN gco3D Engine,
    IN gctBOOL ColorFromStream,
    IN gctBOOL EnableFog,
    IN gctBOOL EnableSmoothPoint,
    IN gctUINT32 ClipPlanes
    );

/* Enable/disable texture stage operation. */
gceSTATUS
gco3D_EnableTextureStage(
    IN gco3D Engine,
    IN gctINT Stage,
    IN gctBOOL Enable
    );

/* Program the channel enable masks for the color texture function. */
gceSTATUS
gco3D_SetTextureColorMask(
    IN gco3D Engine,
    IN gctINT Stage,
    IN gctBOOL ColorEnabled,
    IN gctBOOL AlphaEnabled
    );

/* Program the channel enable masks for the alpha texture function. */
gceSTATUS
gco3D_SetTextureAlphaMask(
    IN gco3D Engine,
    IN gctINT Stage,
    IN gctBOOL ColorEnabled,
    IN gctBOOL AlphaEnabled
    );

/* Program the constant fragment color. */
gceSTATUS
gco3D_SetFragmentColorX(
    IN gco3D Engine,
    IN gctFIXED_POINT Red,
    IN gctFIXED_POINT Green,
    IN gctFIXED_POINT Blue,
    IN gctFIXED_POINT Alpha
    );

gceSTATUS
gco3D_SetFragmentColorF(
    IN gco3D Engine,
    IN gctFLOAT Red,
    IN gctFLOAT Green,
    IN gctFLOAT Blue,
    IN gctFLOAT Alpha
    );

/* Program the constant fog color. */
gceSTATUS
gco3D_SetFogColorX(
    IN gco3D Engine,
    IN gctFIXED_POINT Red,
    IN gctFIXED_POINT Green,
    IN gctFIXED_POINT Blue,
    IN gctFIXED_POINT Alpha
    );

gceSTATUS
gco3D_SetFogColorF(
    IN gco3D Engine,
    IN gctFLOAT Red,
    IN gctFLOAT Green,
    IN gctFLOAT Blue,
    IN gctFLOAT Alpha
    );

/* Program the constant texture color. */
gceSTATUS
gco3D_SetTetxureColorX(
    IN gco3D Engine,
    IN gctINT Stage,
    IN gctFIXED_POINT Red,
    IN gctFIXED_POINT Green,
    IN gctFIXED_POINT Blue,
    IN gctFIXED_POINT Alpha
    );

gceSTATUS
gco3D_SetTetxureColorF(
    IN gco3D Engine,
    IN gctINT Stage,
    IN gctFLOAT Red,
    IN gctFLOAT Green,
    IN gctFLOAT Blue,
    IN gctFLOAT Alpha
    );

/* Configure color texture function. */
gceSTATUS
gco3D_SetColorTextureFunction(
    IN gco3D Engine,
    IN gctINT Stage,
    IN gceTEXTURE_FUNCTION Function,
    IN gceTEXTURE_SOURCE Source0,
    IN gceTEXTURE_CHANNEL Channel0,
    IN gceTEXTURE_SOURCE Source1,
    IN gceTEXTURE_CHANNEL Channel1,
    IN gceTEXTURE_SOURCE Source2,
    IN gceTEXTURE_CHANNEL Channel2,
    IN gctINT Scale
    );

/* Configure alpha texture function. */
gceSTATUS
gco3D_SetAlphaTextureFunction(
    IN gco3D Engine,
    IN gctINT Stage,
    IN gceTEXTURE_FUNCTION Function,
    IN gceTEXTURE_SOURCE Source0,
    IN gceTEXTURE_CHANNEL Channel0,
    IN gceTEXTURE_SOURCE Source1,
    IN gceTEXTURE_CHANNEL Channel1,
    IN gceTEXTURE_SOURCE Source2,
    IN gceTEXTURE_CHANNEL Channel2,
    IN gctINT Scale
    );

/******************************************************************************\
******************************* gcoTEXTURE Object *******************************
\******************************************************************************/

/* Cube faces. */
typedef enum _gceTEXTURE_FACE
{
    gcvFACE_NONE,
    gcvFACE_POSITIVE_X,
    gcvFACE_NEGATIVE_X,
    gcvFACE_POSITIVE_Y,
    gcvFACE_NEGATIVE_Y,
    gcvFACE_POSITIVE_Z,
    gcvFACE_NEGATIVE_Z,
}
gceTEXTURE_FACE;

typedef struct _gcsTEXTURE
{
    /* Addressing modes. */
    gceTEXTURE_ADDRESSING       s;
    gceTEXTURE_ADDRESSING       t;
    gceTEXTURE_ADDRESSING       r;

    gceTEXTURE_SWIZZLE          swizzle[gcvTEXTURE_COMPONENT_NUM];

    /* Border color. */
    gctUINT8                    border[gcvTEXTURE_COMPONENT_NUM];

    /* Filters. */
    gceTEXTURE_FILTER           minFilter;
    gceTEXTURE_FILTER           magFilter;
    gceTEXTURE_FILTER           mipFilter;
    gctUINT                     anisoFilter;

    /* Level of detail. */
    gctFLOAT                    lodBias;
    gctFLOAT                    lodMin;
    gctFLOAT                    lodMax;

    /* base/max level */
    gctINT32                    baseLevel;
    gctINT32                    maxLevel;

    /* depth texture comparison */
    gceTEXTURE_COMPARE_MODE     compareMode;
    gceCOMPARE                  compareFunc;

}
gcsTEXTURE, * gcsTEXTURE_PTR;

/* Construct a new gcoTEXTURE object. */
gceSTATUS
gcoTEXTURE_Construct(
    IN gcoHAL Hal,
    OUT gcoTEXTURE * Texture
    );

/* Construct a new gcoTEXTURE object with type information. */
gceSTATUS
gcoTEXTURE_ConstructEx(
    IN gcoHAL Hal,
    IN gceTEXTURE_TYPE Type,
    OUT gcoTEXTURE * Texture
    );


/* Construct a new sized gcoTEXTURE object. */
gceSTATUS
gcoTEXTURE_ConstructSized(
    IN gcoHAL Hal,
    IN gceSURF_FORMAT Format,
    IN gctUINT Width,
    IN gctUINT Height,
    IN gctUINT Depth,
    IN gctUINT Faces,
    IN gctUINT MipMapCount,
    IN gcePOOL Pool,
    OUT gcoTEXTURE * Texture
    );

/* Destroy an gcoTEXTURE object. */
gceSTATUS
gcoTEXTURE_Destroy(
    IN gcoTEXTURE Texture
    );

/* Upload data to an gcoTEXTURE object. */
gceSTATUS
gcoTEXTURE_Upload(
    IN gcoTEXTURE Texture,
    IN gctINT MipMap,
    IN gceTEXTURE_FACE Face,
    IN gctSIZE_T Width,
    IN gctSIZE_T Height,
    IN gctUINT Slice,
    IN gctCONST_POINTER Memory,
    IN gctSIZE_T Stride,
    IN gceSURF_FORMAT Format,
    IN gceSURF_COLOR_SPACE SrcColorSpace
    );

/* Upload data to an gcoTEXTURE object. */
gceSTATUS
gcoTEXTURE_UploadSub(
    IN gcoTEXTURE Texture,
    IN gctINT MipMap,
    IN gceTEXTURE_FACE Face,
    IN gctSIZE_T X,
    IN gctSIZE_T Y,
    IN gctSIZE_T Width,
    IN gctSIZE_T Height,
    IN gctUINT Slice,
    IN gctCONST_POINTER Memory,
    IN gctSIZE_T Stride,
    IN gceSURF_FORMAT Format,
    IN gceSURF_COLOR_SPACE SrcColorSpace,
    IN gctUINT32 PhysicalAddress
    );


/* Upload YUV data to an gcoTEXTURE object. */
gceSTATUS
gcoTEXTURE_UploadYUV(
    IN gcoTEXTURE Texture,
    IN gceTEXTURE_FACE Face,
    IN gctUINT Width,
    IN gctUINT Height,
    IN gctUINT Slice,
    IN gctPOINTER Memory[3],
    IN gctINT Stride[3],
    IN gceSURF_FORMAT Format
    );

/* Upload compressed data to an gcoTEXTURE object. */
gceSTATUS
gcoTEXTURE_UploadCompressed(
    IN gcoTEXTURE Texture,
    IN gctINT MipMap,
    IN gceTEXTURE_FACE Face,
    IN gctSIZE_T Width,
    IN gctSIZE_T Height,
    IN gctUINT Slice,
    IN gctCONST_POINTER Memory,
    IN gctSIZE_T Bytes
    );

/* Upload compressed sub data to an gcoTEXTURE object. */
gceSTATUS
gcoTEXTURE_UploadCompressedSub(
    IN gcoTEXTURE Texture,
    IN gctINT MipMap,
    IN gceTEXTURE_FACE Face,
    IN gctSIZE_T XOffset,
    IN gctSIZE_T YOffset,
    IN gctSIZE_T Width,
    IN gctSIZE_T Height,
    IN gctUINT Slice,
    IN gctCONST_POINTER Memory,
    IN gctSIZE_T Size
    );

/* Get gcoSURF object for a mipmap level. */
gceSTATUS
gcoTEXTURE_GetMipMap(
    IN gcoTEXTURE Texture,
    IN gctUINT MipMap,
    OUT gcoSURF * Surface
    );

/* Get gcoSURF object for a mipmap level and face offset. */
gceSTATUS
gcoTEXTURE_GetMipMapFace(
    IN gcoTEXTURE Texture,
    IN gctUINT MipMap,
    IN gceTEXTURE_FACE Face,
    OUT gcoSURF * Surface,
    OUT gctSIZE_T_PTR Offset
    );

gceSTATUS
gcoTEXTURE_GetMipMapSlice(
    IN gcoTEXTURE Texture,
    IN gctUINT MipMap,
    IN gctUINT Slice,
    OUT gcoSURF * Surface,
    OUT gctSIZE_T_PTR Offset
    );

gceSTATUS
gcoTEXTURE_AddMipMap(
    IN gcoTEXTURE Texture,
    IN gctINT Level,
    IN gctINT InternalFormat,
    IN gceSURF_FORMAT Format,
    IN gctSIZE_T Width,
    IN gctSIZE_T Height,
    IN gctSIZE_T Depth,
    IN gctUINT Faces,
    IN gcePOOL Pool,
    OUT gcoSURF * Surface
    );

gceSTATUS
gcoTEXTURE_AddMipMapWithFlag(
    IN gcoTEXTURE Texture,
    IN gctINT Level,
    IN gctINT InternalFormat,
    IN gceSURF_FORMAT Format,
    IN gctSIZE_T Width,
    IN gctSIZE_T Height,
    IN gctSIZE_T Depth,
    IN gctUINT Faces,
    IN gcePOOL Pool,
    IN gctBOOL Protected,
    OUT gcoSURF * Surface
    );

gceSTATUS
gcoTEXTURE_AddMipMapFromClient(
    IN gcoTEXTURE Texture,
    IN gctINT     Level,
    IN gcoSURF    Surface
    );

gceSTATUS
gcoTEXTURE_AddMipMapFromSurface(
    IN gcoTEXTURE Texture,
    IN gctINT     Level,
    IN gcoSURF    Surface
    );

gceSTATUS
gcoTEXTURE_SetEndianHint(
    IN gcoTEXTURE Texture,
    IN gceENDIAN_HINT EndianHint
    );

gceSTATUS
gcoTEXTURE_Disable(
    IN gcoHAL Hal,
    IN gctINT Sampler
    );

gceSTATUS
gcoTEXTURE_Flush(
    IN gcoTEXTURE Texture
    );

gceSTATUS
gcoTEXTURE_FlushVS(
    IN gcoTEXTURE Texture
    );

gceSTATUS
gcoTEXTURE_QueryCaps(
    IN  gcoHAL    Hal,
    OUT gctUINT * MaxWidth,
    OUT gctUINT * MaxHeight,
    OUT gctUINT * MaxDepth,
    OUT gctBOOL * Cubic,
    OUT gctBOOL * NonPowerOfTwo,
    OUT gctUINT * VertexSamplers,
    OUT gctUINT * PixelSamplers
    );

gceSTATUS
gcoTEXTURE_GetClosestFormat(
    IN gcoHAL Hal,
    IN gceSURF_FORMAT InFormat,
    OUT gceSURF_FORMAT* OutFormat
    );

gceSTATUS
gcoTEXTURE_GetClosestFormatEx(
    IN gcoHAL Hal,
    IN gceSURF_FORMAT InFormat,
    IN gceTEXTURE_TYPE TextureType,
    OUT gceSURF_FORMAT* OutFormat
    );

gceSTATUS
gcoTEXTURE_GetFormatInfo(
    IN gcoTEXTURE Texture,
    IN gctINT preferLevel,
    OUT gcsSURF_FORMAT_INFO_PTR * TxFormatInfo
    );

gceSTATUS
gcoTEXTURE_GetTextureFormatName(
    IN gcsSURF_FORMAT_INFO_PTR TxFormatInfo,
    OUT gctCONST_STRING * TxName
    );

gceSTATUS
gcoTEXTURE_RenderIntoMipMap(
    IN gcoTEXTURE Texture,
    IN gctINT Level
    );

gceSTATUS
gcoTEXTURE_RenderIntoMipMap2(
    IN gcoTEXTURE Texture,
    IN gctINT Level,
    IN gctBOOL Sync
    );

gceSTATUS
gcoTEXTURE_IsRenderable(
    IN gcoTEXTURE Texture,
    IN gctUINT Level
    );

gceSTATUS
gcoTEXTURE_IsComplete(
    IN gcoTEXTURE Texture,
    IN gcsTEXTURE_PTR Info,
    IN gctINT BaseLevel,
    IN gctINT MaxLevel
    );

gceSTATUS
gcoTEXTURE_BindTexture(
    IN gcoTEXTURE Texture,
    IN gctINT Target,
    IN gctINT Sampler,
    IN gcsTEXTURE_PTR Info
    );

gceSTATUS
gcoTEXTURE_BindTextureEx(
    IN gcoTEXTURE Texture,
    IN gctINT Target,
    IN gctINT Sampler,
    IN gcsTEXTURE_PTR Info,
    IN gctINT textureLayer
    );

gceSTATUS
gcoTEXTURE_InitParams(
    IN gcoHAL Hal,
    IN gcsTEXTURE_PTR TexParams
    );

gceSTATUS
gcoTEXTURE_SetDepthTextureFlag(
    IN gcoTEXTURE Texture,
    IN gctBOOL  unsized
    );


/******************************************************************************\
******************************* gcoSTREAM Object ******************************
\******************************************************************************/

typedef enum _gceVERTEX_FORMAT
{
    gcvVERTEX_BYTE,
    gcvVERTEX_UNSIGNED_BYTE,
    gcvVERTEX_SHORT,
    gcvVERTEX_UNSIGNED_SHORT,
    gcvVERTEX_INT,
    gcvVERTEX_UNSIGNED_INT,
    gcvVERTEX_FIXED,
    gcvVERTEX_HALF,
    gcvVERTEX_FLOAT,
    gcvVERTEX_UNSIGNED_INT_10_10_10_2,
    gcvVERTEX_INT_10_10_10_2,
    gcvVERTEX_UNSIGNED_INT_2_10_10_10_REV,
    gcvVERTEX_INT_2_10_10_10_REV,
    /* integer format */
    gcvVERTEX_INT8,
    gcvVERTEX_INT16,
    gcvVERTEX_INT32,
}
gceVERTEX_FORMAT;

/* What the SW converting scheme to create temp attrib */
typedef enum _gceATTRIB_SCHEME
{
    gcvATTRIB_SCHEME_KEEP = 0,
    gcvATTRIB_SCHEME_2_10_10_10_REV_TO_FLOAT,
    gcvATTRIB_SCHEME_BYTE_TO_INT,
    gcvATTRIB_SCHEME_SHORT_TO_INT,
    gcvATTRIB_SCHEME_UBYTE_TO_UINT,
    gcvATTRIB_SCHEME_USHORT_TO_UINT,
} gceATTRIB_SCHEME;

gceSTATUS
gcoSTREAM_SetSharedLock(
    IN gcoSTREAM Stream,
    IN gctPOINTER sharedLock
    );

gceSTATUS
gcoSTREAM_Construct(
    IN gcoHAL Hal,
    OUT gcoSTREAM * Stream
    );

gceSTATUS
gcoSTREAM_Destroy(
    IN gcoSTREAM Stream
    );

gceSTATUS
gcoSTREAM_Upload(
    IN gcoSTREAM Stream,
    IN gctCONST_POINTER Buffer,
    IN gctSIZE_T Offset,
    IN gctSIZE_T Bytes,
    IN gctBOOL Dynamic
    );

gceSTATUS
gcoSTREAM_SetStride(
    IN gcoSTREAM Stream,
    IN gctUINT32 Stride
    );

gceSTATUS
gcoSTREAM_Size(
    IN gcoSTREAM Stream,
    OUT gctSIZE_T *Size
    );

gceSTATUS
gcoSTREAM_Node(
    IN gcoSTREAM Stream,
    OUT gcsSURF_NODE_PTR * Node
    );

gceSTATUS
gcoSTREAM_Lock(
    IN gcoSTREAM Stream,
    OUT gctPOINTER * Logical,
    OUT gctUINT32 * Physical
    );

gceSTATUS
gcoSTREAM_Unlock(
    IN gcoSTREAM Stream
    );

gceSTATUS
gcoSTREAM_Reserve(
    IN gcoSTREAM Stream,
    IN gctSIZE_T Bytes
    );

gceSTATUS
gcoSTREAM_Flush(
    IN gcoSTREAM Stream
    );

/* Dynamic buffer API. */
gceSTATUS
gcoSTREAM_SetDynamic(
    IN gcoSTREAM Stream,
    IN gctSIZE_T Bytes,
    IN gctUINT Buffers
    );

typedef struct _gcsSTREAM_INFO
{
    gctUINT             index;
    gceVERTEX_FORMAT    format;
    gctBOOL             normalized;
    gctUINT             components;
    gctSIZE_T           size;
    gctCONST_POINTER    data;
    gctUINT             stride;
}
gcsSTREAM_INFO, * gcsSTREAM_INFO_PTR;

gceSTATUS
gcoSTREAM_UploadDynamic(
    IN gcoSTREAM Stream,
    IN gctUINT VertexCount,
    IN gctUINT InfoCount,
    IN gcsSTREAM_INFO_PTR Info,
    IN gcoVERTEX Vertex
    );

gceSTATUS
gcoSTREAM_CPUCacheOperation(
    IN gcoSTREAM Stream,
    IN gceCACHEOPERATION Operation
    );

gceSTATUS
gcoSTREAM_CPUCacheOperation_Range(
    IN gcoSTREAM Stream,
    IN gctSIZE_T Offset,
    IN gctSIZE_T Length,
    IN gceCACHEOPERATION Operation
    );

/******************************************************************************\
******************************** gcoVERTEX Object ******************************
\******************************************************************************/

typedef struct _gcsVERTEX_ATTRIBUTES
{
    gceVERTEX_FORMAT            format;
    gctBOOL                     normalized;
    gctUINT32                   components;
    gctSIZE_T                   size;
    gctUINT32                   stream;
    gctUINT32                   offset;
    gctUINT32                   stride;
}
gcsVERTEX_ATTRIBUTES;

gceSTATUS
gcoVERTEX_Construct(
    IN gcoHAL Hal,
    OUT gcoVERTEX * Vertex
    );

gceSTATUS
gcoVERTEX_Destroy(
    IN gcoVERTEX Vertex
    );

gceSTATUS
gcoVERTEX_Reset(
    IN gcoVERTEX Vertex
    );

gceSTATUS
gcoVERTEX_EnableAttribute(
    IN gcoVERTEX Vertex,
    IN gctUINT32 Index,
    IN gceVERTEX_FORMAT Format,
    IN gctBOOL Normalized,
    IN gctUINT32 Components,
    IN gcoSTREAM Stream,
    IN gctUINT32 Offset,
    IN gctUINT32 Stride
    );

gceSTATUS
gcoVERTEX_DisableAttribute(
    IN gcoVERTEX Vertex,
    IN gctUINT32 Index
    );

gceSTATUS
gcoVERTEX_Bind(
    IN gcoVERTEX Vertex
    );

/*******************************************************************************
***** gcoVERTEXARRAY Object ***************************************************/

typedef struct _gcsATTRIBUTE
{
    /* Enabled. */
    gctBOOL             enable;

    /* Number of components. */
    gctINT              size;

    /* Attribute format. */
    gceVERTEX_FORMAT    format;

    /* Flag whether the attribute is normalized or not. */
    gctBOOL             normalized;

    /* Stride of the component. */
    gctSIZE_T           stride;

    /* Divisor of the attribute */
    gctUINT             divisor;

    /* Pointer to the attribute data. */
    gctCONST_POINTER    pointer;

    /* Stream object owning the attribute data. */
    gcoBUFOBJ           stream;

    /* Generic values for attribute. */
    gctFLOAT            genericValue[4];

    /* Generic size for attribute. */
    gctINT              genericSize;

    /* Vertex shader linkage. */
    gctUINT             linkage;

#if gcdUSE_WCLIP_PATCH
    /* Does it hold positions? */
    gctBOOL             isPosition;
#endif

    /* Index to vertex array */
    gctINT              arrayIdx;

    gceATTRIB_SCHEME    convertScheme;

    /* Pointer to the temporary buffer to be freed */
    gcoBUFOBJ           tempStream;

    /* Pointer to the temporary memory to be freed */
    gctCONST_POINTER    tempMemory;
}
gcsATTRIBUTE,
* gcsATTRIBUTE_PTR;


typedef struct _gcsVERTEXARRAY
{
    /* Enabled. */
    gctBOOL             enable;

    /* Number of components. */
    gctINT              size;

    /* Attribute format. */
    gceVERTEX_FORMAT    format;

    /* Flag whether the attribute is normalized or not. */
    gctBOOL             normalized;

    /* Stride of the component. */
    gctUINT             stride;

    /* Divisor of the attribute */
    gctUINT             divisor;

    /* Pointer to the attribute data. */
    gctCONST_POINTER    pointer;

    /* Stream object owning the attribute data. */
    gcoSTREAM           stream;

    /* Generic values for attribute. */
    gctFLOAT            genericValue[4];

    /* Generic size for attribute. */
    gctINT              genericSize;

    /* Vertex shader linkage. */
    gctUINT             linkage;

    gctBOOL             isPosition;
}
gcsVERTEXARRAY,
* gcsVERTEXARRAY_PTR;

gceSTATUS
gcoVERTEXARRAY_Construct(
    IN gcoHAL Hal,
    OUT gcoVERTEXARRAY * Vertex
    );

gceSTATUS
gcoVERTEXARRAY_Destroy(
    IN gcoVERTEXARRAY Vertex
    );

gceSTATUS
gcoVERTEXARRAY_IndexUpdate(
    IN gctSIZE_T * Count,
    IN gceINDEX_TYPE IndexType,
    IN gcoBUFOBJ IndexObject,
    IN gctPOINTER IndexMemory
    );

gceSTATUS
gcoVERTEXARRAY_Bind_Ex(
    IN gcoVERTEXARRAY Vertex,
    IN gctUINT32 EnableBits,
    IN gcsVERTEXARRAY_PTR VertexArray,
    IN gctUINT First,
    IN gctSIZE_T * Count,
    IN gctBOOL DrawArraysInstanced,
    IN gctSIZE_T InstanceCount,
    IN gceINDEX_TYPE IndexType,
    IN gcoINDEX IndexObject,
    IN gctPOINTER IndexMemory,
    IN OUT gcePRIMITIVE * PrimitiveType,
    IN OUT gctBOOL * SpilitDraw,
    IN OUT gctSIZE_T * SpilitCount,
    IN OUT gcePRIMITIVE * SpilitPrimitiveType,
#if gcdUSE_WCLIP_PATCH
    IN OUT gctUINT * PrimitiveCount,
    IN OUT gctFLOAT * wLimitRms,
    IN OUT gctBOOL * wLimitDirty
#else
    IN OUT gctUINT * PrimitiveCount
#endif
    );

gceSTATUS
gcoVERTEXARRAY_Bind_Ex2(
    IN gcoVERTEXARRAY Vertex,
    IN gctUINT32 EnableBits,
    IN gcsATTRIBUTE_PTR VertexArray,
    IN gctSIZE_T First,
    IN gctSIZE_T * Count,
    IN gctBOOL DrawArraysInstanced,
    IN gctSIZE_T InstanceCount,
    IN gceINDEX_TYPE IndexType,
    IN gcoBUFOBJ IndexObject,
    IN gctPOINTER IndexMemory,
    IN gctBOOL PrimtiveRestart,
    IN OUT gcePRIMITIVE * PrimitiveType,
    IN OUT gctBOOL * SpilitDraw,
    IN OUT gctSIZE_T * SpilitCount,
    IN OUT gcePRIMITIVE * SpilitPrimitiveType,
#if gcdUSE_WCLIP_PATCH
    IN OUT gctSIZE_T * PrimitiveCount,
    IN OUT gctFLOAT * wLimitRms,
    IN OUT gctBOOL * wLimitDirty,
#else
    IN OUT gctUINT * PrimitiveCount,
#endif
    IN gctINT VertexInstanceIdLinkage
    );

gceSTATUS
gcoVERTEXARRAY_Bind(
    IN gcoVERTEXARRAY Vertex,
    IN gctUINT32 EnableBits,
    IN gcsVERTEXARRAY_PTR VertexArray,
    IN gctUINT First,
    IN gctSIZE_T * Count,
    IN gceINDEX_TYPE IndexType,
    IN gcoINDEX IndexObject,
    IN gctPOINTER IndexMemory,
    IN OUT gcePRIMITIVE * PrimitiveType,
    IN OUT gctBOOL * SpilitDraw,
    IN OUT gctSIZE_T * SpilitCount,
    IN OUT gcePRIMITIVE * SpilitPrimitiveType,
#if gcdUSE_WCLIP_PATCH
    IN OUT gctUINT * PrimitiveCount,
    IN OUT gctFLOAT * wLimitRms,
    IN OUT gctBOOL * wLimitDirty
#else
    IN OUT gctUINT * PrimitiveCount
#endif
    );

/*******************************************************************************
***** Composition *************************************************************/

typedef enum _gceCOMPOSITION
{
    gcvCOMPOSE_CLEAR = 1,
    gcvCOMPOSE_BLUR,
    gcvCOMPOSE_DIM,
    gcvCOMPOSE_LAYER
}
gceCOMPOSITION;

typedef struct _gcsCOMPOSITION * gcsCOMPOSITION_PTR;
typedef struct _gcsCOMPOSITION
{
    /* Structure size. */
    gctUINT                         structSize;

    /* Composition operation. */
    gceCOMPOSITION                  operation;

    /* Layer to be composed. */
    gcoSURF                         layer;

    /* Source and target coordinates. */
    gcsRECT                         srcRect;
    gcsRECT                         trgRect;

    /* Target rectangle */
    gcsPOINT                        v0;
    gcsPOINT                        v1;
    gcsPOINT                        v2;

    /* Blending parameters. */
    gctBOOL                         enableBlending;
    gctBOOL                         premultiplied;
    gctUINT8                        alphaValue;

    /* Clear color. */
    gctFLOAT                        r;
    gctFLOAT                        g;
    gctFLOAT                        b;
    gctFLOAT                        a;
}
gcsCOMPOSITION;

gceSTATUS
gco3D_ProbeComposition(
    IN gcoHARDWARE Hardware,
    IN gctBOOL ResetIfEmpty
    );

gceSTATUS
gco3D_CompositionBegin(
    IN gcoHARDWARE Hardware
    );

gceSTATUS
gco3D_ComposeLayer(
    IN gcoHARDWARE Hardware,
    IN gcsCOMPOSITION_PTR Layer
    );

gceSTATUS
gco3D_CompositionSignals(
    IN gcoHARDWARE Hardware,
    IN gctHANDLE Process,
    IN gctSIGNAL Signal1,
    IN gctSIGNAL Signal2
    );

gceSTATUS
gco3D_CompositionEnd(
    IN gcoHARDWARE Hardware,
    IN gcoSURF Target,
    IN gctBOOL Synchronous
    );

/* Frame Database */
gceSTATUS
gcoHAL_AddFrameDB(
    void
    );

gceSTATUS
gcoHAL_DumpFrameDB(
    gctCONST_STRING Filename OPTIONAL
    );

/******************************************************************************
**********************gcoBUFOBJ object*****************************************
*******************************************************************************/
typedef enum _gceBUFOBJ_TYPE
{
    gcvBUFOBJ_TYPE_ARRAY_BUFFER = 1,
    gcvBUFOBJ_TYPE_ELEMENT_ARRAY_BUFFER  = 2,
    gcvBUFOBJ_TYPE_GENERIC_BUFFER = 100

} gceBUFOBJ_TYPE;

typedef enum _gceBUFOBJ_USAGE
{
    gcvBUFOBJ_USAGE_STREAM_DRAW = 1,
    gcvBUFOBJ_USAGE_STREAM_READ,
    gcvBUFOBJ_USAGE_STREAM_COPY,
    gcvBUFOBJ_USAGE_STATIC_DRAW,
    gcvBUFOBJ_USAGE_STATIC_READ,
    gcvBUFOBJ_USAGE_STATIC_COPY,
    gcvBUFOBJ_USAGE_DYNAMIC_DRAW,
    gcvBUFOBJ_USAGE_DYNAMIC_READ,
    gcvBUFOBJ_USAGE_DYNAMIC_COPY,

    /* special patch for optimaize performance,
    ** no fence and duplicate stream to ensure data correct
    */
    gcvBUFOBJ_USAGE_DISABLE_FENCE_DYNAMIC_STREAM = 256
} gceBUFOBJ_USAGE;

/* Construct a new gcoBUFOBJ object. */
gceSTATUS
gcoBUFOBJ_Construct(
    IN gcoHAL Hal,
    IN gceBUFOBJ_TYPE Type,
    OUT gcoBUFOBJ * BufObj
    );

/* Destroy a gcoBUFOBJ object. */
gceSTATUS
gcoBUFOBJ_Destroy(
    IN gcoBUFOBJ BufObj
    );

/* Lock pbo in memory. */
gceSTATUS
gcoBUFOBJ_Lock(
    IN gcoBUFOBJ BufObj,
    OUT gctUINT32 * Address,
    OUT gctPOINTER * Memory
    );

/* Lock pbo in memory. */
gceSTATUS
gcoBUFOBJ_FastLock(
    IN gcoBUFOBJ BufObj,
    OUT gctUINT32 * Address,
    OUT gctPOINTER * Memory
    );

/* Unlock pbo that was previously locked with gcoBUFOBJ_Lock. */
gceSTATUS
gcoBUFOBJ_Unlock(
    IN gcoBUFOBJ BufObj
    );

/* Free existing pbo buffer. */
gceSTATUS
gcoBUFOBJ_Free(
    IN gcoBUFOBJ BufObj
    );

/* Upload data into an pbo buffer. */
gceSTATUS
gcoBUFOBJ_Upload(
    IN gcoBUFOBJ BufObj,
    IN gctCONST_POINTER Buffer,
    IN gctSIZE_T Offset,
    IN gctSIZE_T Bytes,
    IN gceBUFOBJ_USAGE Usage
    );

/* Bind an index object to the hardware. */
gceSTATUS
gcoBUFOBJ_IndexBind (
    IN gcoBUFOBJ Index,
    IN gceINDEX_TYPE Type,
    IN gctUINT32 Offset,
    IN gctSIZE_T Count
    );

/* Find min and max index for the index buffer */
gceSTATUS
gcoBUFOBJ_IndexGetRange(
    IN gcoBUFOBJ Index,
    IN gceINDEX_TYPE Type,
    IN gctUINT32 Offset,
    IN gctUINT32 Count,
    OUT gctUINT32 * MinimumIndex,
    OUT gctUINT32 * MaximumIndex
    );

/*  Sets a buffer object as dirty */
gceSTATUS
gcoBUFOBJ_SetDirty(
    IN gcoBUFOBJ BufObj
    );

/* Creates a new buffer if needed */
gceSTATUS
gcoBUFOBJ_AlignIndexBufferWhenNeeded(
    IN gcoBUFOBJ BufObj,
    IN gctSIZE_T Offset,
    OUT gcoBUFOBJ * AlignedBufObj
    );

/* Cache operations on whole range */
gceSTATUS
gcoBUFOBJ_CPUCacheOperation(
    IN gcoBUFOBJ BufObj,
    IN gceCACHEOPERATION Operation
    );

/* Cache operations on a specified range */
gceSTATUS
gcoBUFOBJ_CPUCacheOperation_Range(
    IN gcoBUFOBJ BufObj,
    IN gctSIZE_T Offset,
    IN gctSIZE_T Length,
    IN gceCACHEOPERATION Operation
    );

/* Return size of the bufobj */
gceSTATUS
gcoBUFOBJ_GetSize(
    IN gcoBUFOBJ BufObj,
    OUT gctSIZE_T_PTR Size
    );

/* Return memory node of the bufobj */
gceSTATUS
gcoBUFOBJ_GetNode(
    IN gcoBUFOBJ BufObj,
    OUT gcsSURF_NODE_PTR * Node
    );

/* Handle GPU cache operations */
gceSTATUS
gcoBUFOBJ_GPUCacheOperation(
    gcoBUFOBJ BufObj
    );

/* Dump buffer. */
void
gcoBUFOBJ_Dump(
    IN gcoBUFOBJ BufObj
    );

#ifdef __cplusplus
}
#endif

#endif /* gcdENABLE_3D */
#endif /* __gc_hal_engine_h_ */
