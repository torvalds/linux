/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H
#define _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H

#define MMU_NO_CONTEXT	~0UL


#include <asm/book3s/64/tlbflush-hash.h>
#include <asm/book3s/64/tlbflush-radix.h>

#define __HAVE_ARCH_FLUSH_PMD_TLB_RANGE
static inline void flush_pmd_tlb_range(struct vm_area_struct *vma,
				       unsigned long start, unsigned long end)
{
	if (radix_enabled())
		return radix__flush_pmd_tlb_range(vma, start, end);
	return hash__flush_tlb_range(vma, start, end);
}

#define __HAVE_ARCH_FLUSH_HUGETLB_TLB_RANGE
static inline void flush_hugetlb_tlb_range(struct vm_area_struct *vma,
					   unsigned long start,
					   unsigned long end)
{
	if (radix_enabled())
		return radix__flush_hugetlb_tlb_range(vma, start, end);
	return hash__flush_tlb_range(vma, start, end);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	if (radix_enabled())
		return radix__flush_tlb_range(vma, start, end);
	return hash__flush_tlb_range(vma, start, end);
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	if (radix_enabled())
		return radix__flush_tlb_kernel_range(start, end);
	return hash__flush_tlb_kernel_range(start, end);
}

static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	if (radix_enabled())
		return radix__local_flush_tlb_mm(mm);
	return hash__local_flush_tlb_mm(mm);
}

static inline void local_flush_tlb_page(struct vm_area_struct *vma,
					unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__local_flush_tlb_page(vma, vmaddr);
	return hash__local_flush_tlb_page(vma, vmaddr);
}

static inline void local_flush_all_mm(struct mm_struct *mm)
{
	if (radix_enabled())
		return radix__local_flush_all_mm(mm);
	return hash__local_flush_all_mm(mm);
}

static inline void tlb_flush(struct mmu_gather *tlb)
{
	if (radix_enabled())
		return radix__tlb_flush(tlb);
	return hash__tlb_flush(tlb);
}

#ifdef CONFIG_SMP
static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (radix_enabled())
		return radix__flush_tlb_mm(mm);
	return hash__flush_tlb_mm(mm);
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long vmaddr)
{
	if (radix_enabled())
		return radix__flush_tlb_page(vma, vmaddr);
	return hash__flush_tlb_page(vma, vmaddr);
}

static inline void flush_all_mm(struct mm_struct *mm)
{
	if (radix_enabled())
		return radix__flush_all_mm(mm);
	return hash__flush_all_mm(mm);
}
#else
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_page(vma, addr)	local_flush_tlb_page(vma, addr)
#define flush_all_mm(mm)		local_flush_all_mm(mm)
#endif /* CONFIG_SMP */
/*
 * flush the page walk cache for the address
 */
static inline void flush_tlb_pgtable(struct mmu_gather *tlb, unsigned long address)
{
	/*
	 * Flush the page table walk cache on freeing a page table. We already
	 * have marked the upper/higher level page table entry none by now.
	 * So it is safe to flush PWC here.
	 */
	if (!radix_enabled())
		return;

	radix__flush_tlb_pwc(tlb, address);
}
#endif /*  _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H */
