/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_HUGETLB_H
#define _ASM_POWERPC_HUGETLB_H

#ifdef CONFIG_HUGETLB_PAGE
#include <asm/page.h>

#ifdef CONFIG_PPC_BOOK3S_64
#include <asm/book3s/64/hugetlb.h>
#elif defined(CONFIG_PPC_E500)
#include <asm/nohash/hugetlb-e500.h>
#elif defined(CONFIG_PPC_8xx)
#include <asm/nohash/32/hugetlb-8xx.h>
#endif /* CONFIG_PPC_BOOK3S_64 */

extern bool hugetlb_disabled;

void __init hugetlbpage_init_defaultsize(void);

int slice_is_hugepage_only_range(struct mm_struct *mm, unsigned long addr,
			   unsigned long len);

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	if (IS_ENABLED(CONFIG_PPC_64S_HASH_MMU) && !radix_enabled())
		return slice_is_hugepage_only_range(mm, addr, len);
	return 0;
}
#define is_hugepage_only_range is_hugepage_only_range

#define __HAVE_ARCH_HUGETLB_FREE_PGD_RANGE
void hugetlb_free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);

#define __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep)
{
	return __pte(pte_update(mm, addr, ptep, ~0UL, 0, 1));
}

#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
static inline pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
					  unsigned long addr, pte_t *ptep)
{
	pte_t pte;

	pte = huge_ptep_get_and_clear(vma->vm_mm, addr, ptep);
	flush_hugetlb_page(vma, addr);
	return pte;
}

#define __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
int huge_ptep_set_access_flags(struct vm_area_struct *vma,
			       unsigned long addr, pte_t *ptep,
			       pte_t pte, int dirty);

void gigantic_hugetlb_cma_reserve(void) __init;
#include <asm-generic/hugetlb.h>

#else /* ! CONFIG_HUGETLB_PAGE */
static inline void flush_hugetlb_page(struct vm_area_struct *vma,
				      unsigned long vmaddr)
{
}

#define hugepd_shift(x) 0
static inline pte_t *hugepte_offset(hugepd_t hpd, unsigned long addr,
				    unsigned pdshift)
{
	return NULL;
}


static inline void __init gigantic_hugetlb_cma_reserve(void)
{
}

static inline void __init hugetlbpage_init_defaultsize(void)
{
}
#endif /* CONFIG_HUGETLB_PAGE */

#endif /* _ASM_POWERPC_HUGETLB_H */
