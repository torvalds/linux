#ifndef _ASM_POWERPC_TLBFLUSH_H
#define _ASM_POWERPC_TLBFLUSH_H

/*
 * TLB flushing:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - local_flush_tlb_mm(mm, full) flushes the specified mm context on
 *                           the local processor
 *  - local_flush_tlb_page(vma, vmaddr) flushes one page on the local processor
 *  - flush_tlb_page_nohash(vma, vmaddr) flushes one page if SW loaded TLB
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifdef __KERNEL__

#ifdef CONFIG_PPC_MMU_NOHASH
/*
 * TLB flushing for software loaded TLB chips
 *
 * TODO: (CONFIG_FSL_BOOKE) determine if flush_tlb_range &
 * flush_tlb_kernel_range are best implemented as tlbia vs
 * specific tlbie's
 */

struct vm_area_struct;
struct mm_struct;

#define MMU_NO_CONTEXT      	((unsigned int)-1)

extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);

extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);

extern void __local_flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
				   int tsize, int ind);

#ifdef CONFIG_SMP
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void __flush_tlb_page(struct mm_struct *mm, unsigned long vmaddr,
			     int tsize, int ind);
#else
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_page(vma,addr)	local_flush_tlb_page(vma,addr)
#define __flush_tlb_page(mm,addr,p,i)	__local_flush_tlb_page(mm,addr,p,i)
#endif

#elif defined(CONFIG_PPC_STD_MMU_32)

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

#elif defined(CONFIG_PPC_STD_MMU_64)
#include <asm/book3s/64/tlbflush.h>
#else
#error Unsupported MMU type
#endif

#endif /*__KERNEL__ */
#endif /* _ASM_POWERPC_TLBFLUSH_H */
