#ifndef _ASM_CRIS_DMA_MAPPING_H
#define _ASM_CRIS_DMA_MAPPING_H

#ifdef CONFIG_PCI
extern struct dma_map_ops v32_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &v32_dma_ops;
}
#else
static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	BUG();
	return NULL;
}
#endif

static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
}

#endif
