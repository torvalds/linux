// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory fault handling for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

/*
 * Page fault handling for the Hexagon Virtual Machine.
 * Can also be called by a native port emulating the HVM
 * execptions.
 */

#include <asm/traps.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/extable.h>
#include <linux/hardirq.h>
#include <linux/perf_event.h>

/*
 * Decode of hardware exception sends us to one of several
 * entry points.  At each, we generate canonical arguments
 * for handling by the abstract memory management code.
 */
#define FLT_IFETCH     -1
#define FLT_LOAD        0
#define FLT_STORE       1


/*
 * Canonical page fault handler
 */
void do_page_fault(unsigned long address, long cause, struct pt_regs *regs)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	int si_signo;
	int si_code = SEGV_MAPERR;
	vm_fault_t fault;
	const struct exception_table_entry *fixup;
	unsigned int flags = FAULT_FLAG_DEFAULT;

	/*
	 * If we're in an interrupt or have no user context,
	 * then must not take the fault.
	 */
	if (unlikely(in_interrupt() || !mm))
		goto no_context;

	local_irq_enable();

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);
retry:
	mmap_read_lock(mm);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;

	if (vma->vm_start <= address)
		goto good_area;

	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;

	if (expand_stack(vma, address))
		goto bad_area;

good_area:
	/* Address space is OK.  Now check access rights. */
	si_code = SEGV_ACCERR;

	switch (cause) {
	case FLT_IFETCH:
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
		break;
	case FLT_LOAD:
		if (!(vma->vm_flags & VM_READ))
			goto bad_area;
		break;
	case FLT_STORE:
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
		flags |= FAULT_FLAG_WRITE;
		break;
	}

	fault = handle_mm_fault(vma, address, flags, regs);

	if (fault_signal_pending(fault, regs))
		return;

	/* The most common case -- we are done. */
	if (likely(!(fault & VM_FAULT_ERROR))) {
		if (fault & VM_FAULT_RETRY) {
			flags |= FAULT_FLAG_TRIED;
			goto retry;
		}

		mmap_read_unlock(mm);
		return;
	}

	mmap_read_unlock(mm);

	/* Handle copyin/out exception cases */
	if (!user_mode(regs))
		goto no_context;

	if (fault & VM_FAULT_OOM) {
		pagefault_out_of_memory();
		return;
	}

	/* User-mode address is in the memory map, but we are
	 * unable to fix up the page fault.
	 */
	if (fault & VM_FAULT_SIGBUS) {
		si_signo = SIGBUS;
		si_code = BUS_ADRERR;
	}
	/* Address is not in the memory map */
	else {
		si_signo = SIGSEGV;
		si_code  = SEGV_ACCERR;
	}
	force_sig_fault(si_signo, si_code, (void __user *)address);
	return;

bad_area:
	mmap_read_unlock(mm);

	if (user_mode(regs)) {
		force_sig_fault(SIGSEGV, si_code, (void __user *)address);
		return;
	}
	/* Kernel-mode fault falls through */

no_context:
	fixup = search_exception_tables(pt_elr(regs));
	if (fixup) {
		pt_set_elr(regs, fixup->fixup);
		return;
	}

	/* Things are looking very, very bad now */
	bust_spinlocks(1);
	printk(KERN_EMERG "Unable to handle kernel paging request at "
		"virtual address 0x%08lx, regs %p\n", address, regs);
	die("Bad Kernel VA", regs, SIGKILL);
}


void read_protection_fault(struct pt_regs *regs)
{
	unsigned long badvadr = pt_badva(regs);

	do_page_fault(badvadr, FLT_LOAD, regs);
}

void write_protection_fault(struct pt_regs *regs)
{
	unsigned long badvadr = pt_badva(regs);

	do_page_fault(badvadr, FLT_STORE, regs);
}

void execute_protection_fault(struct pt_regs *regs)
{
	unsigned long badvadr = pt_badva(regs);

	do_page_fault(badvadr, FLT_IFETCH, regs);
}
