/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/asm-xtensa/pgalloc.h
 *
 * Copyright (C) 2001-2007 Tensilica Inc.
 */

#ifndef _XTENSA_PGALLOC_H
#define _XTENSA_PGALLOC_H

#ifdef CONFIG_MMU
#include <linux/highmem.h>
#include <linux/slab.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
#define __HAVE_ARCH_PTE_ALLOC_ONE
#include <asm-generic/pgalloc.h>

/*
 * Allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */

#define pmd_populate_kernel(mm, pmdp, ptep)				     \
	(pmd_val(*(pmdp)) = ((unsigned long)ptep))
#define pmd_populate(mm, pmdp, page)					     \
	(pmd_val(*(pmdp)) = ((unsigned long)page_to_virt(page)))

static inline pgd_t*
pgd_alloc(struct mm_struct *mm)
{
	return __pgd_alloc(mm, 0);
}

static inline void ptes_clear(pte_t *ptep)
{
	int i;

	for (i = 0; i < PTRS_PER_PTE; i++)
		pte_clear(NULL, 0, ptep + i);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *ptep;

	ptep = (pte_t *)__pte_alloc_one_kernel(mm);
	if (!ptep)
		return NULL;
	ptes_clear(ptep);
	return ptep;
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	struct page *page;

	page = __pte_alloc_one(mm, GFP_PGTABLE_USER);
	if (!page)
		return NULL;
	ptes_clear(page_address(page));
	return page;
}

#endif /* CONFIG_MMU */

#endif /* _XTENSA_PGALLOC_H */
