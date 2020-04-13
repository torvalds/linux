/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright 2008-2015 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/*
 * svga3d_surfacedefs.h --
 *
 *      Surface definitions and inlineable utilities for SVGA3d.
 */

#ifndef _SVGA3D_SURFACEDEFS_H_
#define _SVGA3D_SURFACEDEFS_H_

#define INCLUDE_ALLOW_USERLEVEL
#define INCLUDE_ALLOW_MODULE
#include "includeCheck.h"

#include <linux/kernel.h>
#include <drm/vmwgfx_drm.h>

#include "svga3d_reg.h"

#define surf_size_struct struct drm_vmw_size

/*
 * enum svga3d_block_desc - describes generic properties about formats.
 */
enum svga3d_block_desc {
	/* Nothing special can be said about this format. */
	SVGA3DBLOCKDESC_NONE        = 0,

	/* Format contains Blue/U data */
	SVGA3DBLOCKDESC_BLUE        = 1 << 0,
	SVGA3DBLOCKDESC_W           = 1 << 0,
	SVGA3DBLOCKDESC_BUMP_L      = 1 << 0,

	/* Format contains Green/V data */
	SVGA3DBLOCKDESC_GREEN       = 1 << 1,
	SVGA3DBLOCKDESC_V           = 1 << 1,

	/* Format contains Red/W/Luminance data */
	SVGA3DBLOCKDESC_RED         = 1 << 2,
	SVGA3DBLOCKDESC_U           = 1 << 2,
	SVGA3DBLOCKDESC_LUMINANCE   = 1 << 2,

	/* Format contains Alpha/Q data */
	SVGA3DBLOCKDESC_ALPHA       = 1 << 3,
	SVGA3DBLOCKDESC_Q           = 1 << 3,

	/* Format is a buffer */
	SVGA3DBLOCKDESC_BUFFER      = 1 << 4,

	/* Format is compressed */
	SVGA3DBLOCKDESC_COMPRESSED  = 1 << 5,

	/* Format uses IEEE floating point */
	SVGA3DBLOCKDESC_FP          = 1 << 6,

	/* Three separate blocks store data. */
	SVGA3DBLOCKDESC_PLANAR_YUV  = 1 << 7,

	/* 2 planes of Y, UV, e.g., NV12. */
	SVGA3DBLOCKDESC_2PLANAR_YUV = 1 << 8,

	/* 3 planes of separate Y, U, V, e.g., YV12. */
	SVGA3DBLOCKDESC_3PLANAR_YUV = 1 << 9,

	/* Block with a stencil channel */
	SVGA3DBLOCKDESC_STENCIL     = 1 << 11,

	/* Typeless format */
	SVGA3DBLOCKDESC_TYPELESS    = 1 << 12,

	/* Channels are signed integers */
	SVGA3DBLOCKDESC_SINT        = 1 << 13,

	/* Channels are unsigned integers */
	SVGA3DBLOCKDESC_UINT        = 1 << 14,

	/* Channels are normalized (when sampling) */
	SVGA3DBLOCKDESC_NORM        = 1 << 15,

	/* Channels are in SRGB */
	SVGA3DBLOCKDESC_SRGB        = 1 << 16,

	/* Shared exponent */
	SVGA3DBLOCKDESC_EXP         = 1 << 17,

	/* Format contains color data. */
	SVGA3DBLOCKDESC_COLOR       = 1 << 18,
	/* Format contains depth data. */
	SVGA3DBLOCKDESC_DEPTH       = 1 << 19,
	/* Format contains bump data. */
	SVGA3DBLOCKDESC_BUMP        = 1 << 20,

	/* Format contains YUV video data. */
	SVGA3DBLOCKDESC_YUV_VIDEO   = 1 << 21,

	/* For mixed unsigned/signed formats. */
	SVGA3DBLOCKDESC_MIXED       = 1 << 22,

	/* For distingushing CxV8U8. */
	SVGA3DBLOCKDESC_CX          = 1 << 23,

	/* Different compressed format groups. */
	SVGA3DBLOCKDESC_BC1         = 1 << 24,
	SVGA3DBLOCKDESC_BC2         = 1 << 25,
	SVGA3DBLOCKDESC_BC3         = 1 << 26,
	SVGA3DBLOCKDESC_BC4         = 1 << 27,
	SVGA3DBLOCKDESC_BC5         = 1 << 28,
	SVGA3DBLOCKDESC_BC6H        = 1 << 29,
	SVGA3DBLOCKDESC_BC7         = 1 << 30,

