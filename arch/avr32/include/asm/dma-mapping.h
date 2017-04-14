#ifndef __ASM_AVR32_DMA_MAPPING_H
#define __ASM_AVR32_DMA_MAPPING_H

extern void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	int direction);

extern const struct dma_map_ops avr32_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &avr32_dma_ops;
}

#endif /* __ASM_AVR32_DMA_MAPPING_H */
