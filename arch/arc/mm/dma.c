/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * DMA Coherent API Notes
 *
 * I/O is inherently non-coherent on ARC. So a coherent DMA buffer is
 * implemented by accessing it using a kernel virtual address, with
 * Cache bit off in the TLB entry.
 *
 * The default DMA address == Phy address which is 0x8000_0000 based.
 */

#include <linux/dma-mapping.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>


static void *arc_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, struct dma_attrs *attrs)
{
	unsigned long order = get_order(size);
	struct page *page;
	phys_addr_t paddr;
	void *kvaddr;
	int need_coh = 1, need_kvaddr = 0;

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;

	/*
	 * IOC relies on all data (even coherent DMA data) being in cache
	 * Thus allocate normal cached memory
	 *
	 * The gains with IOC are two pronged:
	 *   -For streaming data, elides need for cache maintenance, saving
	 *    cycles in flush code, and bus bandwidth as all the lines of a
	 *    buffer need to be flushed out to memory
	 *   -For coherent data, Read/Write to buffers terminate early in cache
	 *   (vs. always going to memory - thus are faster)
	 */
	if ((is_isa_arcv2() && ioc_exists) ||
	    dma_get_attr(DMA_ATTR_NON_CONSISTENT, attrs))
		need_coh = 0;

	/*
	 * - A coherent buffer needs MMU mapping to enforce non-cachability
	 * - A highmem page needs a virtual handle (hence MMU mapping)
	 *   independent of cachability
	 */
	if (PageHighMem(page) || need_coh)
		need_kvaddr = 1;

	/* This is linear addr (0x8000_0000 based) */
	paddr = page_to_phys(page);

	*dma_handle = plat_phys_to_dma(dev, paddr);

	/* This is kernel Virtual address (0x7000_0000 based) */
	if (need_kvaddr) {
		kvaddr = ioremap_nocache(paddr, size);
		if (kvaddr == NULL) {
			__free_pages(page, order);
			return NULL;
		}
	} else {
		kvaddr = (void *)(u32)paddr;
	}

	/*
	 * Evict any existing L1 and/or L2 lines for the backing page
	 * in case it was used earlier as a normal "cached" page.
	 * Yeah this bit us - STAR 9000898266
	 *
	 * Although core does call flush_cache_vmap(), it gets kvaddr hence
	 * can't be used to efficiently flush L1 and/or L2 which need paddr
	 * Currently flush_cache_vmap nukes the L1 cache completely which
	 * will be optimized as a separate commit
	 */
	if (need_coh)
		dma_cache_wback_inv(paddr, size);

	return kvaddr;
}

static void arc_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, struct dma_attrs *attrs)
{
	struct page *page = virt_to_page(dma_handle);
	int is_non_coh = 1;

	is_non_coh = dma_get_attr(DMA_ATTR_NON_CONSISTENT, attrs) ||
			(is_isa_arcv2() && ioc_exists);

	if (PageHighMem(page) || !is_non_coh)
		iounmap((void __force __iomem *)vaddr);

	__free_pages(page, get_order(size));
}

/*
 * streaming DMA Mapping API...
 * CPU accesses page via normal paddr, thus needs to explicitly made
 * consistent before each use
 */
static void _dma_cache_sync(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_FROM_DEVICE:
		dma_cache_inv(paddr, size);
		break;
	case DMA_TO_DEVICE:
		dma_cache_wback(paddr, size);
		break;
	case DMA_BIDIRECTIONAL:
		dma_cache_wback_inv(paddr, size);
		break;
	default:
		pr_err("Invalid DMA dir [%d] for OP @ %pa[p]\n", dir, &paddr);
	}
}

static dma_addr_t arc_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	phys_addr_t paddr = page_to_phys(page) + offset;
	_dma_cache_sync(paddr, size, dir);
	return plat_phys_to_dma(dev, paddr);
}

static int arc_dma_map_sg(struct device *dev, struct scatterlist *sg,
	   int nents, enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		s->dma_address = dma_map_page(dev, sg_page(s), s->offset,
					       s->length, dir);

	return nents;
}

static void arc_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t dma_handle, size_t size, enum dma_data_direction dir)
{
	_dma_cache_sync(plat_dma_to_phys(dev, dma_handle), size, DMA_FROM_DEVICE);
}

static void arc_dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size, enum dma_data_direction dir)
{
	_dma_cache_sync(plat_dma_to_phys(dev, dma_handle), size, DMA_TO_DEVICE);
}

static void arc_dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sglist, int nelems,
		enum dma_data_direction dir)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nelems, i)
		_dma_cache_sync(sg_phys(sg), sg->length, dir);
}

static void arc_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sglist, int nelems,
		enum dma_data_direction dir)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nelems, i)
		_dma_cache_sync(sg_phys(sg), sg->length, dir);
}

static int arc_dma_supported(struct device *dev, u64 dma_mask)
{
	/* Support 32 bit DMA mask exclusively */
	return dma_mask == DMA_BIT_MASK(32);
}

struct dma_map_ops arc_dma_ops = {
	.alloc			= arc_dma_alloc,
	.free			= arc_dma_free,
	.map_page		= arc_dma_map_page,
	.map_sg			= arc_dma_map_sg,
	.sync_single_for_device	= arc_dma_sync_single_for_device,
	.sync_single_for_cpu	= arc_dma_sync_single_for_cpu,
	.sync_sg_for_cpu	= arc_dma_sync_sg_for_cpu,
	.sync_sg_for_device	= arc_dma_sync_sg_for_device,
	.dma_supported		= arc_dma_supported,
};
EXPORT_SYMBOL(arc_dma_ops);
