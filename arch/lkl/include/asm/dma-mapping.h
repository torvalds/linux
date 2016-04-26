#ifndef _ASM_LKL_DMA_MAPPING_H
#define _ASM_LKL_DMA_MAPPING_H

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &dma_noop_ops;
}

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
				  enum dma_data_direction direction)
{
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return false;
	return addr + size - 1 <= *dev->dma_mask;
}

#endif
