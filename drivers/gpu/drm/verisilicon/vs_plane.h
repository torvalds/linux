/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 *
 * Based on vs_dc_hw.h, which is:
 *   Copyright (C) 2023 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_PLANE_H_
#define _VS_PLANE_H_

#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane.h>
#include <drm/drm_rect.h>

#define VSDC_MAKE_PLANE_SIZE(w, h) (((w) & 0x7fff) | (((h) & 0x7fff) << 15))
#define VSDC_MAKE_PLANE_POS(x, y) (((x) & 0x7fff) | (((y) & 0x7fff) << 15))

struct vs_dc;

enum vs_color_format {
	VSDC_COLOR_FORMAT_X4R4G4B4,
	VSDC_COLOR_FORMAT_A4R4G4B4,
	VSDC_COLOR_FORMAT_X1R5G5B5,
	VSDC_COLOR_FORMAT_A1R5G5B5,
	VSDC_COLOR_FORMAT_R5G6B5,
	VSDC_COLOR_FORMAT_X8R8G8B8,
	VSDC_COLOR_FORMAT_A8R8G8B8,
	VSDC_COLOR_FORMAT_YUY2,
	VSDC_COLOR_FORMAT_UYVY,
	VSDC_COLOR_FORMAT_INDEX8,
	VSDC_COLOR_FORMAT_MONOCHROME,
	VSDC_COLOR_FORMAT_YV12 = 0xf,
	VSDC_COLOR_FORMAT_A8,
	VSDC_COLOR_FORMAT_NV12,
	VSDC_COLOR_FORMAT_NV16,
	VSDC_COLOR_FORMAT_RG16,
	VSDC_COLOR_FORMAT_R8,
	VSDC_COLOR_FORMAT_NV12_10BIT,
	VSDC_COLOR_FORMAT_A2R10G10B10,
	VSDC_COLOR_FORMAT_NV16_10BIT,
	VSDC_COLOR_FORMAT_INDEX1,
	VSDC_COLOR_FORMAT_INDEX2,
	VSDC_COLOR_FORMAT_INDEX4,
	VSDC_COLOR_FORMAT_P010,
	VSDC_COLOR_FORMAT_YUV444,
	VSDC_COLOR_FORMAT_YUV444_10BIT
};

enum vs_swizzle {
	VSDC_SWIZZLE_ARGB,
	VSDC_SWIZZLE_RGBA,
	VSDC_SWIZZLE_ABGR,
	VSDC_SWIZZLE_BGRA,
};

struct vs_format {
	enum vs_color_format color;
	enum vs_swizzle swizzle;
	bool uv_swizzle;
};

void drm_format_to_vs_format(u32 drm_format, struct vs_format *vs_format);
dma_addr_t vs_fb_get_dma_addr(struct drm_framebuffer *fb,
			      const struct drm_rect *src_rect);

struct drm_plane *vs_primary_plane_init(struct drm_device *dev, struct vs_dc *dc);

#endif /* _VS_PLANE_H_ */
