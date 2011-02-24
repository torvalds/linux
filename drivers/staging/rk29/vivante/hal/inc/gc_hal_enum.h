/****************************************************************************
*
*    Copyright (C) 2005 - 2011 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#ifndef __gc_hal_enum_h_
#define __gc_hal_enum_h_

#ifdef __cplusplus
extern "C" {
#endif

/* Chip models. */
typedef enum _gceCHIPMODEL
{
    gcv300  = 0x0300,
    gcv400  = 0x0400,
    gcv410  = 0x0410,
    gcv450  = 0x0450,
    gcv500  = 0x0500,
    gcv530  = 0x0530,
    gcv600  = 0x0600,
    gcv700  = 0x0700,
    gcv800  = 0x0800,
    gcv860  = 0x0860,
    gcv1000 = 0x1000,
}
gceCHIPMODEL;

/* Chip features. */
typedef enum _gceFEATURE
{
    gcvFEATURE_PIPE_2D,
    gcvFEATURE_PIPE_3D,
    gcvFEATURE_PIPE_VG,
    gcvFEATURE_DC,
    gcvFEATURE_HIGH_DYNAMIC_RANGE,
    gcvFEATURE_MODULE_CG,
    gcvFEATURE_MIN_AREA,
    gcvFEATURE_BUFFER_INTERLEAVING,
    gcvFEATURE_BYTE_WRITE_2D,
    gcvFEATURE_ENDIANNESS_CONFIG,
    gcvFEATURE_DUAL_RETURN_BUS,
    gcvFEATURE_DEBUG_MODE,
    gcvFEATURE_YUY2_RENDER_TARGET,
    gcvFEATURE_FRAGMENT_PROCESSOR,
    gcvFEATURE_2DPE20,
    gcvFEATURE_FAST_CLEAR,
    gcvFEATURE_YUV420_TILER,
    gcvFEATURE_YUY2_AVERAGING,
    gcvFEATURE_FLIP_Y,
    gcvFEATURE_EARLY_Z,
    gcvFEATURE_Z_COMPRESSION,
    gcvFEATURE_MSAA,
    gcvFEATURE_SPECIAL_ANTI_ALIASING,
    gcvFEATURE_SPECIAL_MSAA_LOD,
    gcvFEATURE_422_TEXTURE_COMPRESSION,
    gcvFEATURE_DXT_TEXTURE_COMPRESSION,
    gcvFEATURE_ETC1_TEXTURE_COMPRESSION,
    gcvFEATURE_CORRECT_TEXTURE_CONVERTER,
    gcvFEATURE_TEXTURE_8K,
    gcvFEATURE_SCALER,
    gcvFEATURE_YUV420_SCALER,
    gcvFEATURE_SHADER_HAS_W,
    gcvFEATURE_SHADER_HAS_SIGN,
    gcvFEATURE_SHADER_HAS_FLOOR,
    gcvFEATURE_SHADER_HAS_CEIL,
    gcvFEATURE_SHADER_HAS_SQRT,
    gcvFEATURE_SHADER_HAS_TRIG,
    gcvFEATURE_VAA,
    gcvFEATURE_HZ,
    gcvFEATURE_CORRECT_STENCIL,
    gcvFEATURE_VG20,
    gcvFEATURE_VG_FILTER,
    gcvFEATURE_VG21,
    gcvFEATURE_VG_DOUBLE_BUFFER,
    gcvFEATURE_MC20,
    gcvFEATURE_SUPER_TILED,
    gcvFEATURE_2D_DITHER,
    gcvFEATURE_WIDE_LINE,
}
gceFEATURE;

