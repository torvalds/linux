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

#define INTERLEAVED_RGB_FMT(fmt, a, r, g, b, e0, e1, e2, e3, uc, alpha,   \
bp, flg, fm, np)                                                          \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = alpha,                                            \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = uc,                                               \
	.bpp = bp,                                                        \
	.fetch_mode = fm,                                                 \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT | flg,                      \
	.num_planes = np,                                                 \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define INTERLEAVED_RGB_FMT_TILED(fmt, a, r, g, b, e0, e1, e2, e3, uc,    \
alpha, bp, flg, fm, np, th)                                               \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = alpha,                                            \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = CHROMA_FULL,                                     \
	.unpack_count = uc,                                               \
	.bpp = bp,                                                        \
	.fetch_mode = fm,                                                 \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT | flg,                      \
	.num_planes = np,                                                 \
	.tile_height = th                                                 \
}

#define INTERLEAVED_YUV_FMT(fmt, a, r, g, b, e0, e1, e2, e3,              \
alpha, chroma, count, bp, flg, fm, np)                                    \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_INTERLEAVED,                              \
	.alpha_enable = alpha,                                            \
	.element = { (e0), (e1), (e2), (e3)},                             \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = count,                                            \
	.bpp = bp,                                                        \
	.fetch_mode = fm,                                                 \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT | flg,                      \
	.num_planes = np,                                                 \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define PSEUDO_YUV_FMT(fmt, a, r, g, b, e0, e1, chroma, flg, fm, np)      \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PSEUDO_PLANAR,                            \
	.alpha_enable = 0,                                                \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = fm,                                                 \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT | flg,                      \
	.num_planes = np,                                                 \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define PSEUDO_YUV_FMT_TILED(fmt, a, r, g, b, e0, e1, chroma,             \
flg, fm, np, th)                                                          \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PSEUDO_PLANAR,                            \
	.alpha_enable = 0,                                                \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = fm,                                                 \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT | flg,                      \
	.num_planes = np,                                                 \
	.tile_height = th                                                 \
}

#define PSEUDO_YUV_FMT_LOOSE(fmt, a, r, g, b, e0, e1, chroma, flg, fm, np)\
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PSEUDO_PLANAR,                            \
	.alpha_enable = 0,                                                \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = fm,                                                 \
	.flags = MSM_FORMAT_FLAG_UNPACK_ALIGN_MSB | flg,                  \
	.num_planes = np,                                                 \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

#define PSEUDO_YUV_FMT_LOOSE_TILED(fmt, a, r, g, b, e0, e1, chroma,       \
flg, fm, np, th)                                                          \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PSEUDO_PLANAR,                            \
	.alpha_enable = 0,                                                \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = fm,                                                 \
	.flags = MSM_FORMAT_FLAG_UNPACK_ALIGN_MSB | flg,                  \
	.num_planes = np,                                                 \
	.tile_height = th                                                 \
}

#define PLANAR_YUV_FMT(fmt, a, r, g, b, e0, e1, e2, alpha, chroma, bp,    \
flg, fm, np)                                                      \
{                                                                         \
	.pixel_format = DRM_FORMAT_ ## fmt,                               \
	.fetch_type = MDP_PLANE_PLANAR,                                   \
	.alpha_enable = alpha,                                            \
	.element = { (e0), (e1), (e2), 0 },                               \
	.bpc_g_y = g,                                                     \
	.bpc_b_cb = b,                                                    \
	.bpc_r_cr = r,                                                    \
	.bpc_a = a,                                                       \
	.chroma_sample = chroma,                                          \
	.unpack_count = 1,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = fm,                                                 \
	.flags = MSM_FORMAT_FLAG_UNPACK_TIGHT | flg,                      \
	.num_planes = np,                                                 \
	.tile_height = MDP_TILE_HEIGHT_DEFAULT                            \
}

