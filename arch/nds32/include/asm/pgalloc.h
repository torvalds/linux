/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASMNDS32_PGALLOC_H
#define _ASMNDS32_PGALLOC_H

#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/proc-fns.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE
#include <asm-generic/pgalloc.h>	/* for pte_{alloc,free}_one */

/*
 * Since we have only two-level page tables, these are trivial
 */
#define pmd_alloc_one(mm, addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(mm, pmd)			do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()
#define pmd_pgtable(pmd) pmd_page(pmd)

extern pgd_t *pgd_alloc(struct mm_struct *mm);
extern void pgd_free(struct mm_struct *mm, pgd_t * pgd);

#define check_pgt_cache()		do { } while (0)

static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	pgtable_t pte;

	pte = __pte_alloc_one(mm, GFP_PGTABLE_USER);
	if (pte)
		cpu_dcache_wb_page((unsigned long)page_address(pte));

	return pte;
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 *
 * Ensure that we always set both PMD entries.
 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t * pmdp, pte_t * ptep)
{
	unsigned long pte_ptr = (unsigned long)ptep;
	unsigned long pmdval;

	BUG_ON(mm != &init_mm);

	/*
	 * The pmd must be loaded with the physical
	 * address of the PTE table
	 */
	pmdval = __pa(pte_ptr) | _PAGE_KERNEL_TABLE;
	set_pmd(pmdp, __pmd(pmdval));
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t * pmdp, pgtable_t ptep)
{
	unsigned long pmdval;

	BUG_ON(mm == &init_mm);

	pmdval = page_to_pfn(ptep) << PAGE_SHIFT | _PAGE_USER_TABLE;
	set_pmd(pmdp, __pmd(pmdval));
}

#endif
