/*
 * Copyright (C) 2015 Samsung Electronics Co.Ltd
 * Authors: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef S5P_MFC_IOMMU_H_
#define S5P_MFC_IOMMU_H_

#define S5P_MFC_IOMMU_DMA_BASE	0x20000000lu
#define S5P_MFC_IOMMU_DMA_SIZE	SZ_256M

#if defined(CONFIG_EXYNOS_IOMMU) && defined(CONFIG_ARM_DMA_USE_IOMMU)

#include <asm/dma-iommu.h>

static inline bool exynos_is_iommu_available(struct device *dev)
{
	return dev->archdata.iommu != NULL;
}

static inline void exynos_unconfigure_iommu(struct device *dev)
{
	struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);

	arm_iommu_detach_device(dev);
	arm_iommu_release_mapping(mapping);
}

static inline int exynos_configure_iommu(struct device *dev,
					 unsigned int base, unsigned int size)
{
	struct dma_iommu_mapping *mapping = NULL;
	int ret;

	/* Disable the default mapping created by device core */
	if (to_dma_iommu_mapping(dev))
		exynos_unconfigure_iommu(dev);

	mapping = arm_iommu_create_mapping(dev->bus, base, size);
	if (IS_ERR(mapping)) {
		pr_warn("Failed to create IOMMU mapping for device %s\n",
			dev_name(dev));
		return PTR_ERR(mapping);
	}

	ret = arm_iommu_attach_device(dev, mapping);
	if (ret) {
		pr_warn("Failed to attached device %s to IOMMU_mapping\n",
				dev_name(dev));
		arm_iommu_release_mapping(mapping);
		return ret;
	}

	return 0;
}

#else

static inline bool exynos_is_iommu_available(struct device *dev)
{
	return false;
}

static inline int exynos_configure_iommu(struct device *dev,
					 unsigned int base, unsigned int size)
{
	return -ENOSYS;
}

static inline void exynos_unconfigure_iommu(struct device *dev) { }

#endif

#endif /* S5P_MFC_IOMMU_H_ */
