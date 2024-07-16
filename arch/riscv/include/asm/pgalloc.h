/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PGALLOC_H
#define _ASM_RISCV_PGALLOC_H

#include <linux/mm.h>
#include <asm/tlb.h>

#ifdef CONFIG_MMU
#define __HAVE_ARCH_PUD_ALLOC_ONE
#define __HAVE_ARCH_PUD_FREE
#include <asm-generic/pgalloc.h>

static inline void pmd_populate_kernel(struct mm_struct *mm,
	pmd_t *pmd, pte_t *pte)
{
	unsigned long pfn = virt_to_pfn(pte);

	set_pmd(pmd, __pmd((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
}

static inline void pmd_populate(struct mm_struct *mm,
	pmd_t *pmd, pgtable_t pte)
{
	unsigned long pfn = virt_to_pfn(page_address(pte));

	set_pmd(pmd, __pmd((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
}

#ifndef __PAGETABLE_PMD_FOLDED
static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	unsigned long pfn = virt_to_pfn(pmd);

	set_pud(pud, __pud((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
}

static inline void p4d_populate(struct mm_struct *mm, p4d_t *p4d, pud_t *pud)
{
	if (pgtable_l4_enabled) {
		unsigned long pfn = virt_to_pfn(pud);

		set_p4d(p4d, __p4d((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
	}
}

static inline void p4d_populate_safe(struct mm_struct *mm, p4d_t *p4d,
				     pud_t *pud)
{
	if (pgtable_l4_enabled) {
		unsigned long pfn = virt_to_pfn(pud);

		set_p4d_safe(p4d,
			     __p4d((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
	}
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, p4d_t *p4d)
{
	if (pgtable_l5_enabled) {
		unsigned long pfn = virt_to_pfn(p4d);

		set_pgd(pgd, __pgd((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
	}
}

static inline void pgd_populate_safe(struct mm_struct *mm, pgd_t *pgd,
				     p4d_t *p4d)
{
	if (pgtable_l5_enabled) {
		unsigned long pfn = virt_to_pfn(p4d);

		set_pgd_safe(pgd,
			     __pgd((pfn << _PAGE_PFN_SHIFT) | _PAGE_TABLE));
	}
}

#define pud_alloc_one pud_alloc_one
static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	if (pgtable_l4_enabled)
		return __pud_alloc_one(mm, addr);

	return NULL;
}

#define pud_free pud_free
static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	if (pgtable_l4_enabled)
		__pud_free(mm, pud);
}

#define __pud_free_tlb(tlb, pud, addr)  pud_free((tlb)->mm, pud)

#define p4d_alloc_one p4d_alloc_one
static inline p4d_t *p4d_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	if (pgtable_l5_enabled) {
		gfp_t gfp = GFP_PGTABLE_USER;

		if (mm == &init_mm)
			gfp = GFP_PGTABLE_KERNEL;
		return (p4d_t *)get_zeroed_page(gfp);
	}

	return NULL;
}

static inline void __p4d_free(struct mm_struct *mm, p4d_t *p4d)
{
	BUG_ON((unsigned long)p4d & (PAGE_SIZE-1));
	free_page((unsigned long)p4d);
}

#define p4d_free p4d_free
static inline void p4d_free(struct mm_struct *mm, p4d_t *p4d)
{
	if (pgtable_l5_enabled)
		__p4d_free(mm, p4d);
}

#define __p4d_free_tlb(tlb, p4d, addr)  p4d_free((tlb)->mm, p4d)
#endif /* __PAGETABLE_PMD_FOLDED */

static inline void sync_kernel_mappings(pgd_t *pgd)
{
	memcpy(pgd + USER_PTRS_PER_PGD,
	       init_mm.pgd + USER_PTRS_PER_PGD,
	       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = (pgd_t *)__get_free_page(GFP_KERNEL);
	if (likely(pgd != NULL)) {
		memset(pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
		/* Copy kernel mappings */
		sync_kernel_mappings(pgd);
	}
	return pgd;
}

#ifndef __PAGETABLE_PMD_FOLDED

#define __pmd_free_tlb(tlb, pmd, addr)  pmd_free((tlb)->mm, pmd)

#endif /* __PAGETABLE_PMD_FOLDED */

#define __pte_free_tlb(tlb, pte, buf)   \
do {                                    \
	pgtable_pte_page_dtor(pte);     \
	tlb_remove_page((tlb), pte);    \
} while (0)
#endif /* CONFIG_MMU */

#endif /* _ASM_RISCV_PGALLOC_H */
