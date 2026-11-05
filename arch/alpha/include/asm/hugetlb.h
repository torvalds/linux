/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_HUGETLB_H
#define _ALPHA_HUGETLB_H

#include <asm/page.h>

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags);
#define arch_make_huge_pte arch_make_huge_pte

#define __HAVE_ARCH_HUGE_PTEP_GET
extern pte_t huge_ptep_get(struct mm_struct *mm, unsigned long addr,
			   pte_t *ptep);
#define __HAVE_ARCH_HUGE_SET_HUGE_PTE_AT
extern void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
			    pte_t *ptep, pte_t pte, unsigned long sz);
#define __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
extern pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				     pte_t *ptep, unsigned long sz);
#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
extern pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
				   unsigned long addr, pte_t *ptep);
#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
extern void huge_ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				    pte_t *ptep);
#define __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
extern int huge_ptep_set_access_flags(struct vm_area_struct *vma,
				      unsigned long addr, pte_t *ptep,
				      pte_t pte, int dirty);
#define __HAVE_ARCH_HUGE_PTE_CLEAR
extern void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
			   pte_t *ptep, unsigned long sz);

/*
 * The generic version does not flush, which would leave the old translation
 * live across a restrictive change to PTE bits <15:0>.
 */
#define huge_ptep_modify_prot_start huge_ptep_modify_prot_start
extern pte_t huge_ptep_modify_prot_start(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep);

#include <asm-generic/hugetlb.h>

#endif /* _ALPHA_HUGETLB_H */
