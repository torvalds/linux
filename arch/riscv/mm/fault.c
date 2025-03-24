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
#include <linux/kprobes.h>
#include <linux/kfence.h>
#include <linux/entry-common.h>

#include <asm/ptrace.h>
#include <asm/tlbflush.h>

#include "../kernel/head.h"

static void show_pte(unsigned long addr)
{
	pgd_t *pgdp, pgd;
	p4d_t *p4dp, p4d;
	pud_t *pudp, pud;
	pmd_t *pmdp, pmd;
	pte_t *ptep, pte;
	struct mm_struct *mm = current->mm;

	if (!mm)
		mm = &init_mm;

	pr_alert("Current %s pgtable: %luK pagesize, %d-bit VAs, pgdp=0x%016llx\n",
		 current->comm, PAGE_SIZE / SZ_1K, VA_BITS,
		 mm == &init_mm ? (u64)__pa_symbol(mm->pgd) : virt_to_phys(mm->pgd));

	pgdp = pgd_offset(mm, addr);
	pgd = pgdp_get(pgdp);
	pr_alert("[%016lx] pgd=%016lx", addr, pgd_val(pgd));
	if (pgd_none(pgd) || pgd_bad(pgd) || pgd_leaf(pgd))
		goto out;

	p4dp = p4d_offset(pgdp, addr);
	p4d = p4dp_get(p4dp);
	pr_cont(", p4d=%016lx", p4d_val(p4d));
	if (p4d_none(p4d) || p4d_bad(p4d) || p4d_leaf(p4d))
		goto out;

	pudp = pud_offset(p4dp, addr);
	pud = pudp_get(pudp);
	pr_cont(", pud=%016lx", pud_val(pud));
	if (pud_none(pud) || pud_bad(pud) || pud_leaf(pud))
		goto out;

	pmdp = pmd_offset(pudp, addr);
	pmd = pmdp_get(pmdp);
	pr_cont(", pmd=%016lx", pmd_val(pmd));
	if (pmd_none(pmd) || pmd_bad(pmd) || pmd_leaf(pmd))
		goto out;

	ptep = pte_offset_map(pmdp, addr);
	if (!ptep)
		goto out;

	pte = ptep_get(ptep);
	pr_cont(", pte=%016lx", pte_val(pte));
	pte_unmap(ptep);
out:
	pr_cont("\n");
}

static void die_kernel_fault(const char *msg, unsigned long addr,
		struct pt_regs *regs)
{
	bust_spinlocks(1);

	pr_alert("Unable to handle kernel %s at virtual address " REG_FMT "\n", msg,
		addr);

	bust_spinlocks(0);
	show_pte(addr);
	die(regs, "Oops");
	make_task_dead(SIGKILL);
}

static inline void no_context(struct pt_regs *regs, unsigned long addr)
{
	const char *msg;

	/* Are we prepared to handle this kernel fault? */
	if (fixup_exception(regs))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	if (addr < PAGE_SIZE)
		msg = "NULL pointer dereference";
	else {
		if (kfence_handle_page_fault(addr, regs->cause == EXC_STORE_PAGE_FAULT, regs))
			return;

		msg = "paging request";
	}

	die_kernel_fault(msg, addr, regs);
}

static inline void mm_fault_error(struct pt_regs *regs, unsigned long addr, vm_fault_t fault)
{
	if (!user_mode(regs)) {
		no_context(regs, addr);
		return;
	}

	if (fault & VM_FAULT_OOM) {
		/*
		 * We ran out of memory, call the OOM killer, and return the userspace
		 * (which will retry the fault, or kill us if we got oom-killed).
		 */
		pagefault_out_of_memory();
		return;
	} else if (fault & (VM_FAULT_SIGBUS | VM_FAULT_HWPOISON | VM_FAULT_HWPOISON_LARGE)) {
		/* Kernel mode? Handle exceptions or die */
		do_trap(regs, SIGBUS, BUS_ADRERR, addr);
		return;
	} else if (fault & VM_FAULT_SIGSEGV) {
		do_trap(regs, SIGSEGV, SEGV_MAPERR, addr);
		return;
	}

	BUG();
}

static inline void
bad_area_nosemaphore(struct pt_regs *regs, int code, unsigned long addr)
{
	/*
	 * Something tried to access memory that isn't in our memory map.
	 * Fix it, but check if it's kernel or user first.
	 */
	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs)) {
		do_trap(regs, SIGSEGV, code, addr);
		return;
	}

	no_context(regs, addr);
}

static inline void
bad_area(struct pt_regs *regs, struct mm_struct *mm, int code,
	 unsigned long addr)
{
	mmap_read_unlock(mm);

	bad_area_nosemaphore(regs, code, addr);
}

