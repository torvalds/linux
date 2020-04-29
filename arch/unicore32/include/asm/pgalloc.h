/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/pgalloc.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_PGALLOC_H__
#define __UNICORE_PGALLOC_H__

#include <asm/pgtable-hwdef.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
#define __HAVE_ARCH_PTE_ALLOC_ONE
#include <asm-generic/pgalloc.h>

#define _PAGE_USER_TABLE	(PMD_TYPE_TABLE | PMD_PRESENT)
#define _PAGE_KERNEL_TABLE	(PMD_TYPE_TABLE | PMD_PRESENT)

extern pgd_t *get_pgd_slow(struct mm_struct *mm);
extern void free_pgd_slow(struct mm_struct *mm, pgd_t *pgd);

#define pgd_alloc(mm)			get_pgd_slow(mm)
#define pgd_free(mm, pgd)		free_pgd_slow(mm, pgd)

/*
 * Allocate one PTE table.
 */
static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *pte = __pte_alloc_one_kernel(mm);

	if (pte)
		clean_dcache_area(pte, PTRS_PER_PTE * sizeof(pte_t));

	return pte;
}

static inline pgtable_t
pte_alloc_one(struct mm_struct *mm)
{
	struct page *pte;

	pte = __pte_alloc_one(mm, GFP_PGTABLE_USER);
	if (!pte)
		return NULL;
	if (!PageHighMem(pte))
		clean_pte_table(page_address(pte));
	return pte;
}

static inline void __pmd_populate(pmd_t *pmdp, unsigned long pmdval)
{
	set_pmd(pmdp, __pmd(pmdval));
	flush_pmd_entry(pmdp);
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
	unsigned long pte_ptr = (unsigned long)ptep;

	/*
	 * The pmd must be loaded with the physical
	 * address of the PTE table
	 */
	__pmd_populate(pmdp, __pa(pte_ptr) | _PAGE_KERNEL_TABLE);
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmdp, pgtable_t ptep)
{
	__pmd_populate(pmdp,
			page_to_pfn(ptep) << PAGE_SHIFT | _PAGE_USER_TABLE);
}
#define pmd_pgtable(pmd) pmd_page(pmd)

#endif
