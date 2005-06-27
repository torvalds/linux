/*
 * arch/xtensa/mm/fault.c
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
	pte_t *pte, p;
	int color = ADDR_COLOR(address);
	int i;

	p = (pte_t*) __get_free_pages(GFP_KERNEL|__GFP_REPEAT, COLOR_ORDER);

	if (likely(p)) {
		struct page *page;

		for (i = 0; i < COLOR_SIZE; i++, p++) {
			page = virt_to_page(pte);

			set_page_count(page, 1);
			ClearPageCompound(page);

			if (ADDR_COLOR(p) == color)
				pte = p;
			else
				free_page(p);
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
	struct page *page, p;
	int color = ADDR_COLOR(address);

	p = alloc_pages(GFP_KERNEL | __GFP_REPEAT, PTE_ORDER);

	if (likely(p)) {
		for (i = 0; i < PAGE_ORDER; i++) {
			set_page_count(p, 1);
			ClearPageCompound(p);

			if (PADDR_COLOR(page_address(pg)) == color)
				page = p;
			else
				free_page(p);
		}
		clear_highpage(page);
	}

	return page;
}

#endif



