#ifndef _ALPHA_DMA_MAPPING_H
#define _ALPHA_DMA_MAPPING_H

#include <linux/dma-attrs.h>

extern struct dma_map_ops *dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return dma_ops;
}

#include <asm-generic/dma-mapping-common.h>

#define dma_alloc_coherent(d,s,h,f)	dma_alloc_attrs(d,s,h,f,NULL)

static inline void *dma_alloc_attrs(struct device *dev, size_t size,
				    dma_addr_t *dma_handle, gfp_t gfp,
				    struct dma_attrs *attrs)
{
	return get_dma_ops(dev)->alloc(dev, size, dma_handle, gfp, attrs);
}

#define dma_free_coherent(d,s,c,h) dma_free_attrs(d,s,c,h,NULL)

static inline void dma_free_attrs(struct device *dev, size_t size,
				  void *vaddr, dma_addr_t dma_handle,
				  struct dma_attrs *attrs)
{
	get_dma_ops(dev)->free(dev, size, vaddr, dma_handle, attrs);
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
