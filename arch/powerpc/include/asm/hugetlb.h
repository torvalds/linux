/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_HUGETLB_H
#define _ASM_POWERPC_HUGETLB_H

#ifdef CONFIG_HUGETLB_PAGE
#include <asm/page.h>
#include <asm-generic/hugetlb.h>

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

static inline void __local_flush_hugetlb_page(struct vm_area_struct *vma,
					      unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__local_flush_hugetlb_page(vma, vmaddr);
}
#else

static inline pte_t *hugepd_page(hugepd_t hpd)
{
	BUG_ON(!hugepd_ok(hpd));
#ifdef CONFIG_PPC_8xx
	return (pte_t *)__va(hpd_val(hpd) &
			     ~(_PMD_PAGE_MASK | _PMD_PRESENT_MASK));
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

pte_t *huge_pte_offset_and_shift(struct mm_struct *mm,
				 unsigned long addr, unsigned *shift);

void flush_dcache_icache_hugepage(struct page *page);

#if defined(CONFIG_PPC_MM_SLICES)
int is_hugepage_only_range(struct mm_struct *mm, unsigned long addr,
			   unsigned long len);
#else
static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len)
{
	return 0;
}
#endif

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

void hugetlb_free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
			    unsigned long end, unsigned long floor,
			    unsigned long ceiling);

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

static inline void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, pte_t pte)
{
	set_pte_at(mm, addr, ptep, pte);
}

static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep)
{
#ifdef CONFIG_PPC64
	return __pte(pte_update(mm, addr, ptep, ~0UL, 0, 1));
#else
	return __pte(pte_update(ptep, ~0UL, 0));
#endif
}

static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
					 unsigned long addr, pte_t *ptep)
{
	pte_t pte;
	pte = huge_ptep_get_and_clear(vma->vm_mm, addr, ptep);
	flush_hugetlb_page(vma, addr);
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
#ifdef HUGETLB_NEED_PRELOAD
	/*
	 * The "return 1" forces a call of update_mmu_cache, which will write a
	 * TLB entry.  Without this, platforms that don't do a write of the TLB
	 * entry in the TLB miss handler asm will fault ad infinitum.
	 */
	ptep_set_access_flags(vma, addr, ptep, pte, dirty);
	return 1;
#else
	return ptep_set_access_flags(vma, addr, ptep, pte, dirty);
#endif
}

static inline pte_t huge_ptep_get(pte_t *ptep)
{
	return *ptep;
}

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

#else /* ! CONFIG_HUGETLB_PAGE */
static inline void flush_hugetlb_page(struct vm_area_struct *vma,
				      unsigned long vmaddr)
{
}

#define hugepd_shift(x) 0
static inline pte_t *hugepte_offset(hugepd_t hpd, unsigned long addr,
				    unsigned pdshift)
{
	return 0;
}
#endif /* CONFIG_HUGETLB_PAGE */

#endif /* _ASM_POWERPC_HUGETLB_H */
