#ifndef _ASM_METAG_DMA_MAPPING_H
#define _ASM_METAG_DMA_MAPPING_H

extern struct dma_map_ops metag_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &metag_dma_ops;
}

/*
 * dma_alloc_noncoherent() returns non-cacheable memory, so there's no need to
 * do any flushing here.
 */
static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
}

#endif
