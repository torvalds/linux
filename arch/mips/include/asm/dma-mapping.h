/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <linux/swiotlb.h>

extern const struct dma_map_ops jazz_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
#if defined(CONFIG_MACH_JAZZ)
	return &jazz_dma_ops;
#elif defined(CONFIG_SWIOTLB)
	return &swiotlb_dma_ops;
#elif defined(CONFIG_DMA_NONCOHERENT_OPS)
	return &dma_noncoherent_ops;
#else
	return &dma_direct_ops;
#endif
}

#define arch_setup_dma_ops arch_setup_dma_ops
static inline void arch_setup_dma_ops(struct device *dev, u64 dma_base,
				      u64 size, const struct iommu_ops *iommu,
				      bool coherent)
{
#ifdef CONFIG_DMA_PERDEV_COHERENT
	dev->archdata.dma_coherent = coherent;
#endif
}

#endif /* _ASM_DMA_MAPPING_H */
