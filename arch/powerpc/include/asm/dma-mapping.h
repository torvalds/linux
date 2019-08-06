/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2004 IBM
 *
 * Implements the generic device dma API for powerpc.
 * the pci and vio busses
 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/cache.h>
/* need struct page definitions */
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-debug.h>
#include <asm/io.h>
#include <asm/swiotlb.h>

/* Some dma direct funcs must be visible for use in other dma_ops */
extern void *__dma_nommu_alloc_coherent(struct device *dev, size_t size,
					 dma_addr_t *dma_handle, gfp_t flag,
					 unsigned long attrs);
extern void __dma_nommu_free_coherent(struct device *dev, size_t size,
				       void *vaddr, dma_addr_t dma_handle,
				       unsigned long attrs);
extern int dma_nommu_mmap_coherent(struct device *dev,
				    struct vm_area_struct *vma,
				    void *cpu_addr, dma_addr_t handle,
				    size_t size, unsigned long attrs);

#ifdef CONFIG_NOT_COHERENT_CACHE
/*
 * DMA-consistent mapping functions for PowerPCs that don't support
 * cache snooping.  These allocate/free a region of uncached mapped
 * memory space for use with DMA devices.  Alternatively, you could
 * allocate the space "normally" and use the cache management functions
 * to ensure it is consistent.
 */
struct device;
extern void __dma_sync(void *vaddr, size_t size, int direction);
extern void __dma_sync_page(struct page *page, unsigned long offset,
				 size_t size, int direction);
extern unsigned long __dma_get_coherent_pfn(unsigned long cpu_addr);

#else /* ! CONFIG_NOT_COHERENT_CACHE */
/*
 * Cache coherent cores.
 */

#define __dma_sync(addr, size, rw)		((void)0)
#define __dma_sync_page(pg, off, sz, rw)	((void)0)

#endif /* ! CONFIG_NOT_COHERENT_CACHE */

static inline unsigned long device_to_mask(struct device *dev)
{
	if (dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	/* Assume devices without mask can take 32 bit addresses */
	return 0xfffffffful;
}

/*
 * Available generic sets of operations
 */
#ifdef CONFIG_PPC64
extern struct dma_map_ops dma_iommu_ops;
#endif
extern const struct dma_map_ops dma_nommu_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	/* We don't handle the NULL dev case for ISA for now. We could
	 * do it via an out of line call but it is not needed for now. The
	 * only ISA DMA device we support is the floppy and we have a hack
	 * in the floppy driver directly to get a device for us.
	 */
	return NULL;
}

/*
 * get_dma_offset()
 *
 * Get the dma offset on configurations where the dma address can be determined
 * from the physical address by looking at a simple offset.  Direct dma and
 * swiotlb use this function, but it is typically not used by implementations
 * with an iommu.
 */
static inline dma_addr_t get_dma_offset(struct device *dev)
{
	if (dev)
		return dev->archdata.dma_offset;

	return PCI_DRAM_OFFSET;
}

static inline void set_dma_offset(struct device *dev, dma_addr_t off)
{
	if (dev)
		dev->archdata.dma_offset = off;
}

#define HAVE_ARCH_DMA_SET_MASK 1

extern u64 __dma_get_required_mask(struct device *dev);

#endif /* __KERNEL__ */
#endif	/* _ASM_DMA_MAPPING_H */
