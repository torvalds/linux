// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>

#include "msm_drv.h"
#include "mdp_kms.h"

static struct csc_cfg csc_convert[CSC_MAX] = {
	[CSC_RGB2RGB] = {
		.type = CSC_RGB2RGB,
		.matrix = {
			0x0200, 0x0000, 0x0000,
			0x0000, 0x0200, 0x0000,
			0x0000, 0x0000, 0x0200
		},
		.pre_bias =	{ 0x0, 0x0, 0x0 },
		.post_bias =	{ 0x0, 0x0, 0x0 },
		.pre_clamp =	{ 0x0, 0xff, 0x0, 0xff, 0x0, 0xff },
		.post_clamp =	{ 0x0, 0xff, 0x0, 0xff, 0x0, 0xff },
	},
	[CSC_YUV2RGB] = {
		.type = CSC_YUV2RGB,
		.matrix = {
			0x0254, 0x0000, 0x0331,
			0x0254, 0xff37, 0xfe60,
			0x0254, 0x0409, 0x0000
		},
		.pre_bias =	{ 0xfff0, 0xff80, 0xff80 },
		.post_bias =	{ 0x00, 0x00, 0x00 },
		.pre_clamp =	{ 0x00, 0xff, 0x00, 0xff, 0x00, 0xff },
		.post_clamp =	{ 0x00, 0xff, 0x00, 0xff, 0x00, 0xff },
	},
	[CSC_RGB2YUV] = {
		.type = CSC_RGB2YUV,
		.matrix = {
			0x0083, 0x0102, 0x0032,
			0x1fb5, 0x1f6c, 0x00e1,
			0x00e1, 0x1f45, 0x1fdc
		},
		.pre_bias =	{ 0x00, 0x00, 0x00 },
		.post_bias =	{ 0x10, 0x80, 0x80 },
		.pre_clamp =	{ 0x00, 0xff, 0x00, 0xff, 0x00, 0xff },
		.post_clamp =	{ 0x10, 0xeb, 0x10, 0xf0, 0x10, 0xf0 },
	},
	[CSC_YUV2YUV] = {
		.type = CSC_YUV2YUV,
		.matrix = {
			0x0200, 0x0000, 0x0000,
			0x0000, 0x0200, 0x0000,
			0x0000, 0x0000, 0x0200
		},
		.pre_bias =	{ 0x00, 0x00, 0x00 },
		.post_bias =	{ 0x00, 0x00, 0x00 },
		.pre_clamp =	{ 0x00, 0xff, 0x00, 0xff, 0x00, 0xff },
		.post_clamp =	{ 0x00, 0xff, 0x00, 0xff, 0x00, 0xff },
	},
};

#define MDP_TILE_HEIGHT_DEFAULT	1
#define MDP_TILE_HEIGHT_UBWC	4
#define MDP_TILE_HEIGHT_NV12	8

#define INTERLEAVED_RGB_FMT(fmt, bp, r, g, b, e0, e1, e2)                 \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), (e2), 0 },                               \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = 0,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 3,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT,                            \
	.num_planes = 1,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define INTERLEAVED_RGBA_FMT(fmt, bp, a, r, g, b, e0, e1, e2, e3)         \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = true,                                             \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 4,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT,                            \
	.num_planes = 1,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define INTERLEAVED_RGBX_FMT(fmt, bp, a, r, g, b, e0, e1, e2, e3)         \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 4,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT,                            \
	.num_planes = 1,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define INTERLEAVED_RGBA_DX_FMT(fmt, bp, a, r, g, b, e0, e1, e2, e3)      \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = true,                                             \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 4,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_DX,                                      \
	.num_planes = 1,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define INTERLEAVED_RGBX_DX_FMT(fmt, bp, a, r, g, b, e0, e1, e2, e3)      \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 4,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_DX,                                      \
	.num_planes = 1,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define INTERLEAVED_RGB_FMT_TILED(fmt, bp, r, g, b, e0, e1, e2)           \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), (e2), 0 },                               \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = 0,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 3,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_UBWC,                                     \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_COMPRESSED,                              \
	.num_planes = 2,                                                  \
	.tile_height = MDP_TILE_HEIGHT_UBWC,                              \
}

#define INTERLEAVED_RGBA_FMT_TILED(fmt, bp, a, r, g, b, e0, e1, e2, e3)   \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = true,                                             \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 4,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_UBWC,                                     \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_COMPRESSED,                              \
	.num_planes = 2,                                                  \
	.tile_height = MDP_TILE_HEIGHT_UBWC,                              \
}

#define INTERLEAVED_RGBX_FMT_TILED(fmt, bp, a, r, g, b, e0, e1, e2, e3)   \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 4,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_UBWC,                                     \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_COMPRESSED,                              \
	.num_planes = 2,                                                  \
	.tile_height = MDP_TILE_HEIGHT_UBWC,                              \
}

