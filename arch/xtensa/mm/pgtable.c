/*
 * arch/xtensa/mm/pgtable.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#if (DCACHE_SIZE > PAGE_SIZE)

pte_t* pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte = NULL, *p;
	int color = ADDR_COLOR(address);
	int i;

	p = (pte_t*) __get_free_pages(GFP_KERNEL|__GFP_REPEAT, COLOR_ORDER);

	if (likely(p)) {
		split_page(virt_to_page(p), COLOR_ORDER);

		for (i = 0; i < COLOR_SIZE; i++) {
			if (ADDR_COLOR(p) == color)
				pte = p;
			else
				free_page(p);
			p += PTRS_PER_PTE;
		}
		clear_page(pte);
	}
	return pte;
}

#ifdef PROFILING

int mask;
int hit;
int flush;

#endif

struct page* pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *page = NULL, *p;
	int color = ADDR_COLOR(address);

	p = alloc_pages(GFP_KERNEL | __GFP_REPEAT, PTE_ORDER);

	if (likely(p)) {
		split_page(p, COLOR_ORDER);

		for (i = 0; i < PAGE_ORDER; i++) {
			if (PADDR_COLOR(page_address(p)) == color)
				page = p;
			else
				__free_page(p);
			p++;
		}
		clear_highpage(page);
	}

	return page;
}

#endif



