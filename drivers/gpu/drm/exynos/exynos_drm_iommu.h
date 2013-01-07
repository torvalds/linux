/* exynos_drm_iommu.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * Authoer: Inki Dae <inki.dae@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _EXYNOS_DRM_IOMMU_H_
#define _EXYNOS_DRM_IOMMU_H_

#define EXYNOS_DEV_ADDR_START	0x20000000
#define EXYNOS_DEV_ADDR_SIZE	0x40000000
#define EXYNOS_DEV_ADDR_ORDER	0x4

#ifdef CONFIG_DRM_EXYNOS_IOMMU

int drm_create_iommu_mapping(struct drm_device *drm_dev);

void drm_release_iommu_mapping(struct drm_device *drm_dev);

int drm_iommu_attach_device(struct drm_device *drm_dev,
				struct device *subdrv_dev);

void drm_iommu_detach_device(struct drm_device *dev_dev,
				struct device *subdrv_dev);

static inline bool is_drm_iommu_supported(struct drm_device *drm_dev)
{
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	struct device *dev = drm_dev->dev;

	return dev->archdata.mapping ? true : false;
#else
	return false;
#endif
}

#else

struct dma_iommu_mapping;
static inline int drm_create_iommu_mapping(struct drm_device *drm_dev)
{
	return 0;
}

static inline void drm_release_iommu_mapping(struct drm_device *drm_dev)
{
}

static inline int drm_iommu_attach_device(struct drm_device *drm_dev,
						struct device *subdrv_dev)
{
	return 0;
}

static inline void drm_iommu_detach_device(struct drm_device *drm_dev,
						struct device *subdrv_dev)
{
}

static inline bool is_drm_iommu_supported(struct drm_device *drm_dev)
{
	return false;
}

#endif
#endif
