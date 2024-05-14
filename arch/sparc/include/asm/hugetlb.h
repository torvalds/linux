/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SPARC64_HUGETLB_H
#define _ASM_SPARC64_HUGETLB_H

#include <asm/page.h>

#ifdef CONFIG_HUGETLB_PAGE
struct pud_huge_patch_entry {
	unsigned int addr;
	unsigned int insn;
};
extern struct pud_huge_patch_entry __pud_huge_patch, __pud_huge_patch_end;
#endif

#define __HAVE_ARCH_HUGE_SET_HUGE_PTE_AT
void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte);

#define __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep);

#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
static inline pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
					  unsigned long addr, pte_t *ptep)
{
	return *ptep;
}

#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	pte_t old_pte = *ptep;
	set_huge_pte_at(mm, addr, ptep, pte_wrprotect(old_pte));
}

#define __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
static inline int huge_ptep_set_access_flags(struct vm_area_struct *vma,
					     unsigned long addr, pte_t *ptep,
					     pte_t pte, int dirty)
{
	int changed = !pte_same(*ptep, pte);
	if (changed) {
		set_huge_pte_at(vma->vm_mm, addr, ptep, pte);
		flush_tlb_page(vma, addr);
	}
	return changed;
}

#define __HAVE_ARCH_HUGETLB_FREE_PGD_RANGE
void hugetlb_free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);

#include <asm-generic/hugetlb.h>

#endif /* _ASM_SPARC64_HUGETLB_H */
