/* exynos_drm_iommu.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * Authoer: Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_DRM_IOMMU_H_
#define _EXYNOS_DRM_IOMMU_H_

#define EXYNOS_DEV_ADDR_START	0x20000000
#define EXYNOS_DEV_ADDR_SIZE	0x40000000

#ifdef CONFIG_DRM_EXYNOS_IOMMU

#if defined(CONFIG_ARM_DMA_USE_IOMMU)
#include <asm/dma-iommu.h>

static inline int __exynos_iommu_create_mapping(struct exynos_drm_private *priv,
					unsigned long start, unsigned long size)
{
	priv->mapping = arm_iommu_create_mapping(&platform_bus_type, start,
						 size);
	return IS_ERR(priv->mapping);
}

static inline void
__exynos_iommu_release_mapping(struct exynos_drm_private *priv)
{
	arm_iommu_release_mapping(priv->mapping);
}

static inline int __exynos_iommu_attach(struct exynos_drm_private *priv,
					struct device *dev)
{
	if (dev->archdata.mapping)
		arm_iommu_detach_device(dev);

	return arm_iommu_attach_device(dev, priv->mapping);
}

static inline void __exynos_iommu_detach(struct exynos_drm_private *priv,
					 struct device *dev)
{
	arm_iommu_detach_device(dev);
}

#elif defined(CONFIG_IOMMU_DMA)
#include <linux/dma-iommu.h>

static inline int __exynos_iommu_create_mapping(struct exynos_drm_private *priv,
					unsigned long start, unsigned long size)
{
	struct iommu_domain *domain;
	int ret;

	domain = iommu_domain_alloc(priv->dma_dev->bus);
	if (!domain)
		return -ENOMEM;

	ret = iommu_get_dma_cookie(domain);
	if (ret)
		goto free_domain;

	ret = iommu_dma_init_domain(domain, start, size, NULL);
	if (ret)
		goto put_cookie;

	priv->mapping = domain;
	return 0;

put_cookie:
	iommu_put_dma_cookie(domain);
free_domain:
	iommu_domain_free(domain);
	return ret;
}

static inline void __exynos_iommu_release_mapping(struct exynos_drm_private *priv)
{
	struct iommu_domain *domain = priv->mapping;

	iommu_put_dma_cookie(domain);
	iommu_domain_free(domain);
	priv->mapping = NULL;
}

static inline int __exynos_iommu_attach(struct exynos_drm_private *priv,
					struct device *dev)
{
	struct iommu_domain *domain = priv->mapping;

	return iommu_attach_device(domain, dev);
}

static inline void __exynos_iommu_detach(struct exynos_drm_private *priv,
					 struct device *dev)
{
	struct iommu_domain *domain = priv->mapping;

	iommu_detach_device(domain, dev);
}
#else
#error Unsupported architecture and IOMMU/DMA-mapping glue code
#endif

int drm_create_iommu_mapping(struct drm_device *drm_dev);

void drm_release_iommu_mapping(struct drm_device *drm_dev);

int drm_iommu_attach_device(struct drm_device *drm_dev,
				struct device *subdrv_dev);

void drm_iommu_detach_device(struct drm_device *dev_dev,
				struct device *subdrv_dev);

static inline bool is_drm_iommu_supported(struct drm_device *drm_dev)
{
	struct exynos_drm_private *priv = drm_dev->dev_private;

	return priv->mapping ? true : false;
}

#else

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
