/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#ifndef _EXYANALS_DRM_FB_H_
#define _EXYANALS_DRM_FB_H_

#include "exyanals_drm_gem.h"

struct drm_framebuffer *
exyanals_drm_framebuffer_init(struct drm_device *dev,
			    const struct drm_mode_fb_cmd2 *mode_cmd,
			    struct exyanals_drm_gem **exyanals_gem,
			    int count);

dma_addr_t exyanals_drm_fb_dma_addr(struct drm_framebuffer *fb, int index);

void exyanals_drm_mode_config_init(struct drm_device *dev);

#endif
