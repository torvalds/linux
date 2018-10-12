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

#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_iommu.h"

#if defined(CONFIG_ARM_DMA_USE_IOMMU)
#include <asm/dma-iommu.h>
#else
#define arm_iommu_create_mapping(...)	({ NULL; })
#define arm_iommu_attach_device(...)	({ -ENODEV; })
#define arm_iommu_release_mapping(...)	({ })
#define arm_iommu_detach_device(...)	({ })
#define to_dma_iommu_mapping(dev) NULL
#endif

#if !defined(CONFIG_IOMMU_DMA)
#define iommu_dma_init_domain(...) ({ -EINVAL; })
#endif

#define EXYNOS_DEV_ADDR_START	0x20000000
#define EXYNOS_DEV_ADDR_SIZE	0x40000000

static inline int configure_dma_max_seg_size(struct device *dev)
{
	if (!dev->dma_parms)
		dev->dma_parms = kzalloc(sizeof(*dev->dma_parms), GFP_KERNEL);
	if (!dev->dma_parms)
		return -ENOMEM;

	dma_set_max_seg_size(dev, DMA_BIT_MASK(32));
	return 0;
}

static inline void clear_dma_max_seg_size(struct device *dev)
{
	kfree(dev->dma_parms);
	dev->dma_parms = NULL;
}

/*
 * drm_create_iommu_mapping - create a mapping structure
 *
 * @drm_dev: DRM device
 */
int drm_create_iommu_mapping(struct drm_device *drm_dev)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;

	if (IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU))
		priv->mapping = arm_iommu_create_mapping(&platform_bus_type,
			EXYNOS_DEV_ADDR_START, EXYNOS_DEV_ADDR_SIZE);
	else if (IS_ENABLED(CONFIG_IOMMU_DMA))
		priv->mapping = iommu_get_domain_for_dev(priv->dma_dev);

	return IS_ERR(priv->mapping);
}

/*
 * drm_release_iommu_mapping - release iommu mapping structure
 *
 * @drm_dev: DRM device
 */
void drm_release_iommu_mapping(struct drm_device *drm_dev)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;

	arm_iommu_release_mapping(priv->mapping);
	priv->mapping = NULL;
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

	if (get_dma_ops(priv->dma_dev) != get_dma_ops(subdrv_dev)) {
		DRM_ERROR("Device %s lacks support for IOMMU\n",
			  dev_name(subdrv_dev));
		return -EINVAL;
	}

	ret = configure_dma_max_seg_size(subdrv_dev);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)) {
		if (to_dma_iommu_mapping(subdrv_dev))
			arm_iommu_detach_device(subdrv_dev);

		ret = arm_iommu_attach_device(subdrv_dev, priv->mapping);
	} else if (IS_ENABLED(CONFIG_IOMMU_DMA)) {
		ret = iommu_attach_device(priv->mapping, subdrv_dev);
	}

	if (ret)
		clear_dma_max_seg_size(subdrv_dev);

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

	if (IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU))
		arm_iommu_detach_device(subdrv_dev);
	else if (IS_ENABLED(CONFIG_IOMMU_DMA))
		iommu_detach_device(priv->mapping, subdrv_dev);

	clear_dma_max_seg_size(subdrv_dev);
}
