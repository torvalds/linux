/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 * Author: Sandy Huang <hjc@rock-chips.com>
 */

#ifndef __ROCKCHIP_DRM_DISPLAY_PATTERN_H__
#define __ROCKCHIP_DRM_DISPLAY_PATTERN_H__

#include <linux/kernel.h>
#include <linux/string.h>
#include <drm/drm_fourcc.h>

struct util_color_component {
	unsigned int length;
	unsigned int offset;
};

struct util_rgb_info {
	struct util_color_component red;
	struct util_color_component green;
	struct util_color_component blue;
	struct util_color_component alpha;
};

enum util_yuv_order {
	YUV_YCbCr = 1,
	YUV_YCrCb = 2,
	YUV_YC = 4,
	YUV_CY = 8,
};

struct util_yuv_info {
	enum util_yuv_order order;
	unsigned int xsub;
	unsigned int ysub;
	unsigned int chroma_stride;
};

struct util_format_info {
	uint32_t format;
	const char *name;
	const struct util_rgb_info rgb;
	const struct util_yuv_info yuv;
};

void rockchip_drm_fill_color_bar(uint32_t format,
				 void *planes[3], unsigned int width,
				 unsigned int height, unsigned int stride);

#endif
