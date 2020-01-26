// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/cache.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <asm/cache.h>

void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t *pte)
{
	unsigned long addr;
	struct page *page;

	page = pfn_to_page(pte_pfn(*pte));
	if (page == ZERO_PAGE(0))
		return;

	if (test_and_set_bit(PG_dcache_clean, &page->flags))
		return;

	addr = (unsigned long) kmap_atomic(page);

	dcache_wb_range(addr, addr + PAGE_SIZE);

	if (vma->vm_flags & VM_EXEC)
		icache_inv_range(addr, addr + PAGE_SIZE);

	kunmap_atomic((void *) addr);
}
