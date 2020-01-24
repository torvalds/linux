// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/cache.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <asm/cache.h>

void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t *pte)
{
	unsigned long addr, pfn;
	struct page *page;

	pfn = pte_pfn(*pte);
	if (unlikely(!pfn_valid(pfn)))
		return;

	page = pfn_to_page(pfn);
	if (page == ZERO_PAGE(0))
		return;

	addr = (unsigned long) kmap_atomic(page);

	cache_wbinv_range(addr, addr + PAGE_SIZE);

	kunmap_atomic((void *) addr);
}
