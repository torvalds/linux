/* pgalloc.c: page directory & page table allocation
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/quicklist.h>
#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD] __attribute__((aligned(PAGE_SIZE)));

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte = (pte_t *)__get_free_page(GFP_KERNEL);
	if (pte)
		clear_page(pte);
	return pte;
}

pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *page;

#ifdef CONFIG_HIGHPTE
	page = alloc_pages(GFP_KERNEL|__GFP_HIGHMEM, 0);
#else
	page = alloc_pages(GFP_KERNEL, 0);
#endif
	if (!page)
		return NULL;

	clear_highpage(page);
	if (!pgtable_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}
	flush_dcache_page(page);
	return page;
}

void __set_pmd(pmd_t *pmdptr, unsigned long pmd)
{
	unsigned long *__ste_p = pmdptr->ste;
	int loop;

	if (!pmd) {
		memset(__ste_p, 0, PME_SIZE);
	}
	else {
		BUG_ON(pmd & (0x3f00 | xAMPRx_SS | 0xe));

		for (loop = PME_SIZE; loop > 0; loop -= 4) {
			*__ste_p++ = pmd;
			pmd += __frv_PT_SIZE;
		}
	}

	frv_dcache_writeback((unsigned long) pmdptr, (unsigned long) (pmdptr + 1));
}

/*
 * List of all pgd's needed for non-PAE so it can invalidate entries
 * in both cached and uncached pgd's; not needed for PAE since the
 * kernel pmd is shared. If PAE were not to share the pmd a similar
 * tactic would be needed. This is essentially codepath-based locking
 * against pageattr.c; it is the unique case in which a valid change
 * of kernel pagetables can't be lazily synchronized by vmalloc faults.
 * vmalloc faults work because attached pagetables are never freed.
 * If the locking proves to be non-performant, a ticketing scheme with
 * checks at dup_mmap(), exec(), and other mmlist addition points
 * could be used. The locking scheme was chosen on the basis of
 * manfred's recommendations and having no core impact whatsoever.
 * -- nyc
 */
DEFINE_SPINLOCK(pgd_lock);
struct page *pgd_list;

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);
	page->index = (unsigned long) pgd_list;
	if (pgd_list)
		set_page_private(pgd_list, (unsigned long) &page->index);
	pgd_list = page;
	set_page_private(page, (unsigned long)&pgd_list);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct page *next, **pprev, *page = virt_to_page(pgd);
	next = (struct page *) page->index;
	pprev = (struct page **) page_private(page);
	*pprev = next;
	if (next)
		set_page_private(next, (unsigned long) pprev);
}

void pgd_ctor(void *pgd)
{
	unsigned long flags;

	if (PTRS_PER_PMD == 1)
		spin_lock_irqsave(&pgd_lock, flags);

	memcpy((pgd_t *) pgd + USER_PGDS_IN_LAST_PML4,
	       swapper_pg_dir + USER_PGDS_IN_LAST_PML4,
	       (PTRS_PER_PGD - USER_PGDS_IN_LAST_PML4) * sizeof(pgd_t));

	if (PTRS_PER_PMD > 1)
		return;

	pgd_list_add(pgd);
	spin_unlock_irqrestore(&pgd_lock, flags);
	memset(pgd, 0, USER_PGDS_IN_LAST_PML4 * sizeof(pgd_t));
}

/* never called when PTRS_PER_PMD > 1 */
void pgd_dtor(void *pgd)
{
	unsigned long flags; /* can be called from interrupt context */

	spin_lock_irqsave(&pgd_lock, flags);
	pgd_list_del(pgd);
	spin_unlock_irqrestore(&pgd_lock, flags);
}

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return quicklist_alloc(0, GFP_KERNEL, pgd_ctor);
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	/* in the non-PAE case, clear_page_tables() clears user pgd entries */
 	quicklist_free(0, pgd_dtor, pgd);
}

void __init pgtable_cache_init(void)
{
}

void check_pgt_cache(void)
{
	quicklist_trim(0, pgd_dtor, 25, 16);
}

