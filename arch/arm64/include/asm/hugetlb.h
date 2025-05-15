/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/include/asm/hugetlb.h
 *
 * Copyright (C) 2013 Linaro Ltd.
 *
 * Based on arch/x86/include/asm/hugetlb.h
 */

#ifndef __ASM_HUGETLB_H
#define __ASM_HUGETLB_H

#include <asm/cacheflush.h>
#include <asm/mte.h>
#include <asm/page.h>

#ifdef CONFIG_ARCH_ENABLE_HUGEPAGE_MIGRATION
#define arch_hugetlb_migration_supported arch_hugetlb_migration_supported
extern bool arch_hugetlb_migration_supported(struct hstate *h);
#endif

static inline void arch_clear_hugetlb_flags(struct folio *folio)
{
	clear_bit(PG_dcache_clean, &folio->flags);

#ifdef CONFIG_ARM64_MTE
	if (system_supports_mte()) {
		clear_bit(PG_mte_tagged, &folio->flags);
		clear_bit(PG_mte_lock, &folio->flags);
	}
#endif
}
#define arch_clear_hugetlb_flags arch_clear_hugetlb_flags

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags);
#define arch_make_huge_pte arch_make_huge_pte
#define __HAVE_ARCH_HUGE_SET_HUGE_PTE_AT
extern void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
			    pte_t *ptep, pte_t pte, unsigned long sz);
#define __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
extern int huge_ptep_set_access_flags(struct vm_area_struct *vma,
				      unsigned long addr, pte_t *ptep,
				      pte_t pte, int dirty);
#define __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
extern pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				     pte_t *ptep, unsigned long sz);
#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
extern void huge_ptep_set_wrprotect(struct mm_struct *mm,
				    unsigned long addr, pte_t *ptep);
#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
extern pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
				   unsigned long addr, pte_t *ptep);
#define __HAVE_ARCH_HUGE_PTE_CLEAR
extern void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
			   pte_t *ptep, unsigned long sz);
#define __HAVE_ARCH_HUGE_PTEP_GET
extern pte_t huge_ptep_get(struct mm_struct *mm, unsigned long addr, pte_t *ptep);

void __init arm64_hugetlb_cma_reserve(void);

#define huge_ptep_modify_prot_start huge_ptep_modify_prot_start
extern pte_t huge_ptep_modify_prot_start(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep);

#define huge_ptep_modify_prot_commit huge_ptep_modify_prot_commit
extern void huge_ptep_modify_prot_commit(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep,
					 pte_t old_pte, pte_t new_pte);

#include <asm-generic/hugetlb.h>

#define __HAVE_ARCH_FLUSH_HUGETLB_TLB_RANGE
static inline void flush_hugetlb_tlb_range(struct vm_area_struct *vma,
					   unsigned long start,
					   unsigned long end)
{
	unsigned long stride = huge_page_size(hstate_vma(vma));

	switch (stride) {
#ifndef __PAGETABLE_PMD_FOLDED
	case PUD_SIZE:
		__flush_tlb_range(vma, start, end, PUD_SIZE, false, 1);
		break;
#endif
	case CONT_PMD_SIZE:
	case PMD_SIZE:
		__flush_tlb_range(vma, start, end, PMD_SIZE, false, 2);
		break;
	case CONT_PTE_SIZE:
		__flush_tlb_range(vma, start, end, PAGE_SIZE, false, 3);
		break;
	default:
		__flush_tlb_range(vma, start, end, PAGE_SIZE, false, TLBI_TTL_UNKNOWN);
	}
}

#endif /* __ASM_HUGETLB_H */