	SVGA3DBLOCKDESC_A_UINT    = SVGA3DBLOCKDESC_ALPHA |
				    SVGA3DBLOCKDESC_UINT |
				    SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_A_UNORM   = SVGA3DBLOCKDESC_A_UINT |
				    SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_R_UINT    = SVGA3DBLOCKDESC_RED |
				    SVGA3DBLOCKDESC_UINT |
				    SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_R_UNORM   = SVGA3DBLOCKDESC_R_UINT |
				    SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_R_SINT    = SVGA3DBLOCKDESC_RED |
				    SVGA3DBLOCKDESC_SINT |
				    SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_R_SNORM   = SVGA3DBLOCKDESC_R_SINT |
				    SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_G_UINT    = SVGA3DBLOCKDESC_GREEN |
				    SVGA3DBLOCKDESC_UINT |
				    SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RG_UINT    = SVGA3DBLOCKDESC_RED |
				     SVGA3DBLOCKDESC_GREEN |
				     SVGA3DBLOCKDESC_UINT |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RG_UNORM   = SVGA3DBLOCKDESC_RG_UINT |
				     SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_RG_SINT    = SVGA3DBLOCKDESC_RED |
				     SVGA3DBLOCKDESC_GREEN |
				     SVGA3DBLOCKDESC_SINT |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RG_SNORM   = SVGA3DBLOCKDESC_RG_SINT |
				     SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_RGB_UINT   = SVGA3DBLOCKDESC_RED |
				     SVGA3DBLOCKDESC_GREEN |
				     SVGA3DBLOCKDESC_BLUE |
				     SVGA3DBLOCKDESC_UINT |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RGB_SINT   = SVGA3DBLOCKDESC_RED |
				     SVGA3DBLOCKDESC_GREEN |
				     SVGA3DBLOCKDESC_BLUE |
				     SVGA3DBLOCKDESC_SINT |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RGB_UNORM   = SVGA3DBLOCKDESC_RGB_UINT |
				      SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_RGB_UNORM_SRGB = SVGA3DBLOCKDESC_RGB_UNORM |
					 SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_RGBA_UINT  = SVGA3DBLOCKDESC_RED |
				     SVGA3DBLOCKDESC_GREEN |
				     SVGA3DBLOCKDESC_BLUE |
				     SVGA3DBLOCKDESC_ALPHA |
				     SVGA3DBLOCKDESC_UINT |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RGBA_UNORM = SVGA3DBLOCKDESC_RGBA_UINT |
				     SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_RGBA_UNORM_SRGB = SVGA3DBLOCKDESC_RGBA_UNORM |
					  SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_RGBA_SINT  = SVGA3DBLOCKDESC_RED |
				     SVGA3DBLOCKDESC_GREEN |
				     SVGA3DBLOCKDESC_BLUE |
				     SVGA3DBLOCKDESC_ALPHA |
				     SVGA3DBLOCKDESC_SINT |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RGBA_SNORM = SVGA3DBLOCKDESC_RGBA_SINT |
				     SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_RGBA_FP    = SVGA3DBLOCKDESC_RED |
				     SVGA3DBLOCKDESC_GREEN |
				     SVGA3DBLOCKDESC_BLUE |
				     SVGA3DBLOCKDESC_ALPHA |
				     SVGA3DBLOCKDESC_FP |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_UV         = SVGA3DBLOCKDESC_U |
				     SVGA3DBLOCKDESC_V |
				     SVGA3DBLOCKDESC_BUMP,
	SVGA3DBLOCKDESC_UVL        = SVGA3DBLOCKDESC_UV |
				     SVGA3DBLOCKDESC_BUMP_L |
				     SVGA3DBLOCKDESC_MIXED |
				     SVGA3DBLOCKDESC_BUMP,
	SVGA3DBLOCKDESC_UVW        = SVGA3DBLOCKDESC_UV |
				     SVGA3DBLOCKDESC_W |
				     SVGA3DBLOCKDESC_BUMP,
	SVGA3DBLOCKDESC_UVWA       = SVGA3DBLOCKDESC_UVW |
				     SVGA3DBLOCKDESC_ALPHA |
				     SVGA3DBLOCKDESC_MIXED |
				     SVGA3DBLOCKDESC_BUMP,
	SVGA3DBLOCKDESC_UVWQ       = SVGA3DBLOCKDESC_U |
				     SVGA3DBLOCKDESC_V |
				     SVGA3DBLOCKDESC_W |
				     SVGA3DBLOCKDESC_Q |
				     SVGA3DBLOCKDESC_BUMP,
	SVGA3DBLOCKDESC_L_UNORM    = SVGA3DBLOCKDESC_LUMINANCE |
				     SVGA3DBLOCKDESC_UINT |
				     SVGA3DBLOCKDESC_NORM |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_LA_UNORM   = SVGA3DBLOCKDESC_LUMINANCE |
				     SVGA3DBLOCKDESC_ALPHA |
				     SVGA3DBLOCKDESC_UINT |
				     SVGA3DBLOCKDESC_NORM |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_R_FP       = SVGA3DBLOCKDESC_RED |
				     SVGA3DBLOCKDESC_FP |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RG_FP      = SVGA3DBLOCKDESC_R_FP |
				     SVGA3DBLOCKDESC_GREEN |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RGB_FP     = SVGA3DBLOCKDESC_RG_FP |
				     SVGA3DBLOCKDESC_BLUE |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_YUV        = SVGA3DBLOCKDESC_YUV_VIDEO |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_AYUV       = SVGA3DBLOCKDESC_ALPHA |
				     SVGA3DBLOCKDESC_YUV_VIDEO |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_RGB_EXP       = SVGA3DBLOCKDESC_RED |
					SVGA3DBLOCKDESC_GREEN |
					SVGA3DBLOCKDESC_BLUE |
					SVGA3DBLOCKDESC_EXP |
					SVGA3DBLOCKDESC_COLOR,

	SVGA3DBLOCKDESC_COMP_TYPELESS = SVGA3DBLOCKDESC_COMPRESSED |
					SVGA3DBLOCKDESC_TYPELESS,
	SVGA3DBLOCKDESC_COMP_UNORM = SVGA3DBLOCKDESC_COMPRESSED |
				     SVGA3DBLOCKDESC_UINT |
				     SVGA3DBLOCKDESC_NORM |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_COMP_SNORM = SVGA3DBLOCKDESC_COMPRESSED |
				     SVGA3DBLOCKDESC_SINT |
				     SVGA3DBLOCKDESC_NORM |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_COMP_UNORM_SRGB = SVGA3DBLOCKDESC_COMP_UNORM |
					  SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_BC1_COMP_TYPELESS = SVGA3DBLOCKDESC_BC1 |
					    SVGA3DBLOCKDESC_COMP_TYPELESS,
	SVGA3DBLOCKDESC_BC1_COMP_UNORM = SVGA3DBLOCKDESC_BC1 |
					 SVGA3DBLOCKDESC_COMP_UNORM,
	SVGA3DBLOCKDESC_BC1_COMP_UNORM_SRGB = SVGA3DBLOCKDESC_BC1_COMP_UNORM |
					      SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_BC2_COMP_TYPELESS = SVGA3DBLOCKDESC_BC2 |
					    SVGA3DBLOCKDESC_COMP_TYPELESS,
	SVGA3DBLOCKDESC_BC2_COMP_UNORM = SVGA3DBLOCKDESC_BC2 |
					 SVGA3DBLOCKDESC_COMP_UNORM,
	SVGA3DBLOCKDESC_BC2_COMP_UNORM_SRGB = SVGA3DBLOCKDESC_BC2_COMP_UNORM |
					      SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_BC3_COMP_TYPELESS = SVGA3DBLOCKDESC_BC3 |
					    SVGA3DBLOCKDESC_COMP_TYPELESS,
	SVGA3DBLOCKDESC_BC3_COMP_UNORM = SVGA3DBLOCKDESC_BC3 |
					 SVGA3DBLOCKDESC_COMP_UNORM,
	SVGA3DBLOCKDESC_BC3_COMP_UNORM_SRGB = SVGA3DBLOCKDESC_BC3_COMP_UNORM |
					      SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_BC4_COMP_TYPELESS = SVGA3DBLOCKDESC_BC4 |
					    SVGA3DBLOCKDESC_COMP_TYPELESS,
	SVGA3DBLOCKDESC_BC4_COMP_UNORM = SVGA3DBLOCKDESC_BC4 |
					 SVGA3DBLOCKDESC_COMP_UNORM,
	SVGA3DBLOCKDESC_BC4_COMP_SNORM = SVGA3DBLOCKDESC_BC4 |
					 SVGA3DBLOCKDESC_COMP_SNORM,
	SVGA3DBLOCKDESC_BC5_COMP_TYPELESS = SVGA3DBLOCKDESC_BC5 |
					    SVGA3DBLOCKDESC_COMP_TYPELESS,
	SVGA3DBLOCKDESC_BC5_COMP_UNORM = SVGA3DBLOCKDESC_BC5 |
					 SVGA3DBLOCKDESC_COMP_UNORM,
	SVGA3DBLOCKDESC_BC5_COMP_SNORM = SVGA3DBLOCKDESC_BC5 |
					 SVGA3DBLOCKDESC_COMP_SNORM,
	SVGA3DBLOCKDESC_BC6H_COMP_TYPELESS = SVGA3DBLOCKDESC_BC6H |
					     SVGA3DBLOCKDESC_COMP_TYPELESS,
	SVGA3DBLOCKDESC_BC6H_COMP_UF16 = SVGA3DBLOCKDESC_BC6H |
					 SVGA3DBLOCKDESC_COMPRESSED,
	SVGA3DBLOCKDESC_BC6H_COMP_SF16 = SVGA3DBLOCKDESC_BC6H |
					 SVGA3DBLOCKDESC_COMPRESSED,
	SVGA3DBLOCKDESC_BC7_COMP_TYPELESS = SVGA3DBLOCKDESC_BC7 |
					    SVGA3DBLOCKDESC_COMP_TYPELESS,
	SVGA3DBLOCKDESC_BC7_COMP_UNORM = SVGA3DBLOCKDESC_BC7 |
					 SVGA3DBLOCKDESC_COMP_UNORM,
	SVGA3DBLOCKDESC_BC7_COMP_UNORM_SRGB = SVGA3DBLOCKDESC_BC7_COMP_UNORM |
					      SVGA3DBLOCKDESC_SRGB,