static inline void vmalloc_fault(struct pt_regs *regs, int code, unsigned long addr)
{
	pgd_t *pgd, *pgd_k;
	pud_t *pud_k;
	p4d_t *p4d_k;
	pmd_t *pmd_k;
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

	if (!pgd_present(pgdp_get(pgd_k))) {
		no_context(regs, addr);
		return;
	}
	set_pgd(pgd, pgdp_get(pgd_k));

	p4d_k = p4d_offset(pgd_k, addr);
	if (!p4d_present(p4dp_get(p4d_k))) {
		no_context(regs, addr);
		return;
	}

	pud_k = pud_offset(p4d_k, addr);
	if (!pud_present(pudp_get(pud_k))) {
		no_context(regs, addr);
		return;
	}
	if (pud_leaf(pudp_get(pud_k)))
		goto flush_tlb;

	/*
	 * Since the vmalloc area is global, it is unnecessary
	 * to copy individual PTEs
	 */
	pmd_k = pmd_offset(pud_k, addr);
	if (!pmd_present(pmdp_get(pmd_k))) {
		no_context(regs, addr);
		return;
	}
	if (pmd_leaf(pmdp_get(pmd_k)))
		goto flush_tlb;

	/*
	 * Make sure the actual PTE exists as well to
	 * catch kernel vmalloc-area accesses to non-mapped
	 * addresses. If we don't do this, this will just
	 * silently loop forever.
	 */
	pte_k = pte_offset_kernel(pmd_k, addr);
	if (!pte_present(ptep_get(pte_k))) {
		no_context(regs, addr);
		return;
	}

	/*
	 * The kernel assumes that TLBs don't cache invalid
	 * entries, but in RISC-V, SFENCE.VMA specifies an
	 * ordering constraint, not a cache flush; it is
	 * necessary even after writing invalid entries.
	 */
flush_tlb:
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
void handle_page_fault(struct pt_regs *regs)
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

	if (kprobe_page_fault(regs, cause))
		return;

	/*
	 * Fault-in kernel-space virtual memory on-demand.
	 * The 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 */
	if ((!IS_ENABLED(CONFIG_MMU) || !IS_ENABLED(CONFIG_64BIT)) &&
	    unlikely(addr >= VMALLOC_START && addr < VMALLOC_END)) {
		vmalloc_fault(regs, code, addr);
		return;
	}

	/* Enable interrupts if they were enabled in the parent context. */
	if (!regs_irqs_disabled(regs))
		local_irq_enable();

	/*
	 * If we're in an interrupt, have no user context, or are running
	 * in an atomic region, then we must not take the fault.
	 */
	if (unlikely(faulthandler_disabled() || !mm)) {
		tsk->thread.bad_cause = cause;
		no_context(regs, addr);
		return;
	}

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;

	if (!user_mode(regs) && addr < TASK_SIZE && unlikely(!(regs->status & SR_SUM))) {
		if (fixup_exception(regs))
			return;

		die_kernel_fault("access to user memory without uaccess routines", addr, regs);
	}

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, addr);

	if (cause == EXC_STORE_PAGE_FAULT)
		flags |= FAULT_FLAG_WRITE;
	else if (cause == EXC_INST_PAGE_FAULT)
		flags |= FAULT_FLAG_INSTRUCTION;
	if (!(flags & FAULT_FLAG_USER))
		goto lock_mmap;

	vma = lock_vma_under_rcu(mm, addr);
	if (!vma)
		goto lock_mmap;

	if (unlikely(access_error(cause, vma))) {
		vma_end_read(vma);
		count_vm_vma_lock_event(VMA_LOCK_SUCCESS);
		tsk->thread.bad_cause = cause;
		bad_area_nosemaphore(regs, SEGV_ACCERR, addr);
		return;
	}

	fault = handle_mm_fault(vma, addr, flags | FAULT_FLAG_VMA_LOCK, regs);
	if (!(fault & (VM_FAULT_RETRY | VM_FAULT_COMPLETED)))
		vma_end_read(vma);

	if (!(fault & VM_FAULT_RETRY)) {
		count_vm_vma_lock_event(VMA_LOCK_SUCCESS);
		goto done;
	}
	count_vm_vma_lock_event(VMA_LOCK_RETRY);
	if (fault & VM_FAULT_MAJOR)
		flags |= FAULT_FLAG_TRIED;

	if (fault_signal_pending(fault, regs)) {
		if (!user_mode(regs))
			no_context(regs, addr);
		return;
	}
lock_mmap:

retry:
	vma = lock_mm_and_find_vma(mm, addr, regs);
	if (unlikely(!vma)) {
		tsk->thread.bad_cause = cause;
		bad_area_nosemaphore(regs, code, addr);
		return;
	}

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it.
	 */
	code = SEGV_ACCERR;

	if (unlikely(access_error(cause, vma))) {
		tsk->thread.bad_cause = cause;
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
	if (fault_signal_pending(fault, regs)) {
		if (!user_mode(regs))
			no_context(regs, addr);
		return;
	}

	/* The fault is fully completed (including releasing mmap lock) */
	if (fault & VM_FAULT_COMPLETED)
		return;

	if (unlikely(fault & VM_FAULT_RETRY)) {
		flags |= FAULT_FLAG_TRIED;

		/*
		 * No need to mmap_read_unlock(mm) as we would
		 * have already released it in __lock_page_or_retry
		 * in mm/filemap.c.
		 */
		goto retry;
	}

	mmap_read_unlock(mm);

done:
	if (unlikely(fault & VM_FAULT_ERROR)) {
		tsk->thread.bad_cause = cause;
		mm_fault_error(regs, addr, fault);
		return;
	}
	return;
}
