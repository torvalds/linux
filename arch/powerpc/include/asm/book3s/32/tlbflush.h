/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_TLBFLUSH_H
#define _ASM_POWERPC_BOOK3S_32_TLBFLUSH_H

#include <linux/build_bug.h>

#define MMU_NO_CONTEXT      (0)
/*
 * TLB flushing for "classic" hash-MMU 32-bit CPUs, 6xx, 7xx, 7xxx
 */
void hash__flush_tlb_mm(struct mm_struct *mm);
void hash__flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
void hash__flush_range(struct mm_struct *mm, unsigned long start, unsigned long end);

#ifdef CONFIG_SMP
void _tlbie(unsigned long address);
#else
static inline void _tlbie(unsigned long address)
{
	asm volatile ("tlbie %0; sync" : : "r" (address) : "memory");
}
#endif
void _tlbia(void);

/*
 * Called at the end of a mmu_gather operation to make sure the
 * TLB flush is completely done.
 */
static inline void tlb_flush(struct mmu_gather *tlb)
{
	/* 603 needs to flush the whole TLB here since it doesn't use a hash table. */
	if (!mmu_has_feature(MMU_FTR_HPTE_TABLE))
		_tlbia();
}

static inline void flush_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	start &= PAGE_MASK;
	if (mmu_has_feature(MMU_FTR_HPTE_TABLE))
		hash__flush_range(mm, start, end);
	else if (end - start <= PAGE_SIZE)
		_tlbie(start);
	else
		_tlbia();
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mmu_has_feature(MMU_FTR_HPTE_TABLE))
		hash__flush_tlb_mm(mm);
	else
		_tlbia();
}

static inline void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	if (mmu_has_feature(MMU_FTR_HPTE_TABLE))
		hash__flush_tlb_page(vma, vmaddr);
	else
		_tlbie(vmaddr);
}

static inline void
flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	flush_range(vma->vm_mm, start, end);
}

static inline void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_range(&init_mm, start, end);
}

static inline void local_flush_tlb_page(struct vm_area_struct *vma,
					unsigned long vmaddr)
{
	flush_tlb_page(vma, vmaddr);
}

static inline void local_flush_tlb_page_psize(struct mm_struct *mm,
					      unsigned long vmaddr, int psize)
{
	BUILD_BUG();
}

static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	flush_tlb_mm(mm);
}

#endif /* _ASM_POWERPC_BOOK3S_32_TLBFLUSH_H */