/* Chip Power Status. */
typedef enum _gceCHIPPOWERSTATE
{
    gcvPOWER_ON,
    gcvPOWER_OFF,
    gcvPOWER_IDLE,
    gcvPOWER_SUSPEND,
    gcvPOWER_ON_BROADCAST,
    gcvPOWER_SUSPEND_ATPOWERON,
    gcvPOWER_OFF_ATPOWERON,
    gcvPOWER_IDLE_BROADCAST,
    gcvPOWER_SUSPEND_BROADCAST,
    gcvPOWER_OFF_BROADCAST,
    gcvPOWER_OFF_RECOVERY,
}
gceCHIPPOWERSTATE;

/* Surface types. */
typedef enum _gceSURF_TYPE
{
    gcvSURF_TYPE_UNKNOWN,
    gcvSURF_INDEX,
    gcvSURF_VERTEX,
    gcvSURF_TEXTURE,
    gcvSURF_RENDER_TARGET,
    gcvSURF_DEPTH,
    gcvSURF_BITMAP,
    gcvSURF_TILE_STATUS,
    gcvSURF_MASK,
    gcvSURF_SCISSOR,
    gcvSURF_HIERARCHICAL_DEPTH,
    gcvSURF_NUM_TYPES, /* Make sure this is the last one! */

    /* Combinations. */
    gcvSURF_NO_TILE_STATUS = 0x100,
    gcvSURF_RENDER_TARGET_NO_TILE_STATUS = gcvSURF_RENDER_TARGET
                                         | gcvSURF_NO_TILE_STATUS,
    gcvSURF_DEPTH_NO_TILE_STATUS = gcvSURF_DEPTH
                                 | gcvSURF_NO_TILE_STATUS,
}
gceSURF_TYPE;

typedef enum _gceSURF_COLOR_TYPE
{
    gcvSURF_COLOR_UNKNOWN,
    gcvSURF_COLOR_LINEAR        = 0x01,
    gcvSURF_COLOR_ALPHA_PRE     = 0x02,
}
gceSURF_COLOR_TYPE;

/* Rotation. */
typedef enum _gceSURF_ROTATION
{
    gcvSURF_0_DEGREE,
    gcvSURF_90_DEGREE,
    gcvSURF_180_DEGREE,
    gcvSURF_270_DEGREE
}
gceSURF_ROTATION;

