#ifndef _ALPHA_DMA_MAPPING_H
#define _ALPHA_DMA_MAPPING_H

#include <linux/dma-attrs.h>

extern struct dma_map_ops *dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return dma_ops;
}

#include <asm-generic/dma-mapping-common.h>

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t gfp)
{
	return get_dma_ops(dev)->alloc_coherent(dev, size, dma_handle, gfp);
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *vaddr, dma_addr_t dma_handle)
{
	get_dma_ops(dev)->free_coherent(dev, size, vaddr, dma_handle);
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return get_dma_ops(dev)->mapping_error(dev, dma_addr);
}

static inline int dma_supported(struct device *dev, u64 mask)
{
	return get_dma_ops(dev)->dma_supported(dev, mask);
}

static inline int dma_set_mask(struct device *dev, u64 mask)
{
	return get_dma_ops(dev)->set_dma_mask(dev, mask);
}

#define dma_alloc_noncoherent(d, s, h, f)	dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h)	dma_free_coherent(d, s, v, h)

#define dma_cache_sync(dev, va, size, dir)		  ((void)0)

#endif	/* _ALPHA_DMA_MAPPING_H */
