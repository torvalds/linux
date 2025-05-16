// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC cache.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2015 Jan Henrik Weinstock <jan.weinstock@rwth-aachen.de>
 */

#include <asm/spr.h>
#include <asm/spr_defs.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/cpuinfo.h>
#include <asm/tlbflush.h>

/*
 * Check if the cache component exists.
 */
bool cpu_cache_is_present(const unsigned int cache_type)
{
	unsigned long upr = mfspr(SPR_UPR);
	unsigned long mask = SPR_UPR_UP | cache_type;

	return !((upr & mask) ^ mask);
}

static __always_inline void cache_loop(unsigned long paddr, unsigned long end,
				       const unsigned short reg, const unsigned int cache_type)
{
	if (!cpu_cache_is_present(cache_type))
		return;

	while (paddr < end) {
		mtspr(reg, paddr);
		paddr += L1_CACHE_BYTES;
	}
}

static __always_inline void cache_loop_page(struct page *page, const unsigned short reg,
					    const unsigned int cache_type)
{
	unsigned long paddr = page_to_pfn(page) << PAGE_SHIFT;
	unsigned long end = paddr + PAGE_SIZE;

	paddr &= ~(L1_CACHE_BYTES - 1);

	cache_loop(paddr, end, reg, cache_type);
}

void local_dcache_page_flush(struct page *page)
{
	cache_loop_page(page, SPR_DCBFR, SPR_UPR_DCP);
}
EXPORT_SYMBOL(local_dcache_page_flush);

void local_icache_page_inv(struct page *page)
{
	cache_loop_page(page, SPR_ICBIR, SPR_UPR_ICP);
}
EXPORT_SYMBOL(local_icache_page_inv);

void local_dcache_range_flush(unsigned long start, unsigned long end)
{
	cache_loop(start, end, SPR_DCBFR, SPR_UPR_DCP);
}

void local_dcache_range_inv(unsigned long start, unsigned long end)
{
	cache_loop(start, end, SPR_DCBIR, SPR_UPR_DCP);
}

void local_icache_range_inv(unsigned long start, unsigned long end)
{
	cache_loop(start, end, SPR_ICBIR, SPR_UPR_ICP);
}

void update_cache(struct vm_area_struct *vma, unsigned long address,
	pte_t *pte)
{
	unsigned long pfn = pte_val(*pte) >> PAGE_SHIFT;
	struct folio *folio = page_folio(pfn_to_page(pfn));
	int dirty = !test_and_set_bit(PG_dc_clean, &folio->flags);

	/*
	 * Since icaches do not snoop for updated data on OpenRISC, we
	 * must write back and invalidate any dirty pages manually. We
	 * can skip data pages, since they will not end up in icaches.
	 */
	if ((vma->vm_flags & VM_EXEC) && dirty) {
		unsigned int nr = folio_nr_pages(folio);

		while (nr--)
			sync_icache_dcache(folio_page(folio, nr));
	}
}