static const struct msm_format mdp_formats[] = {
	INTERLEAVED_RGB_FMT(ARGB8888,
		BPC8A, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 4, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ABGR8888,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XBGR8888,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 4, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBA8888,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 4, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRA8888,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 4, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRX8888,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 4, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XRGB8888,
		BPC8A, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 4, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBX8888,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		false, 4, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGB888,
		0, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, 0, 3,
		false, 3, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGR888,
		0, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, 0, 3,
		false, 3, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGB565,
		0, BPC5, BPC6, BPC5,
		C1_B_Cb, C0_G_Y, C2_R_Cr, 0, 3,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGR565,
		0, BPC5, BPC6, BPC5,
		C2_R_Cr, C0_G_Y, C1_B_Cb, 0, 3,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ARGB1555,
		BPC1A, BPC5, BPC5, BPC5,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ABGR1555,
		BPC1A, BPC5, BPC5, BPC5,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBA5551,
		BPC1A, BPC5, BPC5, BPC5,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRA5551,
		BPC1A, BPC5, BPC5, BPC5,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XRGB1555,
		BPC1A, BPC5, BPC5, BPC5,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XBGR1555,
		BPC1A, BPC5, BPC5, BPC5,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBX5551,
		BPC1A, BPC5, BPC5, BPC5,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRX5551,
		BPC1A, BPC5, BPC5, BPC5,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ARGB4444,
		BPC4A, BPC4, BPC4, BPC4,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ABGR4444,
		BPC4A, BPC4, BPC4, BPC4,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBA4444,
		BPC4A, BPC4, BPC4, BPC4,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRA4444,
		BPC4A, BPC4, BPC4, BPC4,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XRGB4444,
		BPC4A, BPC4, BPC4, BPC4,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XBGR4444,
		BPC4A, BPC4, BPC4, BPC4,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBX4444,
		BPC4A, BPC4, BPC4, BPC4,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRX4444,
		BPC4A, BPC4, BPC4, BPC4,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 2, 0,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRA1010102,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 4, MSM_FORMAT_FLAG_DX,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBA1010102,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 4, MSM_FORMAT_FLAG_DX,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ABGR2101010,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, MSM_FORMAT_FLAG_DX,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ARGB2101010,
		BPC8A, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 4, MSM_FORMAT_FLAG_DX,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XRGB2101010,
		BPC8A, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 4, MSM_FORMAT_FLAG_DX,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRX1010102,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 4, MSM_FORMAT_FLAG_DX,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XBGR2101010,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 4, MSM_FORMAT_FLAG_DX,
		MDP_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBX1010102,
		BPC8A, BPC8, BPC8, BPC8,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		false, 4, MSM_FORMAT_FLAG_DX,
		MDP_FETCH_LINEAR, 1),

	/* --- RGB formats above / YUV formats below this line --- */

	/* 2 plane YUV */
	PSEUDO_YUV_FMT(NV12,
		0, BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_420, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	PSEUDO_YUV_FMT(NV21,
		0, BPC8, BPC8, BPC8,
		C2_R_Cr, C1_B_Cb,
		CHROMA_420, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	PSEUDO_YUV_FMT(NV16,
		0, BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_H2V1, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	PSEUDO_YUV_FMT(NV61,
		0, BPC8, BPC8, BPC8,
		C2_R_Cr, C1_B_Cb,
		CHROMA_H2V1, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	PSEUDO_YUV_FMT_LOOSE(P010,
		0, BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_420, MSM_FORMAT_FLAG_DX | MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	/* 1 plane YUV */
	INTERLEAVED_YUV_FMT(VYUY,
		0, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C0_G_Y,
		false, CHROMA_H2V1, 4, 2, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	INTERLEAVED_YUV_FMT(UYVY,
		0, BPC8, BPC8, BPC8,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C0_G_Y,
		false, CHROMA_H2V1, 4, 2, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	INTERLEAVED_YUV_FMT(YUYV,
		0, BPC8, BPC8, BPC8,
		C0_G_Y, C1_B_Cb, C0_G_Y, C2_R_Cr,
		false, CHROMA_H2V1, 4, 2, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	INTERLEAVED_YUV_FMT(YVYU,
		0, BPC8, BPC8, BPC8,
		C0_G_Y, C2_R_Cr, C0_G_Y, C1_B_Cb,
		false, CHROMA_H2V1, 4, 2, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 2),

	/* 3 plane YUV */
	PLANAR_YUV_FMT(YUV420,
		0, BPC8, BPC8, BPC8,
		C2_R_Cr, C1_B_Cb, C0_G_Y,
		false, CHROMA_420, 1, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 3),

	PLANAR_YUV_FMT(YVU420,
		0, BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr, C0_G_Y,
		false, CHROMA_420, 1, MSM_FORMAT_FLAG_YUV,
		MDP_FETCH_LINEAR, 3),
};

/*
 * UBWC formats table:
 * This table holds the UBWC formats supported.
 * If a compression ratio needs to be used for this or any other format,
 * the data will be passed by user-space.
 */
static const struct msm_format mdp_formats_ubwc[] = {
	INTERLEAVED_RGB_FMT_TILED(BGR565,
		0, BPC5, BPC6, BPC5,
		C2_R_Cr, C0_G_Y, C1_B_Cb, 0, 3,
		false, 2, MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(ABGR8888,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	/* ARGB8888 and ABGR8888 purposely have the same color
	 * ordering.  The hardware only supports ABGR8888 UBWC
	 * natively.
	 */
	INTERLEAVED_RGB_FMT_TILED(ARGB8888,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(XBGR8888,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 4, MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(XRGB8888,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 4, MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(ABGR2101010,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, MSM_FORMAT_FLAG_DX | MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(XBGR2101010,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, MSM_FORMAT_FLAG_DX | MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(XRGB2101010,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, MSM_FORMAT_FLAG_DX | MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	/* XRGB2101010 and ARGB2101010 purposely have the same color
	* ordering.  The hardware only supports ARGB2101010 UBWC
	* natively.
	*/
	INTERLEAVED_RGB_FMT_TILED(ARGB2101010,
		BPC8A, BPC8, BPC8, BPC8,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, MSM_FORMAT_FLAG_DX | MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 2, MDP_TILE_HEIGHT_UBWC),

	PSEUDO_YUV_FMT_TILED(NV12,
		0, BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_420, MSM_FORMAT_FLAG_YUV |
				MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 4, MDP_TILE_HEIGHT_NV12),

	PSEUDO_YUV_FMT_TILED(P010,
		0, BPC8, BPC8, BPC8,
		C1_B_Cb, C2_R_Cr,
		CHROMA_420, MSM_FORMAT_FLAG_DX |
				MSM_FORMAT_FLAG_YUV |
				MSM_FORMAT_FLAG_COMPRESSED,
		MDP_FETCH_UBWC, 4, MDP_TILE_HEIGHT_UBWC),
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
