#ifndef ___ASM_SPARC_DMA_MAPPING_H
#define ___ASM_SPARC_DMA_MAPPING_H

#include <linux/scatterlist.h>
#include <linux/mm.h>
#include <linux/dma-debug.h>

#define DMA_ERROR_CODE	(~(dma_addr_t)0x0)

extern int dma_supported(struct device *dev, u64 mask);
extern int dma_set_mask(struct device *dev, u64 dma_mask);

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_is_consistent(d, h)	(1)

extern struct dma_map_ops *dma_ops, pci32_dma_ops;
extern struct bus_type pci_bus_type;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
#if defined(CONFIG_SPARC32) && defined(CONFIG_PCI)
	if (dev->bus == &pci_bus_type)
		return &pci32_dma_ops;
#endif
	return dma_ops;
}

#include <asm-generic/dma-mapping-common.h>

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t flag)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	void *cpu_addr;

	cpu_addr = ops->alloc_coherent(dev, size, dma_handle, flag);
	debug_dma_alloc_coherent(dev, size, *dma_handle, cpu_addr);
	return cpu_addr;
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *cpu_addr, dma_addr_t dma_handle)
{
	struct dma_map_ops *ops = get_dma_ops(dev);

	debug_dma_free_coherent(dev, size, cpu_addr, dma_handle);
	ops->free_coherent(dev, size, cpu_addr, dma_handle);
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return (dma_addr == DMA_ERROR_CODE);
}

static inline int dma_get_cache_alignment(void)
{
	/*
	 * no easy way to get cache size on all processors, so return
	 * the maximum possible, to be safe
	 */
	return (1 << INTERNODE_CACHE_SHIFT);
}

#endif
