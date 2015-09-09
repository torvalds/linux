#ifndef _H8300_DMA_MAPPING_H
#define _H8300_DMA_MAPPING_H

extern struct dma_map_ops h8300_dma_map_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &h8300_dma_map_ops;
}

#include <asm-generic/dma-mapping-common.h>

static inline int dma_supported(struct device *dev, u64 mask)
{
	return 0;
}

static inline int dma_set_mask(struct device *dev, u64 mask)
{
	return 0;
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

#endif
