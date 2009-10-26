#ifndef _ASM_POWERPC_HUGETLB_H
#define _ASM_POWERPC_HUGETLB_H

#include <asm/page.h>

int is_hugepage_only_range(struct mm_struct *mm, unsigned long addr,
			   unsigned long len);

void hugetlb_free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t pte);

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep);

/*
 * The version of vma_mmu_pagesize() in arch/powerpc/mm/hugetlbpage.c needs
 * to override the version in mm/hugetlb.c
 */
#define vma_mmu_pagesize vma_mmu_pagesize

/*
 * If the arch doesn't supply something else, assume that hugepage
 * size aligned regions are ok without further preparation.
 */
static inline int prepare_hugepage_range(struct file *file,
			unsigned long addr, unsigned long len)
{
	struct hstate *h = hstate_file(file);
	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (addr & ~huge_page_mask(h))
		return -EINVAL;
	return 0;
}

static inline void hugetlb_prefault_arch_hook(struct mm_struct *mm)
{
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

#endif /* _ASM_POWERPC_HUGETLB_H */
