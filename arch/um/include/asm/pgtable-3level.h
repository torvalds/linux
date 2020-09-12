/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2003 PathScale Inc
 * Derived from include/asm-i386/pgtable.h
 */

#ifndef __UM_PGTABLE_3LEVEL_H
#define __UM_PGTABLE_3LEVEL_H

#include <asm-generic/pgtable-nopud.h>

/* PGDIR_SHIFT determines what a third-level page table entry can map */

#ifdef CONFIG_64BIT
#define PGDIR_SHIFT	30
#else
#define PGDIR_SHIFT	31
#endif
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* PMD_SHIFT determines the size of the area a second-level page table can
 * map
 */

#define PMD_SHIFT	21
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/*
 * entries per page directory level
 */

#define PTRS_PER_PTE 512
#ifdef CONFIG_64BIT
#define PTRS_PER_PMD 512
#define PTRS_PER_PGD 512
#else
#define PTRS_PER_PMD 1024
#define PTRS_PER_PGD 1024
#endif

#define USER_PTRS_PER_PGD ((TASK_SIZE + (PGDIR_SIZE - 1)) / PGDIR_SIZE)
#define FIRST_USER_ADDRESS	0UL

#define pte_ERROR(e) \
        printk("%s:%d: bad pte %p(%016lx).\n", __FILE__, __LINE__, &(e), \
	       pte_val(e))
#define pmd_ERROR(e) \
        printk("%s:%d: bad pmd %p(%016lx).\n", __FILE__, __LINE__, &(e), \
	       pmd_val(e))
#define pgd_ERROR(e) \
        printk("%s:%d: bad pgd %p(%016lx).\n", __FILE__, __LINE__, &(e), \
	       pgd_val(e))

#define pud_none(x)	(!(pud_val(x) & ~_PAGE_NEWPAGE))
#define	pud_bad(x)	((pud_val(x) & (~PAGE_MASK & ~_PAGE_USER)) != _KERNPG_TABLE)
#define pud_present(x)	(pud_val(x) & _PAGE_PRESENT)
#define pud_populate(mm, pud, pmd) \
	set_pud(pud, __pud(_PAGE_TABLE + __pa(pmd)))

#ifdef CONFIG_64BIT
#define set_pud(pudptr, pudval) set_64bit((u64 *) (pudptr), pud_val(pudval))
#else
#define set_pud(pudptr, pudval) (*(pudptr) = (pudval))
#endif

static inline int pgd_newpage(pgd_t pgd)
{
	return(pgd_val(pgd) & _PAGE_NEWPAGE);
}

static inline void pgd_mkuptodate(pgd_t pgd) { pgd_val(pgd) &= ~_PAGE_NEWPAGE; }

#ifdef CONFIG_64BIT
#define set_pmd(pmdptr, pmdval) set_64bit((u64 *) (pmdptr), pmd_val(pmdval))
#else
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))
#endif

static inline void pud_clear (pud_t *pud)
{
	set_pud(pud, __pud(_PAGE_NEWPAGE));
}

#define pud_page(pud) phys_to_page(pud_val(pud) & PAGE_MASK)
#define pud_page_vaddr(pud) ((unsigned long) __va(pud_val(pud) & PAGE_MASK))

static inline unsigned long pte_pfn(pte_t pte)
{
	return phys_to_pfn(pte_val(pte));
}

static inline pte_t pfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	pte_t pte;
	phys_t phys = pfn_to_phys(page_nr);

	pte_set_val(pte, phys, pgprot);
	return pte;
}

static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot)
{
	return __pmd((page_nr << PAGE_SHIFT) | pgprot_val(pgprot));
}

#endif

