/**************************************************************************
 *
 * Copyright © 2008-2015 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
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

#include <linux/kernel.h>

#ifdef __KERNEL__

#include <drm/vmwgfx_drm.h>
#define surf_size_struct struct drm_vmw_size

#else /* __KERNEL__ */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_A) (sizeof(_A) / sizeof((_A)[0]))
#endif /* ARRAY_SIZE */

#define max_t(type, x, y)  ((x) > (y) ? (x) : (y))
#define surf_size_struct SVGA3dSize
#define u32 uint32

#endif /* __KERNEL__ */

#include "svga3d_reg.h"

/*
 * enum svga3d_block_desc describes the active data channels in a block.
 *
 * There can be at-most four active channels in a block:
 *    1. Red, bump W, luminance and depth are stored in the first channel.
 *    2. Green, bump V and stencil are stored in the second channel.
 *    3. Blue and bump U are stored in the third channel.
 *    4. Alpha and bump Q are stored in the fourth channel.
 *
 * Block channels can be used to store compressed and buffer data:
 *    1. For compressed formats, only the data channel is used and its size
 *       is equal to that of a singular block in the compression scheme.
 *    2. For buffer formats, only the data channel is used and its size is
 *       exactly one byte in length.
 *    3. In each case the bit depth represent the size of a singular block.
 *
 * Note: Compressed and IEEE formats do not use the bitMask structure.
 */

enum svga3d_block_desc {
	SVGA3DBLOCKDESC_NONE        = 0,         /* No channels are active */
	SVGA3DBLOCKDESC_BLUE        = 1 << 0,    /* Block with red channel
						    data */
	SVGA3DBLOCKDESC_U           = 1 << 0,    /* Block with bump U channel
						    data */
	SVGA3DBLOCKDESC_UV_VIDEO    = 1 << 7,    /* Block with alternating video
						    U and V */
	SVGA3DBLOCKDESC_GREEN       = 1 << 1,    /* Block with green channel
						    data */
	SVGA3DBLOCKDESC_V           = 1 << 1,    /* Block with bump V channel
						    data */
	SVGA3DBLOCKDESC_STENCIL     = 1 << 1,    /* Block with a stencil
						    channel */
	SVGA3DBLOCKDESC_RED         = 1 << 2,    /* Block with blue channel
						    data */
	SVGA3DBLOCKDESC_W           = 1 << 2,    /* Block with bump W channel
						    data */
	SVGA3DBLOCKDESC_LUMINANCE   = 1 << 2,    /* Block with luminance channel
						    data */
	SVGA3DBLOCKDESC_Y           = 1 << 2,    /* Block with video luminance
						    data */
	SVGA3DBLOCKDESC_DEPTH       = 1 << 2,    /* Block with depth channel */
	SVGA3DBLOCKDESC_ALPHA       = 1 << 3,    /* Block with an alpha
						    channel */
	SVGA3DBLOCKDESC_Q           = 1 << 3,    /* Block with bump Q channel
						    data */
	SVGA3DBLOCKDESC_BUFFER      = 1 << 4,    /* Block stores 1 byte of
						    data */
	SVGA3DBLOCKDESC_COMPRESSED  = 1 << 5,    /* Block stores n bytes of
						    data depending on the
						    compression method used */
	SVGA3DBLOCKDESC_IEEE_FP     = 1 << 6,    /* Block stores data in an IEEE
						    floating point
						    representation in
						    all channels */
	SVGA3DBLOCKDESC_PLANAR_YUV  = 1 << 8,    /* Three separate blocks store
						    data. */
	SVGA3DBLOCKDESC_U_VIDEO     = 1 << 9,    /* Block with U video data */
	SVGA3DBLOCKDESC_V_VIDEO     = 1 << 10,   /* Block with V video data */
	SVGA3DBLOCKDESC_EXP         = 1 << 11,   /* Shared exponent */
	SVGA3DBLOCKDESC_SRGB        = 1 << 12,   /* Data is in sRGB format */
	SVGA3DBLOCKDESC_2PLANAR_YUV = 1 << 13,   /* 2 planes of Y, UV,
						    e.g., NV12. */
	SVGA3DBLOCKDESC_3PLANAR_YUV = 1 << 14,   /* 3 planes of separate
						    Y, U, V, e.g., YV12. */

