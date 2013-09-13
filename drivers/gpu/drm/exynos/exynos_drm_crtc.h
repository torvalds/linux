/* exynos_drm_crtc.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_CRTC_H_
#define _EXYNOS_DRM_CRTC_H_

int exynos_drm_crtc_create(struct drm_device *dev, unsigned int nr);
int exynos_drm_crtc_enable_vblank(struct drm_device *dev, int crtc);
void exynos_drm_crtc_disable_vblank(struct drm_device *dev, int crtc);
void exynos_drm_crtc_finish_pageflip(struct drm_device *dev, int crtc);

#endif
