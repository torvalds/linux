/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H
#define _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H

#define MMU_NO_CONTEXT	~0UL

#include <linux/mm_types.h>
#include <asm/book3s/64/tlbflush-hash.h>
#include <asm/book3s/64/tlbflush-radix.h>

/* TLB flush actions. Used as argument to tlbiel_all() */
enum {
	TLB_INVAL_SCOPE_GLOBAL = 0,	/* invalidate all TLBs */
	TLB_INVAL_SCOPE_LPID = 1,	/* invalidate TLBs for current LPID */
};

#ifdef CONFIG_PPC_NATIVE
static inline void tlbiel_all(void)
{
	/*
	 * This is used for host machine check and bootup.
	 *
	 * This uses early_radix_enabled and implementations use
	 * early_cpu_has_feature etc because that works early in boot
	 * and this is the machine check path which is not performance
	 * critical.
	 */
	if (early_radix_enabled())
		radix__tlbiel_all(TLB_INVAL_SCOPE_GLOBAL);
	else
		hash__tlbiel_all(TLB_INVAL_SCOPE_GLOBAL);
}
#else
static inline void tlbiel_all(void) { BUG(); }
#endif

static inline void tlbiel_all_lpid(bool radix)
{
	/*
	 * This is used for guest machine check.
	 */
	if (radix)
		radix__tlbiel_all(TLB_INVAL_SCOPE_LPID);
	else
		hash__tlbiel_all(TLB_INVAL_SCOPE_LPID);
}


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

#define flush_tlb_fix_spurious_fault flush_tlb_fix_spurious_fault
static inline void flush_tlb_fix_spurious_fault(struct vm_area_struct *vma,
						unsigned long address)
{
	/* See ptep_set_access_flags comment */
	if (atomic_read(&vma->vm_mm->context.copros) > 0)
		flush_tlb_page(vma, address);
}

extern bool tlbie_capable;
extern bool tlbie_enabled;

static inline bool cputlb_use_tlbie(void)
{
	return tlbie_enabled;
}

#endif /*  _ASM_POWERPC_BOOK3S_64_TLBFLUSH_H */
