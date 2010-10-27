/* MN10300 TLB flushing functions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_TLBFLUSH_H
#define _ASM_TLBFLUSH_H

#include <linux/mm.h>
#include <asm/processor.h>

struct tlb_state {
	struct mm_struct	*active_mm;
	int			state;
};
DECLARE_PER_CPU_SHARED_ALIGNED(struct tlb_state, cpu_tlbstate);

/**
 * local_flush_tlb - Flush the current MM's entries from the local CPU's TLBs
 */
static inline void local_flush_tlb(void)
{
	int w;
	asm volatile(
		"	mov	%1,%0		\n"
		"	or	%2,%0		\n"
		"	mov	%0,%1		\n"
		: "=d"(w)
		: "m"(MMUCTR), "i"(MMUCTR_IIV|MMUCTR_DIV)
		: "cc", "memory");
}

/**
 * local_flush_tlb_all - Flush all entries from the local CPU's TLBs
 */
static inline void local_flush_tlb_all(void)
{
	local_flush_tlb();
}

/**
 * local_flush_tlb_one - Flush one entry from the local CPU's TLBs
 */
static inline void local_flush_tlb_one(unsigned long addr)
{
	local_flush_tlb();
}

/**
 * local_flush_tlb_page - Flush a page's entry from the local CPU's TLBs
 * @mm: The MM to flush for
 * @addr: The address of the target page in RAM (not its page struct)
 */
static inline
void local_flush_tlb_page(struct mm_struct *mm, unsigned long addr)
{
	unsigned long pteu, flags, cnx;

	addr &= PAGE_MASK;

	local_irq_save(flags);

	cnx = 1;
#ifdef CONFIG_MN10300_TLB_USE_PIDR
	cnx = mm->context.tlbpid[smp_processor_id()];
#endif
	if (cnx) {
		pteu = addr;
#ifdef CONFIG_MN10300_TLB_USE_PIDR
		pteu |= cnx & xPTEU_PID;
#endif
		IPTEU = pteu;
		DPTEU = pteu;
		if (IPTEL & xPTEL_V)
			IPTEL = 0;
		if (DPTEL & xPTEL_V)
			DPTEL = 0;
	}
	local_irq_restore(flags);
}

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 */
#ifdef CONFIG_SMP

#include <asm/smp.h>

extern void flush_tlb_all(void);
extern void flush_tlb_current_task(void);
extern void flush_tlb_mm(struct mm_struct *);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);

#define flush_tlb()		flush_tlb_current_task()

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	flush_tlb_mm(vma->vm_mm);
}

#else   /* CONFIG_SMP */

static inline void flush_tlb_all(void)
{
	preempt_disable();
	local_flush_tlb_all();
	preempt_enable();
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	preempt_disable();
	local_flush_tlb_all();
	preempt_enable();
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	preempt_disable();
	local_flush_tlb_all();
	preempt_enable();
}

#define flush_tlb_page(vma, addr)	local_flush_tlb_page((vma)->vm_mm, addr)
#define flush_tlb()			flush_tlb_all()

#endif /* CONFIG_SMP */

static inline void flush_tlb_kernel_range(unsigned long start,
					  unsigned long end)
{
	flush_tlb_all();
}

static inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
}

#endif /* _ASM_TLBFLUSH_H */
