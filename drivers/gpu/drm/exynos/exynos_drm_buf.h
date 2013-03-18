/* exynos_drm_buf.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_BUF_H_
#define _EXYNOS_DRM_BUF_H_

/* create and initialize buffer object. */
struct exynos_drm_gem_buf *exynos_drm_init_buf(struct drm_device *dev,
						unsigned int size);

/* destroy buffer object. */
void exynos_drm_fini_buf(struct drm_device *dev,
				struct exynos_drm_gem_buf *buffer);

/* allocate physical memory region and setup sgt. */
int exynos_drm_alloc_buf(struct drm_device *dev,
				struct exynos_drm_gem_buf *buf,
				unsigned int flags);

/* release physical memory region, and sgt. */
void exynos_drm_free_buf(struct drm_device *dev,
				unsigned int flags,
				struct exynos_drm_gem_buf *buffer);

#endif
