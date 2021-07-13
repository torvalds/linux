/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */

#ifndef __ASM_OPENRISC_PGALLOC_H
#define __ASM_OPENRISC_PGALLOC_H

#include <asm/page.h>
#include <linux/threads.h>
#include <linux/mm.h>
#include <linux/memblock.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
#include <asm-generic/pgalloc.h>

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

extern pte_t *pte_alloc_one_kernel(struct mm_struct *mm);

#define __pte_free_tlb(tlb, pte, addr)	\
do {					\
	pgtable_pte_page_dtor(pte);	\
	tlb_remove_page((tlb), (pte));	\
} while (0)

#endif
