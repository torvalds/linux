/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_PGALLOC_H
#define _ASM_IA64_PGALLOC_H

/*
 * This file contains the functions and defines necessary to allocate
 * page tables.
 *
 * This hopefully works with any (fixed) ia-64 page-size, as defined
 * in <asm/page.h> (currently 8192).
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000, Goutham Rao <goutham.rao@intel.com>
 */


#include <linux/compiler.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/threads.h>
#include <linux/quicklist.h>

#include <asm/mmu_context.h>

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	quicklist_free(0, NULL, pgd);
}

#if CONFIG_PGTABLE_LEVELS == 4
static inline void
pgd_populate(struct mm_struct *mm, pgd_t * pgd_entry, pud_t * pud)
{
	pgd_val(*pgd_entry) = __pa(pud);
}

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	quicklist_free(0, NULL, pud);
}
#define __pud_free_tlb(tlb, pud, address)	pud_free((tlb)->mm, pud)
#endif /* CONFIG_PGTABLE_LEVELS == 4 */

static inline void
pud_populate(struct mm_struct *mm, pud_t * pud_entry, pmd_t * pmd)
{
	pud_val(*pud_entry) = __pa(pmd);
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	quicklist_free(0, NULL, pmd);
}

#define __pmd_free_tlb(tlb, pmd, address)	pmd_free((tlb)->mm, pmd)

static inline void
pmd_populate(struct mm_struct *mm, pmd_t * pmd_entry, pgtable_t pte)
{
	pmd_val(*pmd_entry) = page_to_phys(pte);
}
#define pmd_pgtable(pmd) pmd_page(pmd)

static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t * pmd_entry, pte_t * pte)
{
	pmd_val(*pmd_entry) = __pa(pte);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	struct page *page;
	void *pg;

	pg = quicklist_alloc(0, GFP_KERNEL, NULL);
	if (!pg)
		return NULL;
	page = virt_to_page(pg);
	if (!pgtable_page_ctor(page)) {
		quicklist_free(0, NULL, pg);
		return NULL;
	}
	return page;
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long addr)
{
	return quicklist_alloc(0, GFP_KERNEL, NULL);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_page_dtor(pte);
	quicklist_free_page(0, NULL, pte);
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	quicklist_free(0, NULL, pte);
}

static inline void check_pgt_cache(void)
{
	quicklist_trim(0, NULL, 25, 16);
}

#define __pte_free_tlb(tlb, pte, address)	pte_free((tlb)->mm, pte)

#endif				/* _ASM_IA64_PGALLOC_H */
