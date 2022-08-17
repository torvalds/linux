/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Arch specific extensions to struct device
 */
#ifndef ASMARM_DEVICE_H
#define ASMARM_DEVICE_H

struct dev_archdata {
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	struct dma_iommu_mapping	*mapping;
#endif
	unsigned int dma_coherent:1;
	unsigned int dma_ops_setup:1;
};

struct omap_device;

struct pdev_archdata {
#ifdef CONFIG_ARCH_OMAP
	struct omap_device *od;
#endif
};

#ifdef CONFIG_ARM_DMA_USE_IOMMU
#define to_dma_iommu_mapping(dev) ((dev)->archdata.mapping)
#else
#define to_dma_iommu_mapping(dev) NULL
#endif

#endif
