/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __UM_TLBFLUSH_H
#define __UM_TLBFLUSH_H

#include <linux/mm.h>

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_kernel_vm() flushes the kernel vm area
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 */

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start, 
			    unsigned long end);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long address);
extern void flush_tlb_kernel_vm(void);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
extern void __flush_tlb_one(unsigned long addr);

#endif
