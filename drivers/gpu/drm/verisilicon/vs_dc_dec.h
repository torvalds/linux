/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_DC_DEC_H_
#define _VS_DC_DEC_H_

#include <drm/drm_fourcc.h>

#include "vs_dc_hw.h"

#define fourcc_mod_vs_get_type(val) \
			(((val) & DRM_FORMAT_MOD_VS_TYPE_MASK) >> 54)

/* DEC_READ_CONFIG */
#define COMPRESSION_EN				BIT(0)
#define COMPRESSION_FORMAT_MASK		GENMASK(7, 3)
#define COMPRESSION_ALIGN_MODE_MASK GENMASK(17, 16)
#define TILE_MODE_MASK				GENMASK(30, 25)

/* DEC_READ_EX_CONFIG */
#define BIT_DEPTH_MASK			GENMASK(18, 16)

/* DEC_CONTROL */
#define FLUSH_ENABLE			BIT(0)
#define COMPRESSION_DISABLE		BIT(1)

/* DEC_CONTROL_EX */
#define WRITE_MISS_POLICY1		1
#define WRITE_MISS_POLICY_MASK	BIT(19)
#define READ_MISS_POLICY0		0
#define READ_MISS_POLICY_MASK	BIT(29)

/* DEC_CONTROL_EX2 */
#define TILE_STATUS_READ_ID			16
#define TILE_STATUS_READ_ID_MASK	GENMASK(6, 0)
#define TILE_STATUS_READ_ID_H		0
#define TILE_STATUS_READ_ID_H_MASK	GENMASK(23, 22)
#define DISABLE_HW_DEC_FLUSH		BIT(28)

#define STREAM_COUNT	   3
#define STREAM_TOTAL	   32

#define DEC_PLANE_MAX	   3

enum dc_dec_align_mode {
	DEC_ALIGN_32  = 0x02,
	DEC_ALIGN_64,
};

enum dc_dec_format {
	DEC_FORMAT_ARGB8,
	DEC_FORMAT_XRGB8,
	DEC_FORMAT_AYUV,
	DEC_FORMAT_UYVY,
	DEC_FORMAT_YUY2,
	DEC_FORMAT_YUV_ONLY,
	DEC_FORMAT_UV_MIX,
	DEC_FORMAT_ARGB4,
	DEC_FORMAT_XRGB4,
	DEC_FORMAT_A1RGB5,
	DEC_FORMAT_X1RGB5,
	DEC_FORMAT_R5G6B5,
	DEC_FORMAT_A2R10G10B10 = 0x0F,
	DEC_FORMAT_BAYER,
	DEC_FORMAT_COEFFICIENT = 0x12,
	DEC_FORMAT_ARGB16,
	DEC_FORMAT_X2RGB10 = 0x15,
};

enum dc_dec_depth {
	DEC_DEPTH_8,
	DEC_DEPTH_10,
	DEC_DEPTH_12,
	DEC_DEPTH_14,
	DEC_DEPTH_16,
};

struct dc_dec_stream {
	u32  main_base_addr;
	u32  aligned_stride;
	u32  ts_base_addr;
	u8	 tile_mode;
	u8	 align_mode;
	u8	 format;
	u8	 depth;
	bool dirty;
};

struct dc_dec_fb {
	struct drm_framebuffer *fb;
	u32 addr[DEC_PLANE_MAX];
	u32 stride[DEC_PLANE_MAX];
};

struct dc_dec400l {
	struct dc_dec_stream stream[STREAM_TOTAL];
	u32 stream_status;
};

int dc_dec_config(struct dc_dec400l *dec400l, struct dc_dec_fb *dec_fb,
				  u8 stream_base);
int dc_dec_commit(struct dc_dec400l *dec400l, struct dc_hw *hw);

#endif /* _VS_DC_DEC_H_ */
