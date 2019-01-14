/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <uapi/drm/drm_fourcc.h>

#include "msm_media_info.h"
#include "dpu_kms.h"
#include "dpu_formats.h"

#define DPU_UBWC_META_MACRO_W_H		16
#define DPU_UBWC_META_BLOCK_SIZE	256
#define DPU_UBWC_PLANE_SIZE_ALIGNMENT	4096

#define DPU_TILE_HEIGHT_DEFAULT	1
#define DPU_TILE_HEIGHT_TILED	4
#define DPU_TILE_HEIGHT_UBWC	4
#define DPU_TILE_HEIGHT_NV12	8

#define DPU_MAX_IMG_WIDTH		0x3FFF
#define DPU_MAX_IMG_HEIGHT		0x3FFF

/**
 * DPU supported format packing, bpp, and other format
 * information.
 * DPU currently only supports interleaved RGB formats
 * UBWC support for a pixel format is indicated by the flag,
 * there is additional meta data plane for such formats
 */

#define INTERLEAVED_RGB_FMT(fmt, a, r, g, b, e0, e1, e2, e3, uc, alpha,   \
bp, flg, fm, np)                                                          \
{                                                                         \
	.base.pixel_format = DRM_FORMAT_ ## fmt,                          \
	.fetch_planes = DPU_PLANE_INTERLEAVED,                            \
	.alpha_enable = alpha,                                            \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bits = { g, b, r, a },                                           \
	.chroma_sample = DPU_CHROMA_RGB,                                  \
	.unpack_align_msb = 0,                                            \
	.unpack_tight = 1,                                                \
	.unpack_count = uc,                                               \
	.bpp = bp,                                                        \
	.fetch_mode = fm,                                                 \
	.flag = {(flg)},                                                  \
	.num_planes = np,                                                 \
	.tile_height = DPU_TILE_HEIGHT_DEFAULT                            \
}

#define INTERLEAVED_RGB_FMT_TILED(fmt, a, r, g, b, e0, e1, e2, e3, uc,    \
alpha, bp, flg, fm, np, th)                                               \
{                                                                         \
	.base.pixel_format = DRM_FORMAT_ ## fmt,                          \
	.fetch_planes = DPU_PLANE_INTERLEAVED,                            \
	.alpha_enable = alpha,                                            \
	.element = { (e0), (e1), (e2), (e3) },                            \
	.bits = { g, b, r, a },                                           \
	.chroma_sample = DPU_CHROMA_RGB,                                  \
	.unpack_align_msb = 0,                                            \
	.unpack_tight = 1,                                                \
	.unpack_count = uc,                                               \
	.bpp = bp,                                                        \
	.fetch_mode = fm,                                                 \
	.flag = {(flg)},                                                  \
	.num_planes = np,                                                 \
	.tile_height = th                                                 \
}


#define INTERLEAVED_YUV_FMT(fmt, a, r, g, b, e0, e1, e2, e3,              \
alpha, chroma, count, bp, flg, fm, np)                                    \
{                                                                         \
	.base.pixel_format = DRM_FORMAT_ ## fmt,                          \
	.fetch_planes = DPU_PLANE_INTERLEAVED,                            \
	.alpha_enable = alpha,                                            \
	.element = { (e0), (e1), (e2), (e3)},                             \
	.bits = { g, b, r, a },                                           \
	.chroma_sample = chroma,                                          \
	.unpack_align_msb = 0,                                            \
	.unpack_tight = 1,                                                \
	.unpack_count = count,                                            \
	.bpp = bp,                                                        \
	.fetch_mode = fm,                                                 \
	.flag = {(flg)},                                                  \
	.num_planes = np,                                                 \
	.tile_height = DPU_TILE_HEIGHT_DEFAULT                            \
}

#define PSEUDO_YUV_FMT(fmt, a, r, g, b, e0, e1, chroma, flg, fm, np)      \
{                                                                         \
	.base.pixel_format = DRM_FORMAT_ ## fmt,                          \
	.fetch_planes = DPU_PLANE_PSEUDO_PLANAR,                          \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bits = { g, b, r, a },                                           \
	.chroma_sample = chroma,                                          \
	.unpack_align_msb = 0,                                            \
	.unpack_tight = 1,                                                \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = fm,                                                 \
	.flag = {(flg)},                                                  \
	.num_planes = np,                                                 \
	.tile_height = DPU_TILE_HEIGHT_DEFAULT                            \
}

