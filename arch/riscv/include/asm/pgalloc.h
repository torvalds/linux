/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PGALLOC_H
#define _ASM_RISCV_PGALLOC_H

#include <linux/mm.h>
#include <asm/sbi.h>
#include <asm/tlb.h>

#ifdef CONFIG_MMU
#define __HAVE_ARCH_PUD_FREE
#include <asm-generic/pgalloc.h>

/*
 * While riscv platforms with riscv_ipi_for_rfence as true require an IPI to
 * perform TLB shootdown, some platforms with riscv_ipi_for_rfence as false use
 * SBI to perform TLB shootdown. To keep software pagetable walkers safe in this
 * case we switch to RCU based table free (MMU_GATHER_RCU_TABLE_FREE). See the
 * comment below 'ifdef CONFIG_MMU_GATHER_RCU_TABLE_FREE' in include/asm-generic/tlb.h
 * for more details.
 */
static inline void riscv_tlb_remove_ptdesc(struct mmu_gather *tlb, void *pt)
{
	if (riscv_use_sbi_for_rfence()) {
		tlb_remove_ptdesc(tlb, pt);
	} else {
		pagetable_dtor(pt);
		tlb_remove_page_ptdesc(tlb, pt);
	}
}

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

#define pud_free pud_free
static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	if (pgtable_l4_enabled)
		__pud_free(mm, pud);
}

static inline void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud,
				  unsigned long addr)
{
	if (pgtable_l4_enabled)
		riscv_tlb_remove_ptdesc(tlb, virt_to_ptdesc(pud));
}

static inline void __p4d_free_tlb(struct mmu_gather *tlb, p4d_t *p4d,
				  unsigned long addr)
{
	if (pgtable_l5_enabled)
		riscv_tlb_remove_ptdesc(tlb, virt_to_ptdesc(p4d));
}
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

	pgd = __pgd_alloc(mm, 0);
	if (likely(pgd != NULL)) {
		/* Copy kernel mappings */
		sync_kernel_mappings(pgd);
	}
	return pgd;
}

#ifndef __PAGETABLE_PMD_FOLDED

static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd,
				  unsigned long addr)
{
	riscv_tlb_remove_ptdesc(tlb, virt_to_ptdesc(pmd));
}

#endif /* __PAGETABLE_PMD_FOLDED */

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t pte,
				  unsigned long addr)
{
	riscv_tlb_remove_ptdesc(tlb, page_ptdesc(pte));
}
#endif /* CONFIG_MMU */

#endif /* _ASM_RISCV_PGALLOC_H */
