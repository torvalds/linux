/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <asm/tlbflush.h>
#include <asm/homecache.h>

/* Generic DMA mapping functions: */

/*
 * Allocate what Linux calls "coherent" memory, which for us just
 * means uncached.
 */
void *dma_alloc_coherent(struct device *dev,
			 size_t size,
			 dma_addr_t *dma_handle,
			 gfp_t gfp)
{
	u64 dma_mask = dev->coherent_dma_mask ?: DMA_BIT_MASK(32);
	int node = dev_to_node(dev);
	int order = get_order(size);
	struct page *pg;
	dma_addr_t addr;

	gfp |= __GFP_ZERO;

	/*
	 * By forcing NUMA node 0 for 32-bit masks we ensure that the
	 * high 32 bits of the resulting PA will be zero.  If the mask
	 * size is, e.g., 24, we may still not be able to guarantee a
	 * suitable memory address, in which case we will return NULL.
	 * But such devices are uncommon.
	 */
	if (dma_mask <= DMA_BIT_MASK(32))
		node = 0;

	pg = homecache_alloc_pages_node(node, gfp, order, PAGE_HOME_UNCACHED);
	if (pg == NULL)
		return NULL;

	addr = page_to_phys(pg);
	if (addr + size > dma_mask) {
		homecache_free_pages(addr, order);
		return NULL;
	}

	*dma_handle = addr;
	return page_address(pg);
}
EXPORT_SYMBOL(dma_alloc_coherent);

/*
 * Free memory that was allocated with dma_alloc_coherent.
 */
void dma_free_coherent(struct device *dev, size_t size,
		  void *vaddr, dma_addr_t dma_handle)
{
	homecache_free_pages((unsigned long)vaddr, get_order(size));
}
EXPORT_SYMBOL(dma_free_coherent);

/*
 * The map routines "map" the specified address range for DMA
 * accesses.  The memory belongs to the device after this call is
 * issued, until it is unmapped with dma_unmap_single.
 *
 * We don't need to do any mapping, we just flush the address range
 * out of the cache and return a DMA address.
 *
 * The unmap routines do whatever is necessary before the processor
 * accesses the memory again, and must be called before the driver
 * touches the memory.  We can get away with a cache invalidate if we
 * can count on nothing having been touched.
 */

/* Flush a PA range from cache page by page. */
static void __dma_map_pa_range(dma_addr_t dma_addr, size_t size)
{
	struct page *page = pfn_to_page(PFN_DOWN(dma_addr));
	size_t bytesleft = PAGE_SIZE - (dma_addr & (PAGE_SIZE - 1));

	while ((ssize_t)size > 0) {
		/* Flush the page. */
		homecache_flush_cache(page++, 0);

		/* Figure out if we need to continue on the next page. */
		size -= bytesleft;
		bytesleft = PAGE_SIZE;
	}
}

/*
 * dma_map_single can be passed any memory address, and there appear
 * to be no alignment constraints.
 *
 * There is a chance that the start of the buffer will share a cache
 * line with some other data that has been touched in the meantime.
 */
dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
	       enum dma_data_direction direction)
{
	dma_addr_t dma_addr = __pa(ptr);

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(size == 0);

	__dma_map_pa_range(dma_addr, size);

	return dma_addr;
}
EXPORT_SYMBOL(dma_map_single);

void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}
EXPORT_SYMBOL(dma_unmap_single);

int dma_map_sg(struct device *dev, struct scatterlist *sglist, int nents,
	   enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(!valid_dma_direction(direction));

	WARN_ON(nents == 0 || sglist->length == 0);

	for_each_sg(sglist, sg, nents, i) {
		sg->dma_address = sg_phys(sg);
		__dma_map_pa_range(sg->dma_address, sg->length);
	}

	return nents;
}
EXPORT_SYMBOL(dma_map_sg);

void dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	     enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}
EXPORT_SYMBOL(dma_unmap_sg);

dma_addr_t dma_map_page(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));

	BUG_ON(offset + size > PAGE_SIZE);
	homecache_flush_cache(page, 0);

	return page_to_pa(page) + offset;
}
EXPORT_SYMBOL(dma_map_page);

void dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}
EXPORT_SYMBOL(dma_unmap_page);

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
			     size_t size, enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}
EXPORT_SYMBOL(dma_sync_single_for_cpu);

void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				size_t size, enum dma_data_direction direction)
{
	unsigned long start = PFN_DOWN(dma_handle);
	unsigned long end = PFN_DOWN(dma_handle + size - 1);
	unsigned long i;

	BUG_ON(!valid_dma_direction(direction));
	for (i = start; i <= end; ++i)
		homecache_flush_cache(pfn_to_page(i), 0);
}
EXPORT_SYMBOL(dma_sync_single_for_device);

void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
		    enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(nelems == 0 || sg[0].length == 0);
}
EXPORT_SYMBOL(dma_sync_sg_for_cpu);

/*
 * Flush and invalidate cache for scatterlist.
 */
void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sglist,
			    int nelems, enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(nelems == 0 || sglist->length == 0);

	for_each_sg(sglist, sg, nelems, i) {
		dma_sync_single_for_device(dev, sg->dma_address,
					   sg_dma_len(sg), direction);
	}
}
EXPORT_SYMBOL(dma_sync_sg_for_device);

void dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
				   unsigned long offset, size_t size,
				   enum dma_data_direction direction)
{
	dma_sync_single_for_cpu(dev, dma_handle + offset, size, direction);
}
EXPORT_SYMBOL(dma_sync_single_range_for_cpu);

void dma_sync_single_range_for_device(struct device *dev,
				      dma_addr_t dma_handle,
				      unsigned long offset, size_t size,
				      enum dma_data_direction direction)
{
	dma_sync_single_for_device(dev, dma_handle + offset, size, direction);
}
EXPORT_SYMBOL(dma_sync_single_range_for_device);

/*
 * dma_alloc_noncoherent() returns non-cacheable memory, so there's no
 * need to do any flushing here.
 */
void dma_cache_sync(void *vaddr, size_t size,
		    enum dma_data_direction direction)
{
}
EXPORT_SYMBOL(dma_cache_sync);
