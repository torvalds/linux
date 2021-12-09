// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/spinlock.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>
#include <asm/cachectl.h>

#define PG_dcache_clean		PG_arch_1

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping;

	if (page == ZERO_PAGE(0))
		return;

	mapping = page_mapping_file(page);

	if (mapping && !page_mapcount(page))
		clear_bit(PG_dcache_clean, &page->flags);
	else {
		dcache_wbinv_all();
		if (mapping)
			icache_inv_all();
		set_bit(PG_dcache_clean, &page->flags);
	}
}
EXPORT_SYMBOL(flush_dcache_page);

void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr,
	pte_t *ptep)
{
	unsigned long pfn = pte_pfn(*ptep);
	struct page *page;

	if (!pfn_valid(pfn))
		return;

	page = pfn_to_page(pfn);
	if (page == ZERO_PAGE(0))
		return;

	if (!test_and_set_bit(PG_dcache_clean, &page->flags))
		dcache_wbinv_all();

	if (page_mapping_file(page)) {
		if (vma->vm_flags & VM_EXEC)
			icache_inv_all();
	}
}

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
	unsigned long end)
{
	dcache_wbinv_all();

	if (vma->vm_flags & VM_EXEC)
		icache_inv_all();
}
