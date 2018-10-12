// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
// Author: Andrzej Hajda <a.hajda@samsung.com>

#include "exynos_drm_drv.h"
#include "exynos_drm_iommu.h"

int exynos_drm_register_dma(struct drm_device *drm, struct device *dev)
{
	struct exynos_drm_private *priv = drm->dev_private;
	int ret;

	if (!priv->dma_dev) {
		priv->dma_dev = dev;
		DRM_INFO("Exynos DRM: using %s device for DMA mapping operations\n",
			 dev_name(dev));
		/* create common IOMMU mapping for all Exynos DRM devices */
		ret = drm_create_iommu_mapping(drm);
		if (ret < 0) {
			priv->dma_dev = NULL;
			DRM_ERROR("failed to create iommu mapping.\n");
			return -EINVAL;
		}
	}

	return drm_iommu_attach_device(drm, dev);
}

void exynos_drm_unregister_dma(struct drm_device *drm, struct device *dev)
{
	if (IS_ENABLED(CONFIG_EXYNOS_IOMMU))
		drm_iommu_detach_device(drm, dev);
}

void exynos_drm_cleanup_dma(struct drm_device *drm)
{
	if (IS_ENABLED(CONFIG_EXYNOS_IOMMU))
		drm_release_iommu_mapping(drm);
}
