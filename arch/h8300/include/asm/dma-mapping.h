#ifndef _H8300_DMA_MAPPING_H
#define _H8300_DMA_MAPPING_H

extern const struct dma_map_ops h8300_dma_map_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &h8300_dma_map_ops;
}

#endif
