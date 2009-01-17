/*
 * This file contains the routines for TLB flushing.
 * On machines where the MMU does not use a hash table to store virtual to
 * physical translations (ie, SW loaded TLBs or Book3E compilant processors,
 * this does -not- include 603 however which shares the implementation with
 * hash based processors)
 *
 *  -- BenH
 *
 * Copyright 2008 Ben Herrenschmidt <benh@kernel.crashing.org>
 *                IBM Corp.
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>

#include <asm/tlbflush.h>
#include <asm/tlb.h>

#include "mmu_decl.h"

/*
 * Base TLB flushing operations:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes kernel pages
 *
 *  - local_* variants of page and mm only apply to the current
 *    processor
 */

/*
 * These are the base non-SMP variants of page and mm flushing
 */
void local_flush_tlb_mm(struct mm_struct *mm)
{
	unsigned int pid;

	preempt_disable();
	pid = mm->context.id;
	if (pid != MMU_NO_CONTEXT)
		_tlbil_pid(pid);
	preempt_enable();
}
EXPORT_SYMBOL(local_flush_tlb_mm);

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	unsigned int pid;

	preempt_disable();
	pid = vma ? vma->vm_mm->context.id : 0;
	if (pid != MMU_NO_CONTEXT)
		_tlbil_va(vmaddr, pid);
	preempt_enable();
}
EXPORT_SYMBOL(local_flush_tlb_page);


/*
 * And here are the SMP non-local implementations
 */
#ifdef CONFIG_SMP

static DEFINE_SPINLOCK(tlbivax_lock);

struct tlb_flush_param {
	unsigned long addr;
	unsigned int pid;
};

static void do_flush_tlb_mm_ipi(void *param)
{
	struct tlb_flush_param *p = param;

	_tlbil_pid(p ? p->pid : 0);
}

static void do_flush_tlb_page_ipi(void *param)
{
	struct tlb_flush_param *p = param;

	_tlbil_va(p->addr, p->pid);
}


/* Note on invalidations and PID:
 *
 * We snapshot the PID with preempt disabled. At this point, it can still
 * change either because:
 * - our context is being stolen (PID -> NO_CONTEXT) on another CPU
 * - we are invaliating some target that isn't currently running here
 *   and is concurrently acquiring a new PID on another CPU
 * - some other CPU is re-acquiring a lost PID for this mm
 * etc...
 *
 * However, this shouldn't be a problem as we only guarantee
 * invalidation of TLB entries present prior to this call, so we
 * don't care about the PID changing, and invalidating a stale PID
 * is generally harmless.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	cpumask_t cpu_mask;
	unsigned int pid;

	preempt_disable();
	pid = mm->context.id;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto no_context;
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);
	if (!cpus_empty(cpu_mask)) {
		struct tlb_flush_param p = { .pid = pid };
		smp_call_function_mask(cpu_mask, do_flush_tlb_mm_ipi, &p, 1);
	}
	_tlbil_pid(pid);
 no_context:
	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_mm);

void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	cpumask_t cpu_mask;
	unsigned int pid;

	preempt_disable();
	pid = vma ? vma->vm_mm->context.id : 0;
	if (unlikely(pid == MMU_NO_CONTEXT))
		goto bail;
	cpu_mask = vma->vm_mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);
	if (!cpus_empty(cpu_mask)) {
		/* If broadcast tlbivax is supported, use it */
		if (mmu_has_feature(MMU_FTR_USE_TLBIVAX_BCAST)) {
			int lock = mmu_has_feature(MMU_FTR_LOCK_BCAST_INVAL);
			if (lock)
				spin_lock(&tlbivax_lock);
			_tlbivax_bcast(vmaddr, pid);
			if (lock)
				spin_unlock(&tlbivax_lock);
			goto bail;
		} else {
			struct tlb_flush_param p = { .pid = pid, .addr = vmaddr };
			smp_call_function_mask(cpu_mask,
					       do_flush_tlb_page_ipi, &p, 1);
		}
	}
	_tlbil_va(vmaddr, pid);
 bail:
	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_page);

#endif /* CONFIG_SMP */

/*
 * Flush kernel TLB entries in the given range
 */
void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
#ifdef CONFIG_SMP
	preempt_disable();
	smp_call_function(do_flush_tlb_mm_ipi, NULL, 1);
	_tlbil_pid(0);
	preempt_enable();
#else
	_tlbil_pid(0);
#endif
}
EXPORT_SYMBOL(flush_tlb_kernel_range);

/*
 * Currently, for range flushing, we just do a full mm flush. This should
 * be optimized based on a threshold on the size of the range, since
 * some implementation can stack multiple tlbivax before a tlbsync but
 * for now, we keep it that way
 */
void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)

{
	flush_tlb_mm(vma->vm_mm);
}
EXPORT_SYMBOL(flush_tlb_range);
