/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __UM_TLBFLUSH_H
#define __UM_TLBFLUSH_H

#include <linux/mm.h>

/*
 * In UML, we need to sync the TLB over by using mmap/munmap/mprotect syscalls
 * from the process handling the MM (which can be the kernel itself).
 *
 * To track updates, we can hook into set_ptes and flush_tlb_*. With set_ptes
 * we catch all PTE transitions where memory that was unusable becomes usable.
 * While with flush_tlb_* we can track any memory that becomes unusable and
 * even if a higher layer of the page table was modified.
 *
 * So, we simply track updates using both methods and mark the memory area to
 * be synced later on. The only special case is that flush_tlb_kern_* needs to
 * be executed immediately as there is no good synchronization point in that
 * case. In contrast, in the set_ptes case we can wait for the next kernel
 * segfault before we do the synchornization.
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 */

extern int um_tlb_sync(struct mm_struct *mm);

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long address)
{
	um_tlb_mark_sync(vma->vm_mm, address, address + PAGE_SIZE);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	um_tlb_mark_sync(vma->vm_mm, start, end);
}

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	um_tlb_mark_sync(&init_mm, start, end);

	/* Kernel needs to be synced immediately */
	um_tlb_sync(&init_mm);
}

#endif
