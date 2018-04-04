// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009-2010 PetaLogix
 * Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corporation
 *
 * Provide default implementations of the DMA mapping callbacks for
 * directly mapped busses.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/dma-debug.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <asm/cacheflush.h>

static void *dma_nommu_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t flag,
				       unsigned long attrs)
{
	return consistent_alloc(flag, size, dma_handle);
}

static void dma_nommu_free_coherent(struct device *dev, size_t size,
				     void *vaddr, dma_addr_t dma_handle,
				     unsigned long attrs)
{
	consistent_free(size, vaddr);
}

static inline void __dma_sync(unsigned long paddr,
			      size_t size, enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_TO_DEVICE:
	case DMA_BIDIRECTIONAL:
		flush_dcache_range(paddr, paddr + size);
		break;
	case DMA_FROM_DEVICE:
		invalidate_dcache_range(paddr, paddr + size);
		break;
	default:
		BUG();
	}
}

static int dma_nommu_map_sg(struct device *dev, struct scatterlist *sgl,
			     int nents, enum dma_data_direction direction,
			     unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	/* FIXME this part of code is untested */
	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = sg_phys(sg);

		if (attrs & DMA_ATTR_SKIP_CPU_SYNC)
			continue;

		__dma_sync(sg_phys(sg), sg->length, direction);
	}

	return nents;
}

static inline dma_addr_t dma_nommu_map_page(struct device *dev,
					     struct page *page,
					     unsigned long offset,
					     size_t size,
					     enum dma_data_direction direction,
					     unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		__dma_sync(page_to_phys(page) + offset, size, direction);
	return page_to_phys(page) + offset;
}

static inline void dma_nommu_unmap_page(struct device *dev,
					 dma_addr_t dma_address,
					 size_t size,
					 enum dma_data_direction direction,
					 unsigned long attrs)
{
/* There is not necessary to do cache cleanup
 *
 * phys_to_virt is here because in __dma_sync_page is __virt_to_phys and
 * dma_address is physical address
 */
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		__dma_sync(dma_address, size, direction);
}

static inline void
dma_nommu_sync_single_for_cpu(struct device *dev,
			       dma_addr_t dma_handle, size_t size,
			       enum dma_data_direction direction)
{
	/*
	 * It's pointless to flush the cache as the memory segment
	 * is given to the CPU
	 */

	if (direction == DMA_FROM_DEVICE)
		__dma_sync(dma_handle, size, direction);
}

static inline void
dma_nommu_sync_single_for_device(struct device *dev,
				  dma_addr_t dma_handle, size_t size,
				  enum dma_data_direction direction)
{
	/*
	 * It's pointless to invalidate the cache if the device isn't
	 * supposed to write to the relevant region
	 */

	if (direction == DMA_TO_DEVICE)
		__dma_sync(dma_handle, size, direction);
}

static inline void
dma_nommu_sync_sg_for_cpu(struct device *dev,
			   struct scatterlist *sgl, int nents,
			   enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	/* FIXME this part of code is untested */
	if (direction == DMA_FROM_DEVICE)
		for_each_sg(sgl, sg, nents, i)
			__dma_sync(sg->dma_address, sg->length, direction);
}

static inline void
dma_nommu_sync_sg_for_device(struct device *dev,
			      struct scatterlist *sgl, int nents,
			      enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	/* FIXME this part of code is untested */
	if (direction == DMA_TO_DEVICE)
		for_each_sg(sgl, sg, nents, i)
			__dma_sync(sg->dma_address, sg->length, direction);
}

static
int dma_nommu_mmap_coherent(struct device *dev, struct vm_area_struct *vma,
			     void *cpu_addr, dma_addr_t handle, size_t size,
			     unsigned long attrs)
{
#ifdef CONFIG_MMU
	unsigned long user_count = vma_pages(vma);
	unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long off = vma->vm_pgoff;
	unsigned long pfn;

	if (off >= count || user_count > (count - off))
		return -ENXIO;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	pfn = consistent_virt_to_pfn(cpu_addr);
	return remap_pfn_range(vma, vma->vm_start, pfn + off,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
#else
	return -ENXIO;
#endif
}

const struct dma_map_ops dma_nommu_ops = {
	.alloc			= dma_nommu_alloc_coherent,
	.free			= dma_nommu_free_coherent,
	.mmap			= dma_nommu_mmap_coherent,
	.map_sg			= dma_nommu_map_sg,
	.map_page		= dma_nommu_map_page,
	.unmap_page		= dma_nommu_unmap_page,
	.sync_single_for_cpu	= dma_nommu_sync_single_for_cpu,
	.sync_single_for_device	= dma_nommu_sync_single_for_device,
	.sync_sg_for_cpu	= dma_nommu_sync_sg_for_cpu,
	.sync_sg_for_device	= dma_nommu_sync_sg_for_device,
};
EXPORT_SYMBOL(dma_nommu_ops);

/* Number of entries preallocated for DMA-API debugging */
#define PREALLOC_DMA_DEBUG_ENTRIES (1 << 16)

static int __init dma_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);

	return 0;
}
fs_initcall(dma_init);
