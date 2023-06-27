/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_HUGETLB_H
#define _ASM_RISCV_HUGETLB_H

#include <asm/page.h>

static inline void arch_clear_hugepage_flags(struct page *page)
{
	clear_bit(PG_dcache_clean, &page->flags);
}
#define arch_clear_hugepage_flags arch_clear_hugepage_flags

#ifdef CONFIG_RISCV_ISA_SVNAPOT
#define __HAVE_ARCH_HUGE_PTE_CLEAR
void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
		    pte_t *ptep, unsigned long sz);

#define __HAVE_ARCH_HUGE_SET_HUGE_PTE_AT
void set_huge_pte_at(struct mm_struct *mm,
		     unsigned long addr, pte_t *ptep, pte_t pte);

#define __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
			      unsigned long addr, pte_t *ptep);

#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
			    unsigned long addr, pte_t *ptep);

#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
void huge_ptep_set_wrprotect(struct mm_struct *mm,
			     unsigned long addr, pte_t *ptep);

#define __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
int huge_ptep_set_access_flags(struct vm_area_struct *vma,
			       unsigned long addr, pte_t *ptep,
			       pte_t pte, int dirty);

#define __HAVE_ARCH_HUGE_PTEP_GET
pte_t huge_ptep_get(pte_t *ptep);

pte_t arch_make_huge_pte(pte_t entry, unsigned int shift, vm_flags_t flags);
#define arch_make_huge_pte arch_make_huge_pte

#endif /*CONFIG_RISCV_ISA_SVNAPOT*/

#include <asm-generic/hugetlb.h>

#endif /* _ASM_RISCV_HUGETLB_H */
