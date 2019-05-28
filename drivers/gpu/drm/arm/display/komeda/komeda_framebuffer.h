/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _KOMEDA_FRAMEBUFFER_H_
#define _KOMEDA_FRAMEBUFFER_H_

#include <drm/drm_framebuffer.h>
#include "komeda_format_caps.h"

/**
 * struct komeda_fb - Entending drm_framebuffer with komeda attribute
 */
struct komeda_fb {
	/** @base: &drm_framebuffer */
	struct drm_framebuffer base;
	/**
	 * @format_caps:
	 * extends drm_format_info for komeda specific information
	 */
	const struct komeda_format_caps *format_caps;
	/** @aligned_w: aligned frame buffer width */
	u32 aligned_w;
	/** @aligned_h: aligned frame buffer height */
	u32 aligned_h;
};

#define to_kfb(dfb)	container_of(dfb, struct komeda_fb, base)

struct drm_framebuffer *
komeda_fb_create(struct drm_device *dev, struct drm_file *file,
		 const struct drm_mode_fb_cmd2 *mode_cmd);
dma_addr_t
komeda_fb_get_pixel_addr(struct komeda_fb *kfb, int x, int y, int plane);
bool komeda_fb_is_layer_supported(struct komeda_fb *kfb, u32 layer_type);

#endif
