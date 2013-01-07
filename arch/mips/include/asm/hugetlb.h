/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008, 2009 Cavium Networks, Inc.
 */

#ifndef __ASM_HUGETLB_H
#define __ASM_HUGETLB_H

#include <asm/page.h>


static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	return 0;
}

static inline int prepare_hugepage_range(struct file *file,
					 unsigned long addr,
					 unsigned long len)
{
	unsigned long task_size = STACK_TOP;
	struct hstate *h = hstate_file(file);

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (addr & ~huge_page_mask(h))
		return -EINVAL;
	if (len > task_size)
		return -ENOMEM;
	if (task_size - len < addr)
		return -EINVAL;
	return 0;
}

static inline void hugetlb_prefault_arch_hook(struct mm_struct *mm)
{
}

static inline void hugetlb_free_pgd_range(struct mmu_gather *tlb,
					  unsigned long addr,
					  unsigned long end,
					  unsigned long floor,
					  unsigned long ceiling)
{
	free_pgd_range(tlb, addr, end, floor, ceiling);
}

static inline void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, pte_t pte)
{
	set_pte_at(mm, addr, ptep, pte);
}

static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep)
{
	pte_t clear;
	pte_t pte = *ptep;

	pte_val(clear) = (unsigned long)invalid_pte_table;
	set_pte_at(mm, addr, ptep, clear);
	return pte;
}

static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep)
{
	flush_tlb_page(vma, addr & huge_page_mask(hstate_vma(vma)));
}

static inline int huge_pte_none(pte_t pte)
{
	unsigned long val = pte_val(pte) & ~_PAGE_GLOBAL;
	return !val || (val == (unsigned long)invalid_pte_table);
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
					     unsigned long addr,
					     pte_t *ptep, pte_t pte,
					     int dirty)
{
	int changed = !pte_same(*ptep, pte);

	if (changed) {
		set_pte_at(vma->vm_mm, addr, ptep, pte);
		/*
		 * There could be some standard sized pages in there,
		 * get them all.
		 */
		flush_tlb_range(vma, addr, addr + HPAGE_SIZE);
	}
	return changed;
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

#endif /* __ASM_HUGETLB_H */