/* Surface formats. */
typedef enum _gceSURF_FORMAT
{
    /* Unknown format. */
    gcvSURF_UNKNOWN,

    /* Palettized formats. */
    gcvSURF_INDEX1              = 100,
    gcvSURF_INDEX4,
    gcvSURF_INDEX8,

    /* RGB formats. */
    gcvSURF_A2R2G2B2            = 200,
    gcvSURF_R3G3B2,
    gcvSURF_A8R3G3B2,
    gcvSURF_X4R4G4B4,
    gcvSURF_A4R4G4B4,
    gcvSURF_R4G4B4A4,
    gcvSURF_X1R5G5B5,
    gcvSURF_A1R5G5B5,
    gcvSURF_R5G5B5A1,
    gcvSURF_R5G6B5,
    gcvSURF_R8G8B8,
    gcvSURF_X8R8G8B8,
    gcvSURF_A8R8G8B8,
    gcvSURF_R8G8B8A8,
    gcvSURF_G8R8G8B8,
    gcvSURF_R8G8B8G8,
    gcvSURF_X2R10G10B10,
    gcvSURF_A2R10G10B10,
    gcvSURF_X12R12G12B12,
    gcvSURF_A12R12G12B12,
    gcvSURF_X16R16G16B16,
    gcvSURF_A16R16G16B16,
    gcvSURF_R8G8B8X8,
    gcvSURF_R5G5B5X1,
    gcvSURF_R4G4B4X4,

    /* BGR formats. */
    gcvSURF_A4B4G4R4            = 300,
    gcvSURF_A1B5G5R5,
    gcvSURF_B5G6R5,
    gcvSURF_B8G8R8,
    gcvSURF_X8B8G8R8,
    gcvSURF_A8B8G8R8,
    gcvSURF_A2B10G10R10,
    gcvSURF_A16B16G16R16,
    gcvSURF_G16R16,
    gcvSURF_B4G4R4A4,
    gcvSURF_B5G5R5A1,
    gcvSURF_B8G8R8X8,
    gcvSURF_B8G8R8A8,
    gcvSURF_X4B4G4R4,
    gcvSURF_X1B5G5R5,
    gcvSURF_B4G4R4X4,
    gcvSURF_B5G5R5X1,

    /* Compressed formats. */
    gcvSURF_DXT1                = 400,
    gcvSURF_DXT2,
    gcvSURF_DXT3,
    gcvSURF_DXT4,
    gcvSURF_DXT5,
    gcvSURF_CXV8U8,
    gcvSURF_ETC1,

    /* YUV formats. */
    gcvSURF_YUY2                = 500,
    gcvSURF_UYVY,
    gcvSURF_YV12,
    gcvSURF_I420,
    gcvSURF_NV12,
    gcvSURF_NV21,
    gcvSURF_NV16,
    gcvSURF_NV61,
    gcvSURF_YVYU,
    gcvSURF_VYUY,

    /* Depth formats. */
    gcvSURF_D16                 = 600,
    gcvSURF_D24S8,
    gcvSURF_D32,
    gcvSURF_D24X8,

    /* Alpha formats. */
    gcvSURF_A4                  = 700,
    gcvSURF_A8,
    gcvSURF_A12,
    gcvSURF_A16,
    gcvSURF_A32,
    gcvSURF_A1,

    /* Luminance formats. */
    gcvSURF_L4                  = 800,
    gcvSURF_L8,
    gcvSURF_L12,
    gcvSURF_L16,
    gcvSURF_L32,
    gcvSURF_L1,

    /* Alpha/Luminance formats. */
    gcvSURF_A4L4                = 900,
    gcvSURF_A2L6,
    gcvSURF_A8L8,
    gcvSURF_A4L12,
    gcvSURF_A12L12,
    gcvSURF_A16L16,

    /* Bump formats. */
    gcvSURF_L6V5U5              = 1000,
    gcvSURF_V8U8,
    gcvSURF_X8L8V8U8,
    gcvSURF_Q8W8V8U8,
    gcvSURF_A2W10V10U10,
    gcvSURF_V16U16,
    gcvSURF_Q16W16V16U16,

    /* Floating point formats. */
    gcvSURF_R16F                = 1100,
    gcvSURF_G16R16F,
    gcvSURF_A16B16G16R16F,
    gcvSURF_R32F,
    gcvSURF_G32R32F,
    gcvSURF_A32B32G32R32F,

#if 0
    /* FIXME: remove HDR support for now. */
    /* HDR formats. */
    gcvSURF_HDR7E3              = 1200,
    gcvSURF_HDR6E4,
    gcvSURF_HDR5E5,
    gcvSURF_HDR6E5,
#endif
}
gceSURF_FORMAT;

/* Pixel swizzle modes. */
typedef enum _gceSURF_SWIZZLE
{
    gcvSURF_NOSWIZZLE,
    gcvSURF_ARGB,
    gcvSURF_ABGR,
    gcvSURF_RGBA,
    gcvSURF_BGRA
}
gceSURF_SWIZZLE;

/* Transparency modes. */
typedef enum _gceSURF_TRANSPARENCY
{
    /* Valid only for PE 1.0 */
    gcvSURF_OPAQUE,
    gcvSURF_SOURCE_MATCH,
    gcvSURF_SOURCE_MASK,
    gcvSURF_PATTERN_MASK,
}
gceSURF_TRANSPARENCY;

/* Transparency modes. */
typedef enum _gce2D_TRANSPARENCY
{
    /* Valid only for PE 2.0 */
    gcv2D_OPAQUE,
    gcv2D_KEYED,
    gcv2D_MASKED
}
gce2D_TRANSPARENCY;

/* Mono packing modes. */
typedef enum _gceSURF_MONOPACK
{
    gcvSURF_PACKED8,
    gcvSURF_PACKED16,
    gcvSURF_PACKED32,
    gcvSURF_UNPACKED,
}
gceSURF_MONOPACK;

