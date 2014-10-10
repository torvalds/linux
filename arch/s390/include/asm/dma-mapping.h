#ifndef _ASM_S390_DMA_MAPPING_H
#define _ASM_S390_DMA_MAPPING_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-attrs.h>
#include <linux/dma-debug.h>
#include <linux/io.h>

#define DMA_ERROR_CODE		(~(dma_addr_t) 0x0)

extern struct dma_map_ops s390_dma_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return &s390_dma_ops;
}

extern int dma_set_mask(struct device *dev, u64 mask);

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
				  enum dma_data_direction direction)
{
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

#include <asm-generic/dma-mapping-common.h>

static inline int dma_supported(struct device *dev, u64 mask)
{
	struct dma_map_ops *dma_ops = get_dma_ops(dev);

	if (dma_ops->dma_supported == NULL)
		return 1;
	return dma_ops->dma_supported(dev, mask);
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return 0;
	return addr + size - 1 <= *dev->dma_mask;
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	struct dma_map_ops *dma_ops = get_dma_ops(dev);

	debug_dma_mapping_error(dev, dma_addr);
	if (dma_ops->mapping_error)
		return dma_ops->mapping_error(dev, dma_addr);
	return dma_addr == DMA_ERROR_CODE;
}

#define dma_alloc_coherent(d, s, h, f) dma_alloc_attrs(d, s, h, f, NULL)

static inline void *dma_alloc_attrs(struct device *dev, size_t size,
				    dma_addr_t *dma_handle, gfp_t flags,
				    struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	void *cpu_addr;

	BUG_ON(!ops);

	cpu_addr = ops->alloc(dev, size, dma_handle, flags, attrs);
	debug_dma_alloc_coherent(dev, size, *dma_handle, cpu_addr);

	return cpu_addr;
}

#define dma_free_coherent(d, s, c, h) dma_free_attrs(d, s, c, h, NULL)

static inline void dma_free_attrs(struct device *dev, size_t size,
				  void *cpu_addr, dma_addr_t dma_handle,
				  struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	BUG_ON(!ops);

	debug_dma_free_coherent(dev, size, cpu_addr, dma_handle);
	ops->free(dev, size, cpu_addr, dma_handle, attrs);
}

#endif /* _ASM_S390_DMA_MAPPING_H */
