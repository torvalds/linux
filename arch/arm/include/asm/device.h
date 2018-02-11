/*
 * Arch specific extensions to struct device
 *
 * This file is released under the GPLv2
 */
#ifndef ASMARM_DEVICE_H
#define ASMARM_DEVICE_H

struct dev_archdata {
#ifdef CONFIG_DMABOUNCE
	struct dmabounce_device_info *dmabounce;
#endif
#ifdef CONFIG_IOMMU_API
	void *iommu; /* private IOMMU data */
#endif
#ifdef CONFIG_ARM_DMA_USE_IOMMU
	struct dma_iommu_mapping	*mapping;
#endif
#ifdef CONFIG_XEN
	const struct dma_map_ops *dev_dma_ops;
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
