/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013-15 Synopsys, Inc. (www.synopsys.com)
 */


#ifndef _ASM_ARC_HUGEPAGE_H
#define _ASM_ARC_HUGEPAGE_H

#include <linux/types.h>
#include <asm-generic/pgtable-nopmd.h>

/*
 * Hugetlb definitions.
 */
#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1, UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))

static inline pte_t pmd_pte(pmd_t pmd)
{
	return __pte(pmd_val(pmd));
}

static inline pmd_t pte_pmd(pte_t pte)
{
	return __pmd(pte_val(pte));
}

#define pmd_wrprotect(pmd)	pte_pmd(pte_wrprotect(pmd_pte(pmd)))
#define pmd_mkwrite_novma(pmd)	pte_pmd(pte_mkwrite_novma(pmd_pte(pmd)))
#define pmd_mkdirty(pmd)	pte_pmd(pte_mkdirty(pmd_pte(pmd)))
#define pmd_mkold(pmd)		pte_pmd(pte_mkold(pmd_pte(pmd)))
#define pmd_mkyoung(pmd)	pte_pmd(pte_mkyoung(pmd_pte(pmd)))
#define pmd_mkhuge(pmd)		pte_pmd(pte_mkhuge(pmd_pte(pmd)))
#define pmd_mkinvalid(pmd)	pte_pmd(pte_mknotpresent(pmd_pte(pmd)))
#define pmd_mkclean(pmd)	pte_pmd(pte_mkclean(pmd_pte(pmd)))

#define pmd_write(pmd)		pte_write(pmd_pte(pmd))
#define pmd_young(pmd)		pte_young(pmd_pte(pmd))
#define pmd_dirty(pmd)		pte_dirty(pmd_pte(pmd))

#define mk_pmd(page, prot)	pte_pmd(mk_pte(page, prot))

#define pmd_trans_huge(pmd)	(pmd_val(pmd) & _PAGE_HW_SZ)

#define pfn_pmd(pfn, prot)	(__pmd(((pfn) << PAGE_SHIFT) | pgprot_val(prot)))

static inline pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{
        /*
         * open-coded pte_modify() with additional retaining of HW_SZ bit
         * so that pmd_trans_huge() remains true for this PMD
         */
        return __pmd((pmd_val(pmd) & (_PAGE_CHG_MASK | _PAGE_HW_SZ)) | pgprot_val(newprot));
}

static inline void set_pmd_at(struct mm_struct *mm, unsigned long addr,
			      pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
}

extern void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
				 pmd_t *pmd);

#define __HAVE_ARCH_FLUSH_PMD_TLB_RANGE
extern void flush_pmd_tlb_range(struct vm_area_struct *vma, unsigned long start,
				unsigned long end);

/* We don't have hardware dirty/accessed bits, generic_pmdp_establish is fine.*/
#define pmdp_establish generic_pmdp_establish

#endif
