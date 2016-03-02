/* exynos_drm_iommu.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/exynos_drm.h>

#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/kref.h>

#include <asm/dma-iommu.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_iommu.h"

/*
 * drm_create_iommu_mapping - create a mapping structure
 *
 * @drm_dev: DRM device
 */
int drm_create_iommu_mapping(struct drm_device *drm_dev)
{
	struct dma_iommu_mapping *mapping = NULL;
	struct exynos_drm_private *priv = drm_dev->dev_private;

	if (!priv->da_start)
		priv->da_start = EXYNOS_DEV_ADDR_START;
	if (!priv->da_space_size)
		priv->da_space_size = EXYNOS_DEV_ADDR_SIZE;

	mapping = arm_iommu_create_mapping(&platform_bus_type, priv->da_start,
						priv->da_space_size);

	if (IS_ERR(mapping))
		return PTR_ERR(mapping);

	priv->mapping = mapping;

	return 0;
}

/*
 * drm_release_iommu_mapping - release iommu mapping structure
 *
 * @drm_dev: DRM device
 *
 * if mapping->kref becomes 0 then all things related to iommu mapping
 * will be released
 */
void drm_release_iommu_mapping(struct drm_device *drm_dev)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;

	arm_iommu_release_mapping(priv->mapping);
}

/*
 * drm_iommu_attach_device- attach device to iommu mapping
 *
 * @drm_dev: DRM device
 * @subdrv_dev: device to be attach
 *
 * This function should be called by sub drivers to attach it to iommu
 * mapping.
 */
int drm_iommu_attach_device(struct drm_device *drm_dev,
				struct device *subdrv_dev)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;
	int ret;

	if (!priv->mapping)
		return 0;

	subdrv_dev->dma_parms = devm_kzalloc(subdrv_dev,
					sizeof(*subdrv_dev->dma_parms),
					GFP_KERNEL);
	if (!subdrv_dev->dma_parms)
		return -ENOMEM;

	dma_set_max_seg_size(subdrv_dev, 0xffffffffu);

	if (subdrv_dev->archdata.mapping)
		arm_iommu_detach_device(subdrv_dev);

	ret = arm_iommu_attach_device(subdrv_dev, priv->mapping);
	if (ret < 0) {
		DRM_DEBUG_KMS("failed iommu attach.\n");
		return ret;
	}

	return 0;
}

/*
 * drm_iommu_detach_device -detach device address space mapping from device
 *
 * @drm_dev: DRM device
 * @subdrv_dev: device to be detached
 *
 * This function should be called by sub drivers to detach it from iommu
 * mapping
 */
void drm_iommu_detach_device(struct drm_device *drm_dev,
				struct device *subdrv_dev)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;
	struct dma_iommu_mapping *mapping = priv->mapping;

	if (!mapping || !mapping->domain)
		return;

	arm_iommu_detach_device(subdrv_dev);
}
