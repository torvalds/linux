/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_HUGETLB_H
#define _ASM_POWERPC_HUGETLB_H

#ifdef CONFIG_HUGETLB_PAGE
#include <asm/page.h>

extern struct kmem_cache *hugepte_cache;

#ifdef CONFIG_PPC_BOOK3S_64

#include <asm/book3s/64/hugetlb.h>
/*
 * This should work for other subarchs too. But right now we use the
 * new format only for 64bit book3s
 */
static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!hugepd_ok(hpd));
	/*
	 * We have only four bits to encode, MMU page size
	 */
	BUILD_BUG_ON((MMU_PAGE_COUNT - 1) > 0xf);
	return __va(hpd_val(hpd) & HUGEPD_ADDR_MASK);
}

static inline unsigned int hugepd_mmu_psize(hugepd_t hpd)
{
	return (hpd_val(hpd) & HUGEPD_SHIFT_MASK) >> 2;
}

static inline unsigned int hugepd_shift(hugepd_t hpd)
{
	return mmu_psize_to_shift(hugepd_mmu_psize(hpd));
}
static inline void flush_hugetlb_page(struct vm_area_struct *vma,
				      unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__flush_hugetlb_page(vma, vmaddr);
}

#else

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!hugepd_ok(hpd));
#ifdef CONFIG_PPC_8xx
	return (pte_t *)__va(hpd_val(hpd) & ~HUGEPD_SHIFT_MASK);
#else
	return (pte_t *)((hpd_val(hpd) &
			  ~HUGEPD_SHIFT_MASK) | PD_HUGE);
#endif
}

static inline unsigned int hugepd_shift(hugepd_t hpd)
{
#ifdef CONFIG_PPC_8xx
	return ((hpd_val(hpd) & _PMD_PAGE_MASK) >> 1) + 17;
#else
	return hpd_val(hpd) & HUGEPD_SHIFT_MASK;
#endif
}

#endif /* CONFIG_PPC_BOOK3S_64 */


static inline pte_t *hugepte_offset(hugepd_t hpd, unsigned long addr,
				    unsigned pdshift)
{
	/*
	 * On FSL BookE, we have multiple higher-level table entries that
	 * point to the same hugepte.  Just use the first one since they're all
	 * identical.  So for that case, idx=0.
	 */
	unsigned long idx = 0;

	pte_t *dir = hugepd_page(hpd);
#ifndef CONFIG_PPC_FSL_BOOK3E
	idx = (addr & ((1UL << pdshift) - 1)) >> hugepd_shift(hpd);
#endif

	return dir + idx;
}

void flush_dcache_icache_hugepage(struct page *page);

int slice_is_hugepage_only_range(struct mm_struct *mm, unsigned long addr,
			   unsigned long len);

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	if (IS_ENABLED(CONFIG_PPC_MM_SLICES) && !radix_enabled())
		return slice_is_hugepage_only_range(mm, addr, len);
	return 0;
}

void book3e_hugetlb_preload(struct vm_area_struct *vma, unsigned long ea,
			    pte_t pte);
#ifdef CONFIG_PPC_8xx
static inline void flush_hugetlb_page(struct vm_area_struct *vma,
				      unsigned long vmaddr)
{
	flush_tlb_page(vma, vmaddr);
}
#else
void flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
#endif

#define __HAVE_ARCH_HUGETLB_FREE_PGD_RANGE
void hugetlb_free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);

#define __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep)
{
#ifdef CONFIG_PPC64
	return __pte(pte_update(mm, addr, ptep, ~0UL, 0, 1));
#else
	return __pte(pte_update(ptep, ~0UL, 0));
#endif
}

#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep)
{
	pte_t pte;
	pte = huge_ptep_get_and_clear(vma->vm_mm, addr, ptep);
	flush_hugetlb_page(vma, addr);
}

#define __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
extern int huge_ptep_set_access_flags(struct vm_area_struct *vma,
				      unsigned long addr, pte_t *ptep,
				      pte_t pte, int dirty);

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

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
#endif /* CONFIG_HUGETLB_PAGE */

#endif /* _ASM_POWERPC_HUGETLB_H */
