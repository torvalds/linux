/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_32_TLBFLUSH_H
#define _ASM_POWERPC_BOOK3S_32_TLBFLUSH_H

#define MMU_NO_CONTEXT      (0)
/*
 * TLB flushing for "classic" hash-MMU 32-bit CPUs, 6xx, 7xx, 7xxx
 */
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void flush_tlb_page_nohash(struct vm_area_struct *vma, unsigned long addr);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
static inline void local_flush_tlb_page(struct vm_area_struct *vma,
					unsigned long vmaddr)
{
	flush_tlb_page(vma, vmaddr);
}
static inline void local_flush_tlb_mm(struct mm_struct *mm)
{
	flush_tlb_mm(mm);
}

#endif /* _ASM_POWERPC_TLBFLUSH_H */
