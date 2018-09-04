/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_PGALLOC_H
#define __ASM_OPENRISC_PGALLOC_H

#include <asm/page.h>
#include <linux/threads.h>
#include <linux/mm.h>
#include <linux/memblock.h>

extern int mem_init_done;

#define pmd_populate_kernel(mm, pmd, pte) \
	set_pmd(pmd, __pmd(_KERNPG_TABLE + __pa(pte)))

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				struct page *pte)
{
	set_pmd(pmd, __pmd(_KERNPG_TABLE +
		     ((unsigned long)page_to_pfn(pte) <<
		     (unsigned long) PAGE_SHIFT)));
}

/*
 * Allocate and free page tables.
 */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret = (pgd_t *)__get_free_page(GFP_KERNEL);

	if (ret) {
		memset(ret, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		memcpy(ret + USER_PTRS_PER_PGD,
		       swapper_pg_dir + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));

	}
	return ret;
}

#if 0
/* FIXME: This seems to be the preferred style, but we are using
 * current_pgd (from mm->pgd) to load kernel pages so we need it
 * initialized.  This needs to be looked into.
 */
extern inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return (pgd_t *)get_zeroed_page(GFP_KERNEL);
}
#endif

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

extern pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address);

static inline struct page *pte_alloc_one(struct mm_struct *mm,
					 unsigned long address)
{
	struct page *pte;
	pte = alloc_pages(GFP_KERNEL, 0);
	if (!pte)
		return NULL;
	clear_page(page_address(pte));
	if (!pgtable_page_ctor(pte)) {
		__free_page(pte);
		return NULL;
	}
	return pte;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, struct page *pte)
{
	pgtable_page_dtor(pte);
	__free_page(pte);
}

#define __pte_free_tlb(tlb, pte, addr)	\
do {					\
	pgtable_page_dtor(pte);		\
	tlb_remove_page((tlb), (pte));	\
} while (0)

#define pmd_pgtable(pmd) pmd_page(pmd)

#define check_pgt_cache()          do { } while (0)

#endif