#define PSEUDO_YUV_FMT_TILED(fmt, a, r, g, b, e0, e1, chroma,             \
flg, fm, np, th)                                                          \
{                                                                         \
	.base.pixel_format = DRM_FORMAT_ ## fmt,                          \
	.fetch_planes = DPU_PLANE_PSEUDO_PLANAR,                          \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bits = { g, b, r, a },                                           \
	.chroma_sample = chroma,                                          \
	.unpack_align_msb = 0,                                            \
	.unpack_tight = 1,                                                \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = fm,                                                 \
	.flag = {(flg)},                                                  \
	.num_planes = np,                                                 \
	.tile_height = th                                                 \
}

#define PSEUDO_YUV_FMT_LOOSE(fmt, a, r, g, b, e0, e1, chroma, flg, fm, np)\
{                                                                         \
	.base.pixel_format = DRM_FORMAT_ ## fmt,                          \
	.fetch_planes = DPU_PLANE_PSEUDO_PLANAR,                          \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bits = { g, b, r, a },                                           \
	.chroma_sample = chroma,                                          \
	.unpack_align_msb = 1,                                            \
	.unpack_tight = 0,                                                \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = fm,                                                 \
	.flag = {(flg)},                                                  \
	.num_planes = np,                                                 \
	.tile_height = DPU_TILE_HEIGHT_DEFAULT                            \
}

#define PSEUDO_YUV_FMT_LOOSE_TILED(fmt, a, r, g, b, e0, e1, chroma,       \
flg, fm, np, th)                                                          \
{                                                                         \
	.base.pixel_format = DRM_FORMAT_ ## fmt,                          \
	.fetch_planes = DPU_PLANE_PSEUDO_PLANAR,                          \
	.alpha_enable = false,                                            \
	.element = { (e0), (e1), 0, 0 },                                  \
	.bits = { g, b, r, a },                                           \
	.chroma_sample = chroma,                                          \
	.unpack_align_msb = 1,                                            \
	.unpack_tight = 0,                                                \
	.unpack_count = 2,                                                \
	.bpp = 2,                                                         \
	.fetch_mode = fm,                                                 \
	.flag = {(flg)},                                                  \
	.num_planes = np,                                                 \
	.tile_height = th                                                 \
}


#define PLANAR_YUV_FMT(fmt, a, r, g, b, e0, e1, e2, alpha, chroma, bp,    \
flg, fm, np)                                                      \
{                                                                         \
	.base.pixel_format = DRM_FORMAT_ ## fmt,                          \
	.fetch_planes = DPU_PLANE_PLANAR,                                 \
	.alpha_enable = alpha,                                            \
	.element = { (e0), (e1), (e2), 0 },                               \
	.bits = { g, b, r, a },                                           \
	.chroma_sample = chroma,                                          \
	.unpack_align_msb = 0,                                            \
	.unpack_tight = 1,                                                \
	.unpack_count = 1,                                                \
	.bpp = bp,                                                        \
	.fetch_mode = fm,                                                 \
	.flag = {(flg)},                                                  \
	.num_planes = np,                                                 \
	.tile_height = DPU_TILE_HEIGHT_DEFAULT                            \
}

/*
 * struct dpu_media_color_map - maps drm format to media format
 * @format: DRM base pixel format
 * @color: Media API color related to DRM format
 */
struct dpu_media_color_map {
	uint32_t format;
	uint32_t color;
};