	SVGA3DBLOCKDESC_NV12       = SVGA3DBLOCKDESC_YUV_VIDEO |
				     SVGA3DBLOCKDESC_PLANAR_YUV |
				     SVGA3DBLOCKDESC_2PLANAR_YUV |
				     SVGA3DBLOCKDESC_COLOR,
	SVGA3DBLOCKDESC_YV12       = SVGA3DBLOCKDESC_YUV_VIDEO |
				     SVGA3DBLOCKDESC_PLANAR_YUV |
				     SVGA3DBLOCKDESC_3PLANAR_YUV |
				     SVGA3DBLOCKDESC_COLOR,

	SVGA3DBLOCKDESC_DEPTH_UINT = SVGA3DBLOCKDESC_DEPTH |
				     SVGA3DBLOCKDESC_UINT,
	SVGA3DBLOCKDESC_DEPTH_UNORM = SVGA3DBLOCKDESC_DEPTH_UINT |
				     SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_DS      =    SVGA3DBLOCKDESC_DEPTH |
				     SVGA3DBLOCKDESC_STENCIL,
	SVGA3DBLOCKDESC_DS_UINT =    SVGA3DBLOCKDESC_DEPTH |
				     SVGA3DBLOCKDESC_STENCIL |
				     SVGA3DBLOCKDESC_UINT,
	SVGA3DBLOCKDESC_DS_UNORM =   SVGA3DBLOCKDESC_DS_UINT |
				     SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_DEPTH_FP   = SVGA3DBLOCKDESC_DEPTH |
				     SVGA3DBLOCKDESC_FP,

	SVGA3DBLOCKDESC_UV_UINT    = SVGA3DBLOCKDESC_UV |
				     SVGA3DBLOCKDESC_UINT,
	SVGA3DBLOCKDESC_UV_SNORM   = SVGA3DBLOCKDESC_UV |
				     SVGA3DBLOCKDESC_SINT |
				     SVGA3DBLOCKDESC_NORM,
	SVGA3DBLOCKDESC_UVCX_SNORM = SVGA3DBLOCKDESC_UV_SNORM |
				     SVGA3DBLOCKDESC_CX,
	SVGA3DBLOCKDESC_UVWQ_SNORM = SVGA3DBLOCKDESC_UVWQ |
				     SVGA3DBLOCKDESC_SINT |
				     SVGA3DBLOCKDESC_NORM,
};

struct svga3d_channel_def {
	union {
		u8 blue;
		u8 w_bump;
		u8 l_bump;
		u8 uv_video;
		u8 u_video;
	};
	union {
		u8 green;
		u8 stencil;
		u8 v_bump;
		u8 v_video;
	};
	union {
		u8 red;
		u8 u_bump;
		u8 luminance;
		u8 y_video;
		u8 depth;
		u8 data;
	};
	union {
		u8 alpha;
		u8 q_bump;
		u8 exp;
	};
};

/*
 * struct svga3d_surface_desc - describes the actual pixel data.
 *
 * @format: Format
 * @block_desc: Block description
 * @block_size: Dimensions in pixels of a block
 * @bytes_per_block: Size of block in bytes
 * @pitch_bytes_per_block: Size of a block in bytes for purposes of pitch
 * @bit_depth: Channel bit depths
 * @bit_offset: Channel bit masks (in bits offset from the start of the pointer)
 */
struct svga3d_surface_desc {
	SVGA3dSurfaceFormat format;
	enum svga3d_block_desc block_desc;

	surf_size_struct block_size;
	u32 bytes_per_block;
	u32 pitch_bytes_per_block;

	struct svga3d_channel_def bit_depth;
	struct svga3d_channel_def bit_offset;
};

