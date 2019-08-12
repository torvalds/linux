/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/pgalloc.h
 *
 *  Copyright (C) 2000-2001 Russell King
 */
#ifndef _ASMARM_PGALLOC_H
#define _ASMARM_PGALLOC_H

#include <linux/pagemap.h>

#include <asm/domain.h>
#include <asm/pgtable-hwdef.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#define check_pgt_cache()		do { } while (0)

#ifdef CONFIG_MMU

#define _PAGE_USER_TABLE	(PMD_TYPE_TABLE | PMD_BIT4 | PMD_DOMAIN(DOMAIN_USER))
#define _PAGE_KERNEL_TABLE	(PMD_TYPE_TABLE | PMD_BIT4 | PMD_DOMAIN(DOMAIN_KERNEL))

#ifdef CONFIG_ARM_LPAE

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return (pmd_t *)get_zeroed_page(GFP_KERNEL);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	free_page((unsigned long)pmd);
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud(__pa(pmd) | PMD_TYPE_TABLE));
}

#else	/* !CONFIG_ARM_LPAE */

/*
 * Since we have only two-level page tables, these are trivial
 */
#define pmd_alloc_one(mm,addr)		({ BUG(); ((pmd_t *)2); })
#define pmd_free(mm, pmd)		do { } while (0)
#define pud_populate(mm,pmd,pte)	BUG()

#endif	/* CONFIG_ARM_LPAE */

extern pgd_t *pgd_alloc(struct mm_struct *mm);
extern void pgd_free(struct mm_struct *mm, pgd_t *pgd);

static inline void clean_pte_table(pte_t *pte)
{
	clean_dcache_area(pte + PTE_HWTABLE_PTRS, PTE_HWTABLE_SIZE);
}

/*
 * Allocate one PTE table.
 *
 * This actually allocates two hardware PTE tables, but we wrap this up
 * into one table thus:
 *
 *  +------------+
 *  | Linux pt 0 |
 *  +------------+
 *  | Linux pt 1 |
 *  +------------+
 *  |  h/w pt 0  |
 *  +------------+
 *  |  h/w pt 1  |
 *  +------------+
 */

#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
#define __HAVE_ARCH_PTE_ALLOC_ONE
#include <asm-generic/pgalloc.h>

static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *pte = __pte_alloc_one_kernel(mm);

	if (pte)
		clean_pte_table(pte);

	return pte;
}

#ifdef CONFIG_HIGHPTE
#define PGTABLE_HIGHMEM __GFP_HIGHMEM
#else
#define PGTABLE_HIGHMEM 0
#endif

static inline pgtable_t
pte_alloc_one(struct mm_struct *mm)
{
	struct page *pte;

	pte = __pte_alloc_one(mm, GFP_PGTABLE_USER | PGTABLE_HIGHMEM);
	if (!pte)
		return NULL;
	if (!PageHighMem(pte))
		clean_pte_table(page_address(pte));
	return pte;
}

static inline void __pmd_populate(pmd_t *pmdp, phys_addr_t pte,
				  pmdval_t prot)
{
	pmdval_t pmdval = (pte + PTE_HWTABLE_OFF) | prot;
	pmdp[0] = __pmd(pmdval);
#ifndef CONFIG_ARM_LPAE
	pmdp[1] = __pmd(pmdval + 256 * sizeof(pte_t));
#endif
	flush_pmd_entry(pmdp);
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 *
 * Ensure that we always set both PMD entries.
 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
	/*
	 * The pmd must be loaded with the physical address of the PTE table
	 */
	__pmd_populate(pmdp, __pa(ptep), _PAGE_KERNEL_TABLE);
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmdp, pgtable_t ptep)
{
	extern pmdval_t user_pmd_table;
	pmdval_t prot;

	if (__LINUX_ARM_ARCH__ >= 6 && !IS_ENABLED(CONFIG_ARM_LPAE))
		prot = user_pmd_table;
	else
		prot = _PAGE_USER_TABLE;

	__pmd_populate(pmdp, page_to_phys(ptep), prot);
}
#define pmd_pgtable(pmd) pmd_page(pmd)

#endif /* CONFIG_MMU */

#endif