static const struct dpu_format dpu_format_map[] = {
	INTERLEAVED_RGB_FMT(ARGB8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 4, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ABGR8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XBGR8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBA8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 4, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRA8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 4, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRX8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 4, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XRGB8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 4, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBX8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		false, 4, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGB888,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, 0, 3,
		false, 3, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGR888,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, 0, 3,
		false, 3, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGB565,
		0, COLOR_5BIT, COLOR_6BIT, COLOR_5BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, 0, 3,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGR565,
		0, COLOR_5BIT, COLOR_6BIT, COLOR_5BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, 0, 3,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ARGB1555,
		COLOR_ALPHA_1BIT, COLOR_5BIT, COLOR_5BIT, COLOR_5BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ABGR1555,
		COLOR_ALPHA_1BIT, COLOR_5BIT, COLOR_5BIT, COLOR_5BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBA5551,
		COLOR_ALPHA_1BIT, COLOR_5BIT, COLOR_5BIT, COLOR_5BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRA5551,
		COLOR_ALPHA_1BIT, COLOR_5BIT, COLOR_5BIT, COLOR_5BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XRGB1555,
		COLOR_ALPHA_1BIT, COLOR_5BIT, COLOR_5BIT, COLOR_5BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XBGR1555,
		COLOR_ALPHA_1BIT, COLOR_5BIT, COLOR_5BIT, COLOR_5BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBX5551,
		COLOR_ALPHA_1BIT, COLOR_5BIT, COLOR_5BIT, COLOR_5BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRX5551,
		COLOR_ALPHA_1BIT, COLOR_5BIT, COLOR_5BIT, COLOR_5BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ARGB4444,
		COLOR_ALPHA_4BIT, COLOR_4BIT, COLOR_4BIT, COLOR_4BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ABGR4444,
		COLOR_ALPHA_4BIT, COLOR_4BIT, COLOR_4BIT, COLOR_4BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBA4444,
		COLOR_ALPHA_4BIT, COLOR_4BIT, COLOR_4BIT, COLOR_4BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRA4444,
		COLOR_ALPHA_4BIT, COLOR_4BIT, COLOR_4BIT, COLOR_4BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XRGB4444,
		COLOR_ALPHA_4BIT, COLOR_4BIT, COLOR_4BIT, COLOR_4BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XBGR4444,
		COLOR_ALPHA_4BIT, COLOR_4BIT, COLOR_4BIT, COLOR_4BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBX4444,
		COLOR_ALPHA_4BIT, COLOR_4BIT, COLOR_4BIT, COLOR_4BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRX4444,
		COLOR_ALPHA_4BIT, COLOR_4BIT, COLOR_4BIT, COLOR_4BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 2, 0,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRA1010102,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBA1010102,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ABGR2101010,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(ARGB2101010,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XRGB2101010,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(BGRX1010102,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(XBGR2101010,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_LINEAR, 1),

	INTERLEAVED_RGB_FMT(RGBX1010102,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		false, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_LINEAR, 1),

	PSEUDO_YUV_FMT(NV12,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C2_R_Cr,
		DPU_CHROMA_420, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 2),

	PSEUDO_YUV_FMT(NV21,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C1_B_Cb,
		DPU_CHROMA_420, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 2),

	PSEUDO_YUV_FMT(NV16,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C2_R_Cr,
		DPU_CHROMA_H2V1, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 2),

	PSEUDO_YUV_FMT(NV61,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C1_B_Cb,
		DPU_CHROMA_H2V1, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 2),

	INTERLEAVED_YUV_FMT(VYUY,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C0_G_Y,
		false, DPU_CHROMA_H2V1, 4, 2, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 2),

	INTERLEAVED_YUV_FMT(UYVY,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C0_G_Y,
		false, DPU_CHROMA_H2V1, 4, 2, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 2),

	INTERLEAVED_YUV_FMT(YUYV,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C0_G_Y, C1_B_Cb, C0_G_Y, C2_R_Cr,
		false, DPU_CHROMA_H2V1, 4, 2, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 2),

	INTERLEAVED_YUV_FMT(YVYU,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C0_G_Y, C2_R_Cr, C0_G_Y, C1_B_Cb,
		false, DPU_CHROMA_H2V1, 4, 2, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 2),

	PLANAR_YUV_FMT(YUV420,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C1_B_Cb, C0_G_Y,
		false, DPU_CHROMA_420, 1, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 3),

	PLANAR_YUV_FMT(YVU420,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C2_R_Cr, C0_G_Y,
		false, DPU_CHROMA_420, 1, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_LINEAR, 3),
};

/*
 * A5x tile formats tables:
 * These tables hold the A5x tile formats supported.
 */
static const struct dpu_format dpu_format_map_tile[] = {
	INTERLEAVED_RGB_FMT_TILED(BGR565,
		0, COLOR_5BIT, COLOR_6BIT, COLOR_5BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, 0, 3,
		false, 2, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(ARGB8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		true, 4, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(ABGR8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C1_B_Cb, C0_G_Y, C2_R_Cr, 4,
		true, 4, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(XBGR8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 4, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(RGBA8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(BGRA8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		true, 4, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(BGRX8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C0_G_Y, C2_R_Cr, C3_ALPHA, 4,
		false, 4, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(XRGB8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C3_ALPHA, C2_R_Cr, C0_G_Y, C1_B_Cb, 4,
		false, 4, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(RGBX8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 4, 0,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(ABGR2101010,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	INTERLEAVED_RGB_FMT_TILED(XBGR2101010,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, DPU_FORMAT_FLAG_DX,
		DPU_FETCH_UBWC, 1, DPU_TILE_HEIGHT_TILED),

	PSEUDO_YUV_FMT_TILED(NV12,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C2_R_Cr,
		DPU_CHROMA_420, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_UBWC, 2, DPU_TILE_HEIGHT_NV12),

	PSEUDO_YUV_FMT_TILED(NV21,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C1_B_Cb,
		DPU_CHROMA_420, DPU_FORMAT_FLAG_YUV,
		DPU_FETCH_UBWC, 2, DPU_TILE_HEIGHT_NV12),
};

/*
 * UBWC formats table:
 * This table holds the UBWC formats supported.
 * If a compression ratio needs to be used for this or any other format,
 * the data will be passed by user-space.
 */
static const struct dpu_format dpu_format_map_ubwc[] = {
	INTERLEAVED_RGB_FMT_TILED(BGR565,
		0, COLOR_5BIT, COLOR_6BIT, COLOR_5BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, 0, 3,
		false, 2, DPU_FORMAT_FLAG_COMPRESSED,
		DPU_FETCH_UBWC, 2, DPU_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(ABGR8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, DPU_FORMAT_FLAG_COMPRESSED,
		DPU_FETCH_UBWC, 2, DPU_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(XBGR8888,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		false, 4, DPU_FORMAT_FLAG_COMPRESSED,
		DPU_FETCH_UBWC, 2, DPU_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(ABGR2101010,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, DPU_FORMAT_FLAG_DX | DPU_FORMAT_FLAG_COMPRESSED,
		DPU_FETCH_UBWC, 2, DPU_TILE_HEIGHT_UBWC),

	INTERLEAVED_RGB_FMT_TILED(XBGR2101010,
		COLOR_8BIT, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C2_R_Cr, C0_G_Y, C1_B_Cb, C3_ALPHA, 4,
		true, 4, DPU_FORMAT_FLAG_DX | DPU_FORMAT_FLAG_COMPRESSED,
		DPU_FETCH_UBWC, 2, DPU_TILE_HEIGHT_UBWC),

	PSEUDO_YUV_FMT_TILED(NV12,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C2_R_Cr,
		DPU_CHROMA_420, DPU_FORMAT_FLAG_YUV |
				DPU_FORMAT_FLAG_COMPRESSED,
		DPU_FETCH_UBWC, 4, DPU_TILE_HEIGHT_NV12),
};

static const struct dpu_format dpu_format_map_p010[] = {
	PSEUDO_YUV_FMT_LOOSE(NV12,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C2_R_Cr,
		DPU_CHROMA_420, (DPU_FORMAT_FLAG_YUV | DPU_FORMAT_FLAG_DX),
		DPU_FETCH_LINEAR, 2),
};

static const struct dpu_format dpu_format_map_p010_ubwc[] = {
	PSEUDO_YUV_FMT_LOOSE_TILED(NV12,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C2_R_Cr,
		DPU_CHROMA_420, (DPU_FORMAT_FLAG_YUV | DPU_FORMAT_FLAG_DX |
				DPU_FORMAT_FLAG_COMPRESSED),
		DPU_FETCH_UBWC, 4, DPU_TILE_HEIGHT_NV12),
};

static const struct dpu_format dpu_format_map_tp10_ubwc[] = {
	PSEUDO_YUV_FMT_TILED(NV12,
		0, COLOR_8BIT, COLOR_8BIT, COLOR_8BIT,
		C1_B_Cb, C2_R_Cr,
		DPU_CHROMA_420, (DPU_FORMAT_FLAG_YUV | DPU_FORMAT_FLAG_DX |
				DPU_FORMAT_FLAG_COMPRESSED),
		DPU_FETCH_UBWC, 4, DPU_TILE_HEIGHT_NV12),
};

/* _dpu_get_v_h_subsample_rate - Get subsample rates for all formats we support
 *   Note: Not using the drm_format_*_subsampling since we have formats
 */
static void _dpu_get_v_h_subsample_rate(
	enum dpu_chroma_samp_type chroma_sample,
	uint32_t *v_sample,
	uint32_t *h_sample)
{
	if (!v_sample || !h_sample)
		return;

	switch (chroma_sample) {
	case DPU_CHROMA_H2V1:
		*v_sample = 1;
		*h_sample = 2;
		break;
	case DPU_CHROMA_H1V2:
		*v_sample = 2;
		*h_sample = 1;
		break;
	case DPU_CHROMA_420:
		*v_sample = 2;
		*h_sample = 2;
		break;
	default:
		*v_sample = 1;
		*h_sample = 1;
		break;
	}
}

static int _dpu_format_get_media_color_ubwc(const struct dpu_format *fmt)
{
	static const struct dpu_media_color_map dpu_media_ubwc_map[] = {
		{DRM_FORMAT_ABGR8888, COLOR_FMT_RGBA8888_UBWC},
		{DRM_FORMAT_XBGR8888, COLOR_FMT_RGBA8888_UBWC},
		{DRM_FORMAT_ABGR2101010, COLOR_FMT_RGBA1010102_UBWC},
		{DRM_FORMAT_XBGR2101010, COLOR_FMT_RGBA1010102_UBWC},
		{DRM_FORMAT_BGR565, COLOR_FMT_RGB565_UBWC},
	};
	int color_fmt = -1;
	int i;

	if (fmt->base.pixel_format == DRM_FORMAT_NV12) {
		if (DPU_FORMAT_IS_DX(fmt)) {
			if (fmt->unpack_tight)
				color_fmt = COLOR_FMT_NV12_BPP10_UBWC;
			else
				color_fmt = COLOR_FMT_P010_UBWC;
		} else
			color_fmt = COLOR_FMT_NV12_UBWC;
		return color_fmt;
	}

	for (i = 0; i < ARRAY_SIZE(dpu_media_ubwc_map); ++i)
		if (fmt->base.pixel_format == dpu_media_ubwc_map[i].format) {
			color_fmt = dpu_media_ubwc_map[i].color;
			break;
		}
	return color_fmt;
}

static int _dpu_format_get_plane_sizes_ubwc(
		const struct dpu_format *fmt,
		const uint32_t width,
		const uint32_t height,
		struct dpu_hw_fmt_layout *layout)
{
	int i;
	int color;
	bool meta = DPU_FORMAT_IS_UBWC(fmt);

	memset(layout, 0, sizeof(struct dpu_hw_fmt_layout));
	layout->format = fmt;
	layout->width = width;
	layout->height = height;
	layout->num_planes = fmt->num_planes;

	color = _dpu_format_get_media_color_ubwc(fmt);
	if (color < 0) {
		DRM_ERROR("UBWC format not supported for fmt: %4.4s\n",
			(char *)&fmt->base.pixel_format);
		return -EINVAL;
	}

	if (DPU_FORMAT_IS_YUV(layout->format)) {
		uint32_t y_sclines, uv_sclines;
		uint32_t y_meta_scanlines = 0;
		uint32_t uv_meta_scanlines = 0;

		layout->num_planes = 2;
		layout->plane_pitch[0] = VENUS_Y_STRIDE(color, width);
		y_sclines = VENUS_Y_SCANLINES(color, height);
		layout->plane_size[0] = MSM_MEDIA_ALIGN(layout->plane_pitch[0] *
			y_sclines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		layout->plane_pitch[1] = VENUS_UV_STRIDE(color, width);
		uv_sclines = VENUS_UV_SCANLINES(color, height);
		layout->plane_size[1] = MSM_MEDIA_ALIGN(layout->plane_pitch[1] *
			uv_sclines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		if (!meta)
			goto done;

		layout->num_planes += 2;
		layout->plane_pitch[2] = VENUS_Y_META_STRIDE(color, width);
		y_meta_scanlines = VENUS_Y_META_SCANLINES(color, height);
		layout->plane_size[2] = MSM_MEDIA_ALIGN(layout->plane_pitch[2] *
			y_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		layout->plane_pitch[3] = VENUS_UV_META_STRIDE(color, width);
		uv_meta_scanlines = VENUS_UV_META_SCANLINES(color, height);
		layout->plane_size[3] = MSM_MEDIA_ALIGN(layout->plane_pitch[3] *
			uv_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

	} else {
		uint32_t rgb_scanlines, rgb_meta_scanlines;

		layout->num_planes = 1;

		layout->plane_pitch[0] = VENUS_RGB_STRIDE(color, width);
		rgb_scanlines = VENUS_RGB_SCANLINES(color, height);
		layout->plane_size[0] = MSM_MEDIA_ALIGN(layout->plane_pitch[0] *
			rgb_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);

		if (!meta)
			goto done;
		layout->num_planes += 2;
		layout->plane_pitch[2] = VENUS_RGB_META_STRIDE(color, width);
		rgb_meta_scanlines = VENUS_RGB_META_SCANLINES(color, height);
		layout->plane_size[2] = MSM_MEDIA_ALIGN(layout->plane_pitch[2] *
			rgb_meta_scanlines, DPU_UBWC_PLANE_SIZE_ALIGNMENT);
	}

done:
	for (i = 0; i < DPU_MAX_PLANES; i++)
		layout->total_size += layout->plane_size[i];

	return 0;
}

static int _dpu_format_get_plane_sizes_linear(
		const struct dpu_format *fmt,
		const uint32_t width,
		const uint32_t height,
		struct dpu_hw_fmt_layout *layout,
		const uint32_t *pitches)
{
	int i;

	memset(layout, 0, sizeof(struct dpu_hw_fmt_layout));
	layout->format = fmt;
	layout->width = width;
	layout->height = height;
	layout->num_planes = fmt->num_planes;

	/* Due to memset above, only need to set planes of interest */
	if (fmt->fetch_planes == DPU_PLANE_INTERLEAVED) {
		layout->num_planes = 1;
		layout->plane_size[0] = width * height * layout->format->bpp;
		layout->plane_pitch[0] = width * layout->format->bpp;
	} else {
		uint32_t v_subsample, h_subsample;
		uint32_t chroma_samp;
		uint32_t bpp = 1;

		chroma_samp = fmt->chroma_sample;
		_dpu_get_v_h_subsample_rate(chroma_samp, &v_subsample,
				&h_subsample);

		if (width % h_subsample || height % v_subsample) {
			DRM_ERROR("mismatch in subsample vs dimensions\n");
			return -EINVAL;
		}

		if ((fmt->base.pixel_format == DRM_FORMAT_NV12) &&
			(DPU_FORMAT_IS_DX(fmt)))
			bpp = 2;
		layout->plane_pitch[0] = width * bpp;
		layout->plane_pitch[1] = layout->plane_pitch[0] / h_subsample;
		layout->plane_size[0] = layout->plane_pitch[0] * height;
		layout->plane_size[1] = layout->plane_pitch[1] *
				(height / v_subsample);

		if (fmt->fetch_planes == DPU_PLANE_PSEUDO_PLANAR) {
			layout->num_planes = 2;
			layout->plane_size[1] *= 2;
			layout->plane_pitch[1] *= 2;
		} else {
			/* planar */
			layout->num_planes = 3;
			layout->plane_size[2] = layout->plane_size[1];
			layout->plane_pitch[2] = layout->plane_pitch[1];
		}
	}

	/*
	 * linear format: allow user allocated pitches if they are greater than
	 * the requirement.
	 * ubwc format: pitch values are computed uniformly across
	 * all the components based on ubwc specifications.
	 */
	for (i = 0; i < layout->num_planes && i < DPU_MAX_PLANES; ++i) {
		if (pitches && layout->plane_pitch[i] < pitches[i])
			layout->plane_pitch[i] = pitches[i];
	}

	for (i = 0; i < DPU_MAX_PLANES; i++)
		layout->total_size += layout->plane_size[i];

	return 0;
}

static int dpu_format_get_plane_sizes(
		const struct dpu_format *fmt,
		const uint32_t w,
		const uint32_t h,
		struct dpu_hw_fmt_layout *layout,
		const uint32_t *pitches)
{
	if (!layout || !fmt) {
		DRM_ERROR("invalid pointer\n");
		return -EINVAL;
	}

	if ((w > DPU_MAX_IMG_WIDTH) || (h > DPU_MAX_IMG_HEIGHT)) {
		DRM_ERROR("image dimensions outside max range\n");
		return -ERANGE;
	}

	if (DPU_FORMAT_IS_UBWC(fmt) || DPU_FORMAT_IS_TILE(fmt))
		return _dpu_format_get_plane_sizes_ubwc(fmt, w, h, layout);

	return _dpu_format_get_plane_sizes_linear(fmt, w, h, layout, pitches);
}

static int _dpu_format_populate_addrs_ubwc(
		struct msm_gem_address_space *aspace,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	uint32_t base_addr = 0;
	bool meta;

	if (!fb || !layout) {
		DRM_ERROR("invalid pointers\n");
		return -EINVAL;
	}

	if (aspace)
		base_addr = msm_framebuffer_iova(fb, aspace, 0);
	if (!base_addr) {
		DRM_ERROR("failed to retrieve base addr\n");
		return -EFAULT;
	}

	meta = DPU_FORMAT_IS_UBWC(layout->format);

	/* Per-format logic for verifying active planes */
	if (DPU_FORMAT_IS_YUV(layout->format)) {
		/************************************************/
		/*      UBWC            **                      */
		/*      buffer          **      DPU PLANE       */
		/*      format          **                      */
		/************************************************/
		/* -------------------  ** -------------------- */
		/* |      Y meta     |  ** |    Y bitstream   | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/* |    Y bitstream  |  ** |  CbCr bitstream  | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/* |   Cbcr metadata |  ** |       Y meta     | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/* |  CbCr bitstream |  ** |     CbCr meta    | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/************************************************/

		/* configure Y bitstream plane */
		layout->plane_addr[0] = base_addr + layout->plane_size[2];

		/* configure CbCr bitstream plane */
		layout->plane_addr[1] = base_addr + layout->plane_size[0]
			+ layout->plane_size[2] + layout->plane_size[3];

		if (!meta)
			goto done;

		/* configure Y metadata plane */
		layout->plane_addr[2] = base_addr;

		/* configure CbCr metadata plane */
		layout->plane_addr[3] = base_addr + layout->plane_size[0]
			+ layout->plane_size[2];

	} else {
		/************************************************/
		/*      UBWC            **                      */
		/*      buffer          **      DPU PLANE       */
		/*      format          **                      */
		/************************************************/
		/* -------------------  ** -------------------- */
		/* |      RGB meta   |  ** |   RGB bitstream  | */
		/* |       data      |  ** |       plane      | */
		/* -------------------  ** -------------------- */
		/* |  RGB bitstream  |  ** |       NONE       | */
		/* |       data      |  ** |                  | */
		/* -------------------  ** -------------------- */
		/*                      ** |     RGB meta     | */
		/*                      ** |       plane      | */
		/*                      ** -------------------- */
		/************************************************/

		layout->plane_addr[0] = base_addr + layout->plane_size[2];
		layout->plane_addr[1] = 0;

		if (!meta)
			goto done;

		layout->plane_addr[2] = base_addr;
		layout->plane_addr[3] = 0;
	}
done:
	return 0;
}

static int _dpu_format_populate_addrs_linear(
		struct msm_gem_address_space *aspace,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	unsigned int i;

	/* Can now check the pitches given vs pitches expected */
	for (i = 0; i < layout->num_planes; ++i) {
		if (layout->plane_pitch[i] > fb->pitches[i]) {
			DRM_ERROR("plane %u expected pitch %u, fb %u\n",
				i, layout->plane_pitch[i], fb->pitches[i]);
			return -EINVAL;
		}
	}

	/* Populate addresses for simple formats here */
	for (i = 0; i < layout->num_planes; ++i) {
		if (aspace)
			layout->plane_addr[i] =
				msm_framebuffer_iova(fb, aspace, i);
		if (!layout->plane_addr[i]) {
			DRM_ERROR("failed to retrieve base addr\n");
			return -EFAULT;
		}
	}

	return 0;
}

int dpu_format_populate_layout(
		struct msm_gem_address_space *aspace,
		struct drm_framebuffer *fb,
		struct dpu_hw_fmt_layout *layout)
{
	uint32_t plane_addr[DPU_MAX_PLANES];
	int i, ret;

	if (!fb || !layout) {
		DRM_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	if ((fb->width > DPU_MAX_IMG_WIDTH) ||
			(fb->height > DPU_MAX_IMG_HEIGHT)) {
		DRM_ERROR("image dimensions outside max range\n");
		return -ERANGE;
	}

	layout->format = to_dpu_format(msm_framebuffer_format(fb));

	/* Populate the plane sizes etc via get_format */
	ret = dpu_format_get_plane_sizes(layout->format, fb->width, fb->height,
			layout, fb->pitches);
	if (ret)
		return ret;

	for (i = 0; i < DPU_MAX_PLANES; ++i)
		plane_addr[i] = layout->plane_addr[i];

	/* Populate the addresses given the fb */
	if (DPU_FORMAT_IS_UBWC(layout->format) ||
			DPU_FORMAT_IS_TILE(layout->format))
		ret = _dpu_format_populate_addrs_ubwc(aspace, fb, layout);
	else
		ret = _dpu_format_populate_addrs_linear(aspace, fb, layout);

	/* check if anything changed */
	if (!ret && !memcmp(plane_addr, layout->plane_addr, sizeof(plane_addr)))
		ret = -EAGAIN;

	return ret;
}

int dpu_format_check_modified_format(
		const struct msm_kms *kms,
		const struct msm_format *msm_fmt,
		const struct drm_mode_fb_cmd2 *cmd,
		struct drm_gem_object **bos)
{
	int ret, i, num_base_fmt_planes;
	const struct dpu_format *fmt;
	struct dpu_hw_fmt_layout layout;
	uint32_t bos_total_size = 0;

	if (!msm_fmt || !cmd || !bos) {
		DRM_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	fmt = to_dpu_format(msm_fmt);
	num_base_fmt_planes = drm_format_num_planes(fmt->base.pixel_format);

	ret = dpu_format_get_plane_sizes(fmt, cmd->width, cmd->height,
			&layout, cmd->pitches);
	if (ret)
		return ret;

	for (i = 0; i < num_base_fmt_planes; i++) {
		if (!bos[i]) {
			DRM_ERROR("invalid handle for plane %d\n", i);
			return -EINVAL;
		}
		if ((i == 0) || (bos[i] != bos[0]))
			bos_total_size += bos[i]->size;
	}

	if (bos_total_size < layout.total_size) {
		DRM_ERROR("buffers total size too small %u expected %u\n",
				bos_total_size, layout.total_size);
		return -EINVAL;
	}

	return 0;
}

const struct dpu_format *dpu_get_dpu_format_ext(
		const uint32_t format,
		const uint64_t modifier)
{
	uint32_t i = 0;
	const struct dpu_format *fmt = NULL;
	const struct dpu_format *map = NULL;
	ssize_t map_size = 0;

	/*
	 * Currently only support exactly zero or one modifier.
	 * All planes use the same modifier.
	 */
	DPU_DEBUG("plane format modifier 0x%llX\n", modifier);

	switch (modifier) {
	case 0:
		map = dpu_format_map;
		map_size = ARRAY_SIZE(dpu_format_map);
		break;
	case DRM_FORMAT_MOD_QCOM_COMPRESSED:
		map = dpu_format_map_ubwc;
		map_size = ARRAY_SIZE(dpu_format_map_ubwc);
		DPU_DEBUG("found fmt: %4.4s  DRM_FORMAT_MOD_QCOM_COMPRESSED\n",
				(char *)&format);
		break;
	default:
		DPU_ERROR("unsupported format modifier %llX\n", modifier);
		return NULL;
	}

	for (i = 0; i < map_size; i++) {
		if (format == map[i].base.pixel_format) {
			fmt = &map[i];
			break;
		}
	}

	if (fmt == NULL)
		DPU_ERROR("unsupported fmt: %4.4s modifier 0x%llX\n",
			(char *)&format, modifier);
	else
		DPU_DEBUG("fmt %4.4s mod 0x%llX ubwc %d yuv %d\n",
				(char *)&format, modifier,
				DPU_FORMAT_IS_UBWC(fmt),
				DPU_FORMAT_IS_YUV(fmt));

	return fmt;
}

const struct msm_format *dpu_get_msm_format(
		struct msm_kms *kms,
		const uint32_t format,
		const uint64_t modifiers)
{
	const struct dpu_format *fmt = dpu_get_dpu_format_ext(format,
			modifiers);
	if (fmt)
		return &fmt->base;
	return NULL;
}

uint32_t dpu_populate_formats(
		const struct dpu_format_extended *format_list,
		uint32_t *pixel_formats,
		uint64_t *pixel_modifiers,
		uint32_t pixel_formats_max)
{
	uint32_t i, fourcc_format;

	if (!format_list || !pixel_formats)
		return 0;

	for (i = 0, fourcc_format = 0;
			format_list->fourcc_format && i < pixel_formats_max;
			++format_list) {
		/* verify if listed format is in dpu_format_map? */

		/* optionally return modified formats */
		if (pixel_modifiers) {
			/* assume same modifier for all fb planes */
			pixel_formats[i] = format_list->fourcc_format;
			pixel_modifiers[i++] = format_list->modifier;
		} else {
			/* assume base formats grouped together */
			if (fourcc_format != format_list->fourcc_format) {
				fourcc_format = format_list->fourcc_format;
				pixel_formats[i++] = fourcc_format;
			}
		}
	}

	return i;
}
