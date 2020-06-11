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
	p4d_t *p4d, *p4d_k;
	pud_t *pud, *pud_k;
	pmd_t *pmd, *pmd_k;

	pgd = pgd_offset_fast(current->active_mm, address);
	pgd_k = pgd_offset_k(address);

	if (!pgd_present(*pgd_k))
		goto bad_area;

	p4d = p4d_offset(pgd, address);
	p4d_k = p4d_offset(pgd_k, address);
	if (!p4d_present(*p4d_k))
		goto bad_area;

	pud = pud_offset(p4d, address);
	pud_k = pud_offset(p4d_k, address);
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
	int sig, si_code = SEGV_MAPERR;
	unsigned int write = 0, exec = 0, mask;
	vm_fault_t fault = VM_FAULT_SIGSEGV;	/* handle_mm_fault() output */
	unsigned int flags;			/* handle_mm_fault() input */

	/*
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 */
	if (address >= VMALLOC_START && !user_mode(regs)) {
		if (unlikely(handle_kernel_vaddr_fault(address)))
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

	if (regs->ecr_cause & ECR_C_PROTV_STORE)	/* ST/EX */
		write = 1;
	else if ((regs->ecr_vec == ECR_V_PROTV) &&
	         (regs->ecr_cause == ECR_C_PROTV_INST_FETCH))
		exec = 1;

	flags = FAULT_FLAG_DEFAULT;
	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
	if (write)
		flags |= FAULT_FLAG_WRITE;

retry:
	mmap_read_lock(mm);

	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (unlikely(address < vma->vm_start)) {
		if (!(vma->vm_flags & VM_GROWSDOWN) || expand_stack(vma, address))
			goto bad_area;
	}

	/*
	 * vm_area is good, now check permissions for this memory access
	 */
	mask = VM_READ;
	if (write)
		mask = VM_WRITE;
	if (exec)
		mask = VM_EXEC;

	if (!(vma->vm_flags & mask)) {
		si_code = SEGV_ACCERR;
		goto bad_area;
	}

	fault = handle_mm_fault(vma, address, flags);

	/* Quick path to respond to signals */
	if (fault_signal_pending(fault, regs)) {
		if (!user_mode(regs))
			goto no_context;
		return;
	}

	/*
	 * Fault retry nuances, mmap_lock already relinquished by core mm
	 */
	if (unlikely((fault & VM_FAULT_RETRY) &&
		     (flags & FAULT_FLAG_ALLOW_RETRY))) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}

bad_area:
	mmap_read_unlock(mm);

	/*
	 * Major/minor page fault accounting
	 * (in case of retry we only land here once)
	 */
	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);

	if (likely(!(fault & VM_FAULT_ERROR))) {
		if (fault & VM_FAULT_MAJOR) {
			tsk->maj_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1,
				      regs, address);
		} else {
			tsk->min_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1,
				      regs, address);
		}

		/* Normal return path: fault Handled Gracefully */
		return;
	}

	if (!user_mode(regs))
		goto no_context;

	if (fault & VM_FAULT_OOM) {
		pagefault_out_of_memory();
		return;
	}

	if (fault & VM_FAULT_SIGBUS) {
		sig = SIGBUS;
		si_code = BUS_ADRERR;
	}
	else {
		sig = SIGSEGV;
	}

	tsk->thread.fault_address = address;
	force_sig_fault(sig, si_code, (void __user *)address);
	return;

no_context:
	if (fixup_exception(regs))
		return;

	die("Oops", regs, address);
}
