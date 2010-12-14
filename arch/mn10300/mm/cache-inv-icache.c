/* Invalidate icache when dcache doesn't need invalidation as it's in
 * write-through mode
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/smp.h>
#include "cache-smp.h"

/**
 * flush_icache_page_range - Flush dcache and invalidate icache for part of a
 *				single page
 * @start: The starting virtual address of the page part.
 * @end: The ending virtual address of the page part.
 *
 * Invalidate the icache for part of a single page, as determined by the
 * virtual addresses given.  The page must be in the paged area.  The dcache is
 * not flushed as the cache must be in write-through mode to get here.
 */
static void flush_icache_page_range(unsigned long start, unsigned long end)
{
	unsigned long addr, size, off;
	struct page *page;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ppte, pte;

	/* work out how much of the page to flush */
	off = start & ~PAGE_MASK;
	size = end - start;

	/* get the physical address the page is mapped to from the page
	 * tables */
	pgd = pgd_offset(current->mm, start);
	if (!pgd || !pgd_val(*pgd))
		return;

	pud = pud_offset(pgd, start);
	if (!pud || !pud_val(*pud))
		return;

	pmd = pmd_offset(pud, start);
	if (!pmd || !pmd_val(*pmd))
		return;

	ppte = pte_offset_map(pmd, start);
	if (!ppte)
		return;
	pte = *ppte;
	pte_unmap(ppte);

	if (pte_none(pte))
		return;

	page = pte_page(pte);
	if (!page)
		return;

	addr = page_to_phys(page);

	/* invalidate the icache coverage on that region */
	mn10300_local_icache_inv_range2(addr + off, size);
	smp_cache_call(SMP_ICACHE_INV_FLUSH_RANGE, start, end);
}

/**
 * flush_icache_range - Globally flush dcache and invalidate icache for region
 * @start: The starting virtual address of the region.
 * @end: The ending virtual address of the region.
 *
 * This is used by the kernel to globally flush some code it has just written
 * from the dcache back to RAM and then to globally invalidate the icache over
 * that region so that that code can be run on all CPUs in the system.
 */
void flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long start_page, end_page;
	unsigned long flags;

	flags = smp_lock_cache();

	if (end > 0x80000000UL) {
		/* addresses above 0xa0000000 do not go through the cache */
		if (end > 0xa0000000UL) {
			end = 0xa0000000UL;
			if (start >= end)
				goto done;
		}

		/* kernel addresses between 0x80000000 and 0x9fffffff do not
		 * require page tables, so we just map such addresses
		 * directly */
		start_page = (start >= 0x80000000UL) ? start : 0x80000000UL;
		mn10300_icache_inv_range(start_page, end);
		smp_cache_call(SMP_ICACHE_INV_FLUSH_RANGE, start, end);
		if (start_page == start)
			goto done;
		end = start_page;
	}

	start_page = start & PAGE_MASK;
	end_page = (end - 1) & PAGE_MASK;

	if (start_page == end_page) {
		/* the first and last bytes are on the same page */
		flush_icache_page_range(start, end);
	} else if (start_page + 1 == end_page) {
		/* split over two virtually contiguous pages */
		flush_icache_page_range(start, end_page);
		flush_icache_page_range(end_page, end);
	} else {
		/* more than 2 pages; just flush the entire cache */
		mn10300_local_icache_inv();
		smp_cache_call(SMP_ICACHE_INV, 0, 0);
	}

done:
	smp_unlock_cache(flags);
}
EXPORT_SYMBOL(flush_icache_range);