	SVGA3DBLOCKDESC_RG         = SVGA3DBLOCKDESC_RED |
	SVGA3DBLOCKDESC_GREEN,
	SVGA3DBLOCKDESC_RGB        = SVGA3DBLOCKDESC_RG |
	SVGA3DBLOCKDESC_BLUE,
	SVGA3DBLOCKDESC_RGB_SRGB   = SVGA3DBLOCKDESC_RGB |
	SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_RGBA       = SVGA3DBLOCKDESC_RGB |
	SVGA3DBLOCKDESC_ALPHA,
	SVGA3DBLOCKDESC_RGBA_SRGB  = SVGA3DBLOCKDESC_RGBA |
	SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_UV         = SVGA3DBLOCKDESC_U |
	SVGA3DBLOCKDESC_V,
	SVGA3DBLOCKDESC_UVL        = SVGA3DBLOCKDESC_UV |
	SVGA3DBLOCKDESC_LUMINANCE,
	SVGA3DBLOCKDESC_UVW        = SVGA3DBLOCKDESC_UV |
	SVGA3DBLOCKDESC_W,
	SVGA3DBLOCKDESC_UVWA       = SVGA3DBLOCKDESC_UVW |
	SVGA3DBLOCKDESC_ALPHA,
	SVGA3DBLOCKDESC_UVWQ       = SVGA3DBLOCKDESC_U |
	SVGA3DBLOCKDESC_V |
	SVGA3DBLOCKDESC_W |
	SVGA3DBLOCKDESC_Q,
	SVGA3DBLOCKDESC_LA         = SVGA3DBLOCKDESC_LUMINANCE |
	SVGA3DBLOCKDESC_ALPHA,
	SVGA3DBLOCKDESC_R_FP       = SVGA3DBLOCKDESC_RED |
	SVGA3DBLOCKDESC_IEEE_FP,
	SVGA3DBLOCKDESC_RG_FP      = SVGA3DBLOCKDESC_R_FP |
	SVGA3DBLOCKDESC_GREEN,
	SVGA3DBLOCKDESC_RGB_FP     = SVGA3DBLOCKDESC_RG_FP |
	SVGA3DBLOCKDESC_BLUE,
	SVGA3DBLOCKDESC_RGBA_FP    = SVGA3DBLOCKDESC_RGB_FP |
	SVGA3DBLOCKDESC_ALPHA,
	SVGA3DBLOCKDESC_DS         = SVGA3DBLOCKDESC_DEPTH |
	SVGA3DBLOCKDESC_STENCIL,
	SVGA3DBLOCKDESC_YUV        = SVGA3DBLOCKDESC_UV_VIDEO |
	SVGA3DBLOCKDESC_Y,
	SVGA3DBLOCKDESC_AYUV       = SVGA3DBLOCKDESC_ALPHA |
	SVGA3DBLOCKDESC_Y |
	SVGA3DBLOCKDESC_U_VIDEO |
	SVGA3DBLOCKDESC_V_VIDEO,
	SVGA3DBLOCKDESC_RGBE       = SVGA3DBLOCKDESC_RGB |
	SVGA3DBLOCKDESC_EXP,
	SVGA3DBLOCKDESC_COMPRESSED_SRGB = SVGA3DBLOCKDESC_COMPRESSED |
	SVGA3DBLOCKDESC_SRGB,
	SVGA3DBLOCKDESC_NV12       = SVGA3DBLOCKDESC_PLANAR_YUV |
	SVGA3DBLOCKDESC_2PLANAR_YUV,
	SVGA3DBLOCKDESC_YV12       = SVGA3DBLOCKDESC_PLANAR_YUV |
	SVGA3DBLOCKDESC_3PLANAR_YUV,
};

