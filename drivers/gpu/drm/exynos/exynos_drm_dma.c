// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
// Author: Inki Dae <inki.dae@samsung.com>
// Author: Andrzej Hajda <a.hajda@samsung.com>

#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>

#include <drm/drm_print.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"

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

/*
 * drm_iommu_attach_device- attach device to iommu mapping
 *
 * @drm_dev: DRM device
 * @subdrv_dev: device to be attach
 *
 * This function should be called by sub drivers to attach it to iommu
 * mapping.
 */
static int drm_iommu_attach_device(struct drm_device *drm_dev,
				struct device *subdrv_dev, void **dma_priv)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;
	int ret = 0;

	if (get_dma_ops(priv->dma_dev) != get_dma_ops(subdrv_dev)) {
		DRM_DEV_ERROR(subdrv_dev, "Device %s lacks support for IOMMU\n",
			  dev_name(subdrv_dev));
		return -EINVAL;
	}

	dma_set_max_seg_size(subdrv_dev, DMA_BIT_MASK(32));
	if (IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)) {
		/*
		 * Keep the original DMA mapping of the sub-device and
		 * restore it on Exynos DRM detach, otherwise the DMA
		 * framework considers it as IOMMU-less during the next
		 * probe (in case of deferred probe or modular build)
		 */
		*dma_priv = to_dma_iommu_mapping(subdrv_dev);
		if (*dma_priv)
			arm_iommu_detach_device(subdrv_dev);

		ret = arm_iommu_attach_device(subdrv_dev, priv->mapping);
	} else if (IS_ENABLED(CONFIG_IOMMU_DMA)) {
		ret = iommu_attach_device(priv->mapping, subdrv_dev);
	}

	return ret;
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
static void drm_iommu_detach_device(struct drm_device *drm_dev,
				    struct device *subdrv_dev, void **dma_priv)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;

	if (IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)) {
		arm_iommu_detach_device(subdrv_dev);
		arm_iommu_attach_device(subdrv_dev, *dma_priv);
	} else if (IS_ENABLED(CONFIG_IOMMU_DMA))
		iommu_detach_device(priv->mapping, subdrv_dev);
}

int exynos_drm_register_dma(struct drm_device *drm, struct device *dev,
			    void **dma_priv)
{
	struct exynos_drm_private *priv = drm->dev_private;

	if (!priv->dma_dev) {
		priv->dma_dev = dev;
		DRM_INFO("Exynos DRM: using %s device for DMA mapping operations\n",
			 dev_name(dev));
	}

	if (!IS_ENABLED(CONFIG_EXYNOS_IOMMU))
		return 0;

	if (!priv->mapping) {
		void *mapping;

		if (IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU))
			mapping = arm_iommu_create_mapping(&platform_bus_type,
				EXYNOS_DEV_ADDR_START, EXYNOS_DEV_ADDR_SIZE);
		else if (IS_ENABLED(CONFIG_IOMMU_DMA))
			mapping = iommu_get_domain_for_dev(priv->dma_dev);

		if (IS_ERR(mapping))
			return PTR_ERR(mapping);
		priv->mapping = mapping;
	}

	return drm_iommu_attach_device(drm, dev, dma_priv);
}

void exynos_drm_unregister_dma(struct drm_device *drm, struct device *dev,
			       void **dma_priv)
{
	if (IS_ENABLED(CONFIG_EXYNOS_IOMMU))
		drm_iommu_detach_device(drm, dev, dma_priv);
}

void exynos_drm_cleanup_dma(struct drm_device *drm)
{
	struct exynos_drm_private *priv = drm->dev_private;

	if (!IS_ENABLED(CONFIG_EXYNOS_IOMMU))
		return;

	arm_iommu_release_mapping(priv->mapping);
	priv->mapping = NULL;
	priv->dma_dev = NULL;
}
