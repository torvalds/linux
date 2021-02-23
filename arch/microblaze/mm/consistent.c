// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microblaze support for cache consistent memory.
 * Copyright (C) 2010 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2010 PetaLogix
 * Copyright (C) 2005 John Williams <jwilliams@itee.uq.edu.au>
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/dma-map-ops.h>
#include <asm/cpuinfo.h>
#include <asm/cacheflush.h>

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	phys_addr_t paddr = page_to_phys(page);

	flush_dcache_range(paddr, paddr + size);
}
