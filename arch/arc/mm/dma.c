// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/dma-map-ops.h>
#include <linux/dma-noncoherent.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>

/*
 * ARCH specific callbacks for generic noncoherent DMA ops
 *  - hardware IOC not available (or "dma-coherent" not set for device in DT)
 *  - But still handle both coherent and non-coherent requests from caller
 *
 * For DMA coherent hardware (IOC) generic code suffices
 */

void arch_dma_prep_coherent(struct page *page, size_t size)
{
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
	dma_cache_wback_inv(page_to_phys(page), size);
}

/*
 * Cache operations depending on function and direction argument, inspired by
 * https://lkml.org/lkml/2018/5/18/979
 * "dma_sync_*_for_cpu and direction=TO_DEVICE (was Re: [PATCH 02/20]
 * dma-mapping: provide a generic dma-noncoherent implementation)"
 *
 *          |   map          ==  for_device     |   unmap     ==  for_cpu
 *          |----------------------------------------------------------------
 * TO_DEV   |   writeback        writeback      |   none          none
 * FROM_DEV |   invalidate       invalidate     |   invalidate*   invalidate*
 * BIDIR    |   writeback+inv    writeback+inv  |   invalidate    invalidate
 *
 *     [*] needed for CPU speculative prefetches
 *
 * NOTE: we don't check the validity of direction argument as it is done in
 * upper layer functions (in include/linux/dma-mapping.h)
 */

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		dma_cache_wback(paddr, size);
		break;

	case DMA_FROM_DEVICE:
		dma_cache_inv(paddr, size);
		break;

	case DMA_BIDIRECTIONAL:
		dma_cache_wback_inv(paddr, size);
		break;

	default:
		break;
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		break;

	/* FROM_DEVICE invalidate needed if speculative CPU prefetch only */
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		dma_cache_inv(paddr, size);
		break;

	default:
		break;
	}
}

/*
 * Plug in direct dma map ops.
 */
void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent)
{
	/*
	 * IOC hardware snoops all DMA traffic keeping the caches consistent
	 * with memory - eliding need for any explicit cache maintenance of
	 * DMA buffers.
	 */
	if (is_isa_arcv2() && ioc_enable && coherent)
		dev->dma_coherent = true;

	dev_info(dev, "use %scoherent DMA ops\n",
		 dev->dma_coherent ? "" : "non");
}