/* Blending modes. */
typedef enum _gceSURF_BLEND_MODE
{
    /* Porter-Duff blending modes.                   */
    /*                         Fsrc      Fdst        */
    gcvBLEND_CLEAR,         /* 0         0           */
    gcvBLEND_SRC,           /* 1         0           */
    gcvBLEND_DST,           /* 0         1           */
    gcvBLEND_SRC_OVER_DST,  /* 1         1 - Asrc    */
    gcvBLEND_DST_OVER_SRC,  /* 1 - Adst  1           */
    gcvBLEND_SRC_IN_DST,    /* Adst      0           */
    gcvBLEND_DST_IN_SRC,    /* 0         Asrc        */
    gcvBLEND_SRC_OUT_DST,   /* 1 - Adst  0           */
    gcvBLEND_DST_OUT_SRC,   /* 0         1 - Asrc    */
    gcvBLEND_SRC_ATOP_DST,  /* Adst      1 - Asrc    */
    gcvBLEND_DST_ATOP_SRC,  /* 1 - Adst  Asrc        */
    gcvBLEND_SRC_XOR_DST,   /* 1 - Adst  1 - Asrc    */

    /* Special blending modes.                       */
    gcvBLEND_SET,           /* DST = 1               */
    gcvBLEND_SUB            /* DST = DST * (1 - SRC) */
}
gceSURF_BLEND_MODE;

/* Per-pixel alpha modes. */
typedef enum _gceSURF_PIXEL_ALPHA_MODE
{
    gcvSURF_PIXEL_ALPHA_STRAIGHT,
    gcvSURF_PIXEL_ALPHA_INVERSED
}
gceSURF_PIXEL_ALPHA_MODE;

/* Global alpha modes. */
typedef enum _gceSURF_GLOBAL_ALPHA_MODE
{
    gcvSURF_GLOBAL_ALPHA_OFF,
    gcvSURF_GLOBAL_ALPHA_ON,
    gcvSURF_GLOBAL_ALPHA_SCALE
}
gceSURF_GLOBAL_ALPHA_MODE;

/* Color component modes for alpha blending. */
typedef enum _gceSURF_PIXEL_COLOR_MODE
{
    gcvSURF_COLOR_STRAIGHT,
    gcvSURF_COLOR_MULTIPLY
}
gceSURF_PIXEL_COLOR_MODE;

/* Color component modes for alpha blending. */
typedef enum _gce2D_PIXEL_COLOR_MULTIPLY_MODE
{
    gcv2D_COLOR_MULTIPLY_DISABLE,
    gcv2D_COLOR_MULTIPLY_ENABLE
}
gce2D_PIXEL_COLOR_MULTIPLY_MODE;

/* Color component modes for alpha blending. */
typedef enum _gce2D_GLOBAL_COLOR_MULTIPLY_MODE
{
    gcv2D_GLOBAL_COLOR_MULTIPLY_DISABLE,
    gcv2D_GLOBAL_COLOR_MULTIPLY_ALPHA,
    gcv2D_GLOBAL_COLOR_MULTIPLY_COLOR
}
gce2D_GLOBAL_COLOR_MULTIPLY_MODE;

/* Alpha blending factor modes. */
typedef enum _gceSURF_BLEND_FACTOR_MODE
{
    gcvSURF_BLEND_ZERO,
    gcvSURF_BLEND_ONE,
    gcvSURF_BLEND_STRAIGHT,
    gcvSURF_BLEND_INVERSED,
    gcvSURF_BLEND_COLOR,
    gcvSURF_BLEND_COLOR_INVERSED,
    gcvSURF_BLEND_SRC_ALPHA_SATURATED
}
gceSURF_BLEND_FACTOR_MODE;

