// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */


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

#define FMT(name, a, r, g, b, e0, e1, e2, e3, alpha, tight, c, cnt, fp, cs, yuv) { \
		.base = { .pixel_format = DRM_FORMAT_ ## name }, \
		.bpc_a = BPC ## a ## A,                          \
		.bpc_r = BPC ## r,                               \
		.bpc_g = BPC ## g,                               \
		.bpc_b = BPC ## b,                               \
		.unpack = { e0, e1, e2, e3 },                    \
		.alpha_enable = alpha,                           \
		.unpack_tight = tight,                           \
		.cpp = c,                                        \
		.unpack_count = cnt,                             \
		.fetch_type = fp,                                \
		.chroma_sample = cs,                             \
		.is_yuv = yuv,                                   \
}

#define BPC0A 0

/*
 * Note: Keep RGB formats 1st, followed by YUV formats to avoid breaking
 * mdp_get_rgb_formats()'s implementation.
 */
static const struct mdp_format formats[] = {
	/*  name      a  r  g  b   e0 e1 e2 e3  alpha   tight  cpp cnt ... */
	FMT(ARGB8888, 8, 8, 8, 8,  1, 0, 2, 3,  true,   true,  4,  4,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(ABGR8888, 8, 8, 8, 8,  2, 0, 1, 3,  true,   true,  4,  4,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(RGBA8888, 8, 8, 8, 8,  3, 1, 0, 2,  true,   true,  4,  4,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(BGRA8888, 8, 8, 8, 8,  3, 2, 0, 1,  true,   true,  4,  4,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(XRGB8888, 8, 8, 8, 8,  1, 0, 2, 3,  false,  true,  4,  4,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(XBGR8888, 8, 8, 8, 8,  2, 0, 1, 3,  false,   true,  4,  4,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(RGBX8888, 8, 8, 8, 8,  3, 1, 0, 2,  false,   true,  4,  4,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(BGRX8888, 8, 8, 8, 8,  3, 2, 0, 1,  false,   true,  4,  4,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(RGB888,   0, 8, 8, 8,  1, 0, 2, 0,  false,  true,  3,  3,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(BGR888,   0, 8, 8, 8,  2, 0, 1, 0,  false,  true,  3,  3,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(RGB565,   0, 5, 6, 5,  1, 0, 2, 0,  false,  true,  2,  3,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),
	FMT(BGR565,   0, 5, 6, 5,  2, 0, 1, 0,  false,  true,  2,  3,
			MDP_PLANE_INTERLEAVED, CHROMA_FULL, false),

	/* --- RGB formats above / YUV formats below this line --- */

	/* 2 plane YUV */
	FMT(NV12,     0, 8, 8, 8,  1, 2, 0, 0,  false,  true,  2, 2,
			MDP_PLANE_PSEUDO_PLANAR, CHROMA_420, true),
	FMT(NV21,     0, 8, 8, 8,  2, 1, 0, 0,  false,  true,  2, 2,
			MDP_PLANE_PSEUDO_PLANAR, CHROMA_420, true),
	FMT(NV16,     0, 8, 8, 8,  1, 2, 0, 0,  false,  true,  2, 2,
			MDP_PLANE_PSEUDO_PLANAR, CHROMA_H2V1, true),
	FMT(NV61,     0, 8, 8, 8,  2, 1, 0, 0,  false,  true,  2, 2,
			MDP_PLANE_PSEUDO_PLANAR, CHROMA_H2V1, true),
	/* 1 plane YUV */
	FMT(VYUY,     0, 8, 8, 8,  2, 0, 1, 0,  false,  true,  2, 4,
			MDP_PLANE_INTERLEAVED, CHROMA_H2V1, true),
	FMT(UYVY,     0, 8, 8, 8,  1, 0, 2, 0,  false,  true,  2, 4,
			MDP_PLANE_INTERLEAVED, CHROMA_H2V1, true),
	FMT(YUYV,     0, 8, 8, 8,  0, 1, 0, 2,  false,  true,  2, 4,
			MDP_PLANE_INTERLEAVED, CHROMA_H2V1, true),
	FMT(YVYU,     0, 8, 8, 8,  0, 2, 0, 1,  false,  true,  2, 4,
			MDP_PLANE_INTERLEAVED, CHROMA_H2V1, true),
	/* 3 plane YUV */
	FMT(YUV420,   0, 8, 8, 8,  2, 1, 0, 0,  false,  true,  1, 1,
			MDP_PLANE_PLANAR, CHROMA_420, true),
	FMT(YVU420,   0, 8, 8, 8,  1, 2, 0, 0,  false,  true,  1, 1,
			MDP_PLANE_PLANAR, CHROMA_420, true),
};

/*
 * Note:
 * @rgb_only must be set to true, when requesting
 * supported formats for RGB pipes.
 */
uint32_t mdp_get_formats(uint32_t *pixel_formats, uint32_t max_formats,
		bool rgb_only)
{
	uint32_t i;
	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		const struct mdp_format *f = &formats[i];

		if (i == max_formats)
			break;

		if (rgb_only && MDP_FORMAT_IS_YUV(f))
			break;

		pixel_formats[i] = f->base.pixel_format;
	}

	return i;
}

const struct msm_format *mdp_get_format(struct msm_kms *kms, uint32_t format,
		uint64_t modifier)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		const struct mdp_format *f = &formats[i];
		if (f->base.pixel_format == format)
			return &f->base;
	}
	return NULL;
}

struct csc_cfg *mdp_get_default_csc_cfg(enum csc_type type)
{
	if (WARN_ON(type >= CSC_MAX))
		return NULL;

	return &csc_convert[type];
}