#define INTERLEAVED_RGBA_DX_FMT_TILED(fmt, bp, a, r, g, b, e0, e1, e2, e3) \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = true,                                             \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = 4,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_UBWC,                                     \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_DX |                                     \
		 MSM_FORMAT_FLAG_COMPRESSED,                              \
	.num_planes = 2,                                                  \
	.tile_height = MDP_TILE_HEIGHT_UBWC,                              \
}

#define INTERLEAVED_YUV_FMT(fmt, bp, r, g, b, e0, e1, e2, e3, chroma)     \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), (e2), (e3)},                             \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = 0,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 4,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_YUV,                                     \
	.num_planes = 1,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define PSEUDO_YUV_FMT(fmt, r, g, b, e0, e1, chroma)                      \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PSEUDO_PLANAR,                            \
	.alpha_enable = 0,                                                \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = 0,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_YUV,                                     \
	.num_planes = 2,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define PSEUDO_YUV_FMT_TILED(fmt, r, g, b, e0, e1, chroma, flg, th)       \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PSEUDO_PLANAR,                            \
	.alpha_enable = 0,                                                \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = 0,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = MDP_FETCH_UBWC,                                     \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_YUV |                                    \
		 MSM_FORMAT_FLAG_COMPRESSED | flg,                        \
	.num_planes = 4,                                                  \
	.tile_height = th                                                 \
}

#define PSEUDO_YUV_FMT_LOOSE(fmt, r, g, b, e0, e1, chroma)                \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PSEUDO_PLANAR,                            \
	.alpha_enable = 0,                                                \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = 0,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_ALIGN_MSB |                       \
		 MSM_FORMAT_FLAG_DX |                                     \
		 MSM_FORMAT_FLAG_YUV,                                     \
	.num_planes = 2,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define PLANAR_YUV_FMT(fmt, bp, r, g, b, e0, e1, e2, chroma)              \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PLANAR,                                   \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), (e2), 0 },                               \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = 0,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 1,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = MDP_FETCH_LINEAR,                                   \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT |                           \
		 MSM_FORMAT_FLAG_YUV,                                     \
	.num_planes = 3,                                                  \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