/* Alpha blending porter duff rules. */
typedef enum _gce2D_PORTER_DUFF_RULE
{
    gcvPD_CLEAR,
    gcvPD_SRC,
    gcvPD_SRC_OVER,
    gcvPD_DST_OVER,
    gcvPD_SRC_IN,
    gcvPD_DST_IN,
    gcvPD_SRC_OUT,
    gcvPD_DST_OUT,
    gcvPD_SRC_ATOP,
    gcvPD_DST_ATOP,
    gcvPD_ADD,
    gcvPD_XOR,
    gcvPD_DST
}
gce2D_PORTER_DUFF_RULE;

/* Alpha blending factor modes. */
typedef enum _gce2D_YUV_COLOR_MODE
{
    gcv2D_YUV_601,
    gcv2D_YUV_709
}
gce2D_YUV_COLOR_MODE;

/* 2D Rotation and flipping. */
typedef enum _gce2D_ORIENTATION
{
    gcv2D_0_DEGREE,
    gcv2D_90_DEGREE,
    gcv2D_180_DEGREE,
    gcv2D_270_DEGREE,
    gcv2D_X_FLIP,
    gcv2D_Y_FLIP
}
gce2D_ORIENTATION;

typedef enum _gce2D_COMMAND
{
    gcv2D_CLEAR,
    gcv2D_LINE,
    gcv2D_BLT,
    gcv2D_STRETCH,
    gcv2D_HOR_FILTER,
    gcv2D_VER_FILTER,
}
gce2D_COMMAND;

/* Texture functions. */
typedef enum _gceTEXTURE_FUNCTION
{
    gcvTEXTURE_DUMMY = 0,
    gcvTEXTURE_REPLACE = 0,
    gcvTEXTURE_MODULATE,
    gcvTEXTURE_ADD,
    gcvTEXTURE_ADD_SIGNED,
    gcvTEXTURE_INTERPOLATE,
    gcvTEXTURE_SUBTRACT,
    gcvTEXTURE_DOT3
}
gceTEXTURE_FUNCTION;

/* Texture sources. */
typedef enum _gceTEXTURE_SOURCE
{
    gcvCOLOR_FROM_TEXTURE,
    gcvCOLOR_FROM_CONSTANT_COLOR,
    gcvCOLOR_FROM_PRIMARY_COLOR,
    gcvCOLOR_FROM_PREVIOUS_COLOR
}
gceTEXTURE_SOURCE;

/* Texture source channels. */
typedef enum _gceTEXTURE_CHANNEL
{
    gcvFROM_COLOR,
    gcvFROM_ONE_MINUS_COLOR,
    gcvFROM_ALPHA,
    gcvFROM_ONE_MINUS_ALPHA
}
gceTEXTURE_CHANNEL;

/* Filter types. */
typedef enum _gceFILTER_TYPE
{
    gcvFILTER_SYNC,
    gcvFILTER_BLUR,
    gcvFILTER_USER
}
gceFILTER_TYPE;

/* Filter pass types. */
typedef enum _gceFILTER_PASS_TYPE
{
    gcvFILTER_HOR_PASS,
    gcvFILTER_VER_PASS
}
gceFILTER_PASS_TYPE;

/* Endian hints. */
typedef enum _gceENDIAN_HINT
{
    gcvENDIAN_NO_SWAP           = 0,
    gcvENDIAN_SWAP_WORD,
    gcvENDIAN_SWAP_DWORD
}
gceENDIAN_HINT;

/* Endian hints. */
typedef enum _gceTILING
{
    gcvLINEAR,
    gcvTILED,
    gcvSUPERTILED
}
gceTILING;

/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef struct _gcoCONTEXT *        gcoCONTEXT;
typedef struct _gcoCMDBUF *         gcoCMDBUF;
typedef struct _gcoQUEUE *          gcoQUEUE;
typedef struct _gcsHAL_INTERFACE *  gcsHAL_INTERFACE_PTR;
typedef struct gcs2D_PROFILE *      gcs2D_PROFILE_PTR;

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_enum_h_ */

