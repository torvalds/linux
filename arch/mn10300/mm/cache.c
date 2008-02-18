/* MN10300 Cache flushing routines
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/threads.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/uaccess.h>

EXPORT_SYMBOL(mn10300_icache_inv);
EXPORT_SYMBOL(mn10300_dcache_inv);
EXPORT_SYMBOL(mn10300_dcache_inv_range);
EXPORT_SYMBOL(mn10300_dcache_inv_range2);
EXPORT_SYMBOL(mn10300_dcache_inv_page);

#ifdef CONFIG_MN10300_CACHE_WBACK
EXPORT_SYMBOL(mn10300_dcache_flush);
EXPORT_SYMBOL(mn10300_dcache_flush_inv);
EXPORT_SYMBOL(mn10300_dcache_flush_inv_range);
EXPORT_SYMBOL(mn10300_dcache_flush_inv_range2);
EXPORT_SYMBOL(mn10300_dcache_flush_inv_page);
EXPORT_SYMBOL(mn10300_dcache_flush_range);
EXPORT_SYMBOL(mn10300_dcache_flush_range2);
EXPORT_SYMBOL(mn10300_dcache_flush_page);
#endif

/*
 * write a page back from the dcache and invalidate the icache so that we can
 * run code from it that we've just written into it
 */
void flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	mn10300_dcache_flush_page(page_to_phys(page));
	mn10300_icache_inv();
}
EXPORT_SYMBOL(flush_icache_page);

/*
 * write some code we've just written back from the dcache and invalidate the
 * icache so that we can run that code
 */
void flush_icache_range(unsigned long start, unsigned long end)
{
#ifdef CONFIG_MN10300_CACHE_WBACK
	unsigned long addr, size, off;
	struct page *page;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ppte, pte;

	for (; start < end; start += size) {
		/* work out how much of the page to flush */
		off = start & (PAGE_SIZE - 1);

		size = end - start;
		if (size > PAGE_SIZE - off)
			size = PAGE_SIZE - off;

		/* get the physical address the page is mapped to from the page
		 * tables */
		pgd = pgd_offset(current->mm, start);
		if (!pgd || !pgd_val(*pgd))
			continue;

		pud = pud_offset(pgd, start);
		if (!pud || !pud_val(*pud))
			continue;

		pmd = pmd_offset(pud, start);
		if (!pmd || !pmd_val(*pmd))
			continue;

		ppte = pte_offset_map(pmd, start);
		if (!ppte)
			continue;
		pte = *ppte;
		pte_unmap(ppte);

		if (pte_none(pte))
			continue;

		page = pte_page(pte);
		if (!page)
			continue;

		addr = page_to_phys(page);

		/* flush the dcache and invalidate the icache coverage on that
		 * region */
		mn10300_dcache_flush_range2(addr + off, size);
	}
#endif

	mn10300_icache_inv();
}
EXPORT_SYMBOL(flush_icache_range);

/*
 * allow userspace to flush the instruction cache
 */
asmlinkage long sys_cacheflush(unsigned long start, unsigned long end)
{
	if (end < start)
		return -EINVAL;

	flush_icache_range(start, end);
	return 0;
}
