// SPDX-License-Identifier: GPL-2.0-only
/* Page Fault Handling for ARC (TLB Miss / ProtV)
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/sched/signal.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/uaccess.h>
#include <linux/kdebug.h>
#include <linux/perf_event.h>
#include <linux/mm_types.h>
#include <asm/pgalloc.h>
#include <asm/mmu.h>

/*
 * kernel virtual address is required to implement vmalloc/pkmap/fixmap
 * Refer to asm/processor.h for System Memory Map
 *
 * It simply copies the PMD entry (pointer to 2nd level page table or hugepage)
 * from swapper pgdir to task pgdir. The 2nd level table/page is thus shared
 */
noinline static int handle_kernel_vaddr_fault(unsigned long address)
{
	/*
	 * Synchronize this task's top level page-table
	 * with the 'reference' page table.
	 */
	pgd_t *pgd, *pgd_k;
	pud_t *pud, *pud_k;
	pmd_t *pmd, *pmd_k;

	pgd = pgd_offset_fast(current->active_mm, address);
	pgd_k = pgd_offset_k(address);

	if (!pgd_present(*pgd_k))
		goto bad_area;

	pud = pud_offset(pgd, address);
	pud_k = pud_offset(pgd_k, address);
	if (!pud_present(*pud_k))
		goto bad_area;

	pmd = pmd_offset(pud, address);
	pmd_k = pmd_offset(pud_k, address);
	if (!pmd_present(*pmd_k))
		goto bad_area;

	set_pmd(pmd, *pmd_k);

	/* XXX: create the TLB entry here */
	return 0;

bad_area:
	return 1;
}

void do_page_fault(unsigned long address, struct pt_regs *regs)
{
	struct vm_area_struct *vma = NULL;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	int si_code = SEGV_MAPERR;
	int ret;
	vm_fault_t fault;
	int write = regs->ecr_cause & ECR_C_PROTV_STORE;  /* ST/EX */
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 */
	if (address >= VMALLOC_START && !user_mode(regs)) {
		ret = handle_kernel_vaddr_fault(address);
		if (unlikely(ret))
			goto no_context;
		else
			return;
	}

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (faulthandler_disabled() || !mm)
		goto no_context;

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
retry:
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
good_area:
	si_code = SEGV_ACCERR;

	/* Handle protection violation, execute on heap or stack */

	if ((regs->ecr_vec == ECR_V_PROTV) &&
	    (regs->ecr_cause == ECR_C_PROTV_INST_FETCH))
		goto bad_area;

	if (write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
		flags |= FAULT_FLAG_WRITE;
	} else {
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(vma, address, flags);

	if (fatal_signal_pending(current)) {

		/*
		 * if fault retry, mmap_sem already relinquished by core mm
		 * so OK to return to user mode (with signal handled first)
		 */
		if (fault & VM_FAULT_RETRY) {
			if (!user_mode(regs))
				goto no_context;
			return;
		}
	}

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);

	if (likely(!(fault & VM_FAULT_ERROR))) {
		if (flags & FAULT_FLAG_ALLOW_RETRY) {
			/* To avoid updating stats twice for retry case */
			if (fault & VM_FAULT_MAJOR) {
				tsk->maj_flt++;
				perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1,
					      regs, address);
			} else {
				tsk->min_flt++;
				perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1,
					      regs, address);
			}

			if (fault & VM_FAULT_RETRY) {
				flags &= ~FAULT_FLAG_ALLOW_RETRY;
				flags |= FAULT_FLAG_TRIED;
				goto retry;
			}
		}

		/* Fault Handled Gracefully */
		up_read(&mm->mmap_sem);
		return;
	}

	if (fault & VM_FAULT_OOM)
		goto out_of_memory;
	else if (fault & VM_FAULT_SIGSEGV)
		goto bad_area;
	else if (fault & VM_FAULT_SIGBUS)
		goto do_sigbus;

	/* no man's land */
	BUG();

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up_read(&mm->mmap_sem);

	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs)) {
		tsk->thread.fault_address = address;
		force_sig_fault(SIGSEGV, si_code, (void __user *)address);
		return;
	}

no_context:
	/* Are we prepared to handle this kernel fault?
	 *
	 * (The kernel has valid exception-points in the source
	 *  when it accesses user-memory. When it fails in one
	 *  of those points, we find it in a table and do a jump
	 *  to some fixup code that loads an appropriate error
	 *  code)
	 */
	if (fixup_exception(regs))
		return;

	die("Oops", regs, address);

out_of_memory:
	up_read(&mm->mmap_sem);

	if (user_mode(regs)) {
		pagefault_out_of_memory();
		return;
	}

	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	if (!user_mode(regs))
		goto no_context;

	tsk->thread.fault_address = address;
	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);
}
