/*
 * arch/sh/mm/tlb-flush_64.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Richard Curnow (/proc/tlb, bug fixes)
 * Copyright (C) 2003 - 2012 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/signal.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/perf_event.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/tlb.h>
#include <linux/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

void local_flush_tlb_one(unsigned long asid, unsigned long page)
{
	unsigned long long match, pteh=0, lpage;
	unsigned long tlb;

	/*
	 * Sign-extend based on neff.
	 */
	lpage = neff_sign_extend(page);
	match = (asid << PTEH_ASID_SHIFT) | PTEH_VALID;
	match |= lpage;

	for_each_itlb_entry(tlb) {
		asm volatile ("getcfg	%1, 0, %0"
			      : "=r" (pteh)
			      : "r" (tlb) );

		if (pteh == match) {
			__flush_tlb_slot(tlb);
			break;
		}
	}

	for_each_dtlb_entry(tlb) {
		asm volatile ("getcfg	%1, 0, %0"
			      : "=r" (pteh)
			      : "r" (tlb) );

		if (pteh == match) {
			__flush_tlb_slot(tlb);
			break;
		}

	}
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	unsigned long flags;

	if (vma->vm_mm) {
		page &= PAGE_MASK;
		local_irq_save(flags);
		local_flush_tlb_one(get_asid(), page);
		local_irq_restore(flags);
	}
}

void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			   unsigned long end)
{
	unsigned long flags;
	unsigned long long match, pteh=0, pteh_epn, pteh_low;
	unsigned long tlb;
	unsigned int cpu = smp_processor_id();
	struct mm_struct *mm;

	mm = vma->vm_mm;
	if (cpu_context(cpu, mm) == NO_CONTEXT)
		return;

	local_irq_save(flags);

	start &= PAGE_MASK;
	end &= PAGE_MASK;

	match = (cpu_asid(cpu, mm) << PTEH_ASID_SHIFT) | PTEH_VALID;

	/* Flush ITLB */
	for_each_itlb_entry(tlb) {
		asm volatile ("getcfg	%1, 0, %0"
			      : "=r" (pteh)
			      : "r" (tlb) );

		pteh_epn = pteh & PAGE_MASK;
		pteh_low = pteh & ~PAGE_MASK;

		if (pteh_low == match && pteh_epn >= start && pteh_epn <= end)
			__flush_tlb_slot(tlb);
	}

	/* Flush DTLB */
	for_each_dtlb_entry(tlb) {
		asm volatile ("getcfg	%1, 0, %0"
			      : "=r" (pteh)
			      : "r" (tlb) );

		pteh_epn = pteh & PAGE_MASK;
		pteh_low = pteh & ~PAGE_MASK;

		if (pteh_low == match && pteh_epn >= start && pteh_epn <= end)
			__flush_tlb_slot(tlb);
	}

	local_irq_restore(flags);
}

void local_flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long flags;
	unsigned int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) == NO_CONTEXT)
		return;

	local_irq_save(flags);

	cpu_context(cpu, mm) = NO_CONTEXT;
	if (mm == current->mm)
		activate_context(mm, cpu);

	local_irq_restore(flags);
}

void local_flush_tlb_all(void)
{
	/* Invalidate all, including shared pages, excluding fixed TLBs */
	unsigned long flags, tlb;

	local_irq_save(flags);

	/* Flush each ITLB entry */
	for_each_itlb_entry(tlb)
		__flush_tlb_slot(tlb);

	/* Flush each DTLB entry */
	for_each_dtlb_entry(tlb)
		__flush_tlb_slot(tlb);

	local_irq_restore(flags);
}

void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
        /* FIXME: Optimize this later.. */
        flush_tlb_all();
}

void __flush_tlb_global(void)
{
	flush_tlb_all();
}
