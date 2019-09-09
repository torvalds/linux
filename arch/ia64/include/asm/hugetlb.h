/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_HUGETLB_H
#define _ASM_IA64_HUGETLB_H

#include <asm/page.h>

#define __HAVE_ARCH_HUGETLB_FREE_PGD_RANGE
void hugetlb_free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);

#define __HAVE_ARCH_PREPARE_HUGEPAGE_RANGE
int prepare_hugepage_range(struct file *file,
			unsigned long addr, unsigned long len);

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	return (REGION_NUMBER(addr) == RGN_HPAGE ||
		REGION_NUMBER((addr)+(len)-1) == RGN_HPAGE);
}

#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep)
{
}

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

#include <asm-generic/hugetlb.h>

#endif /* _ASM_IA64_HUGETLB_H */
