#ifndef __ASM_AVR32_DMA_MAPPING_H
#define __ASM_AVR32_DMA_MAPPING_H

extern void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	int direction);

extern struct dma_map_ops avr32_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &avr32_dma_ops;
}

#endif /* __ASM_AVR32_DMA_MAPPING_H */
