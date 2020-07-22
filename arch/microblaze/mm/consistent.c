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
#include <linux/dma-noncoherent.h>
#include <asm/cpuinfo.h>
#include <asm/cacheflush.h>

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	phys_addr_t paddr = page_to_phys(page);

	flush_dcache_range(paddr, paddr + size);
}

#ifndef CONFIG_MMU
/*
 * Consistent memory allocators. Used for DMA devices that want to share
 * uncached memory with the processor core.  My crufty no-MMU approach is
 * simple.  In the HW platform we can optionally mirror the DDR up above the
 * processor cacheable region.  So, memory accessed in this mirror region will
 * not be cached.  It's alloced from the same pool as normal memory, but the
 * handle we return is shifted up into the uncached region.  This will no doubt
 * cause big problems if memory allocated here is not also freed properly. -- JW
 *
 * I have to use dcache values because I can't relate on ram size:
 */
#ifdef CONFIG_XILINX_UNCACHED_SHADOW
#define UNCACHED_SHADOW_MASK (cpuinfo.dcache_high - cpuinfo.dcache_base + 1)
#else
#define UNCACHED_SHADOW_MASK 0
#endif /* CONFIG_XILINX_UNCACHED_SHADOW */

void *arch_dma_set_uncached(void *ptr, size_t size)
{
	unsigned long addr = (unsigned long)ptr;

	addr |= UNCACHED_SHADOW_MASK;
	if (addr > cpuinfo.dcache_base && addr < cpuinfo.dcache_high)
		pr_warn("ERROR: Your cache coherent area is CACHED!!!\n");
	return (void *)addr;
}
#endif /* CONFIG_MMU */
