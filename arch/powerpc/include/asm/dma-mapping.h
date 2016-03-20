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
#include <linux/dma-attrs.h>
#include <linux/dma-debug.h>
#include <asm/io.h>
#include <asm/swiotlb.h>

#ifdef CONFIG_PPC64
#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)
#endif

/* Some dma direct funcs must be visible for use in other dma_ops */
extern void *__dma_direct_alloc_coherent(struct device *dev, size_t size,
					 dma_addr_t *dma_handle, gfp_t flag,
					 struct dma_attrs *attrs);
extern void __dma_direct_free_coherent(struct device *dev, size_t size,
				       void *vaddr, dma_addr_t dma_handle,
				       struct dma_attrs *attrs);
extern int dma_direct_mmap_coherent(struct device *dev,
				    struct vm_area_struct *vma,
				    void *cpu_addr, dma_addr_t handle,
				    size_t size, struct dma_attrs *attrs);

#ifdef CONFIG_NOT_COHERENT_CACHE
/*
 * DMA-consistent mapping functions for PowerPCs that don't support
 * cache snooping.  These allocate/free a region of uncached mapped
 * memory space for use with DMA devices.  Alternatively, you could
 * allocate the space "normally" and use the cache management functions
 * to ensure it is consistent.
 */
struct device;
extern void *__dma_alloc_coherent(struct device *dev, size_t size,
				  dma_addr_t *handle, gfp_t gfp);
extern void __dma_free_coherent(size_t size, void *vaddr);
extern void __dma_sync(void *vaddr, size_t size, int direction);
extern void __dma_sync_page(struct page *page, unsigned long offset,
				 size_t size, int direction);
extern unsigned long __dma_get_coherent_pfn(unsigned long cpu_addr);

#else /* ! CONFIG_NOT_COHERENT_CACHE */
/*
 * Cache coherent cores.
 */

#define __dma_alloc_coherent(dev, gfp, size, handle)	NULL
#define __dma_free_coherent(size, addr)		((void)0)
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
extern struct dma_map_ops dma_direct_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
{
	/* We don't handle the NULL dev case for ISA for now. We could
	 * do it via an out of line call but it is not needed for now. The
	 * only ISA DMA device we support is the floppy and we have a hack
	 * in the floppy driver directly to get a device for us.
	 */
	if (unlikely(dev == NULL))
		return NULL;

	return dev->archdata.dma_ops;
}

static inline void set_dma_ops(struct device *dev, struct dma_map_ops *ops)
{
	dev->archdata.dma_ops = ops;
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

/* this will be removed soon */
#define flush_write_buffers()

#define HAVE_ARCH_DMA_SET_MASK 1
extern int dma_set_mask(struct device *dev, u64 dma_mask);

extern int __dma_set_mask(struct device *dev, u64 dma_mask);
extern u64 __dma_get_required_mask(struct device *dev);

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
#ifdef CONFIG_SWIOTLB
	struct dev_archdata *sd = &dev->archdata;

	if (sd->max_direct_dma_addr && addr + size > sd->max_direct_dma_addr)
		return false;
#endif

	if (!dev->dma_mask)
		return false;

	return addr + size - 1 <= *dev->dma_mask;
}

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return paddr + get_dma_offset(dev);
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return daddr - get_dma_offset(dev);
}

#define ARCH_HAS_DMA_MMAP_COHERENT

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	__dma_sync(vaddr, size, (int)direction);
}

#endif /* __KERNEL__ */
#endif	/* _ASM_DMA_MAPPING_H */
