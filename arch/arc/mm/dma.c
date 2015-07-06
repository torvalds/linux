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
#include <linux/dma-debug.h>
#include <linux/export.h>
#include <asm/cacheflush.h>

/*
 * Helpers for Coherent DMA API.
 */
void *dma_alloc_noncoherent(struct device *dev, size_t size,
			    dma_addr_t *dma_handle, gfp_t gfp)
{
	void *paddr;

	/* This is linear addr (0x8000_0000 based) */
	paddr = alloc_pages_exact(size, gfp);
	if (!paddr)
		return NULL;

	/* This is bus address, platform dependent */
	*dma_handle = (dma_addr_t)paddr;

	return paddr;
}
EXPORT_SYMBOL(dma_alloc_noncoherent);

void dma_free_noncoherent(struct device *dev, size_t size, void *vaddr,
			  dma_addr_t dma_handle)
{
	free_pages_exact((void *)dma_handle, size);
}
EXPORT_SYMBOL(dma_free_noncoherent);

void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t gfp)
{
	void *paddr, *kvaddr;

	/* This is linear addr (0x8000_0000 based) */
	paddr = alloc_pages_exact(size, gfp);
	if (!paddr)
		return NULL;

	/* This is kernel Virtual address (0x7000_0000 based) */
	kvaddr = ioremap_nocache((unsigned long)paddr, size);
	if (kvaddr != NULL)
		memset(kvaddr, 0, size);

	/* This is bus address, platform dependent */
	*dma_handle = (dma_addr_t)paddr;

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
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *dev, size_t size, void *kvaddr,
		       dma_addr_t dma_handle)
{
	iounmap((void __force __iomem *)kvaddr);

	free_pages_exact((void *)dma_handle, size);
}
EXPORT_SYMBOL(dma_free_coherent);

/*
 * Helper for streaming DMA...
 */
void __arc_dma_cache_sync(unsigned long paddr, size_t size,
			  enum dma_data_direction dir)
{
	__inline_dma_cache_sync(paddr, size, dir);
}
EXPORT_SYMBOL(__arc_dma_cache_sync);