static const struct svga3d_surface_desc svga3d_surface_descs[] = {
   {SVGA3D_FORMAT_INVALID, SVGA3DBLOCKDESC_NONE,
      {1, 1, 1},  0, 0,
      {{0}, {0}, {0}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_X8R8G8B8, SVGA3DBLOCKDESC_RGB_UNORM,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {0}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_A8R8G8B8, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_R5G6B5, SVGA3DBLOCKDESC_RGB_UNORM,
      {1, 1, 1},  2, 2,
      {{5}, {6}, {5}, {0}},
      {{0}, {5}, {11}, {0}}},

   {SVGA3D_X1R5G5B5, SVGA3DBLOCKDESC_RGB_UNORM,
      {1, 1, 1},  2, 2,
      {{5}, {5}, {5}, {0}},
      {{0}, {5}, {10}, {0}}},

   {SVGA3D_A1R5G5B5, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  2, 2,
      {{5}, {5}, {5}, {1}},
      {{0}, {5}, {10}, {15}}},

   {SVGA3D_A4R4G4B4, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  2, 2,
      {{4}, {4}, {4}, {4}},
      {{0}, {4}, {8}, {12}}},

   {SVGA3D_Z_D32, SVGA3DBLOCKDESC_DEPTH_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_Z_D16, SVGA3DBLOCKDESC_DEPTH_UNORM,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_Z_D24S8, SVGA3DBLOCKDESC_DS_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {8}, {24}, {0}},
      {{0}, {0}, {8}, {0}}},

   {SVGA3D_Z_D15S1, SVGA3DBLOCKDESC_DS_UNORM,
      {1, 1, 1},  2, 2,
      {{0}, {1}, {15}, {0}},
      {{0}, {0}, {1}, {0}}},

   {SVGA3D_LUMINANCE8, SVGA3DBLOCKDESC_L_UNORM,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_LUMINANCE4_ALPHA4, SVGA3DBLOCKDESC_LA_UNORM,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {4}, {4}},
      {{0}, {0}, {0}, {4}}},

   {SVGA3D_LUMINANCE16, SVGA3DBLOCKDESC_L_UNORM,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_LUMINANCE8_ALPHA8, SVGA3DBLOCKDESC_LA_UNORM,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {8}, {8}},
      {{0}, {0}, {0}, {8}}},

   {SVGA3D_DXT1, SVGA3DBLOCKDESC_BC1_COMP_UNORM,
      {4, 4, 1},  8, 8,
      {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_DXT2, SVGA3DBLOCKDESC_BC2_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_DXT3, SVGA3DBLOCKDESC_BC2_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_DXT4, SVGA3DBLOCKDESC_BC3_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_DXT5, SVGA3DBLOCKDESC_BC3_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BUMPU8V8, SVGA3DBLOCKDESC_UV_SNORM,
      {1, 1, 1},  2, 2,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_BUMPL6V5U5, SVGA3DBLOCKDESC_UVL,
      {1, 1, 1},  2, 2,
      {{6}, {5}, {5}, {0}},
      {{10}, {5}, {0}, {0}}},

   {SVGA3D_BUMPX8L8V8U8, SVGA3DBLOCKDESC_UVL,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {0}},
      {{16}, {8}, {0}, {0}}},

   {SVGA3D_FORMAT_DEAD1, SVGA3DBLOCKDESC_NONE,
      {1, 1, 1},  3, 3,
      {{8}, {8}, {8}, {0}},
      {{16}, {8}, {0}, {0}}},

   {SVGA3D_ARGB_S10E5, SVGA3DBLOCKDESC_RGBA_FP,
      {1, 1, 1},  8, 8,
      {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_ARGB_S23E8, SVGA3DBLOCKDESC_RGBA_FP,
      {1, 1, 1},  16, 16,
      {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_A2R10G10B10, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  4, 4,
      {{10}, {10}, {10}, {2}},
      {{0}, {10}, {20}, {30}}},

   {SVGA3D_V8U8, SVGA3DBLOCKDESC_UV_SNORM,
      {1, 1, 1},  2, 2,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_Q8W8V8U8, SVGA3DBLOCKDESC_UVWQ_SNORM,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_CxV8U8, SVGA3DBLOCKDESC_UVCX_SNORM,
      {1, 1, 1},  2, 2,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_X8L8V8U8, SVGA3DBLOCKDESC_UVL,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {0}},
      {{16}, {8}, {0}, {0}}},

   {SVGA3D_A2W10V10U10, SVGA3DBLOCKDESC_UVWA,
      {1, 1, 1},  4, 4,
      {{10}, {10}, {10}, {2}},
      {{20}, {10}, {0}, {30}}},

   {SVGA3D_ALPHA8, SVGA3DBLOCKDESC_A_UNORM,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {0}, {8}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R_S10E5, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R_S23E8, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_RG_S10E5, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_RG_S23E8, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  8, 8,
      {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_BUFFER, SVGA3DBLOCKDESC_BUFFER,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_Z_D24X8, SVGA3DBLOCKDESC_DEPTH_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {24}, {0}},
      {{0}, {0}, {8}, {0}}},

   {SVGA3D_V16U16, SVGA3DBLOCKDESC_UV_SNORM,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_G16R16, SVGA3DBLOCKDESC_RG_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_A16B16G16R16, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  8, 8,
      {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_UYVY, SVGA3DBLOCKDESC_YUV,
      {2, 1, 1},  4, 4,
      {{8}, {0}, {8}, {0}},
      {{0}, {0}, {8}, {0}}},

   {SVGA3D_YUY2, SVGA3DBLOCKDESC_YUV,
      {2, 1, 1},  4, 4,
      {{8}, {0}, {8}, {0}},
      {{8}, {0}, {0}, {0}}},

   {SVGA3D_NV12, SVGA3DBLOCKDESC_NV12,
      {2, 2, 1},  6, 2,
      {{0}, {0}, {48}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_FORMAT_DEAD2, SVGA3DBLOCKDESC_NONE,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_R32G32B32A32_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  16, 16,
      {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_R32G32B32A32_UINT, SVGA3DBLOCKDESC_RGBA_UINT,
      {1, 1, 1},  16, 16,
      {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_R32G32B32A32_SINT, SVGA3DBLOCKDESC_RGBA_SINT,
      {1, 1, 1},  16, 16,
      {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_R32G32B32_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  12, 12,
      {{32}, {32}, {32}, {0}},
      {{64}, {32}, {0}, {0}}},

   {SVGA3D_R32G32B32_FLOAT, SVGA3DBLOCKDESC_RGB_FP,
      {1, 1, 1},  12, 12,
      {{32}, {32}, {32}, {0}},
      {{64}, {32}, {0}, {0}}},

   {SVGA3D_R32G32B32_UINT, SVGA3DBLOCKDESC_RGB_UINT,
      {1, 1, 1},  12, 12,
      {{32}, {32}, {32}, {0}},
      {{64}, {32}, {0}, {0}}},

   {SVGA3D_R32G32B32_SINT, SVGA3DBLOCKDESC_RGB_SINT,
      {1, 1, 1},  12, 12,
      {{32}, {32}, {32}, {0}},
      {{64}, {32}, {0}, {0}}},

   {SVGA3D_R16G16B16A16_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  8, 8,
      {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R16G16B16A16_UINT, SVGA3DBLOCKDESC_RGBA_UINT,
      {1, 1, 1},  8, 8,
      {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R16G16B16A16_SNORM, SVGA3DBLOCKDESC_RGBA_SNORM,
      {1, 1, 1},  8, 8,
      {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R16G16B16A16_SINT, SVGA3DBLOCKDESC_RGBA_SINT,
      {1, 1, 1},  8, 8,
      {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R32G32_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  8, 8,
      {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R32G32_UINT, SVGA3DBLOCKDESC_RG_UINT,
      {1, 1, 1},  8, 8,
      {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R32G32_SINT, SVGA3DBLOCKDESC_RG_SINT,
      {1, 1, 1},  8, 8,
      {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R32G8X24_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  8, 8,
      {{0}, {8}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_D32_FLOAT_S8X24_UINT, SVGA3DBLOCKDESC_DS,
      {1, 1, 1},  8, 8,
      {{0}, {8}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R32_FLOAT_X8X24, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  8, 8,
      {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_X32_G8X24_UINT, SVGA3DBLOCKDESC_G_UINT,
      {1, 1, 1},  8, 8,
      {{0}, {8}, {0}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R10G10B10A2_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  4, 4,
      {{10}, {10}, {10}, {2}},
      {{20}, {10}, {0}, {30}}},

   {SVGA3D_R10G10B10A2_UINT, SVGA3DBLOCKDESC_RGBA_UINT,
      {1, 1, 1},  4, 4,
      {{10}, {10}, {10}, {2}},
      {{20}, {10}, {0}, {30}}},

   {SVGA3D_R11G11B10_FLOAT, SVGA3DBLOCKDESC_RGB_FP,
      {1, 1, 1},  4, 4,
      {{10}, {11}, {11}, {0}},
      {{22}, {11}, {0}, {0}}},

   {SVGA3D_R8G8B8A8_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R8G8B8A8_UNORM, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R8G8B8A8_UNORM_SRGB, SVGA3DBLOCKDESC_RGBA_UNORM_SRGB,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R8G8B8A8_UINT, SVGA3DBLOCKDESC_RGBA_UINT,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R8G8B8A8_SINT, SVGA3DBLOCKDESC_RGBA_SINT,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R16G16_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R16G16_UINT, SVGA3DBLOCKDESC_RG_UINT,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R16G16_SINT, SVGA3DBLOCKDESC_RG_SINT,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R32_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_D32_FLOAT, SVGA3DBLOCKDESC_DEPTH_FP,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R32_UINT, SVGA3DBLOCKDESC_R_UINT,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R32_SINT, SVGA3DBLOCKDESC_R_SINT,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R24G8_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  4, 4,
      {{0}, {8}, {24}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_D24_UNORM_S8_UINT, SVGA3DBLOCKDESC_DS_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {8}, {24}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_R24_UNORM_X8, SVGA3DBLOCKDESC_R_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {24}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_X24_G8_UINT, SVGA3DBLOCKDESC_G_UINT,
      {1, 1, 1},  4, 4,
      {{0}, {8}, {0}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_R8G8_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  2, 2,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R8G8_UNORM, SVGA3DBLOCKDESC_RG_UNORM,
      {1, 1, 1},  2, 2,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R8G8_UINT, SVGA3DBLOCKDESC_RG_UINT,
      {1, 1, 1},  2, 2,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R8G8_SINT, SVGA3DBLOCKDESC_RG_SINT,
      {1, 1, 1},  2, 2,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R16_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R16_UNORM, SVGA3DBLOCKDESC_R_UNORM,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R16_UINT, SVGA3DBLOCKDESC_R_UINT,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R16_SNORM, SVGA3DBLOCKDESC_R_SNORM,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R16_SINT, SVGA3DBLOCKDESC_R_SINT,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_UNORM, SVGA3DBLOCKDESC_R_UNORM,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_UINT, SVGA3DBLOCKDESC_R_UINT,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_SNORM, SVGA3DBLOCKDESC_R_SNORM,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_SINT, SVGA3DBLOCKDESC_R_SINT,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_P8, SVGA3DBLOCKDESC_NONE,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R9G9B9E5_SHAREDEXP, SVGA3DBLOCKDESC_RGB_EXP,
      {1, 1, 1},  4, 4,
      {{9}, {9}, {9}, {5}},
      {{18}, {9}, {0}, {27}}},

   {SVGA3D_R8G8_B8G8_UNORM, SVGA3DBLOCKDESC_NONE,
      {2, 1, 1},  4, 4,
      {{0}, {8}, {8}, {0}},
      {{0}, {0}, {8}, {0}}},

   {SVGA3D_G8R8_G8B8_UNORM, SVGA3DBLOCKDESC_NONE,
      {2, 1, 1},  4, 4,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_BC1_TYPELESS, SVGA3DBLOCKDESC_BC1_COMP_TYPELESS,
      {4, 4, 1},  8, 8,
      {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC1_UNORM_SRGB, SVGA3DBLOCKDESC_BC1_COMP_UNORM_SRGB,
      {4, 4, 1},  8, 8,
      {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC2_TYPELESS, SVGA3DBLOCKDESC_BC2_COMP_TYPELESS,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC2_UNORM_SRGB, SVGA3DBLOCKDESC_BC2_COMP_UNORM_SRGB,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC3_TYPELESS, SVGA3DBLOCKDESC_BC3_COMP_TYPELESS,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC3_UNORM_SRGB, SVGA3DBLOCKDESC_BC3_COMP_UNORM_SRGB,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC4_TYPELESS, SVGA3DBLOCKDESC_BC4_COMP_TYPELESS,
      {4, 4, 1},  8, 8,
      {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_ATI1, SVGA3DBLOCKDESC_BC4_COMP_UNORM,
      {4, 4, 1},  8, 8,
      {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC4_SNORM, SVGA3DBLOCKDESC_BC4_COMP_SNORM,
      {4, 4, 1},  8, 8,
      {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC5_TYPELESS, SVGA3DBLOCKDESC_BC5_COMP_TYPELESS,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_ATI2, SVGA3DBLOCKDESC_BC5_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC5_SNORM, SVGA3DBLOCKDESC_BC5_COMP_SNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R10G10B10_XR_BIAS_A2_UNORM, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  4, 4,
      {{10}, {10}, {10}, {2}},
     {{20}, {10}, {0}, {30}}},

   {SVGA3D_B8G8R8A8_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_B8G8R8A8_UNORM_SRGB, SVGA3DBLOCKDESC_RGBA_UNORM_SRGB,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_B8G8R8X8_TYPELESS, SVGA3DBLOCKDESC_TYPELESS,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {0}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_B8G8R8X8_UNORM_SRGB, SVGA3DBLOCKDESC_RGB_UNORM_SRGB,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {0}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_Z_DF16, SVGA3DBLOCKDESC_DEPTH_UNORM,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_Z_DF24, SVGA3DBLOCKDESC_DEPTH_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {24}, {0}},
      {{0}, {0}, {8}, {0}}},

   {SVGA3D_Z_D24S8_INT, SVGA3DBLOCKDESC_DS_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {8}, {24}, {0}},
      {{0}, {0}, {8}, {0}}},

   {SVGA3D_YV12, SVGA3DBLOCKDESC_YV12,
      {2, 2, 1},  6, 2,
      {{0}, {0}, {48}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R32G32B32A32_FLOAT, SVGA3DBLOCKDESC_RGBA_FP,
      {1, 1, 1},  16, 16,
      {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_R16G16B16A16_FLOAT, SVGA3DBLOCKDESC_RGBA_FP,
      {1, 1, 1},  8, 8,
      {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R16G16B16A16_UNORM, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  8, 8,
      {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R32G32_FLOAT, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  8, 8,
      {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R10G10B10A2_UNORM, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  4, 4,
      {{10}, {10}, {10}, {2}},
      {{20}, {10}, {0}, {30}}},

   {SVGA3D_R8G8B8A8_SNORM, SVGA3DBLOCKDESC_RGBA_SNORM,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R16G16_FLOAT, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R16G16_UNORM, SVGA3DBLOCKDESC_RG_UNORM,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R16G16_SNORM, SVGA3DBLOCKDESC_RG_SNORM,
      {1, 1, 1},  4, 4,
      {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R32_FLOAT, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  4, 4,
      {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8G8_SNORM, SVGA3DBLOCKDESC_RG_SNORM,
      {1, 1, 1},  2, 2,
      {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R16_FLOAT, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_D16_UNORM, SVGA3DBLOCKDESC_DEPTH_UNORM,
      {1, 1, 1},  2, 2,
      {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_A8_UNORM, SVGA3DBLOCKDESC_A_UNORM,
      {1, 1, 1},  1, 1,
      {{0}, {0}, {0}, {8}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC1_UNORM, SVGA3DBLOCKDESC_BC1_COMP_UNORM,
      {4, 4, 1},  8, 8,
      {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC2_UNORM, SVGA3DBLOCKDESC_BC2_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC3_UNORM, SVGA3DBLOCKDESC_BC3_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_B5G6R5_UNORM, SVGA3DBLOCKDESC_RGB_UNORM,
      {1, 1, 1},  2, 2,
      {{5}, {6}, {5}, {0}},
      {{0}, {5}, {11}, {0}}},

   {SVGA3D_B5G5R5A1_UNORM, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  2, 2,
      {{5}, {5}, {5}, {1}},
      {{0}, {5}, {10}, {15}}},

   {SVGA3D_B8G8R8A8_UNORM, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_B8G8R8X8_UNORM, SVGA3DBLOCKDESC_RGB_UNORM,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {0}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_BC4_UNORM, SVGA3DBLOCKDESC_BC4_COMP_UNORM,
      {4, 4, 1},  8, 8,
      {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC5_UNORM, SVGA3DBLOCKDESC_BC5_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_B4G4R4A4_UNORM, SVGA3DBLOCKDESC_RGBA_UNORM,
      {1, 1, 1},  2, 2,
      {{4}, {4}, {4}, {4}},
      {{0}, {4}, {8}, {12}}},

   {SVGA3D_BC6H_TYPELESS, SVGA3DBLOCKDESC_BC6H_COMP_TYPELESS,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC6H_UF16, SVGA3DBLOCKDESC_BC6H_COMP_UF16,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC6H_SF16, SVGA3DBLOCKDESC_BC6H_COMP_SF16,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC7_TYPELESS, SVGA3DBLOCKDESC_BC7_COMP_TYPELESS,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC7_UNORM, SVGA3DBLOCKDESC_BC7_COMP_UNORM,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC7_UNORM_SRGB, SVGA3DBLOCKDESC_BC7_COMP_UNORM_SRGB,
      {4, 4, 1},  16, 16,
      {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_AYUV, SVGA3DBLOCKDESC_AYUV,
      {1, 1, 1},  4, 4,
      {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},
};

static inline u32 clamped_umul32(u32 a, u32 b)
{
	uint64_t tmp = (uint64_t) a*b;
	return (tmp > (uint64_t) ((u32) -1)) ? (u32) -1 : tmp;
}

/**
 * svga3dsurface_get_desc - Look up the appropriate SVGA3dSurfaceDesc for the
 * given format.
 */
static inline const struct svga3d_surface_desc *
svga3dsurface_get_desc(SVGA3dSurfaceFormat format)
{
	if (format < ARRAY_SIZE(svga3d_surface_descs))
		return &svga3d_surface_descs[format];

	return &svga3d_surface_descs[SVGA3D_FORMAT_INVALID];
}

/**
 * svga3dsurface_get_mip_size -  Given a base level size and the mip level,
 * compute the size of the mip level.
 */
static inline surf_size_struct
svga3dsurface_get_mip_size(surf_size_struct base_level, u32 mip_level)
{
	surf_size_struct size;

	size.width = max_t(u32, base_level.width >> mip_level, 1);
	size.height = max_t(u32, base_level.height >> mip_level, 1);
	size.depth = max_t(u32, base_level.depth >> mip_level, 1);
	size.pad64 = 0;

	return size;
}

static inline void
svga3dsurface_get_size_in_blocks(const struct svga3d_surface_desc *desc,
				 const surf_size_struct *pixel_size,
				 surf_size_struct *block_size)
{
	block_size->width = __KERNEL_DIV_ROUND_UP(pixel_size->width,
						  desc->block_size.width);
	block_size->height = __KERNEL_DIV_ROUND_UP(pixel_size->height,
						   desc->block_size.height);
	block_size->depth = __KERNEL_DIV_ROUND_UP(pixel_size->depth,
						  desc->block_size.depth);
}

static inline bool
svga3dsurface_is_planar_surface(const struct svga3d_surface_desc *desc)
{
	return (desc->block_desc & SVGA3DBLOCKDESC_PLANAR_YUV) != 0;
}

static inline u32
svga3dsurface_calculate_pitch(const struct svga3d_surface_desc *desc,
			      const surf_size_struct *size)
{
	u32 pitch;
	surf_size_struct blocks;

	svga3dsurface_get_size_in_blocks(desc, size, &blocks);

	pitch = blocks.width * desc->pitch_bytes_per_block;

	return pitch;
}

/**
 * svga3dsurface_get_image_buffer_size - Calculates image buffer size.
 *
 * Return the number of bytes of buffer space required to store one image of a
 * surface, optionally using the specified pitch.
 *
 * If pitch is zero, it is assumed that rows are tightly packed.
 *
 * This function is overflow-safe. If the result would have overflowed, instead
 * we return MAX_UINT32.
 */
static inline u32
svga3dsurface_get_image_buffer_size(const struct svga3d_surface_desc *desc,
				    const surf_size_struct *size,
				    u32 pitch)
{
	surf_size_struct image_blocks;
	u32 slice_size, total_size;

	svga3dsurface_get_size_in_blocks(desc, size, &image_blocks);

	if (svga3dsurface_is_planar_surface(desc)) {
		total_size = clamped_umul32(image_blocks.width,
					    image_blocks.height);
		total_size = clamped_umul32(total_size, image_blocks.depth);
		total_size = clamped_umul32(total_size, desc->bytes_per_block);
		return total_size;
	}

	if (pitch == 0)
		pitch = svga3dsurface_calculate_pitch(desc, size);

	slice_size = clamped_umul32(image_blocks.height, pitch);
	total_size = clamped_umul32(slice_size, image_blocks.depth);

	return total_size;
}

/**
 * svga3dsurface_get_serialized_size - Get the serialized size for the image.
 */
static inline u32
svga3dsurface_get_serialized_size(SVGA3dSurfaceFormat format,
				  surf_size_struct base_level_size,
				  u32 num_mip_levels,
				  u32 num_layers)
{
	const struct svga3d_surface_desc *desc = svga3dsurface_get_desc(format);
	u32 total_size = 0;
	u32 mip;

	for (mip = 0; mip < num_mip_levels; mip++) {
		surf_size_struct size =
			svga3dsurface_get_mip_size(base_level_size, mip);
		total_size += svga3dsurface_get_image_buffer_size(desc,
								  &size, 0);
	}

	return total_size * num_layers;
}

/**
 * svga3dsurface_get_serialized_size_extended - Returns the number of bytes
 * required for a surface with given parameters. Support for sample count.
 */
static inline u32
svga3dsurface_get_serialized_size_extended(SVGA3dSurfaceFormat format,
					   surf_size_struct base_level_size,
					   u32 num_mip_levels,
					   u32 num_layers,
					   u32 num_samples)
{
	uint64_t total_size =
		svga3dsurface_get_serialized_size(format,
						  base_level_size,
						  num_mip_levels,
						  num_layers);
	total_size *= max_t(u32, 1, num_samples);

	return min_t(uint64_t, total_size, (uint64_t)U32_MAX);
}

/**
 * svga3dsurface_get_pixel_offset - Compute the offset (in bytes) to a pixel
 * in an image (or volume).
 *
 * @width: The image width in pixels.
 * @height: The image height in pixels
 */
static inline u32
svga3dsurface_get_pixel_offset(SVGA3dSurfaceFormat format,
			       u32 width, u32 height,
			       u32 x, u32 y, u32 z)
{
	const struct svga3d_surface_desc *desc = svga3dsurface_get_desc(format);
	const u32 bw = desc->block_size.width, bh = desc->block_size.height;
	const u32 bd = desc->block_size.depth;
	const u32 rowstride = __KERNEL_DIV_ROUND_UP(width, bw) *
			      desc->bytes_per_block;
	const u32 imgstride = __KERNEL_DIV_ROUND_UP(height, bh) * rowstride;
	const u32 offset = (z / bd * imgstride +
			    y / bh * rowstride +
			    x / bw * desc->bytes_per_block);
	return offset;
}

static inline u32
svga3dsurface_get_image_offset(SVGA3dSurfaceFormat format,
			       surf_size_struct baseLevelSize,
			       u32 numMipLevels,
			       u32 face,
			       u32 mip)

{
	u32 offset;
	u32 mipChainBytes;
	u32 mipChainBytesToLevel;
	u32 i;
	const struct svga3d_surface_desc *desc;
	surf_size_struct mipSize;
	u32 bytes;

	desc = svga3dsurface_get_desc(format);

	mipChainBytes = 0;
	mipChainBytesToLevel = 0;
	for (i = 0; i < numMipLevels; i++) {
		mipSize = svga3dsurface_get_mip_size(baseLevelSize, i);
		bytes = svga3dsurface_get_image_buffer_size(desc, &mipSize, 0);
		mipChainBytes += bytes;
		if (i < mip)
			mipChainBytesToLevel += bytes;
	}

	offset = mipChainBytes * face + mipChainBytesToLevel;

	return offset;
}


/**
 * svga3dsurface_is_gb_screen_target_format - Is the specified format usable as
 *                                            a ScreenTarget?
 *                                            (with just the GBObjects cap-bit
 *                                             set)
 * @format: format to queried
 *
 * RETURNS:
 * true if queried format is valid for screen targets
 */
static inline bool
svga3dsurface_is_gb_screen_target_format(SVGA3dSurfaceFormat format)
{
	return (format == SVGA3D_X8R8G8B8 ||
		format == SVGA3D_A8R8G8B8 ||
		format == SVGA3D_R5G6B5   ||
		format == SVGA3D_X1R5G5B5 ||
		format == SVGA3D_A1R5G5B5 ||
		format == SVGA3D_P8);
}


/**
 * svga3dsurface_is_dx_screen_target_format - Is the specified format usable as
 *                                            a ScreenTarget?
 *                                            (with DX10 enabled)
 *
 * @format: format to queried
 *
 * Results:
 * true if queried format is valid for screen targets
 */
static inline bool
svga3dsurface_is_dx_screen_target_format(SVGA3dSurfaceFormat format)
{
	return (format == SVGA3D_R8G8B8A8_UNORM ||
		format == SVGA3D_B8G8R8A8_UNORM ||
		format == SVGA3D_B8G8R8X8_UNORM);
}


/**
 * svga3dsurface_is_screen_target_format - Is the specified format usable as a
 *                                         ScreenTarget?
 *                                         (for some combination of caps)
 *
 * @format: format to queried
 *
 * Results:
 * true if queried format is valid for screen targets
 */
static inline bool
svga3dsurface_is_screen_target_format(SVGA3dSurfaceFormat format)
{
	if (svga3dsurface_is_gb_screen_target_format(format)) {
		return true;
	}
	return svga3dsurface_is_dx_screen_target_format(format);
}

/**
 * struct svga3dsurface_mip - Mimpmap level information
 * @bytes: Bytes required in the backing store of this mipmap level.
 * @img_stride: Byte stride per image.
 * @row_stride: Byte stride per block row.
 * @size: The size of the mipmap.
 */
struct svga3dsurface_mip {
	size_t bytes;
	size_t img_stride;
	size_t row_stride;
	struct drm_vmw_size size;

};

/**
 * struct svga3dsurface_cache - Cached surface information
 * @desc: Pointer to the surface descriptor
 * @mip: Array of mipmap level information. Valid size is @num_mip_levels.
 * @mip_chain_bytes: Bytes required in the backing store for the whole chain
 * of mip levels.
 * @sheet_bytes: Bytes required in the backing store for a sheet
 * representing a single sample.
 * @num_mip_levels: Valid size of the @mip array. Number of mipmap levels in
 * a chain.
 * @num_layers: Number of slices in an array texture or number of faces in
 * a cubemap texture.
 */
struct svga3dsurface_cache {
	const struct svga3d_surface_desc *desc;
	struct svga3dsurface_mip mip[DRM_VMW_MAX_MIP_LEVELS];
	size_t mip_chain_bytes;
	size_t sheet_bytes;
	u32 num_mip_levels;
	u32 num_layers;
};

/**
 * struct svga3dsurface_loc - Surface location
 * @sub_resource: Surface subresource. Defined as layer * num_mip_levels +
 * mip_level.
 * @x: X coordinate.
 * @y: Y coordinate.
 * @z: Z coordinate.
 */
struct svga3dsurface_loc {
	u32 sub_resource;
	u32 x, y, z;
};

/**
 * svga3dsurface_subres - Compute the subresource from layer and mipmap.
 * @cache: Surface layout data.
 * @mip_level: The mipmap level.
 * @layer: The surface layer (face or array slice).
 *
 * Return: The subresource.
 */
static inline u32 svga3dsurface_subres(const struct svga3dsurface_cache *cache,
				       u32 mip_level, u32 layer)
{
	return cache->num_mip_levels * layer + mip_level;
}

/**
 * svga3dsurface_setup_cache - Build a surface cache entry
 * @size: The surface base level dimensions.
 * @format: The surface format.
 * @num_mip_levels: Number of mipmap levels.
 * @num_layers: Number of layers.
 * @cache: Pointer to a struct svga3dsurface_cach object to be filled in.
 *
 * Return: Zero on success, -EINVAL on invalid surface layout.
 */
static inline int svga3dsurface_setup_cache(const struct drm_vmw_size *size,
					    SVGA3dSurfaceFormat format,
					    u32 num_mip_levels,
					    u32 num_layers,
					    u32 num_samples,
					    struct svga3dsurface_cache *cache)
{
	const struct svga3d_surface_desc *desc;
	u32 i;

	memset(cache, 0, sizeof(*cache));
	cache->desc = desc = svga3dsurface_get_desc(format);
	cache->num_mip_levels = num_mip_levels;
	cache->num_layers = num_layers;
	for (i = 0; i < cache->num_mip_levels; i++) {
		struct svga3dsurface_mip *mip = &cache->mip[i];

		mip->size = svga3dsurface_get_mip_size(*size, i);
		mip->bytes = svga3dsurface_get_image_buffer_size
			(desc, &mip->size, 0);
		mip->row_stride =
			__KERNEL_DIV_ROUND_UP(mip->size.width,
					      desc->block_size.width) *
			desc->bytes_per_block * num_samples;
		if (!mip->row_stride)
			goto invalid_dim;

		mip->img_stride =
			__KERNEL_DIV_ROUND_UP(mip->size.height,
					      desc->block_size.height) *
			mip->row_stride;
		if (!mip->img_stride)
			goto invalid_dim;

		cache->mip_chain_bytes += mip->bytes;
	}
	cache->sheet_bytes = cache->mip_chain_bytes * num_layers;
	if (!cache->sheet_bytes)
		goto invalid_dim;

	return 0;

invalid_dim:
	VMW_DEBUG_USER("Invalid surface layout for dirty tracking.\n");
	return -EINVAL;
}

/**
 * svga3dsurface_get_loc - Get a surface location from an offset into the
 * backing store
 * @cache: Surface layout data.
 * @loc: Pointer to a struct svga3dsurface_loc to be filled in.
 * @offset: Offset into the surface backing store.
 */
static inline void
svga3dsurface_get_loc(const struct svga3dsurface_cache *cache,
		      struct svga3dsurface_loc *loc,
		      size_t offset)
{
	const struct svga3dsurface_mip *mip = &cache->mip[0];
	const struct svga3d_surface_desc *desc = cache->desc;
	u32 layer;
	int i;

	if (offset >= cache->sheet_bytes)
		offset %= cache->sheet_bytes;

	layer = offset / cache->mip_chain_bytes;
	offset -= layer * cache->mip_chain_bytes;
	for (i = 0; i < cache->num_mip_levels; ++i, ++mip) {
		if (mip->bytes > offset)
			break;
		offset -= mip->bytes;
	}

	loc->sub_resource = svga3dsurface_subres(cache, i, layer);
	loc->z = offset / mip->img_stride;
	offset -= loc->z * mip->img_stride;
	loc->z *= desc->block_size.depth;
	loc->y = offset / mip->row_stride;
	offset -= loc->y * mip->row_stride;
	loc->y *= desc->block_size.height;
	loc->x = offset / desc->bytes_per_block;
	loc->x *= desc->block_size.width;
}

/**
 * svga3dsurface_inc_loc - Clamp increment a surface location with one block
 * size
 * in each dimension.
 * @loc: Pointer to a struct svga3dsurface_loc to be incremented.
 *
 * When computing the size of a range as size = end - start, the range does not
 * include the end element. However a location representing the last byte
 * of a touched region in the backing store *is* included in the range.
 * This function modifies such a location to match the end definition
 * given as start + size which is the one used in a SVGA3dBox.
 */
static inline void
svga3dsurface_inc_loc(const struct svga3dsurface_cache *cache,
		      struct svga3dsurface_loc *loc)
{
	const struct svga3d_surface_desc *desc = cache->desc;
	u32 mip = loc->sub_resource % cache->num_mip_levels;
	const struct drm_vmw_size *size = &cache->mip[mip].size;

	loc->sub_resource++;
	loc->x += desc->block_size.width;
	if (loc->x > size->width)
		loc->x = size->width;
	loc->y += desc->block_size.height;
	if (loc->y > size->height)
		loc->y = size->height;
	loc->z += desc->block_size.depth;
	if (loc->z > size->depth)
		loc->z = size->depth;
}

/**
 * svga3dsurface_min_loc - The start location in a subresource
 * @cache: Surface layout data.
 * @sub_resource: The subresource.
 * @loc: Pointer to a struct svga3dsurface_loc to be filled in.
 */
static inline void
svga3dsurface_min_loc(const struct svga3dsurface_cache *cache,
		      u32 sub_resource,
		      struct svga3dsurface_loc *loc)
{
	loc->sub_resource = sub_resource;
	loc->x = loc->y = loc->z = 0;
}

/**
 * svga3dsurface_min_loc - The end location in a subresource
 * @cache: Surface layout data.
 * @sub_resource: The subresource.
 * @loc: Pointer to a struct svga3dsurface_loc to be filled in.
 *
 * Following the end definition given in svga3dsurface_inc_loc(),
 * Compute the end location of a surface subresource.
 */
static inline void
svga3dsurface_max_loc(const struct svga3dsurface_cache *cache,
		      u32 sub_resource,
		      struct svga3dsurface_loc *loc)
{
	const struct drm_vmw_size *size;
	u32 mip;

	loc->sub_resource = sub_resource + 1;
	mip = sub_resource % cache->num_mip_levels;
	size = &cache->mip[mip].size;
	loc->x = size->width;
	loc->y = size->height;
	loc->z = size->depth;
}

#endif /* _SVGA3D_SURFACEDEFS_H_ */
