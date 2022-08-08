// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/extable.h>
#include <linux/kprobes.h>
#include <linux/mmu_context.h>
#include <linux/perf_event.h>

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(instruction_pointer(regs));
	if (fixup) {
		regs->pc = fixup->fixup;

		return 1;
	}

	return 0;
}

static inline bool is_write(struct pt_regs *regs)
{
	switch (trap_no(regs)) {
	case VEC_TLBINVALIDS:
		return true;
	case VEC_TLBMODIFIED:
		return true;
	}

	return false;
}

#ifdef CONFIG_CPU_HAS_LDSTEX
static inline void csky_cmpxchg_fixup(struct pt_regs *regs)
{
	return;
}
#else
extern unsigned long csky_cmpxchg_ldw;
extern unsigned long csky_cmpxchg_stw;
static inline void csky_cmpxchg_fixup(struct pt_regs *regs)
{
	if (trap_no(regs) != VEC_TLBMODIFIED)
		return;

	if (instruction_pointer(regs) == csky_cmpxchg_stw)
		instruction_pointer_set(regs, csky_cmpxchg_ldw);
	return;
}
#endif

static inline void no_context(struct pt_regs *regs, unsigned long addr)
{
	current->thread.trap_no = trap_no(regs);

	/* Are we prepared to handle this kernel fault? */
	if (fixup_exception(regs))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	bust_spinlocks(1);
	pr_alert("Unable to handle kernel paging request at virtual "
		 "addr 0x%08lx, pc: 0x%08lx\n", addr, regs->pc);
	die(regs, "Oops");
	do_exit(SIGKILL);
}

static inline void mm_fault_error(struct pt_regs *regs, unsigned long addr, vm_fault_t fault)
{
	current->thread.trap_no = trap_no(regs);

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
	pmd_t *pmd, *pmd_k;
	pte_t *pte_k;
	int offset;

	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs)) {
		do_trap(regs, SIGSEGV, code, addr);
		return;
	}

	/*
	 * Synchronize this task's top level page-table
	 * with the 'reference' page table.
	 *
	 * Do _not_ use "tsk" here. We might be inside
	 * an interrupt in the middle of a task switch..
	 */
	offset = pgd_index(addr);

	pgd = get_pgd() + offset;
	pgd_k = init_mm.pgd + offset;

	if (!pgd_present(*pgd_k)) {
		no_context(regs, addr);
		return;
	}
	set_pgd(pgd, *pgd_k);

	pud = (pud_t *)pgd;
	pud_k = (pud_t *)pgd_k;
	if (!pud_present(*pud_k)) {
		no_context(regs, addr);
		return;
	}

	pmd = pmd_offset(pud, addr);
	pmd_k = pmd_offset(pud_k, addr);
	if (!pmd_present(*pmd_k)) {
		no_context(regs, addr);
		return;
	}
	set_pmd(pmd, *pmd_k);

	pte_k = pte_offset_kernel(pmd_k, addr);
	if (!pte_present(*pte_k)) {
		no_context(regs, addr);
		return;
	}

	flush_tlb_one(addr);
}

static inline bool access_error(struct pt_regs *regs, struct vm_area_struct *vma)
{
	if (is_write(regs)) {
		if (!(vma->vm_flags & VM_WRITE))
			return true;
	} else {
		if (unlikely(!vma_is_accessible(vma)))
			return true;
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
	unsigned long addr = read_mmu_entryhi() & PAGE_MASK;
	unsigned int flags = FAULT_FLAG_DEFAULT;
	int code = SEGV_MAPERR;
	vm_fault_t fault;

	tsk = current;
	mm = tsk->mm;

	csky_cmpxchg_fixup(regs);

	if (kprobe_page_fault(regs, tsk->thread.trap_no))
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
	if (unlikely((addr >= VMALLOC_START) && (addr <= VMALLOC_END))) {
		vmalloc_fault(regs, code, addr);
		return;
	}

	/* Enable interrupts if they were enabled in the parent context. */
	if (likely(regs->sr & BIT(6)))
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

	if (is_write(regs))
		flags |= FAULT_FLAG_WRITE;
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

	if (unlikely(access_error(regs, vma))) {
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
