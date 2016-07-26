/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
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
*    Copyright (C) 2014 - 2016 Vivante Corporation
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


#ifndef __gc_hal_engine_vg_h_
#define __gc_hal_engine_vg_h_

#ifdef __cplusplus
extern "C" {
#endif

#include "gc_hal_types.h"

/******************************************************************************\
******************************** VG Enumerations *******************************
\******************************************************************************/

/**
**  @ingroup gcoVG
**
**  @brief  Tiling mode for painting and imagig.
**
**  This enumeration defines the tiling modes supported by the HAL.  This is
**  in fact a one-to-one mapping of the OpenVG 1.1 tile modes.
*/
typedef enum _gceTILE_MODE
{
    gcvTILE_FILL,
    gcvTILE_PAD,
    gcvTILE_REPEAT,
    gcvTILE_REFLECT
}
gceTILE_MODE;

/******************************************************************************/
/** @ingroup gcoVG
**
**  @brief  The different paint modes.
**
**  This enumeration lists the available paint modes.
*/
typedef enum _gcePAINT_TYPE
{
    /** Solid color. */
    gcvPAINT_MODE_SOLID,

    /** Linear gradient. */
    gcvPAINT_MODE_LINEAR,

    /** Radial gradient. */
    gcvPAINT_MODE_RADIAL,

    /** Pattern. */
    gcvPAINT_MODE_PATTERN,

    /** Mode count. */
    gcvPAINT_MODE_COUNT
}
gcePAINT_TYPE;

/**
** @ingroup gcoVG
**
**  @brief Types of path data supported by HAL.
**
**  This enumeration defines the types of path data supported by the HAL.
**  This is in fact a one-to-one mapping of the OpenVG 1.1 path types.
*/
typedef enum _gcePATHTYPE
{
    gcePATHTYPE_UNKNOWN = -1,
    gcePATHTYPE_INT8,
    gcePATHTYPE_INT16,
    gcePATHTYPE_INT32,
    gcePATHTYPE_FLOAT
}
gcePATHTYPE;

/**
** @ingroup gcoVG
**
**  @brief Supported path segment commands.
**
**  This enumeration defines the path segment commands supported by the HAL.
*/
typedef enum _gceVGCMD
{
    gcvVGCMD_END,                        /*  0: GCCMD_TS_OPCODE_END           */
    gcvVGCMD_CLOSE,                      /*  1: GCCMD_TS_OPCODE_CLOSE         */
    gcvVGCMD_MOVE,                       /*  2: GCCMD_TS_OPCODE_MOVE          */
    gcvVGCMD_MOVE_REL,                   /*  3: GCCMD_TS_OPCODE_MOVE_REL      */
    gcvVGCMD_LINE,                       /*  4: GCCMD_TS_OPCODE_LINE          */
    gcvVGCMD_LINE_REL,                   /*  5: GCCMD_TS_OPCODE_LINE_REL      */
    gcvVGCMD_QUAD,                       /*  6: GCCMD_TS_OPCODE_QUADRATIC     */
    gcvVGCMD_QUAD_REL,                   /*  7: GCCMD_TS_OPCODE_QUADRATIC_REL */
    gcvVGCMD_CUBIC,                      /*  8: GCCMD_TS_OPCODE_CUBIC         */
    gcvVGCMD_CUBIC_REL,                  /*  9: GCCMD_TS_OPCODE_CUBIC_REL     */
    gcvVGCMD_BREAK,                      /* 10: GCCMD_TS_OPCODE_BREAK         */
    gcvVGCMD_HLINE,                      /* 11: ******* R E S E R V E D *******/
    gcvVGCMD_HLINE_REL,                  /* 12: ******* R E S E R V E D *******/
    gcvVGCMD_VLINE,                      /* 13: ******* R E S E R V E D *******/
    gcvVGCMD_VLINE_REL,                  /* 14: ******* R E S E R V E D *******/
    gcvVGCMD_SQUAD,                      /* 15: ******* R E S E R V E D *******/
    gcvVGCMD_SQUAD_REL,                  /* 16: ******* R E S E R V E D *******/
    gcvVGCMD_SCUBIC,                     /* 17: ******* R E S E R V E D *******/
    gcvVGCMD_SCUBIC_REL,                 /* 18: ******* R E S E R V E D *******/
    gcvVGCMD_SCCWARC,                    /* 19: ******* R E S E R V E D *******/
    gcvVGCMD_SCCWARC_REL,                /* 20: ******* R E S E R V E D *******/
    gcvVGCMD_SCWARC,                     /* 21: ******* R E S E R V E D *******/
    gcvVGCMD_SCWARC_REL,                 /* 22: ******* R E S E R V E D *******/
    gcvVGCMD_LCCWARC,                    /* 23: ******* R E S E R V E D *******/
    gcvVGCMD_LCCWARC_REL,                /* 24: ******* R E S E R V E D *******/
    gcvVGCMD_LCWARC,                     /* 25: ******* R E S E R V E D *******/
    gcvVGCMD_LCWARC_REL,                 /* 26: ******* R E S E R V E D *******/

    /* The width of the command recognized by the hardware on bits. */
    gcvVGCMD_WIDTH = 5,

    /* Hardware command mask. */
    gcvVGCMD_MASK = (1 << gcvVGCMD_WIDTH) - 1,

    /* Command modifiers. */
    gcvVGCMD_H_MOD   = 1 << gcvVGCMD_WIDTH,  /* =  32 */
    gcvVGCMD_V_MOD   = 2 << gcvVGCMD_WIDTH,  /* =  64 */
    gcvVGCMD_S_MOD   = 3 << gcvVGCMD_WIDTH,  /* =  96 */
    gcvVGCMD_ARC_MOD = 4 << gcvVGCMD_WIDTH,  /* = 128 */

    /* Emulated LINE commands. */
    gcvVGCMD_HLINE_EMUL     = gcvVGCMD_H_MOD | gcvVGCMD_LINE,        /* =  36 */
    gcvVGCMD_HLINE_EMUL_REL = gcvVGCMD_H_MOD | gcvVGCMD_LINE_REL,    /* =  37 */
    gcvVGCMD_VLINE_EMUL     = gcvVGCMD_V_MOD | gcvVGCMD_LINE,        /* =  68 */
    gcvVGCMD_VLINE_EMUL_REL = gcvVGCMD_V_MOD | gcvVGCMD_LINE_REL,    /* =  69 */

    /* Emulated SMOOTH commands. */
    gcvVGCMD_SQUAD_EMUL      = gcvVGCMD_S_MOD | gcvVGCMD_QUAD,       /* = 102 */
    gcvVGCMD_SQUAD_EMUL_REL  = gcvVGCMD_S_MOD | gcvVGCMD_QUAD_REL,   /* = 103 */
    gcvVGCMD_SCUBIC_EMUL     = gcvVGCMD_S_MOD | gcvVGCMD_CUBIC,      /* = 104 */
    gcvVGCMD_SCUBIC_EMUL_REL = gcvVGCMD_S_MOD | gcvVGCMD_CUBIC_REL,  /* = 105 */

    /* Emulation ARC commands. */
    gcvVGCMD_ARC_LINE     = gcvVGCMD_ARC_MOD | gcvVGCMD_LINE,        /* = 132 */
    gcvVGCMD_ARC_LINE_REL = gcvVGCMD_ARC_MOD | gcvVGCMD_LINE_REL,    /* = 133 */
    gcvVGCMD_ARC_QUAD     = gcvVGCMD_ARC_MOD | gcvVGCMD_QUAD,        /* = 134 */
    gcvVGCMD_ARC_QUAD_REL = gcvVGCMD_ARC_MOD | gcvVGCMD_QUAD_REL     /* = 135 */
}
gceVGCMD;
typedef enum _gceVGCMD * gceVGCMD_PTR;

/**
**  @ingroup gcoVG
**
**  @brief  Blending modes supported by the HAL.
**
**  This enumeration defines the blending modes supported by the HAL.  This is
**  in fact a one-to-one mapping of the OpenVG 1.1 blending modes.
*/
typedef enum _gceVG_BLEND
{
    gcvVG_BLEND_SRC,
    gcvVG_BLEND_SRC_OVER,
    gcvVG_BLEND_DST_OVER,
    gcvVG_BLEND_SRC_IN,
    gcvVG_BLEND_DST_IN,
    gcvVG_BLEND_MULTIPLY,
    gcvVG_BLEND_SCREEN,
    gcvVG_BLEND_DARKEN,
    gcvVG_BLEND_LIGHTEN,
    gcvVG_BLEND_ADDITIVE,
    gcvVG_BLEND_SUBTRACT,
    gcvVG_BLEND_FILTER
}
gceVG_BLEND;

/**
**  @ingroup gcoVG
**
**  @brief  Image modes supported by the HAL.
**
**  This enumeration defines the image modes supported by the HAL.  This is
**  in fact a one-to-one mapping of the OpenVG 1.1 image modes with the addition
**  of NO IMAGE.
*/
typedef enum _gceVG_IMAGE
{
    gcvVG_IMAGE_NONE,
    gcvVG_IMAGE_NORMAL,
    gcvVG_IMAGE_MULTIPLY,
    gcvVG_IMAGE_STENCIL,
    gcvVG_IMAGE_FILTER
}
gceVG_IMAGE;

/**
**  @ingroup gcoVG
**
**  @brief  Filter mode patterns and imaging.
**
**  This enumeration defines the filter modes supported by the HAL.
*/
typedef enum _gceIMAGE_FILTER
{
    gcvFILTER_POINT,
    gcvFILTER_LINEAR,
    gcvFILTER_BI_LINEAR
}
gceIMAGE_FILTER;

/**
**  @ingroup gcoVG
**
**  @brief  Primitive modes supported by the HAL.
**
**  This enumeration defines the primitive modes supported by the HAL.
*/
typedef enum _gceVG_PRIMITIVE
{
    gcvVG_SCANLINE,
    gcvVG_RECTANGLE,
    gcvVG_TESSELLATED,
    gcvVG_TESSELLATED_TILED
}
gceVG_PRIMITIVE;

/**
**  @ingroup gcoVG
**
**  @brief  Rendering quality modes supported by the HAL.
**
**  This enumeration defines the rendering quality modes supported by the HAL.
*/
typedef enum _gceRENDER_QUALITY
{
    gcvVG_NONANTIALIASED,
    gcvVG_2X2_MSAA,
    gcvVG_2X4_MSAA,
    gcvVG_4X4_MSAA
}
gceRENDER_QUALITY;

/**
**  @ingroup gcoVG
**
**  @brief  Fill rules supported by the HAL.
**
**  This enumeration defines the fill rules supported by the HAL.
*/
typedef enum _gceFILL_RULE
{
    gcvVG_EVEN_ODD,
    gcvVG_NON_ZERO
}
gceFILL_RULE;

/**
**  @ingroup gcoVG
**
**  @brief  Cap styles supported by the HAL.
**
**  This enumeration defines the cap styles supported by the HAL.
*/
typedef enum _gceCAP_STYLE
{
    gcvCAP_BUTT,
    gcvCAP_ROUND,
    gcvCAP_SQUARE
}
gceCAP_STYLE;

/**
**  @ingroup gcoVG
**
**  @brief  Join styles supported by the HAL.
**
**  This enumeration defines the join styles supported by the HAL.
*/
typedef enum _gceJOIN_STYLE
{
    gcvJOIN_MITER,
    gcvJOIN_ROUND,
    gcvJOIN_BEVEL
}
gceJOIN_STYLE;

/**
**  @ingroup gcoVG
**
**  @brief  Channel mask values.
**
**  This enumeration defines the values for channel mask used in image
**  filtering.
*/

/* Base values for channel mask definitions. */
#define gcvCHANNEL_X    (0)
#define gcvCHANNEL_R    (1 << 0)
#define gcvCHANNEL_G    (1 << 1)
#define gcvCHANNEL_B    (1 << 2)
#define gcvCHANNEL_A    (1 << 3)

typedef enum _gceCHANNEL
{
    gcvCHANNEL_XXXX = (gcvCHANNEL_X | gcvCHANNEL_X | gcvCHANNEL_X | gcvCHANNEL_X),
    gcvCHANNEL_XXXA = (gcvCHANNEL_X | gcvCHANNEL_X | gcvCHANNEL_X | gcvCHANNEL_A),
    gcvCHANNEL_XXBX = (gcvCHANNEL_X | gcvCHANNEL_X | gcvCHANNEL_B | gcvCHANNEL_X),
    gcvCHANNEL_XXBA = (gcvCHANNEL_X | gcvCHANNEL_X | gcvCHANNEL_B | gcvCHANNEL_A),

    gcvCHANNEL_XGXX = (gcvCHANNEL_X | gcvCHANNEL_G | gcvCHANNEL_X | gcvCHANNEL_X),
    gcvCHANNEL_XGXA = (gcvCHANNEL_X | gcvCHANNEL_G | gcvCHANNEL_X | gcvCHANNEL_A),
    gcvCHANNEL_XGBX = (gcvCHANNEL_X | gcvCHANNEL_G | gcvCHANNEL_B | gcvCHANNEL_X),
    gcvCHANNEL_XGBA = (gcvCHANNEL_X | gcvCHANNEL_G | gcvCHANNEL_B | gcvCHANNEL_A),

    gcvCHANNEL_RXXX = (gcvCHANNEL_R | gcvCHANNEL_X | gcvCHANNEL_X | gcvCHANNEL_X),
    gcvCHANNEL_RXXA = (gcvCHANNEL_R | gcvCHANNEL_X | gcvCHANNEL_X | gcvCHANNEL_A),
    gcvCHANNEL_RXBX = (gcvCHANNEL_R | gcvCHANNEL_X | gcvCHANNEL_B | gcvCHANNEL_X),
    gcvCHANNEL_RXBA = (gcvCHANNEL_R | gcvCHANNEL_X | gcvCHANNEL_B | gcvCHANNEL_A),

    gcvCHANNEL_RGXX = (gcvCHANNEL_R | gcvCHANNEL_G | gcvCHANNEL_X | gcvCHANNEL_X),
    gcvCHANNEL_RGXA = (gcvCHANNEL_R | gcvCHANNEL_G | gcvCHANNEL_X | gcvCHANNEL_A),
    gcvCHANNEL_RGBX = (gcvCHANNEL_R | gcvCHANNEL_G | gcvCHANNEL_B | gcvCHANNEL_X),
    gcvCHANNEL_RGBA = (gcvCHANNEL_R | gcvCHANNEL_G | gcvCHANNEL_B | gcvCHANNEL_A),
}
gceCHANNEL;

/******************************************************************************\
******************************** VG Structures *******************************
\******************************************************************************/

/**
**  @ingroup    gcoVG
**
**  @brief      Definition of the color ramp used by the gradient paints.
**
**  The gcsCOLOR_RAMP structure defines the layout of one single color inside
**  a color ramp which is used by gradient paints.
*/
typedef struct _gcsCOLOR_RAMP
{
    /** Value for the color stop. */
    gctFLOAT        stop;

    /** Red color channel value for the color stop. */
    gctFLOAT        red;

    /** Green color channel value for the color stop. */
    gctFLOAT        green;

    /** Blue color channel value for the color stop. */
    gctFLOAT        blue;

    /** Alpha color channel value for the color stop. */
    gctFLOAT        alpha;
}
gcsCOLOR_RAMP, * gcsCOLOR_RAMP_PTR;

/**
**  @ingroup    gcoVG
**
**  @brief      Definition of the color ramp used by the gradient paints in fixed form.
**
**  The gcsCOLOR_RAMP structure defines the layout of one single color inside
**  a color ramp which is used by gradient paints.
*/
typedef struct _gcsFIXED_COLOR_RAMP
{
    /** Value for the color stop. */
    gctFIXED_POINT      stop;

    /** Red color channel value for the color stop. */
    gctFIXED_POINT      red;

    /** Green color channel value for the color stop. */
    gctFIXED_POINT      green;

    /** Blue color channel value for the color stop. */
    gctFIXED_POINT      blue;

    /** Alpha color channel value for the color stop. */
    gctFIXED_POINT      alpha;
}
gcsFIXED_COLOR_RAMP, * gcsFIXED_COLOR_RAMP_PTR;


/**
**  @ingroup gcoVG
**
**  @brief  Rectangle structure used by the gcoVG object.
**
**  This structure defines the layout of a rectangle.  Make sure width and
**  height are larger than 0.
*/
typedef struct _gcsVG_RECT * gcsVG_RECT_PTR;
typedef struct _gcsVG_RECT
{
    /** Left location of the rectangle. */
    gctINT      x;

    /** Top location of the rectangle. */
    gctINT      y;

    /** Width of the rectangle. */
    gctINT      width;

    /** Height of the rectangle. */
    gctINT      height;
}
gcsVG_RECT;

/**
**  @ingroup    gcoVG
**
**  @brief      Path command buffer attribute structure.
**
**  The gcsPATH_BUFFER_INFO structure contains the specifics about
**  the layout of the path data command buffer.
*/
typedef struct _gcsPATH_BUFFER_INFO * gcsPATH_BUFFER_INFO_PTR;
typedef struct _gcsPATH_BUFFER_INFO
{
    gctUINT     reservedForHead;
    gctUINT     reservedForTail;
}
gcsPATH_BUFFER_INFO;

/**
**  @ingroup    gcoVG
**
**  @brief      Definition of the path data container structure.
**
**  The gcsPATH structure defines the layout of the path data container.
*/
typedef struct _gcsPATH_DATA * gcsPATH_DATA_PTR;
typedef struct _gcsPATH_DATA
{
    /* Data container in command buffer format. */
    gcsCMDBUFFER    data;

    /* Path data type. */
    gcePATHTYPE     dataType;
}
gcsPATH_DATA;


/******************************************************************************\
********************************* gcoHAL Object ********************************
\******************************************************************************/

/* Query path data storage attributes. */
gceSTATUS
gcoHAL_QueryPathStorage(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    OUT gcsPATH_BUFFER_INFO_PTR Information
    );

/* Associate a completion signal with the command buffer. */
gceSTATUS
gcoHAL_AssociateCompletion(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcsPATH_DATA_PTR PathData
    );

/* Release the current command buffer completion signal. */
gceSTATUS
gcoHAL_DeassociateCompletion(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcsPATH_DATA_PTR PathData
    );

/* Verify whether the command buffer is still in use. */
gceSTATUS
gcoHAL_CheckCompletion(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcsPATH_DATA_PTR PathData
    );

/* Wait until the command buffer is no longer in use. */
gceSTATUS
gcoHAL_WaitCompletion(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcsPATH_DATA_PTR PathData
    );

/* Flush the pixel cache. */
gceSTATUS
gcoHAL_Flush(
    IN gcoHAL Hal
#if gcdGC355_PROFILER
    ,
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth
#endif
    );

/* Split a harwdare address into pool and offset. */
gceSTATUS
gcoHAL_SplitAddress(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctUINT32 Address,
    OUT gcePOOL * Pool,
    OUT gctUINT32 * Offset
    );

/* Combine pool and offset into a harwdare address. */
gceSTATUS
gcoHAL_CombineAddress(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcePOOL Pool,
    IN gctUINT32 Offset,
    OUT gctUINT32 * Address
    );

/* Schedule to free linear video memory allocated. */
gceSTATUS
gcoHAL_ScheduleVideoMemory(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctUINT32 Node
    );

/* Free linear video memory allocated with gcoHAL_AllocateLinearVideoMemory. */
gceSTATUS
gcoHAL_FreeVideoMemory(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctUINT32 Node,
    IN gctBOOL asynchroneous
    );

/* Query command buffer attributes. */
gceSTATUS
gcoHAL_QueryCommandBuffer(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    OUT gcsCOMMAND_BUFFER_INFO_PTR Information
    );
/* Allocate and lock linear video memory. */
gceSTATUS
gcoHAL_AllocateLinearVideoMemory(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctUINT Size,
    IN gctUINT Alignment,
    IN gcePOOL Pool,
    OUT gctUINT32 * Node,
    OUT gctUINT32 * Address,
    OUT gctPOINTER * Memory
    );

/* Align the specified size accordingly to the hardware requirements. */
gceSTATUS
gcoHAL_GetAlignedSurfaceSize(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceSURF_TYPE Type,
    IN OUT gctUINT32_PTR Width,
    IN OUT gctUINT32_PTR Height
    );

gceSTATUS
gcoHAL_ReserveTask(
    IN gcoHAL Hal,
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceBLOCK Block,
    IN gctUINT TaskCount,
    IN gctUINT32 Bytes,
    OUT gctPOINTER * Memory
    );
/******************************************************************************\
********************************** gcoVG Object ********************************
\******************************************************************************/

/** @defgroup gcoVG gcoVG
**
**  The gcoVG object abstracts the VG hardware pipe.
*/
#if gcdGC355_PROFILER
void
gcoVG_ProfilerEnableDisable(
    IN gcoVG Vg,
    IN gctUINT enableGetAPITimes,
    IN gctFILE apiTimeFile
    );

void
gcoVG_ProfilerTreeDepth(
    IN gcoVG Vg,
    IN gctUINT TreeDepth
    );

void
gcoVG_ProfilerSetStates(
    IN gcoVG Vg,
    IN gctUINT treeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth
    );
#endif

gctBOOL
gcoVG_IsMaskSupported(
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceSURF_FORMAT Format
    );

gctBOOL
gcoVG_IsTargetSupported(
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceSURF_FORMAT Format
    );

gctBOOL
gcoVG_IsImageSupported(
#if gcdGC355_PROFILER
    IN gcoVG Vg,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceSURF_FORMAT Format
    );

gctUINT8 gcoVG_PackColorComponent(
#if gcdGC355_PROFILER
    gcoVG Vg,
    gctUINT TreeDepth,
    gctUINT saveLayerTreeDepth,
    gctUINT varTreeDepth,
#endif
    gctFLOAT Value
    );

gceSTATUS
gcoVG_Construct(
    IN gcoHAL Hal,
    OUT gcoVG * Vg
    );

gceSTATUS
gcoVG_Destroy(
    IN gcoVG Vg
#if gcdGC355_PROFILER
    ,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth
#endif
    );

gceSTATUS
gcoVG_SetTarget(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Target,
    IN gceORIENTATION orientation
    );

gceSTATUS
gcoVG_UnsetTarget(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Surface
    );

gceSTATUS
gcoVG_SetUserToSurface(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctFLOAT UserToSurface[9]
    );

gceSTATUS
gcoVG_SetSurfaceToImage(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctFLOAT SurfaceToImage[9]
    );

gceSTATUS
gcoVG_EnableMask(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctBOOL Enable
    );

gceSTATUS
gcoVG_SetMask(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Mask
    );

gceSTATUS
gcoVG_UnsetMask(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Surface
    );

gceSTATUS
gcoVG_FlushMask(
    IN gcoVG Vg
#if gcdGC355_PROFILER
    ,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth
#endif
    );

gceSTATUS
gcoVG_EnableScissor(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctBOOL Enable
    );

gceSTATUS
gcoVG_SetScissor(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctSIZE_T RectangleCount,
    IN gcsVG_RECT_PTR Rectangles
    );

gceSTATUS
gcoVG_EnableColorTransform(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctBOOL Enable
    );

gceSTATUS
gcoVG_SetColorTransform(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctFLOAT ColorTransform[8]
    );

gceSTATUS
gcoVG_SetTileFillColor(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctFLOAT Red,
    IN gctFLOAT Green,
    IN gctFLOAT Blue,
    IN gctFLOAT Alpha
    );

gceSTATUS
gcoVG_SetSolidPaint(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctUINT8 Red,
    IN gctUINT8 Green,
    IN gctUINT8 Blue,
    IN gctUINT8 Alpha
    );

gceSTATUS
gcoVG_SetLinearPaint(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctFLOAT Constant,
    IN gctFLOAT StepX,
    IN gctFLOAT StepY
    );

gceSTATUS
gcoVG_SetRadialPaint(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctFLOAT LinConstant,
    IN gctFLOAT LinStepX,
    IN gctFLOAT LinStepY,
    IN gctFLOAT RadConstant,
    IN gctFLOAT RadStepX,
    IN gctFLOAT RadStepY,
    IN gctFLOAT RadStepXX,
    IN gctFLOAT RadStepYY,
    IN gctFLOAT RadStepXY
    );

gceSTATUS
gcoVG_SetPatternPaint(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctFLOAT UConstant,
    IN gctFLOAT UStepX,
    IN gctFLOAT UStepY,
    IN gctFLOAT VConstant,
    IN gctFLOAT VStepX,
    IN gctFLOAT VStepY,
    IN gctBOOL Linear
    );

gceSTATUS
gcoVG_SetColorRamp(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF ColorRamp,
    IN gceTILE_MODE ColorRampSpreadMode
    );

gceSTATUS
gcoVG_SetPattern(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctINT32 width,
    IN gctINT32 height,
    IN gcoSURF Pattern,
    IN gceTILE_MODE TileMode,
    IN gceIMAGE_FILTER Filter
    );

gceSTATUS
gcoVG_SetImageMode(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceVG_IMAGE Mode
    );

gceSTATUS
gcoVG_SetBlendMode(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceVG_BLEND Mode
    );

gceSTATUS
gcoVG_SetRenderingQuality(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceRENDER_QUALITY Quality
    );

gceSTATUS
gcoVG_SetFillRule(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceFILL_RULE FillRule
    );

gceSTATUS
gcoVG_FinalizePath(
    IN gcoVG Vg,
    IN gcsPATH_DATA_PTR PathData
    );

gceSTATUS
gcoVG_Clear(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctINT X,
    IN gctINT Y,
    IN gctINT Width,
    IN gctINT Height
    );

gceSTATUS
gcoVG_DrawPath(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcsPATH_DATA_PTR PathData,
    IN gctFLOAT Scale,
    IN gctFLOAT Bias,
#if gcdMOVG
    IN gctUINT32 Width,
    IN gctUINT32 Height,
    IN gctFLOAT *Bounds,
#endif
    IN gctBOOL SoftwareTesselation
    );

gceSTATUS
gcoVG_DrawImage(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gceORIENTATION orientation,
    IN gcoSURF Source,
    IN gcsPOINT_PTR SourceOrigin,
    IN gcsPOINT_PTR TargetOrigin,
    IN gcsSIZE_PTR SourceSize,
    IN gctINT SourceX,
    IN gctINT SourceY,
    IN gctINT TargetX,
    IN gctINT TargetY,
    IN gctINT Width,
    IN gctINT Height,
    IN gctBOOL Mask,
    IN gctBOOL isDrawImage
    );

gceSTATUS
gcoVG_TesselateImage(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Image,
    IN gcsVG_RECT_PTR Rectangle,
    IN gceIMAGE_FILTER Filter,
    IN gctBOOL Mask,
#if gcdMOVG
    IN gctBOOL SoftwareTesselation,
    IN gceVG_BLEND BlendMode,
    IN gctINT Width,
    IN gctINT Height
#else
    IN gctBOOL SoftwareTesselation
#endif
    );

gceSTATUS
gcoVG_DrawSurfaceToImage(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Image,
    IN const gcsVG_RECT_PTR SrcRectangle,
    IN const gcsVG_RECT_PTR DstRectangle,
    IN const gctFLOAT Matrix[9],
    IN gceIMAGE_FILTER Filter,
    IN gctBOOL Mask,
    IN gctBOOL FirstTime
    );

gceSTATUS
gcoVG_Blit(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Source,
    IN gcoSURF Target,
    IN gcsVG_RECT_PTR SrcRect,
    IN gcsVG_RECT_PTR TrgRect,
    IN gceIMAGE_FILTER Filter,
    IN gceVG_BLEND Mode
    );

gceSTATUS
gcoVG_ColorMatrix(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Source,
    IN gcoSURF Target,
    IN const gctFLOAT * Matrix,
    IN gceCHANNEL ColorChannels,
    IN gctBOOL FilterLinear,
    IN gctBOOL FilterPremultiplied,
    IN gcsPOINT_PTR SourceOrigin,
    IN gcsPOINT_PTR TargetOrigin,
    IN gctINT Width,
    IN gctINT Height
    );

gceSTATUS
gcoVG_SeparableConvolve(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Source,
    IN gcoSURF Target,
    IN gctINT KernelWidth,
    IN gctINT KernelHeight,
    IN gctINT ShiftX,
    IN gctINT ShiftY,
    IN const gctINT16 * KernelX,
    IN const gctINT16 * KernelY,
    IN gctFLOAT Scale,
    IN gctFLOAT Bias,
    IN gceTILE_MODE TilingMode,
    IN gctFLOAT_PTR FillColor,
    IN gceCHANNEL ColorChannels,
    IN gctBOOL FilterLinear,
    IN gctBOOL FilterPremultiplied,
    IN gcsPOINT_PTR SourceOrigin,
    IN gcsPOINT_PTR TargetOrigin,
    IN gcsSIZE_PTR SourceSize,
    IN gctINT Width,
    IN gctINT Height
    );

gceSTATUS
gcoVG_GaussianBlur(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gcoSURF Source,
    IN gcoSURF Target,
    IN gctFLOAT StdDeviationX,
    IN gctFLOAT StdDeviationY,
    IN gceTILE_MODE TilingMode,
    IN gctFLOAT_PTR FillColor,
    IN gceCHANNEL ColorChannels,
    IN gctBOOL FilterLinear,
    IN gctBOOL FilterPremultiplied,
    IN gcsPOINT_PTR SourceOrigin,
    IN gcsPOINT_PTR TargetOrigin,
    IN gcsSIZE_PTR SourceSize,
    IN gctINT Width,
    IN gctINT Height
    );

gceSTATUS
gcoVG_EnableDither(
    IN gcoVG Vg,
#if gcdGC355_PROFILER
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctBOOL Enable
    );

/* Color Key States. */
gceSTATUS
gcoVG_SetColorKey(
    IN gcoVG        Vg,
#if gcdGC355_PROFILER
    IN gcsPROFILERFUNCNODE *DList,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctUINT32*    Values,
    IN gctBOOL *    Enables
);

/* Index Color States. */
gceSTATUS
gcoVG_SetColorIndexTable(
    IN gcoVG        Vg,
#if gcdGC355_PROFILER
    IN gcsPROFILERFUNCNODE *DList,
    IN gctUINT TreeDepth,
    IN gctUINT saveLayerTreeDepth,
    IN gctUINT varTreeDepth,
#endif
    IN gctUINT32*    Values,
    IN gctINT32      Count
);

/* VG RS feature support: YUV format conversion. */
gceSTATUS
gcoVG_Resolve(
    IN gcoVG        Vg,
    IN gcoSURF      Source,
    IN gcoSURF      Target,
    IN gctINT       SX,
    IN gctINT       SY,
    IN gctINT       DX,
    IN gctINT       DY,
    IN gctINT       Width,
    IN gctINT       Height,
    IN gctINT       Src_uv,
    IN gctINT       Src_standard,
    IN gctINT       Dst_uv,
    IN gctINT       Dst_standard,
    IN gctINT       Dst_alpha
);
#ifdef __cplusplus
}
#endif

#endif  /* __gc_hal_vg_h_ */
