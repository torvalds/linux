#ifndef _ASM_IA64_HUGETLB_H
#define _ASM_IA64_HUGETLB_H

#include <asm/page.h>
#include <asm-generic/hugetlb.h>


void hugetlb_free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);

int prepare_hugepage_range(struct file *file,
			unsigned long addr, unsigned long len);

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	return (REGION_NUMBER(addr) == RGN_HPAGE ||
		REGION_NUMBER((addr)+(len)-1) == RGN_HPAGE);
}

static inline void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, pte_t pte)
{
	set_pte_at(mm, addr, ptep, pte);
}

static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep)
{
	return ptep_get_and_clear(mm, addr, ptep);
}

static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep)
{
}

static inline int huge_pte_none(pte_t pte)
{
	return pte_none(pte);
}

static inline pte_t huge_pte_wrprotect(pte_t pte)
{
	return pte_wrprotect(pte);
}

static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	ptep_set_wrprotect(mm, addr, ptep);
}

static inline int huge_ptep_set_access_flags(struct vm_area_struct *vma,
					     unsigned long addr, pte_t *ptep,
					     pte_t pte, int dirty)
{
	return ptep_set_access_flags(vma, addr, ptep, pte, dirty);
}

static inline pte_t huge_ptep_get(pte_t *ptep)
{
	return *ptep;
}

static inline int arch_prepare_hugepage(struct page *page)
{
	return 0;
}

static inline void arch_release_hugepage(struct page *page)
{
}

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

#endif /* _ASM_IA64_HUGETLB_H */
