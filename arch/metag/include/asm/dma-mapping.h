#ifndef _ASM_METAG_DMA_MAPPING_H
#define _ASM_METAG_DMA_MAPPING_H

extern const struct dma_map_ops metag_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &metag_dma_ops;
}

/*
 * dma_alloc_attrs() always returns non-cacheable memory, so there's no need to
 * do any flushing here.
 */
static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
}

#endif
