#ifndef ___ASM_SPARC_DMA_MAPPING_H
#define ___ASM_SPARC_DMA_MAPPING_H

#include <linux/scatterlist.h>
#include <linux/mm.h>
#include <linux/dma-debug.h>

#define DMA_ERROR_CODE	(~(dma_addr_t)0x0)

#define HAVE_ARCH_DMA_SUPPORTED 1
int dma_supported(struct device *dev, u64 mask);

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
				  enum dma_data_direction dir)
{
	/* Since dma_{alloc,free}_noncoherent() allocated coherent memory, this
	 * routine can be a nop.
	 */
}

extern struct dma_map_ops *dma_ops;
extern struct dma_map_ops *leon_dma_ops;
extern struct dma_map_ops pci32_dma_ops;

extern struct bus_type pci_bus_type;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
#ifdef CONFIG_SPARC_LEON
	if (sparc_cpu_model == sparc_leon)
		return leon_dma_ops;
#endif
#if defined(CONFIG_SPARC32) && defined(CONFIG_PCI)
	if (dev->bus == &pci_bus_type)
		return &pci32_dma_ops;
#endif
	return dma_ops;
}

#endif
