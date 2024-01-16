// SPDX-License-Identifier: GPL-2.0-only
/*
 * DMA implementation for Hexagon
 *
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-map-ops.h>
#include <linux/memblock.h>
#include <asm/page.h>

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	void *addr = phys_to_virt(paddr);

	switch (dir) {
	case DMA_TO_DEVICE:
		hexagon_clean_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	case DMA_FROM_DEVICE:
		hexagon_inv_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	case DMA_BIDIRECTIONAL:
		flush_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	default:
		BUG();
	}
}

/*
 * Our max_low_pfn should have been backed off by 16MB in mm/init.c to create
 * DMA coherent space.  Use that for the pool.
 */
static int __init hexagon_dma_init(void)
{
	return dma_init_global_coherent(PFN_PHYS(max_low_pfn),
					hexagon_coherent_pool_size);
}
core_initcall(hexagon_dma_init);
