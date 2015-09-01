#ifndef _H8300_DMA_MAPPING_H
#define _H8300_DMA_MAPPING_H

#include <asm-generic/dma-coherent.h>

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

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

#define dma_alloc_coherent(d, s, h, f) dma_alloc_attrs(d, s, h, f, NULL)

static inline void *dma_alloc_attrs(struct device *dev, size_t size,
				    dma_addr_t *dma_handle, gfp_t flag,
				    struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	void *memory;

	memory = ops->alloc(dev, size, dma_handle, flag, attrs);
	return memory;
}

#define dma_free_coherent(d, s, c, h) dma_free_attrs(d, s, c, h, NULL)

static inline void dma_free_attrs(struct device *dev, size_t size,
				  void *cpu_addr, dma_addr_t dma_handle,
				  struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	ops->free(dev, size, cpu_addr, dma_handle, attrs);
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

#endif
