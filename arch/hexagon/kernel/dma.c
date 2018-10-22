/*
 * DMA implementation for Hexagon
 *
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/dma-noncoherent.h>
#include <linux/bootmem.h>
#include <linux/genalloc.h>
#include <linux/module.h>
#include <asm/page.h>

static struct gen_pool *coherent_pool;


/* Allocates from a pool of uncached memory that was reserved at boot time */

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_addr,
		gfp_t flag, unsigned long attrs)
{
	void *ret;

	/*
	 * Our max_low_pfn should have been backed off by 16MB in
	 * mm/init.c to create DMA coherent space.  Use that as the VA
	 * for the pool.
	 */

	if (coherent_pool == NULL) {
		coherent_pool = gen_pool_create(PAGE_SHIFT, -1);

		if (coherent_pool == NULL)
			panic("Can't create %s() memory pool!", __func__);
		else
			gen_pool_add(coherent_pool,
				(unsigned long)pfn_to_virt(max_low_pfn),
				hexagon_coherent_pool_size, -1);
	}

	ret = (void *) gen_pool_alloc(coherent_pool, size);

	if (ret) {
		memset(ret, 0, size);
		*dma_addr = (dma_addr_t) virt_to_phys(ret);
	} else
		*dma_addr = ~0;

	return ret;
}

void arch_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_addr, unsigned long attrs)
{
	gen_pool_free(coherent_pool, (unsigned long) vaddr, size);
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
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
