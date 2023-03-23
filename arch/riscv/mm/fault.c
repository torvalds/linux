// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *  Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2012 Regents of the University of California
 */


#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/perf_event.h>
#include <linux/signal.h>
#include <linux/uaccess.h>

#include <asm/ptrace.h>
#include <asm/tlbflush.h>

#include "../kernel/head.h"

static inline void no_context(struct pt_regs *regs, unsigned long addr)
{
	/* Are we prepared to handle this kernel fault? */
	if (fixup_exception(regs))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	bust_spinlocks(1);
	pr_alert("Unable to handle kernel %s at virtual address " REG_FMT "\n",
		(addr < PAGE_SIZE) ? "NULL pointer dereference" :
		"paging request", addr);
	die(regs, "Oops");
	make_task_dead(SIGKILL);
}

static inline void mm_fault_error(struct pt_regs *regs, unsigned long addr, vm_fault_t fault)
{
	if (fault & VM_FAULT_OOM) {
		/*
		 * We ran out of memory, call the OOM killer, and return the userspace
		 * (which will retry the fault, or kill us if we got oom-killed).
		 */
		if (!user_mode(regs)) {
			no_context(regs, addr);
			return;
		}
		pagefault_out_of_memory();
		return;
	} else if (fault & VM_FAULT_SIGBUS) {
		/* Kernel mode? Handle exceptions or die */
		if (!user_mode(regs)) {
			no_context(regs, addr);
			return;
		}
		do_trap(regs, SIGBUS, BUS_ADRERR, addr);
		return;
	}
	BUG();
}

static inline void bad_area(struct pt_regs *regs, struct mm_struct *mm, int code, unsigned long addr)
{
	/*
	 * Something tried to access memory that isn't in our memory map.
	 * Fix it, but check if it's kernel or user first.
	 */
	mmap_read_unlock(mm);
	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs)) {
		do_trap(regs, SIGSEGV, code, addr);
		return;
	}

	no_context(regs, addr);
}

static inline void vmalloc_fault(struct pt_regs *regs, int code, unsigned long addr)
{
	pgd_t *pgd, *pgd_k;
	pud_t *pud, *pud_k;
	p4d_t *p4d, *p4d_k;
	pmd_t *pmd, *pmd_k;
	pte_t *pte_k;
	int index;
	unsigned long pfn;

	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs))
		return do_trap(regs, SIGSEGV, code, addr);

	/*
	 * Synchronize this task's top level page-table
	 * with the 'reference' page table.
	 *
	 * Do _not_ use "tsk->active_mm->pgd" here.
	 * We might be inside an interrupt in the middle
	 * of a task switch.
	 */
	index = pgd_index(addr);
	pfn = csr_read(CSR_SATP) & SATP_PPN;
	pgd = (pgd_t *)pfn_to_virt(pfn) + index;
	pgd_k = init_mm.pgd + index;

	if (!pgd_present(*pgd_k)) {
		no_context(regs, addr);
		return;
	}
	set_pgd(pgd, *pgd_k);

	p4d = p4d_offset(pgd, addr);
	p4d_k = p4d_offset(pgd_k, addr);
	if (!p4d_present(*p4d_k)) {
		no_context(regs, addr);
		return;
	}

	pud = pud_offset(p4d, addr);
	pud_k = pud_offset(p4d_k, addr);
	if (!pud_present(*pud_k)) {
		no_context(regs, addr);
		return;
	}

	/*
	 * Since the vmalloc area is global, it is unnecessary
	 * to copy individual PTEs
	 */
	pmd = pmd_offset(pud, addr);
	pmd_k = pmd_offset(pud_k, addr);
	if (!pmd_present(*pmd_k)) {
		no_context(regs, addr);
		return;
	}
	set_pmd(pmd, *pmd_k);

	/*
	 * Make sure the actual PTE exists as well to
	 * catch kernel vmalloc-area accesses to non-mapped
	 * addresses. If we don't do this, this will just
	 * silently loop forever.
	 */
	pte_k = pte_offset_kernel(pmd_k, addr);
	if (!pte_present(*pte_k)) {
		no_context(regs, addr);
		return;
	}

	/*
	 * The kernel assumes that TLBs don't cache invalid
	 * entries, but in RISC-V, SFENCE.VMA specifies an
	 * ordering constraint, not a cache flush; it is
	 * necessary even after writing invalid entries.
	 */
	local_flush_tlb_page(addr);
}

