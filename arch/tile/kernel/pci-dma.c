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
#include <linux/export.h>
#include <asm/tlbflush.h>
#include <asm/homecache.h>

/* Generic DMA mapping functions: */

/*
 * Allocate what Linux calls "coherent" memory.  On TILEPro this is
 * uncached memory; on TILE-Gx it is hash-for-home memory.
 */
#ifdef __tilepro__
#define PAGE_HOME_DMA PAGE_HOME_UNCACHED
#else
#define PAGE_HOME_DMA PAGE_HOME_HASH
#endif

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
	 * If the mask specifies that the memory be in the first 4 GB, then
	 * we force the allocation to come from the DMA zone.  We also
	 * force the node to 0 since that's the only node where the DMA
	 * zone isn't empty.  If the mask size is smaller than 32 bits, we
	 * may still not be able to guarantee a suitable memory address, in
	 * which case we will return NULL.  But such devices are uncommon.
	 */
	if (dma_mask <= DMA_BIT_MASK(32)) {
		gfp |= GFP_DMA;
		node = 0;
	}

	pg = homecache_alloc_pages_node(node, gfp, order, PAGE_HOME_DMA);
	if (pg == NULL)
		return NULL;

	addr = page_to_phys(pg);
	if (addr + size > dma_mask) {
		__homecache_free_pages(pg, order);
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

/* Set up a single page for DMA access. */
static void __dma_prep_page(struct page *page, unsigned long offset,
			    size_t size, enum dma_data_direction direction)
{
	/*
	 * Flush the page from cache if necessary.
	 * On tilegx, data is delivered to hash-for-home L3; on tilepro,
	 * data is delivered direct to memory.
	 *
	 * NOTE: If we were just doing DMA_TO_DEVICE we could optimize
	 * this to be a "flush" not a "finv" and keep some of the
	 * state in cache across the DMA operation, but it doesn't seem
	 * worth creating the necessary flush_buffer_xxx() infrastructure.
	 */
	int home = page_home(page);
	switch (home) {
	case PAGE_HOME_HASH:
#ifdef __tilegx__
		return;
#endif
		break;
	case PAGE_HOME_UNCACHED:
#ifdef __tilepro__
		return;
#endif
		break;
	case PAGE_HOME_IMMUTABLE:
		/* Should be going to the device only. */
		BUG_ON(direction == DMA_FROM_DEVICE ||
		       direction == DMA_BIDIRECTIONAL);
		return;
	case PAGE_HOME_INCOHERENT:
		/* Incoherent anyway, so no need to work hard here. */
		return;
	default:
		BUG_ON(home < 0 || home >= NR_CPUS);
		break;
	}
	homecache_finv_page(page);

#ifdef DEBUG_ALIGNMENT
	/* Warn if the region isn't cacheline aligned. */
	if (offset & (L2_CACHE_BYTES - 1) || (size & (L2_CACHE_BYTES - 1)))
		pr_warn("Unaligned DMA to non-hfh memory: PA %#llx/%#lx\n",
			PFN_PHYS(page_to_pfn(page)) + offset, size);
#endif
}

/* Make the page ready to be read by the core. */
static void __dma_complete_page(struct page *page, unsigned long offset,
				size_t size, enum dma_data_direction direction)
{
#ifdef __tilegx__
	switch (page_home(page)) {
	case PAGE_HOME_HASH:
		/* I/O device delivered data the way the cpu wanted it. */
		break;
	case PAGE_HOME_INCOHERENT:
		/* Incoherent anyway, so no need to work hard here. */
		break;
	case PAGE_HOME_IMMUTABLE:
		/* Extra read-only copies are not a problem. */
		break;
	default:
		/* Flush the bogus hash-for-home I/O entries to memory. */
		homecache_finv_map_page(page, PAGE_HOME_HASH);
		break;
	}
#endif
}

static void __dma_prep_pa_range(dma_addr_t dma_addr, size_t size,
				enum dma_data_direction direction)
{
	struct page *page = pfn_to_page(PFN_DOWN(dma_addr));
	unsigned long offset = dma_addr & (PAGE_SIZE - 1);
	size_t bytes = min(size, (size_t)(PAGE_SIZE - offset));

	while (size != 0) {
		__dma_prep_page(page, offset, bytes, direction);
		size -= bytes;
		++page;
		offset = 0;
		bytes = min((size_t)PAGE_SIZE, size);
	}
}

static void __dma_complete_pa_range(dma_addr_t dma_addr, size_t size,
				    enum dma_data_direction direction)
{
	struct page *page = pfn_to_page(PFN_DOWN(dma_addr));
	unsigned long offset = dma_addr & (PAGE_SIZE - 1);
	size_t bytes = min(size, (size_t)(PAGE_SIZE - offset));

	while (size != 0) {
		__dma_complete_page(page, offset, bytes, direction);
		size -= bytes;
		++page;
		offset = 0;
		bytes = min((size_t)PAGE_SIZE, size);
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

	__dma_prep_pa_range(dma_addr, size, direction);

	return dma_addr;
}
EXPORT_SYMBOL(dma_map_single);

void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		      enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	__dma_complete_pa_range(dma_addr, size, direction);
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
		__dma_prep_pa_range(sg->dma_address, sg->length, direction);
	}

	return nents;
}
EXPORT_SYMBOL(dma_map_sg);

void dma_unmap_sg(struct device *dev, struct scatterlist *sglist, int nents,
		  enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(!valid_dma_direction(direction));
	for_each_sg(sglist, sg, nents, i) {
		sg->dma_address = sg_phys(sg);
		__dma_complete_pa_range(sg->dma_address, sg->length,
					direction);
	}
}
EXPORT_SYMBOL(dma_unmap_sg);

dma_addr_t dma_map_page(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));

	BUG_ON(offset + size > PAGE_SIZE);
	__dma_prep_page(page, offset, size, direction);
	return page_to_pa(page) + offset;
}
EXPORT_SYMBOL(dma_map_page);

void dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
		    enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	__dma_complete_page(pfn_to_page(PFN_DOWN(dma_address)),
			    dma_address & PAGE_OFFSET, size, direction);
}
EXPORT_SYMBOL(dma_unmap_page);

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
			     size_t size, enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	__dma_complete_pa_range(dma_handle, size, direction);
}
EXPORT_SYMBOL(dma_sync_single_for_cpu);

void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				size_t size, enum dma_data_direction direction)
{
	__dma_prep_pa_range(dma_handle, size, direction);
}
EXPORT_SYMBOL(dma_sync_single_for_device);

void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sglist,
			 int nelems, enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(nelems == 0 || sglist->length == 0);

	for_each_sg(sglist, sg, nelems, i) {
		dma_sync_single_for_cpu(dev, sg->dma_address,
					sg_dma_len(sg), direction);
	}
}
EXPORT_SYMBOL(dma_sync_sg_for_cpu);

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
 * dma_alloc_noncoherent() is #defined to return coherent memory,
 * so there's no need to do any flushing here.
 */
void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		    enum dma_data_direction direction)
{
}
EXPORT_SYMBOL(dma_cache_sync);
