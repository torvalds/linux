/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CRIS_TLBFLUSH_H
#define _CRIS_TLBFLUSH_H

#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

/*
 * TLB flushing (implemented in arch/cris/mm/tlb.c):
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 */

extern void __flush_tlb_all(void);
extern void __flush_tlb_mm(struct mm_struct *mm);
extern void __flush_tlb_page(struct vm_area_struct *vma,
			   unsigned long addr);

#define flush_tlb_all __flush_tlb_all
#define flush_tlb_mm __flush_tlb_mm
#define flush_tlb_page __flush_tlb_page

static inline void flush_tlb_range(struct vm_area_struct * vma, unsigned long start, unsigned long end)
{
	flush_tlb_mm(vma->vm_mm);
}

static inline void flush_tlb(void)
{
	flush_tlb_mm(current->mm);
}

#define flush_tlb_kernel_range(start, end) flush_tlb_all()

#endif /* _CRIS_TLBFLUSH_H */
