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


void *dma_alloc_coherent(struct device *dev, size_t size,
			    dma_addr_t *dma_handle, gfp_t gfp)
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
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *dev, size_t size, void *vaddr,
			dma_addr_t dma_handle)
{
	unsigned long addr = (unsigned long) CAC_ADDR((unsigned long) vaddr);
	free_pages(addr, get_order(size));
}
EXPORT_SYMBOL(dma_free_coherent);

int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction direction)
{
	int i;

	BUG_ON(!valid_dma_direction(direction));

	for_each_sg(sg, sg, nents, i) {
		void *addr;

		addr = sg_virt(sg);
		if (addr) {
			__dma_sync(addr, sg->length, direction);
			sg->dma_address = sg_phys(sg);
		}
	}

	return nents;
}
EXPORT_SYMBOL(dma_map_sg);

dma_addr_t dma_map_page(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction direction)
{
	void *addr;

	BUG_ON(!valid_dma_direction(direction));

	addr = page_address(page) + offset;
	__dma_sync(addr, size, direction);

	return page_to_phys(page) + offset;
}
EXPORT_SYMBOL(dma_map_page);

void dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
		    enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));

	if (direction != DMA_TO_DEVICE)
		__dma_sync(phys_to_virt(dma_address), size, direction);
}
EXPORT_SYMBOL(dma_unmap_page);

void dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
		  enum dma_data_direction direction)
{
	void *addr;
	int i;

	BUG_ON(!valid_dma_direction(direction));

	if (direction == DMA_TO_DEVICE)
		return;

	for_each_sg(sg, sg, nhwentries, i) {
		addr = sg_virt(sg);
		if (addr)
			__dma_sync(addr, sg->length, direction);
	}
}
EXPORT_SYMBOL(dma_unmap_sg);

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
			     size_t size, enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));

	__dma_sync(phys_to_virt(dma_handle), size, direction);
}
EXPORT_SYMBOL(dma_sync_single_for_cpu);

void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				size_t size, enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));

	__dma_sync(phys_to_virt(dma_handle), size, direction);
}
EXPORT_SYMBOL(dma_sync_single_for_device);

void dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
					unsigned long offset, size_t size,
					enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));

	__dma_sync(phys_to_virt(dma_handle), size, direction);
}
EXPORT_SYMBOL(dma_sync_single_range_for_cpu);

void dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
					unsigned long offset, size_t size,
					enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));

	__dma_sync(phys_to_virt(dma_handle), size, direction);
}
EXPORT_SYMBOL(dma_sync_single_range_for_device);

void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
			 enum dma_data_direction direction)
{
	int i;

	BUG_ON(!valid_dma_direction(direction));

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for_each_sg(sg, sg, nelems, i)
		__dma_sync(sg_virt(sg), sg->length, direction);
}
EXPORT_SYMBOL(dma_sync_sg_for_cpu);

void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
				int nelems, enum dma_data_direction direction)
{
	int i;

	BUG_ON(!valid_dma_direction(direction));

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for_each_sg(sg, sg, nelems, i)
		__dma_sync(sg_virt(sg), sg->length, direction);

}
EXPORT_SYMBOL(dma_sync_sg_for_device);
