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

#ifdef CONFIG_DRM_FBDEV_EMULATION

int exynos_drm_fbdev_init(struct drm_device *dev);
void exynos_drm_fbdev_fini(struct drm_device *dev);

#else

static inline int exynos_drm_fbdev_init(struct drm_device *dev)
{
	return 0;
}

static inline void exynos_drm_fbdev_fini(struct drm_device *dev)
{
}

static inline void exynos_drm_fbdev_restore_mode(struct drm_device *dev)
{
}

#define exynos_drm_output_poll_changed (NULL)

#endif

#endif
