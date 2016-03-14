#ifndef _H8300_DMA_MAPPING_H
#define _H8300_DMA_MAPPING_H

extern struct dma_map_ops h8300_dma_map_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &h8300_dma_map_ops;
}

#endif
