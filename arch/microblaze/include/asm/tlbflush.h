/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_TLBFLUSH_H
#define _ASM_MICROBLAZE_TLBFLUSH_H

#include <linux/sched.h>
#include <linux/threads.h>
#include <asm/processor.h>	/* For TASK_SIZE */
#include <asm/mmu.h>
#include <asm/page.h>

extern void _tlbie(unsigned long address);
extern void _tlbia(void);

#define __tlbia()	{ preempt_disable(); _tlbia(); preempt_enable(); }
#define __tlbie(x)	{ _tlbie(x); }

static inline void local_flush_tlb_all(void)
	{ __tlbia(); }
static inline void local_flush_tlb_mm(struct mm_struct *mm)
	{ __tlbia(); }
static inline void local_flush_tlb_page(struct vm_area_struct *vma,
				unsigned long vmaddr)
	{ __tlbie(vmaddr); }
static inline void local_flush_tlb_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
	{ __tlbia(); }

#define flush_tlb_kernel_range(start, end)	do { } while (0)

#define update_mmu_cache(vma, addr, ptep)	do { } while (0)

#define flush_tlb_all local_flush_tlb_all
#define flush_tlb_mm local_flush_tlb_mm
#define flush_tlb_page local_flush_tlb_page
#define flush_tlb_range local_flush_tlb_range

/*
 * This is called in munmap when we have freed up some page-table
 * pages.  We don't need to do anything here, there's nothing special
 * about our page-table pages.  -- paulus
 */
static inline void flush_tlb_pgtables(struct mm_struct *mm,
	unsigned long start, unsigned long end) { }

#endif /* _ASM_MICROBLAZE_TLBFLUSH_H */
