/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *  Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 *
 * Based on DMA code from MIPS.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/cache.h>
#include <asm/cacheflush.h>

static inline void __dma_sync_for_device(void *vaddr, size_t size,
			      enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_FROM_DEVICE:
		invalidate_dcache_range((unsigned long)vaddr,
			(unsigned long)(vaddr + size));
		break;
	case DMA_TO_DEVICE:
		/*
		 * We just need to flush the caches here , but Nios2 flush
		 * instruction will do both writeback and invalidate.
		 */
	case DMA_BIDIRECTIONAL: /* flush and invalidate */
		flush_dcache_range((unsigned long)vaddr,
			(unsigned long)(vaddr + size));
		break;
	default:
		BUG();
	}
}

static inline void __dma_sync_for_cpu(void *vaddr, size_t size,
			      enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_BIDIRECTIONAL:
	case DMA_FROM_DEVICE:
		invalidate_dcache_range((unsigned long)vaddr,
			(unsigned long)(vaddr + size));
		break;
	case DMA_TO_DEVICE:
		break;
	default:
		BUG();
	}
}

static void *nios2_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	void *ret;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	/* optimized page clearing */
	gfp |= __GFP_ZERO;

	if (dev == NULL || (dev->coherent_dma_mask < 0xffffffff))
		gfp |= GFP_DMA;

	ret = (void *) __get_free_pages(gfp, get_order(size));
	if (ret != NULL) {
		*dma_handle = virt_to_phys(ret);
		flush_dcache_range((unsigned long) ret,
			(unsigned long) ret + size);
		ret = UNCAC_ADDR(ret);
	}

	return ret;
}

static void nios2_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	unsigned long addr = (unsigned long) CAC_ADDR((unsigned long) vaddr);

	free_pages(addr, get_order(size));
}

static int nios2_dma_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction direction,
		unsigned long attrs)
{
	int i;

	for_each_sg(sg, sg, nents, i) {
		void *addr;

		addr = sg_virt(sg);
		if (addr) {
			__dma_sync_for_device(addr, sg->length, direction);
			sg->dma_address = sg_phys(sg);
		}
	}

	return nents;
}

static dma_addr_t nios2_dma_map_page(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction direction,
			unsigned long attrs)
{
	void *addr = page_address(page) + offset;

	__dma_sync_for_device(addr, size, direction);
	return page_to_phys(page) + offset;
}

static void nios2_dma_unmap_page(struct device *dev, dma_addr_t dma_address,
		size_t size, enum dma_data_direction direction,
		unsigned long attrs)
{
	__dma_sync_for_cpu(phys_to_virt(dma_address), size, direction);
}

static void nios2_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
		int nhwentries, enum dma_data_direction direction,
		unsigned long attrs)
{
	void *addr;
	int i;

	if (direction == DMA_TO_DEVICE)
		return;

	for_each_sg(sg, sg, nhwentries, i) {
		addr = sg_virt(sg);
		if (addr)
			__dma_sync_for_cpu(addr, sg->length, direction);
	}
}

static void nios2_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	__dma_sync_for_cpu(phys_to_virt(dma_handle), size, direction);
}

static void nios2_dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	__dma_sync_for_device(phys_to_virt(dma_handle), size, direction);
}

static void nios2_dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sg, int nelems,
		enum dma_data_direction direction)
{
	int i;

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for_each_sg(sg, sg, nelems, i)
		__dma_sync_for_cpu(sg_virt(sg), sg->length, direction);
}

static void nios2_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sg, int nelems,
		enum dma_data_direction direction)
{
	int i;

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for_each_sg(sg, sg, nelems, i)
		__dma_sync_for_device(sg_virt(sg), sg->length, direction);

}

struct dma_map_ops nios2_dma_ops = {
	.alloc			= nios2_dma_alloc,
	.free			= nios2_dma_free,
	.map_page		= nios2_dma_map_page,
	.unmap_page		= nios2_dma_unmap_page,
	.map_sg			= nios2_dma_map_sg,
	.unmap_sg		= nios2_dma_unmap_sg,
	.sync_single_for_device	= nios2_dma_sync_single_for_device,
	.sync_single_for_cpu	= nios2_dma_sync_single_for_cpu,
	.sync_sg_for_cpu	= nios2_dma_sync_sg_for_cpu,
	.sync_sg_for_device	= nios2_dma_sync_sg_for_device,
};
EXPORT_SYMBOL(nios2_dma_ops);
