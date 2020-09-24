/*
 * OpenRISC cache.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2015 Jan Henrik Weinstock <jan.weinstock@rwth-aachen.de>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <asm/spr.h>
#include <asm/spr_defs.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static __always_inline void cache_loop(struct page *page, const unsigned int reg)
{
	unsigned long paddr = page_to_pfn(page) << PAGE_SHIFT;
	unsigned long line = paddr & ~(L1_CACHE_BYTES - 1);

	while (line < paddr + PAGE_SIZE) {
		mtspr(reg, line);
		line += L1_CACHE_BYTES;
	}
}

void local_dcache_page_flush(struct page *page)
{
	cache_loop(page, SPR_DCBFR);
}
EXPORT_SYMBOL(local_dcache_page_flush);

void local_icache_page_inv(struct page *page)
{
	cache_loop(page, SPR_ICBIR);
}
EXPORT_SYMBOL(local_icache_page_inv);

void update_cache(struct vm_area_struct *vma, unsigned long address,
	pte_t *pte)
{
	unsigned long pfn = pte_val(*pte) >> PAGE_SHIFT;
	struct page *page = pfn_to_page(pfn);
	int dirty = !test_and_set_bit(PG_dc_clean, &page->flags);

	/*
	 * Since icaches do not snoop for updated data on OpenRISC, we
	 * must write back and invalidate any dirty pages manually. We
	 * can skip data pages, since they will not end up in icaches.
	 */
	if ((vma->vm_flags & VM_EXEC) && dirty)
		sync_icache_dcache(page);
}

