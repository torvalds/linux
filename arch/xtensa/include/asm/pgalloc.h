/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/asm-xtensa/pgalloc.h
 *
 * Copyright (C) 2001-2007 Tensilica Inc.
 */

#ifndef _XTENSA_PGALLOC_H
#define _XTENSA_PGALLOC_H

#include <linux/highmem.h>
#include <linux/slab.h>

/*
 * Allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#define pmd_populate_kernel(mm, pmdp, ptep)				     \
	(pmd_val(*(pmdp)) = ((unsigned long)ptep))
#define pmd_populate(mm, pmdp, page)					     \
	(pmd_val(*(pmdp)) = ((unsigned long)page_to_virt(page)))
#define pmd_pgtable(pmd) pmd_page(pmd)

static inline pgd_t*
pgd_alloc(struct mm_struct *mm)
{
	return (pgd_t*) __get_free_pages(GFP_KERNEL | __GFP_ZERO, PGD_ORDER);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *ptep;
	int i;

	ptep = (pte_t *)__get_free_page(GFP_KERNEL);
	if (!ptep)
		return NULL;
	for (i = 0; i < 1024; i++)
		pte_clear(NULL, 0, ptep + i);
	return ptep;
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	pte_t *pte;
	struct page *page;

	pte = pte_alloc_one_kernel(mm);
	if (!pte)
		return NULL;
	page = virt_to_page(pte);
	if (!pgtable_pte_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}
	return page;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_pte_page_dtor(pte);
	__free_page(pte);
}
#define pmd_pgtable(pmd) pmd_page(pmd)

#endif /* _XTENSA_PGALLOC_H */
