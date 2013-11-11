/* exynos_drm_dmabuf.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_DMABUF_H_
#define _EXYNOS_DRM_DMABUF_H_

#ifdef CONFIG_DRM_EXYNOS_DMABUF
struct dma_buf *exynos_dmabuf_prime_export(struct drm_device *drm_dev,
				struct drm_gem_object *obj, int flags);

struct drm_gem_object *exynos_dmabuf_prime_import(struct drm_device *drm_dev,
						struct dma_buf *dma_buf);
#else
#define exynos_dmabuf_prime_export		NULL
#define exynos_dmabuf_prime_import		NULL
#endif
#endif
