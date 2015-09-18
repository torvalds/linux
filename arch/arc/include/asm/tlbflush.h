/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_TLBFLUSH__
#define __ASM_ARC_TLBFLUSH__

#include <linux/mm.h>

void local_flush_tlb_all(void);
void local_flush_tlb_mm(struct mm_struct *mm);
void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page);
void local_flush_tlb_kernel_range(unsigned long start, unsigned long end);
void local_flush_tlb_range(struct vm_area_struct *vma,
			   unsigned long start, unsigned long end);

#ifndef CONFIG_SMP
#define flush_tlb_range(vma, s, e)	local_flush_tlb_range(vma, s, e)
#define flush_tlb_page(vma, page)	local_flush_tlb_page(vma, page)
#define flush_tlb_kernel_range(s, e)	local_flush_tlb_kernel_range(s, e)
#define flush_tlb_all()			local_flush_tlb_all()
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#else
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
							 unsigned long end);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long page);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
#endif /* CONFIG_SMP */
#endif
