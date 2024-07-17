/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_FORMAT_H__
#define __MSM_FORMAT_H__

#include "mdp_common.xml.h"

enum msm_format_flags {
	MSM_FORMAT_FLAG_YUV_BIT,
	MSM_FORMAT_FLAG_DX_BIT,
	MSM_FORMAT_FLAG_COMPRESSED_BIT,
	MSM_FORMAT_FLAG_UNPACK_TIGHT_BIT,
	MSM_FORMAT_FLAG_UNPACK_ALIGN_MSB_BIT,
};

#define MSM_FORMAT_FLAG_YUV		BIT(MSM_FORMAT_FLAG_YUV_BIT)
#define MSM_FORMAT_FLAG_DX		BIT(MSM_FORMAT_FLAG_DX_BIT)
#define MSM_FORMAT_FLAG_COMPRESSED	BIT(MSM_FORMAT_FLAG_COMPRESSED_BIT)
#define MSM_FORMAT_FLAG_UNPACK_TIGHT	BIT(MSM_FORMAT_FLAG_UNPACK_TIGHT_BIT)
#define MSM_FORMAT_FLAG_UNPACK_ALIGN_MSB BIT(MSM_FORMAT_FLAG_UNPACK_ALIGN_MSB_BIT)

/**
 * DPU HW,Component order color map
 */
enum {
	C0_G_Y = 0,
	C1_B_Cb = 1,
	C2_R_Cr = 2,
	C3_ALPHA = 3
};

/**
 * struct msm_format: defines the format configuration
 * @pixel_format: format fourcc
 * @element: element color ordering
 * @fetch_type: how the color components are packed in pixel format
 * @chroma_sample: chroma sub-samplng type
 * @alpha_enable: whether the format has an alpha channel
 * @unpack_count: number of the components to unpack
 * @bpp: bytes per pixel
 * @flags: usage bit flags
 * @num_planes: number of planes (including meta data planes)
 * @fetch_mode: linear, tiled, or ubwc hw fetch behavior
 * @tile_height: format tile height
 */
struct msm_format {
	uint32_t pixel_format;
	enum mdp_bpc bpc_g_y, bpc_b_cb, bpc_r_cr;
	enum mdp_bpc_alpha bpc_a;
	u8 element[4];
	enum mdp_fetch_type fetch_type;
	enum mdp_chroma_samp_type chroma_sample;
	bool alpha_enable;
	u8 unpack_count;
	u8 bpp;
	unsigned long flags;
	u8 num_planes;
	enum mdp_fetch_mode fetch_mode;
	u16 tile_height;
};

#define MSM_FORMAT_IS_YUV(X)		((X)->flags & MSM_FORMAT_FLAG_YUV)
#define MSM_FORMAT_IS_DX(X)		((X)->flags & MSM_FORMAT_FLAG_DX)
#define MSM_FORMAT_IS_LINEAR(X)		((X)->fetch_mode == MDP_FETCH_LINEAR)
#define MSM_FORMAT_IS_TILE(X) \
	(((X)->fetch_mode == MDP_FETCH_UBWC) && \
	 !((X)->flags & MSM_FORMAT_FLAG_COMPRESSED))
#define MSM_FORMAT_IS_UBWC(X) \
	(((X)->fetch_mode == MDP_FETCH_UBWC) && \
	 ((X)->flags & MSM_FORMAT_FLAG_COMPRESSED))

#endif