static inline bool access_error(unsigned long cause, struct vm_area_struct *vma)
{
	switch (cause) {
	case EXC_INST_PAGE_FAULT:
		if (!(vma->vm_flags & VM_EXEC)) {
			return true;
		}
		break;
	case EXC_LOAD_PAGE_FAULT:
		/* Write implies read */
		if (!(vma->vm_flags & (VM_READ | VM_WRITE))) {
			return true;
		}
		break;
	case EXC_STORE_PAGE_FAULT:
		if (!(vma->vm_flags & VM_WRITE)) {
			return true;
		}
		break;
	default:
		panic("%s: unhandled cause %lu", __func__, cause);
	}
	return false;
}

/*
 * This routine handles page faults.  It determines the address and the
 * problem, and then passes it off to one of the appropriate routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long addr, cause;
	unsigned int flags = FAULT_FLAG_DEFAULT;
	int code = SEGV_MAPERR;
	vm_fault_t fault;

	cause = regs->cause;
	addr = regs->badaddr;

	tsk = current;
	mm = tsk->mm;

	/*
	 * Fault-in kernel-space virtual memory on-demand.
	 * The 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 */
	if (unlikely((addr >= VMALLOC_START) && (addr <= VMALLOC_END))) {
		vmalloc_fault(regs, code, addr);
		return;
	}

	/* Enable interrupts if they were enabled in the parent context. */
	if (likely(regs->status & SR_PIE))
		local_irq_enable();

	/*
	 * If we're in an interrupt, have no user context, or are running
	 * in an atomic region, then we must not take the fault.
	 */
	if (unlikely(faulthandler_disabled() || !mm)) {
		no_context(regs, addr);
		return;
	}

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, addr);

	if (cause == EXC_STORE_PAGE_FAULT)
		flags |= FAULT_FLAG_WRITE;
	else if (cause == EXC_INST_PAGE_FAULT)
		flags |= FAULT_FLAG_INSTRUCTION;
retry:
	mmap_read_lock(mm);
	vma = find_vma(mm, addr);
	if (unlikely(!vma)) {
		bad_area(regs, mm, code, addr);
		return;
	}
	if (likely(vma->vm_start <= addr))
		goto good_area;
	if (unlikely(!(vma->vm_flags & VM_GROWSDOWN))) {
		bad_area(regs, mm, code, addr);
		return;
	}
	if (unlikely(expand_stack(vma, addr))) {
		bad_area(regs, mm, code, addr);
		return;
	}

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it.
	 */
good_area:
	code = SEGV_ACCERR;

	if (unlikely(access_error(cause, vma))) {
		bad_area(regs, mm, code, addr);
		return;
	}

	/*
	 * If for any reason at all we could not handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(vma, addr, flags, regs);

	/*
	 * If we need to retry but a fatal signal is pending, handle the
	 * signal first. We do not need to release the mmap_lock because it
	 * would already be released in __lock_page_or_retry in mm/filemap.c.
	 */
	if (fault_signal_pending(fault, regs))
		return;

	if (unlikely((fault & VM_FAULT_RETRY) && (flags & FAULT_FLAG_ALLOW_RETRY))) {
		flags |= FAULT_FLAG_TRIED;

		/*
		 * No need to mmap_read_unlock(mm) as we would
		 * have already released it in __lock_page_or_retry
		 * in mm/filemap.c.
		 */
		goto retry;
	}

	mmap_read_unlock(mm);

	if (unlikely(fault & VM_FAULT_ERROR)) {
		mm_fault_error(regs, addr, fault);
		return;
	}
	return;
}
