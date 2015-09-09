#ifndef _ALPHA_DMA_MAPPING_H
#define _ALPHA_DMA_MAPPING_H

#include <linux/dma-attrs.h>

extern struct dma_map_ops *dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return dma_ops;
}

#include <asm-generic/dma-mapping-common.h>

static inline int dma_supported(struct device *dev, u64 mask)
{
	return get_dma_ops(dev)->dma_supported(dev, mask);
}

static inline int dma_set_mask(struct device *dev, u64 mask)
{
	return get_dma_ops(dev)->set_dma_mask(dev, mask);
}

#define dma_cache_sync(dev, va, size, dir)		  ((void)0)

#endif	/* _ALPHA_DMA_MAPPING_H */