static const struct msm_format mdp_formats[] = {
	INTERLEAVED_RGBA_FMT(ARGB8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA),

	INTERLEAVED_RGBA_FMT(ABGR8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBX_FMT(XBGR8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBA_FMT(RGBA8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGBA_FMT(BGRA8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBX_FMT(BGRX8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBX_FMT(XRGB8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA),

	INTERLEAVED_RGBX_FMT(RGBX8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGB_FMT(RGB888, 3,
		BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGB_FMT(BGR888, 3,
		BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGB_FMT(RGB565, 2,
		BPC5, BPC6, BPC5,
		C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGB_FMT(BGR565, 2,
		BPC5, BPC6, BPC5,
		C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBA_FMT(ARGB1555, 2,
		BPC1A, BPC5, BPC5, BPC5,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA),

	INTERLEAVED_RGBA_FMT(ABGR1555, 2,
		BPC1A, BPC5, BPC5, BPC5,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBA_FMT(RGBA5551, 2,
		BPC1A, BPC5, BPC5, BPC5,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGBA_FMT(BGRA5551, 2,
		BPC1A, BPC5, BPC5, BPC5,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBX_FMT(XRGB1555, 2,
		BPC1A, BPC5, BPC5, BPC5,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA),

	INTERLEAVED_RGBX_FMT(XBGR1555, 2,
		BPC1A, BPC5, BPC5, BPC5,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBX_FMT(RGBX5551, 2,
		BPC1A, BPC5, BPC5, BPC5,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGBX_FMT(BGRX5551, 2,
		BPC1A, BPC5, BPC5, BPC5,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBA_FMT(ARGB4444, 2,
		BPC4A, BPC4, BPC4, BPC4,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA),

	INTERLEAVED_RGBA_FMT(ABGR4444, 2,
		BPC4A, BPC4, BPC4, BPC4,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBA_FMT(RGBA4444, 2,
		BPC4A, BPC4, BPC4, BPC4,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGBA_FMT(BGRA4444, 2,
		BPC4A, BPC4, BPC4, BPC4,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBX_FMT(XRGB4444, 2,
		BPC4A, BPC4, BPC4, BPC4,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA),

	INTERLEAVED_RGBX_FMT(XBGR4444, 2,
		BPC4A, BPC4, BPC4, BPC4,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBX_FMT(RGBX4444, 2,
		BPC4A, BPC4, BPC4, BPC4,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGBX_FMT(BGRX4444, 2,
		BPC4A, BPC4, BPC4, BPC4,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBA_DX_FMT(BGRA1010102, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBA_DX_FMT(RGBA1010102, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr),

	INTERLEAVED_RGBA_DX_FMT(ABGR2101010, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBA_DX_FMT(ARGB2101010, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA),

	INTERLEAVED_RGBX_DX_FMT(XRGB2101010, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA),

	INTERLEAVED_RGBX_DX_FMT(BGRX1010102, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBX_DX_FMT(XBGR2101010, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBX_DX_FMT(RGBX1010102, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr),

	/* --- RGB formats above / YUV formats below this line --- */

	/* 2 plane YUV */
	PSEUDO_YUV_FMT(NV12,
		BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_420),

	PSEUDO_YUV_FMT(NV21,
		BPC8, BPC8, BPC8,
		C2_R_Cr, C1_B_Cb,
		CHROMA_420),

	PSEUDO_YUV_FMT(NV16,
		BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_H2V1),

	PSEUDO_YUV_FMT(NV61,
		BPC8, BPC8, BPC8,
		C2_R_Cr, C1_B_Cb,
		CHROMA_H2V1),

	PSEUDO_YUV_FMT_LOOSE(P010,
		BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_420),

	/* 1 plane YUV */
	INTERLEAVED_YUV_FMT(VYUY, 2,
		BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C0_G_Y,
		CHROMA_H2V1),

	INTERLEAVED_YUV_FMT(UYVY, 2,
		BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C0_G_Y,
		CHROMA_H2V1),

	INTERLEAVED_YUV_FMT(YUYV, 2,
		BPC8, BPC8, BPC8,
		C0_G_Y, C1_B_Cb, C0_G_Y, C2_R_Cr,
		CHROMA_H2V1),

	INTERLEAVED_YUV_FMT(YVYU, 2,
		BPC8, BPC8, BPC8,
		C0_G_Y, C2_R_Cr, C0_G_Y, C1_B_Cb,
		CHROMA_H2V1),

	/* 3 plane YUV */
	PLANAR_YUV_FMT(YUV420, 1,
		BPC8, BPC8, BPC8,
		C2_R_Cr, C1_B_Cb, C0_G_Y,
		CHROMA_420),

	PLANAR_YUV_FMT(YVU420, 1,
		BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr, C0_G_Y,
		CHROMA_420),
};

/*
 * UBWC formats table:
 * This table holds the UBWC formats supported.
 * If a compression ratio needs to be used for this or any other format,
 * the data will be passed by user-space.
 */
static const struct msm_format mdp_formats_ubwc[] = {
	INTERLEAVED_RGB_FMT_TILED(BGR565, 2,
		BPC5, BPC6, BPC5,
		C2_R_Cr, C0_G_Y, C1_B_Cb),

	INTERLEAVED_RGBA_FMT_TILED(ABGR8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	/* ARGB8888 and ABGR8888 purposely have the same color
	 * ordering.  The hardware only supports ABGR8888 UBWC
	 * natively.
	 */
	INTERLEAVED_RGBA_FMT_TILED(ARGB8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBX_FMT_TILED(XBGR8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBX_FMT_TILED(XRGB8888, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBA_DX_FMT_TILED(ABGR2101010, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBA_DX_FMT_TILED(XBGR2101010, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	INTERLEAVED_RGBA_DX_FMT_TILED(XRGB2101010, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	/* XRGB2101010 and ARGB2101010 purposely have the same color
	* ordering.  The hardware only supports ARGB2101010 UBWC
	* natively.
	*/
	INTERLEAVED_RGBA_DX_FMT_TILED(ARGB2101010, 4,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA),

	PSEUDO_YUV_FMT_TILED(NV12,
		BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_420, 0,
		MDP_TILE_HEIGHT_NV12),

	PSEUDO_YUV_FMT_TILED(P010,
		BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_420, MSM_FORMAT_FLAG_DX,
		MDP_TILE_HEIGHT_UBWC),
};

const struct msm_format *mdp_get_format(struct msm_kms *kms, uint32_t format,
		uint64_t modifier)
{
	const struct msm_format *map = NULL;
	ssize_t map_size;
	int i;

	switch (modifier) {
	case 0:
		map = mdp_formats;
		map_size = ARRAY_SIZE(mdp_formats);
		break;
	case DRM_FORMAT_MOD_QCOM_COMPRESSED:
		map = mdp_formats_ubwc;
		map_size = ARRAY_SIZE(mdp_formats_ubwc);
		break;
	default:
		drm_err(kms->dev, "unsupported format modifier %llX\n", modifier);
		return NULL;
	}

	for (i = 0; i < map_size; i++) {
		const struct msm_format *f = &map[i];

		if (f->pixel_format == format)
			return f;
	}

	drm_err(kms->dev, "unsupported fmt: %p4cc modifier 0x%llX\n",
		&format, modifier);

	return NULL;
}

struct csc_cfg *mdp_get_default_csc_cfg(enum csc_type type)
{
	if (WARN_ON(type >= CSC_MAX))
		return NULL;

	return &csc_convert[type];
}
