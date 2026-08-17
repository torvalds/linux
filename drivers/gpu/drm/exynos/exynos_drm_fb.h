/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#ifndef _EXYNOS_DRM_FB_H_
#define _EXYNOS_DRM_FB_H_

#include <linux/types.h>

struct drm_device;
struct drm_framebuffer;

dma_addr_t exynos_drm_fb_dma_addr(struct drm_framebuffer *fb, int index);

void exynos_drm_mode_config_init(struct drm_device *dev);

#endif
