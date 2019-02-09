// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASMNDS32_PGALLOC_H
#define _ASMNDS32_PGALLOC_H

#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/proc-fns.h>

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

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long addr)
{
	pte_t *pte;

	pte =
	    (pte_t *) __get_free_page(GFP_KERNEL | __GFP_RETRY_MAYFAIL |
				      __GFP_ZERO);

	return pte;
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	pgtable_t pte;

	pte = alloc_pages(GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_ZERO, 0);
	if (pte)
		cpu_dcache_wb_page((unsigned long)page_address(pte));

	return pte;
}

/*
 * Free one PTE table.
 */
static inline void pte_free_kernel(struct mm_struct *mm, pte_t * pte)
{
	if (pte) {
		free_page((unsigned long)pte);
	}
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	__free_page(pte);
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
