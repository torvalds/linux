/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#ifndef _EXYNOS_DRM_FBDEV_H_
#define _EXYNOS_DRM_FBDEV_H_

struct drm_fb_helper;
struct drm_fb_helper_surface_size;

#if defined(CONFIG_DRM_FBDEV_EMULATION)
int exynos_drm_fbdev_driver_fbdev_probe(struct drm_fb_helper *fbh,
					struct drm_fb_helper_surface_size *sizes);
#define EXYNOS_DRM_FBDEV_DRIVER_OPS \
	.fbdev_probe = exynos_drm_fbdev_driver_fbdev_probe
#else
#define EXYNOS_DRM_FBDEV_DRIVER_OPS \
	.fbdev_probe = NULL
#endif

#endif
