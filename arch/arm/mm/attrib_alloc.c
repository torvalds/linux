/*
 * arch/arm/mm/attrib_alloc.c
 *
 * Page allocator with custom cache attributes
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/page-flags.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/outercache.h>
#include <asm/attrib_alloc.h>
#include "mm.h"

static void update_kmap_pte(struct page *page, pgprot_t prot)
{
#ifdef CONFIG_HIGHMEM
	unsigned long addr;
	pte_t *pte;

	addr = (unsigned long)kmap_high_get(page);
	BUG_ON(!PageHighMem(page) || addr >= FIXADDR_START);
	if (!addr)
		return;

	pte = &pkmap_page_table[PKMAP_NR(addr)];
	set_pte_at(&init_mm, addr, pte, mk_pte(page, __pgprot(prot)));
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	kunmap_high(page);
#endif
}

static void update_pte(struct page *page, pgprot_t prot)
{
#ifdef CONFIG_ARCH_LOWMEM_IN_PTES
	unsigned long addr = (unsigned long)page_address(page);
	pgd_t *pgd = pgd_offset_k(addr);
	pmd_t *pmd = pmd_offset(pgd, addr);
	pte_t *pte;

	BUG_ON(pmd_none(*pmd));
	pte = pte_offset_kernel(pmd, addr);
	set_pte_at(&init_mm, addr, pte, mk_pte(page, __pgprot(prot)));
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
#endif
}

void arm_attrib_free_pages_exact(struct page *page, size_t size)
{
	struct page *p, *e;

	size = PAGE_ALIGN(size);
	e = page + (size >> PAGE_SHIFT);

	/* reset the page's mappings back to the standard kernel mappings
	 * before returning it to the page allocator */
	for (p = page; p < e; p++) {
		if (PageHighMem(p))
			update_kmap_pte(p, pgprot_kernel);
		else
			update_pte(p, pgprot_kernel);

		__free_page(p);
	}
}

struct page *arm_attrib_alloc_pages_exact_node(int nid, gfp_t gfp,
					       size_t size, pgprot_t prot)
{
	struct page *page, *p, *e;
	unsigned int order;
	unsigned long base;

	size = PAGE_ALIGN(size);
	order = get_order(size);
	page = alloc_pages_node(nid, gfp, order);

	if (!page)
		return NULL;

	split_page(page, order);

	e = page + (1 << order);
	for (p = page + (size >> PAGE_SHIFT); p < e; p++)
		__free_page(p);

	e = page + (size >> PAGE_SHIFT);

	for (p = page; p < e; p++) {
		__flush_dcache_page(page_mapping(p), p);

		/* even though a freshly-allocated highmem page shouldn't
		 * be mapped, because the kmaps are flushed lazily, it
		 * is possible that a mapping from an old kmap_high call
		 * is still present, and its cache attributes need to
		 * be updated to match the new expectations */
		if (PageHighMem(p))
			update_kmap_pte(p, prot);
		else
			update_pte(p, prot);
	}
	base = page_to_phys(page);
	outer_flush_range(base, base + (size >> PAGE_SHIFT));
	return page;
}
