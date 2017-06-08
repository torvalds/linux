#ifndef _M68K_DMA_MAPPING_H
#define _M68K_DMA_MAPPING_H

extern const struct dma_map_ops m68k_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
        return &m68k_dma_ops;
}

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
				  enum dma_data_direction dir)
{
	/* we use coherent allocation, so not much to do here. */
}

#endif  /* _M68K_DMA_MAPPING_H */
