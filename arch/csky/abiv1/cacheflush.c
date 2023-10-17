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
#include <asm/tlbflush.h>

#define PG_dcache_clean		PG_arch_1

void flush_dcache_folio(struct folio *folio)
{
	struct address_space *mapping;

	if (is_zero_pfn(folio_pfn(folio)))
		return;

	mapping = folio_flush_mapping(folio);

	if (mapping && !folio_mapped(folio))
		clear_bit(PG_dcache_clean, &folio->flags);
	else {
		dcache_wbinv_all();
		if (mapping)
			icache_inv_all();
		set_bit(PG_dcache_clean, &folio->flags);
	}
}
EXPORT_SYMBOL(flush_dcache_folio);

void flush_dcache_page(struct page *page)
{
	flush_dcache_folio(page_folio(page));
}
EXPORT_SYMBOL(flush_dcache_page);

void update_mmu_cache_range(struct vm_fault *vmf, struct vm_area_struct *vma,
		unsigned long addr, pte_t *ptep, unsigned int nr)
{
	unsigned long pfn = pte_pfn(*ptep);
	struct folio *folio;

	flush_tlb_page(vma, addr);

	if (!pfn_valid(pfn))
		return;

	if (is_zero_pfn(pfn))
		return;

	folio = page_folio(pfn_to_page(pfn));
	if (!test_and_set_bit(PG_dcache_clean, &folio->flags))
		dcache_wbinv_all();

	if (folio_flush_mapping(folio)) {
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
