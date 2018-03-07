#ifndef _ASM_LKL_DMA_MAPPING_H
#define _ASM_LKL_DMA_MAPPING_H

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &dma_noop_ops;
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return false;
	return addr + size - 1 <= *dev->dma_mask;
}

#endif