/*
 * SVGA3dSurfaceDesc describes the actual pixel data.
 *
 * This structure provides the following information:
 *    1. Block description.
 *    2. Dimensions of a block in the surface.
 *    3. Size of block in bytes.
 *    4. Bit depth of the pixel data.
 *    5. Channel bit depths and masks (if applicable).
 */
struct svga3d_channel_def {
	union {
		u8 blue;
		u8 u;
		u8 uv_video;
		u8 u_video;
	};
	union {
		u8 green;
		u8 v;
		u8 stencil;
		u8 v_video;
	};
	union {
		u8 red;
		u8 w;
		u8 luminance;
		u8 y;
		u8 depth;
		u8 data;
	};
	union {
		u8 alpha;
		u8 q;
		u8 exp;
	};
};

struct svga3d_surface_desc {
	SVGA3dSurfaceFormat format;
	enum svga3d_block_desc block_desc;
	surf_size_struct block_size;
	u32 bytes_per_block;
	u32 pitch_bytes_per_block;

	u32 total_bit_depth;
	struct svga3d_channel_def bit_depth;
	struct svga3d_channel_def bit_offset;
};

static const struct svga3d_surface_desc svga3d_surface_descs[] = {
   {SVGA3D_FORMAT_INVALID, SVGA3DBLOCKDESC_NONE,
      {1, 1, 1},  0, 0,
      0, {{0}, {0}, {0}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_X8R8G8B8, SVGA3DBLOCKDESC_RGB,
      {1, 1, 1},  4, 4,
      24, {{8}, {8}, {8}, {0}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_A8R8G8B8, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_R5G6B5, SVGA3DBLOCKDESC_RGB,
      {1, 1, 1},  2, 2,
      16, {{5}, {6}, {5}, {0}},
      {{0}, {5}, {11}, {0}}},

   {SVGA3D_X1R5G5B5, SVGA3DBLOCKDESC_RGB,
      {1, 1, 1},  2, 2,
      15, {{5}, {5}, {5}, {0}},
      {{0}, {5}, {10}, {0}}},

   {SVGA3D_A1R5G5B5, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  2, 2,
      16, {{5}, {5}, {5}, {1}},
      {{0}, {5}, {10}, {15}}},

   {SVGA3D_A4R4G4B4, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  2, 2,
      16, {{4}, {4}, {4}, {4}},
      {{0}, {4}, {8}, {12}}},

   {SVGA3D_Z_D32, SVGA3DBLOCKDESC_DEPTH,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_Z_D16, SVGA3DBLOCKDESC_DEPTH,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_Z_D24S8, SVGA3DBLOCKDESC_DS,
      {1, 1, 1},  4, 4,
      32, {{0}, {8}, {24}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_Z_D15S1, SVGA3DBLOCKDESC_DS,
      {1, 1, 1},  2, 2,
      16, {{0}, {1}, {15}, {0}},
      {{0}, {15}, {0}, {0}}},

   {SVGA3D_LUMINANCE8, SVGA3DBLOCKDESC_LUMINANCE,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_LUMINANCE4_ALPHA4, SVGA3DBLOCKDESC_LA,
    {1  , 1, 1},  1, 1,
      8, {{0}, {0}, {4}, {4}},
      {{0}, {0}, {0}, {4}}},

   {SVGA3D_LUMINANCE16, SVGA3DBLOCKDESC_LUMINANCE,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_LUMINANCE8_ALPHA8, SVGA3DBLOCKDESC_LA,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {8}, {8}},
      {{0}, {0}, {0}, {8}}},

   {SVGA3D_DXT1, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  8, 8,
      64, {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_DXT2, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_DXT3, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_DXT4, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_DXT5, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BUMPU8V8, SVGA3DBLOCKDESC_UV,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {8}, {8}},
      {{0}, {0}, {0}, {8}}},

   {SVGA3D_BUMPL6V5U5, SVGA3DBLOCKDESC_UVL,
      {1, 1, 1},  2, 2,
      16, {{5}, {5}, {6}, {0}},
      {{11}, {6}, {0}, {0}}},

   {SVGA3D_BUMPX8L8V8U8, SVGA3DBLOCKDESC_UVL,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {0}},
      {{16}, {8}, {0}, {0}}},

   {SVGA3D_BUMPL8V8U8, SVGA3DBLOCKDESC_UVL,
      {1, 1, 1},  3, 3,
      24, {{8}, {8}, {8}, {0}},
      {{16}, {8}, {0}, {0}}},

   {SVGA3D_ARGB_S10E5, SVGA3DBLOCKDESC_RGBA_FP,
      {1, 1, 1},  8, 8,
      64, {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_ARGB_S23E8, SVGA3DBLOCKDESC_RGBA_FP,
      {1, 1, 1},  16, 16,
      128, {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_A2R10G10B10, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{10}, {10}, {10}, {2}},
      {{0}, {10}, {20}, {30}}},

   {SVGA3D_V8U8, SVGA3DBLOCKDESC_UV,
      {1, 1, 1},  2, 2,
      16, {{8}, {8}, {0}, {0}},
      {{8}, {0}, {0}, {0}}},

   {SVGA3D_Q8W8V8U8, SVGA3DBLOCKDESC_UVWQ,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{24}, {16}, {8}, {0}}},

   {SVGA3D_CxV8U8, SVGA3DBLOCKDESC_UV,
      {1, 1, 1},  2, 2,
      16, {{8}, {8}, {0}, {0}},
      {{8}, {0}, {0}, {0}}},

   {SVGA3D_X8L8V8U8, SVGA3DBLOCKDESC_UVL,
      {1, 1, 1},  4, 4,
      24, {{8}, {8}, {8}, {0}},
      {{16}, {8}, {0}, {0}}},

   {SVGA3D_A2W10V10U10, SVGA3DBLOCKDESC_UVWA,
      {1, 1, 1},  4, 4,
      32, {{10}, {10}, {10}, {2}},
      {{0}, {10}, {20}, {30}}},

   {SVGA3D_ALPHA8, SVGA3DBLOCKDESC_ALPHA,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {0}, {8}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R_S10E5, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R_S23E8, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_RG_S10E5, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  4, 4,
      32, {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_RG_S23E8, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  8, 8,
      64, {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_BUFFER, SVGA3DBLOCKDESC_BUFFER,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_Z_D24X8, SVGA3DBLOCKDESC_DEPTH,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {24}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_V16U16, SVGA3DBLOCKDESC_UV,
      {1, 1, 1},  4, 4,
      32, {{16}, {16}, {0}, {0}},
      {{16}, {0}, {0}, {0}}},

   {SVGA3D_G16R16, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  4, 4,
      32, {{0}, {16}, {16}, {0}},
      {{0}, {0}, {16}, {0}}},

   {SVGA3D_A16B16G16R16, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  8, 8,
      64, {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_UYVY, SVGA3DBLOCKDESC_YUV,
      {1, 1, 1},  2, 2,
      16, {{8}, {0}, {8}, {0}},
      {{0}, {0}, {8}, {0}}},

   {SVGA3D_YUY2, SVGA3DBLOCKDESC_YUV,
      {1, 1, 1},  2, 2,
      16, {{8}, {0}, {8}, {0}},
      {{8}, {0}, {0}, {0}}},

   {SVGA3D_NV12, SVGA3DBLOCKDESC_NV12,
      {2, 2, 1},  6, 2,
      48, {{0}, {0}, {48}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_AYUV, SVGA3DBLOCKDESC_AYUV,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_R32G32B32A32_TYPELESS, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  16, 16,
      128, {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_R32G32B32A32_UINT, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  16, 16,
      128, {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_R32G32B32A32_SINT, SVGA3DBLOCKDESC_UVWQ,
      {1, 1, 1},  16, 16,
      128, {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_R32G32B32_TYPELESS, SVGA3DBLOCKDESC_RGB,
      {1, 1, 1},  12, 12,
      96, {{32}, {32}, {32}, {0}},
      {{64}, {32}, {0}, {0}}},

   {SVGA3D_R32G32B32_FLOAT, SVGA3DBLOCKDESC_RGB_FP,
      {1, 1, 1},  12, 12,
      96, {{32}, {32}, {32}, {0}},
      {{64}, {32}, {0}, {0}}},

   {SVGA3D_R32G32B32_UINT, SVGA3DBLOCKDESC_RGB,
      {1, 1, 1},  12, 12,
      96, {{32}, {32}, {32}, {0}},
      {{64}, {32}, {0}, {0}}},

   {SVGA3D_R32G32B32_SINT, SVGA3DBLOCKDESC_UVW,
      {1, 1, 1},  12, 12,
      96, {{32}, {32}, {32}, {0}},
      {{64}, {32}, {0}, {0}}},

   {SVGA3D_R16G16B16A16_TYPELESS, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  8, 8,
      64, {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R16G16B16A16_UINT, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  8, 8,
      64, {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R16G16B16A16_SNORM, SVGA3DBLOCKDESC_UVWQ,
      {1, 1, 1},  8, 8,
      64, {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R16G16B16A16_SINT, SVGA3DBLOCKDESC_UVWQ,
      {1, 1, 1},  8, 8,
      64, {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R32G32_TYPELESS, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  8, 8,
      64, {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R32G32_UINT, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  8, 8,
      64, {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R32G32_SINT, SVGA3DBLOCKDESC_UV,
      {1, 1, 1},  8, 8,
      64, {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R32G8X24_TYPELESS, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  8, 8,
      64, {{0}, {8}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_D32_FLOAT_S8X24_UINT, SVGA3DBLOCKDESC_DS,
      {1, 1, 1},  8, 8,
      64, {{0}, {8}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R32_FLOAT_X8X24_TYPELESS, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  8, 8,
      64, {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_X32_TYPELESS_G8X24_UINT, SVGA3DBLOCKDESC_GREEN,
      {1, 1, 1},  8, 8,
      64, {{0}, {8}, {0}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R10G10B10A2_TYPELESS, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{10}, {10}, {10}, {2}},
      {{0}, {10}, {20}, {30}}},

   {SVGA3D_R10G10B10A2_UINT, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{10}, {10}, {10}, {2}},
      {{0}, {10}, {20}, {30}}},

   {SVGA3D_R11G11B10_FLOAT, SVGA3DBLOCKDESC_RGB_FP,
      {1, 1, 1},  4, 4,
      32, {{10}, {11}, {11}, {0}},
      {{0}, {10}, {21}, {0}}},

   {SVGA3D_R8G8B8A8_TYPELESS, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R8G8B8A8_UNORM, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R8G8B8A8_UNORM_SRGB, SVGA3DBLOCKDESC_RGBA_SRGB,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R8G8B8A8_UINT, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R8G8B8A8_SINT, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{16}, {8}, {0}, {24}}},

   {SVGA3D_R16G16_TYPELESS, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  4, 4,
      32, {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R16G16_UINT, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  4, 4,
      32, {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R16G16_SINT, SVGA3DBLOCKDESC_UV,
      {1, 1, 1},  4, 4,
      32, {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R32_TYPELESS, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_D32_FLOAT, SVGA3DBLOCKDESC_DEPTH,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R32_UINT, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R32_SINT, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R24G8_TYPELESS, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  4, 4,
      32, {{0}, {8}, {24}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_D24_UNORM_S8_UINT, SVGA3DBLOCKDESC_DS,
      {1, 1, 1},  4, 4,
      32, {{0}, {8}, {24}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_R24_UNORM_X8_TYPELESS, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {24}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_X24_TYPELESS_G8_UINT, SVGA3DBLOCKDESC_GREEN,
      {1, 1, 1},  4, 4,
      32, {{0}, {8}, {0}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_R8G8_TYPELESS, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  2, 2,
      16, {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R8G8_UNORM, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  2, 2,
      16, {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R8G8_UINT, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  2, 2,
      16, {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R8G8_SINT, SVGA3DBLOCKDESC_UV,
      {1, 1, 1},  2, 2,
      16, {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_R16_TYPELESS, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R16_UNORM, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R16_UINT, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R16_SNORM, SVGA3DBLOCKDESC_U,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R16_SINT, SVGA3DBLOCKDESC_U,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_TYPELESS, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_UNORM, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_UINT, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_SNORM, SVGA3DBLOCKDESC_U,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8_SINT, SVGA3DBLOCKDESC_U,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_P8, SVGA3DBLOCKDESC_RED,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {8}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R9G9B9E5_SHAREDEXP, SVGA3DBLOCKDESC_RGBE,
      {1, 1, 1},  4, 4,
      32, {{9}, {9}, {9}, {5}},
      {{18}, {9}, {0}, {27}}},

   {SVGA3D_R8G8_B8G8_UNORM, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  2, 2,
      16, {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_G8R8_G8B8_UNORM, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  2, 2,
      16, {{0}, {8}, {8}, {0}},
      {{0}, {8}, {0}, {0}}},

   {SVGA3D_BC1_TYPELESS, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  8, 8,
      64, {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC1_UNORM_SRGB, SVGA3DBLOCKDESC_COMPRESSED_SRGB,
      {4, 4, 1},  8, 8,
      64, {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC2_TYPELESS, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC2_UNORM_SRGB, SVGA3DBLOCKDESC_COMPRESSED_SRGB,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC3_TYPELESS, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC3_UNORM_SRGB, SVGA3DBLOCKDESC_COMPRESSED_SRGB,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC4_TYPELESS, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  8, 8,
      64, {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_ATI1, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  8, 8,
      64, {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC4_SNORM, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  8, 8,
      64, {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC5_TYPELESS, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_ATI2, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC5_SNORM, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R10G10B10_XR_BIAS_A2_UNORM, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{10}, {10}, {10}, {2}},
      {{0}, {10}, {20}, {30}}},

   {SVGA3D_B8G8R8A8_TYPELESS, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_B8G8R8A8_UNORM_SRGB, SVGA3DBLOCKDESC_RGBA_SRGB,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_B8G8R8X8_TYPELESS, SVGA3DBLOCKDESC_RGB,
      {1, 1, 1},  4, 4,
      24, {{8}, {8}, {8}, {0}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_B8G8R8X8_UNORM_SRGB, SVGA3DBLOCKDESC_RGB_SRGB,
      {1, 1, 1},  4, 4,
      24, {{8}, {8}, {8}, {0}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_Z_DF16, SVGA3DBLOCKDESC_DEPTH,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_Z_DF24, SVGA3DBLOCKDESC_DEPTH,
      {1, 1, 1},  4, 4,
      32, {{0}, {8}, {24}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_Z_D24S8_INT, SVGA3DBLOCKDESC_DS,
      {1, 1, 1},  4, 4,
      32, {{0}, {8}, {24}, {0}},
      {{0}, {24}, {0}, {0}}},

   {SVGA3D_YV12, SVGA3DBLOCKDESC_YV12,
      {2, 2, 1},  6, 2,
      48, {{0}, {0}, {48}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R32G32B32A32_FLOAT, SVGA3DBLOCKDESC_RGBA_FP,
      {1, 1, 1},  16, 16,
      128, {{32}, {32}, {32}, {32}},
      {{64}, {32}, {0}, {96}}},

   {SVGA3D_R16G16B16A16_FLOAT, SVGA3DBLOCKDESC_RGBA_FP,
      {1, 1, 1},  8, 8,
      64, {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R16G16B16A16_UNORM, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  8, 8,
      64, {{16}, {16}, {16}, {16}},
      {{32}, {16}, {0}, {48}}},

   {SVGA3D_R32G32_FLOAT, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  8, 8,
      64, {{0}, {32}, {32}, {0}},
      {{0}, {32}, {0}, {0}}},

   {SVGA3D_R10G10B10A2_UNORM, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{10}, {10}, {10}, {2}},
      {{0}, {10}, {20}, {30}}},

   {SVGA3D_R8G8B8A8_SNORM, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{24}, {16}, {8}, {0}}},

   {SVGA3D_R16G16_FLOAT, SVGA3DBLOCKDESC_RG_FP,
      {1, 1, 1},  4, 4,
      32, {{0}, {16}, {16}, {0}},
      {{0}, {16}, {0}, {0}}},

   {SVGA3D_R16G16_UNORM, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  4, 4,
      32, {{0}, {16}, {16}, {0}},
      {{0}, {0}, {16}, {0}}},

   {SVGA3D_R16G16_SNORM, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  4, 4,
      32, {{16}, {16}, {0}, {0}},
      {{16}, {0}, {0}, {0}}},

   {SVGA3D_R32_FLOAT, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  4, 4,
      32, {{0}, {0}, {32}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_R8G8_SNORM, SVGA3DBLOCKDESC_RG,
      {1, 1, 1},  2, 2,
      16, {{8}, {8}, {0}, {0}},
      {{8}, {0}, {0}, {0}}},

   {SVGA3D_R16_FLOAT, SVGA3DBLOCKDESC_R_FP,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_D16_UNORM, SVGA3DBLOCKDESC_DEPTH,
      {1, 1, 1},  2, 2,
      16, {{0}, {0}, {16}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_A8_UNORM, SVGA3DBLOCKDESC_ALPHA,
      {1, 1, 1},  1, 1,
      8, {{0}, {0}, {0}, {8}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC1_UNORM, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  8, 8,
      64, {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC2_UNORM, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC3_UNORM, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_B5G6R5_UNORM, SVGA3DBLOCKDESC_RGB,
      {1, 1, 1},  2, 2,
      16, {{5}, {6}, {5}, {0}},
      {{0}, {5}, {11}, {0}}},

   {SVGA3D_B5G5R5A1_UNORM, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  2, 2,
      16, {{5}, {5}, {5}, {1}},
      {{0}, {5}, {10}, {15}}},

   {SVGA3D_B8G8R8A8_UNORM, SVGA3DBLOCKDESC_RGBA,
      {1, 1, 1},  4, 4,
      32, {{8}, {8}, {8}, {8}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_B8G8R8X8_UNORM, SVGA3DBLOCKDESC_RGB,
      {1, 1, 1},  4, 4,
      24, {{8}, {8}, {8}, {0}},
      {{0}, {8}, {16}, {24}}},

   {SVGA3D_BC4_UNORM, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  8, 8,
      64, {{0}, {0}, {64}, {0}},
      {{0}, {0}, {0}, {0}}},

   {SVGA3D_BC5_UNORM, SVGA3DBLOCKDESC_COMPRESSED,
      {4, 4, 1},  16, 16,
      128, {{0}, {0}, {128}, {0}},
      {{0}, {0}, {0}, {0}}},

};

static inline u32 clamped_umul32(u32 a, u32 b)
{
	uint64_t tmp = (uint64_t) a*b;
	return (tmp > (uint64_t) ((u32) -1)) ? (u32) -1 : tmp;
}

static inline const struct svga3d_surface_desc *
svga3dsurface_get_desc(SVGA3dSurfaceFormat format)
{
	if (format < ARRAY_SIZE(svga3d_surface_descs))
		return &svga3d_surface_descs[format];

	return &svga3d_surface_descs[SVGA3D_FORMAT_INVALID];
}

/*
 *----------------------------------------------------------------------
 *
 * svga3dsurface_get_mip_size --
 *
 *      Given a base level size and the mip level, compute the size of
 *      the mip level.
 *
 * Results:
 *      See above.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static inline surf_size_struct
svga3dsurface_get_mip_size(surf_size_struct base_level, u32 mip_level)
{
	surf_size_struct size;

	size.width = max_t(u32, base_level.width >> mip_level, 1);
	size.height = max_t(u32, base_level.height >> mip_level, 1);
	size.depth = max_t(u32, base_level.depth >> mip_level, 1);
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

/*
 *-----------------------------------------------------------------------------
 *
 * svga3dsurface_get_image_buffer_size --
 *
 *      Return the number of bytes of buffer space required to store
 *      one image of a surface, optionally using the specified pitch.
 *
 *      If pitch is zero, it is assumed that rows are tightly packed.
 *
 *      This function is overflow-safe. If the result would have
 *      overflowed, instead we return MAX_UINT32.
 *
 * Results:
 *      Byte count.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
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
