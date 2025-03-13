/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  IBM System z Huge TLB Page Support for Kernel.
 *
 *    Copyright IBM Corp. 2008
 *    Author(s): Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#ifndef _ASM_S390_HUGETLB_H
#define _ASM_S390_HUGETLB_H

#include <linux/pgtable.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <asm/page.h>

#define hugepages_supported()			(MACHINE_HAS_EDAT1)

#define __HAVE_ARCH_HUGE_SET_HUGE_PTE_AT
void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte, unsigned long sz);
void __set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		       pte_t *ptep, pte_t pte);

#define __HAVE_ARCH_HUGE_PTEP_GET
pte_t huge_ptep_get(struct mm_struct *mm, unsigned long addr, pte_t *ptep);

pte_t __huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep);

#define __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep,
					    unsigned long sz)
{
	return __huge_ptep_get_and_clear(mm, addr, ptep);
}

static inline void arch_clear_hugetlb_flags(struct folio *folio)
{
	clear_bit(PG_arch_1, &folio->flags);
}
#define arch_clear_hugetlb_flags arch_clear_hugetlb_flags

#define __HAVE_ARCH_HUGE_PTE_CLEAR
static inline void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
				  pte_t *ptep, unsigned long sz)
{
	if ((pte_val(*ptep) & _REGION_ENTRY_TYPE_MASK) == _REGION_ENTRY_TYPE_R3)
		set_pte(ptep, __pte(_REGION3_ENTRY_EMPTY));
	else
		set_pte(ptep, __pte(_SEGMENT_ENTRY_EMPTY));
}

#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
static inline pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
					  unsigned long address, pte_t *ptep)
{
	return __huge_ptep_get_and_clear(vma->vm_mm, address, ptep);
}

#define  __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
static inline int huge_ptep_set_access_flags(struct vm_area_struct *vma,
					     unsigned long addr, pte_t *ptep,
					     pte_t pte, int dirty)
{
	int changed = !pte_same(huge_ptep_get(vma->vm_mm, addr, ptep), pte);

	if (changed) {
		__huge_ptep_get_and_clear(vma->vm_mm, addr, ptep);
		__set_huge_pte_at(vma->vm_mm, addr, ptep, pte);
	}
	return changed;
}

#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	pte_t pte = __huge_ptep_get_and_clear(mm, addr, ptep);

	__set_huge_pte_at(mm, addr, ptep, pte_wrprotect(pte));
}

#define __HAVE_ARCH_HUGE_PTE_MKUFFD_WP
static inline pte_t huge_pte_mkuffd_wp(pte_t pte)
{
	return pte;
}

#define __HAVE_ARCH_HUGE_PTE_CLEAR_UFFD_WP
static inline pte_t huge_pte_clear_uffd_wp(pte_t pte)
{
	return pte;
}

#define __HAVE_ARCH_HUGE_PTE_UFFD_WP
static inline int huge_pte_uffd_wp(pte_t pte)
{
	return 0;
}

#include <asm-generic/hugetlb.h>

#endif /* _ASM_S390_HUGETLB_H */
