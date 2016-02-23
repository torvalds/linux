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
 * implemented by accessintg it using a kernel virtual address, with
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
	void *paddr, *kvaddr;

	/* This is linear addr (0x8000_0000 based) */
	paddr = alloc_pages_exact(size, gfp);
	if (!paddr)
		return NULL;

	/* This is bus address, platform dependent */
	*dma_handle = (dma_addr_t)paddr;

	/*
	 * IOC relies on all data (even coherent DMA data) being in cache
	 * Thus allocate normal cached memory
	 *
	 * The gains with IOC are two pronged:
	 *   -For streaming data, elides needs for cache maintenance, saving
	 *    cycles in flush code, and bus bandwidth as all the lines of a
	 *    buffer need to be flushed out to memory
	 *   -For coherent data, Read/Write to buffers terminate early in cache
	 *   (vs. always going to memory - thus are faster)
	 */
	if ((is_isa_arcv2() && ioc_exists) ||
	    dma_get_attr(DMA_ATTR_NON_CONSISTENT, attrs))
		return paddr;

	/* This is kernel Virtual address (0x7000_0000 based) */
	kvaddr = ioremap_nocache((unsigned long)paddr, size);
	if (kvaddr == NULL)
		return NULL;

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
	dma_cache_wback_inv((unsigned long)paddr, size);

	return kvaddr;
}

static void arc_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, struct dma_attrs *attrs)
{
	if (!dma_get_attr(DMA_ATTR_NON_CONSISTENT, attrs) &&
	    !(is_isa_arcv2() && ioc_exists))
		iounmap((void __force __iomem *)vaddr);

	free_pages_exact((void *)dma_handle, size);
}

/*
 * streaming DMA Mapping API...
 * CPU accesses page via normal paddr, thus needs to explicitly made
 * consistent before each use
 */
static void _dma_cache_sync(unsigned long paddr, size_t size,
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
		pr_err("Invalid DMA dir [%d] for OP @ %lx\n", dir, paddr);
	}
}

static dma_addr_t arc_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	unsigned long paddr = page_to_phys(page) + offset;
	_dma_cache_sync(paddr, size, dir);
	return (dma_addr_t)paddr;
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
	_dma_cache_sync(dma_handle, size, DMA_FROM_DEVICE);
}

static void arc_dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size, enum dma_data_direction dir)
{
	_dma_cache_sync(dma_handle, size, DMA_TO_DEVICE);
}

static void arc_dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sglist, int nelems,
		enum dma_data_direction dir)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nelems, i)
		_dma_cache_sync((unsigned int)sg_virt(sg), sg->length, dir);
}

static void arc_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sglist, int nelems,
		enum dma_data_direction dir)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nelems, i)
		_dma_cache_sync((unsigned int)sg_virt(sg), sg->length, dir);
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
