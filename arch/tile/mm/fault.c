/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * From i386 code copyright (C) 1995  Linus Torvalds
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>		/* For unblank_screen() */
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/hugetlb.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/traps.h>
#include <asm/syscalls.h>

#include <arch/interrupts.h>

static noinline void force_sig_info_fault(const char *type, int si_signo,
					  int si_code, unsigned long address,
					  int fault_num,
					  struct task_struct *tsk,
					  struct pt_regs *regs)
{
	siginfo_t info;

	if (unlikely(tsk->pid < 2)) {
		panic("Signal %d (code %d) at %#lx sent to %s!",
		      si_signo, si_code & 0xffff, address,
		      is_idle_task(tsk) ? "the idle task" : "init");
	}

	info.si_signo = si_signo;
	info.si_errno = 0;
	info.si_code = si_code;
	info.si_addr = (void __user *)address;
	info.si_trapno = fault_num;
	trace_unhandled_signal(type, regs, address, si_signo);
	force_sig_info(si_signo, &info, tsk);
}

#ifndef __tilegx__
/*
 * Synthesize the fault a PL0 process would get by doing a word-load of
 * an unaligned address or a high kernel address.
 */
SYSCALL_DEFINE2(cmpxchg_badaddr, unsigned long, address,
		struct pt_regs *, regs)
{
	if (address >= PAGE_OFFSET)
		force_sig_info_fault("atomic segfault", SIGSEGV, SEGV_MAPERR,
				     address, INT_DTLB_MISS, current, regs);
	else
		force_sig_info_fault("atomic alignment fault", SIGBUS,
				     BUS_ADRALN, address,
				     INT_UNALIGN_DATA, current, regs);

	/*
	 * Adjust pc to point at the actual instruction, which is unusual
	 * for syscalls normally, but is appropriate when we are claiming
	 * that a syscall swint1 caused a page fault or bus error.
	 */
	regs->pc -= 8;

	/*
	 * Mark this as a caller-save interrupt, like a normal page fault,
	 * so that when we go through the signal handler path we will
	 * properly restore r0, r1, and r2 for the signal handler arguments.
	 */
	regs->flags |= PT_FLAGS_CALLER_SAVES;

	return 0;
}
#endif

static inline pmd_t *vmalloc_sync_one(pgd_t *pgd, unsigned long address)
{
	unsigned index = pgd_index(address);
	pgd_t *pgd_k;
	pud_t *pud, *pud_k;
	pmd_t *pmd, *pmd_k;

	pgd += index;
	pgd_k = init_mm.pgd + index;

	if (!pgd_present(*pgd_k))
		return NULL;

	pud = pud_offset(pgd, address);
	pud_k = pud_offset(pgd_k, address);
	if (!pud_present(*pud_k))
		return NULL;

	pmd = pmd_offset(pud, address);
	pmd_k = pmd_offset(pud_k, address);
	if (!pmd_present(*pmd_k))
		return NULL;
	if (!pmd_present(*pmd)) {
		set_pmd(pmd, *pmd_k);
		arch_flush_lazy_mmu_mode();
	} else
		BUG_ON(pmd_ptfn(*pmd) != pmd_ptfn(*pmd_k));
	return pmd_k;
}

/*
 * Handle a fault on the vmalloc area.
 */
static inline int vmalloc_fault(pgd_t *pgd, unsigned long address)
{
	pmd_t *pmd_k;
	pte_t *pte_k;

	/* Make sure we are in vmalloc area */
	if (!(address >= VMALLOC_START && address < VMALLOC_END))
		return -1;

	/*
	 * Synchronize this task's top level page-table
	 * with the 'reference' page table.
	 */
	pmd_k = vmalloc_sync_one(pgd, address);
	if (!pmd_k)
		return -1;
	if (pmd_huge(*pmd_k))
		return 0;   /* support TILE huge_vmap() API */
	pte_k = pte_offset_kernel(pmd_k, address);
	if (!pte_present(*pte_k))
		return -1;
	return 0;
}

/* Wait until this PTE has completed migration. */
static void wait_for_migration(pte_t *pte)
{
	if (pte_migrating(*pte)) {
		/*
		 * Wait until the migrater fixes up this pte.
		 * We scale the loop count by the clock rate so we'll wait for
		 * a few seconds here.
		 */
		int retries = 0;
		int bound = get_clock_rate();
		while (pte_migrating(*pte)) {
			barrier();
			if (++retries > bound)
				panic("Hit migrating PTE (%#llx) and"
				      " page PFN %#lx still migrating",
				      pte->val, pte_pfn(*pte));
		}
	}
}

/*
 * It's not generally safe to use "current" to get the page table pointer,
 * since we might be running an oprofile interrupt in the middle of a
 * task switch.
 */
static pgd_t *get_current_pgd(void)
{
	HV_Context ctx = hv_inquire_context();
	unsigned long pgd_pfn = ctx.page_table >> PAGE_SHIFT;
	struct page *pgd_page = pfn_to_page(pgd_pfn);
	BUG_ON(PageHighMem(pgd_page));   /* oops, HIGHPTE? */
	return (pgd_t *) __va(ctx.page_table);
}

/*
 * We can receive a page fault from a migrating PTE at any time.
 * Handle it by just waiting until the fault resolves.
 *
 * It's also possible to get a migrating kernel PTE that resolves
 * itself during the downcall from hypervisor to Linux.  We just check
 * here to see if the PTE seems valid, and if so we retry it.
 *
 * NOTE! We MUST NOT take any locks for this case.  We may be in an
 * interrupt or a critical region, and must do as little as possible.
 * Similarly, we can't use atomic ops here, since we may be handling a
 * fault caused by an atomic op access.
 *
 * If we find a migrating PTE while we're in an NMI context, and we're
 * at a PC that has a registered exception handler, we don't wait,
 * since this thread may (e.g.) have been interrupted while migrating
 * its own stack, which would then cause us to self-deadlock.
 */
static int handle_migrating_pte(pgd_t *pgd, int fault_num,
				unsigned long address, unsigned long pc,
				int is_kernel_mode, int write)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t pteval;

	if (pgd_addr_invalid(address))
		return 0;

	pgd += pgd_index(address);
	pud = pud_offset(pgd, address);
	if (!pud || !pud_present(*pud))
		return 0;
	pmd = pmd_offset(pud, address);
	if (!pmd || !pmd_present(*pmd))
		return 0;
	pte = pmd_huge_page(*pmd) ? ((pte_t *)pmd) :
		pte_offset_kernel(pmd, address);
	pteval = *pte;
	if (pte_migrating(pteval)) {
		if (in_nmi() && search_exception_tables(pc))
			return 0;
		wait_for_migration(pte);
		return 1;
	}

	if (!is_kernel_mode || !pte_present(pteval))
		return 0;
	if (fault_num == INT_ITLB_MISS) {
		if (pte_exec(pteval))
			return 1;
	} else if (write) {
		if (pte_write(pteval))
			return 1;
	} else {
		if (pte_read(pteval))
			return 1;
	}

	return 0;
}

/*
 * This routine is responsible for faulting in user pages.
 * It passes the work off to one of the appropriate routines.
 * It returns true if the fault was successfully handled.
 */
static int handle_page_fault(struct pt_regs *regs,
			     int fault_num,
			     int is_page_fault,
			     unsigned long address,
			     int write)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long stack_offset;
	int fault;
	int si_code;
	int is_kernel_mode;
	pgd_t *pgd;

	/* on TILE, protection faults are always writes */
	if (!is_page_fault)
		write = 1;

	is_kernel_mode = (EX1_PL(regs->ex1) != USER_PL);

	tsk = validate_current();

	/*
	 * Check to see if we might be overwriting the stack, and bail
	 * out if so.  The page fault code is a relatively likely
	 * place to get trapped in an infinite regress, and once we
	 * overwrite the whole stack, it becomes very hard to recover.
	 */
	stack_offset = stack_pointer & (THREAD_SIZE-1);
	if (stack_offset < THREAD_SIZE / 8) {
		pr_alert("Potential stack overrun: sp %#lx\n",
		       stack_pointer);
		show_regs(regs);
		pr_alert("Killing current process %d/%s\n",
		       tsk->pid, tsk->comm);
		do_group_exit(SIGKILL);
	}

	/*
	 * Early on, we need to check for migrating PTE entries;
	 * see homecache.c.  If we find a migrating PTE, we wait until
	 * the backing page claims to be done migrating, then we proceed.
	 * For kernel PTEs, we rewrite the PTE and return and retry.
	 * Otherwise, we treat the fault like a normal "no PTE" fault,
	 * rather than trying to patch up the existing PTE.
	 */
	pgd = get_current_pgd();
	if (handle_migrating_pte(pgd, fault_num, address, regs->pc,
				 is_kernel_mode, write))
		return 1;

	si_code = SEGV_MAPERR;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 *
	 * This verifies that the fault happens in kernel space
	 * and that the fault was not a protection fault.
	 */
	if (unlikely(address >= TASK_SIZE &&
		     !is_arch_mappable_range(address, 0))) {
		if (is_kernel_mode && is_page_fault &&
		    vmalloc_fault(pgd, address) >= 0)
			return 1;
		/*
		 * Don't take the mm semaphore here. If we fixup a prefetch
		 * fault we could otherwise deadlock.
		 */
		mm = NULL;  /* happy compiler */
		vma = NULL;
		goto bad_area_nosemaphore;
	}

	/*
	 * If we're trying to touch user-space addresses, we must
	 * be either at PL0, or else with interrupts enabled in the
	 * kernel, so either way we can re-enable interrupts here
	 * unless we are doing atomic access to user space with
	 * interrupts disabled.
	 */
	if (!(regs->flags & PT_FLAGS_DISABLE_IRQ))
		local_irq_enable();

	mm = tsk->mm;

	/*
	 * If we're in an interrupt, have no user context or are running in an
	 * atomic region then we must not take the fault.
	 */
	if (in_atomic() || !mm) {
		vma = NULL;  /* happy compiler */
		goto bad_area_nosemaphore;
	}

	/*
	 * When running in the kernel we expect faults to occur only to
	 * addresses in user space.  All other faults represent errors in the
	 * kernel and should generate an OOPS.  Unfortunately, in the case of an
	 * erroneous fault occurring in a code path which already holds mmap_sem
	 * we will deadlock attempting to validate the fault against the
	 * address space.  Luckily the kernel only validly references user
	 * space from well defined areas of code, which are listed in the
	 * exceptions table.
	 *
	 * As the vast majority of faults will be valid we will only perform
	 * the source reference check when there is a possibility of a deadlock.
	 * Attempt to lock the address space, if we cannot we then validate the
	 * source.  If this is invalid we can skip the address space check,
	 * thus avoiding the deadlock.
	 */
	if (!down_read_trylock(&mm->mmap_sem)) {
		if (is_kernel_mode &&
		    !search_exception_tables(regs->pc)) {
			vma = NULL;  /* happy compiler */
			goto bad_area_nosemaphore;
		}
		down_read(&mm->mmap_sem);
	}

	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (regs->sp < PAGE_OFFSET) {
		/*
		 * accessing the stack below sp is always a bug.
		 */
		if (address < regs->sp)
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;

/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	si_code = SEGV_ACCERR;
	if (fault_num == INT_ITLB_MISS) {
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
	} else if (write) {
#ifdef TEST_VERIFY_AREA
		if (!is_page_fault && regs->cs == KERNEL_CS)
			pr_err("WP fault at "REGFMT"\n", regs->eip);
#endif
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if (!is_page_fault || !(vma->vm_flags & VM_READ))
			goto bad_area;
	}

 survive:
	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(mm, vma, address, write);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}
	if (fault & VM_FAULT_MAJOR)
		tsk->maj_flt++;
	else
		tsk->min_flt++;

#if CHIP_HAS_TILE_DMA() || CHIP_HAS_SN_PROC()
	/*
	 * If this was an asynchronous fault,
	 * restart the appropriate engine.
	 */
	switch (fault_num) {
#if CHIP_HAS_TILE_DMA()
	case INT_DMATLB_MISS:
	case INT_DMATLB_MISS_DWNCL:
	case INT_DMATLB_ACCESS:
	case INT_DMATLB_ACCESS_DWNCL:
		__insn_mtspr(SPR_DMA_CTR, SPR_DMA_CTR__REQUEST_MASK);
		break;
#endif
#if CHIP_HAS_SN_PROC()
	case INT_SNITLB_MISS:
	case INT_SNITLB_MISS_DWNCL:
		__insn_mtspr(SPR_SNCTL,
			     __insn_mfspr(SPR_SNCTL) &
			     ~SPR_SNCTL__FRZPROC_MASK);
		break;
#endif
	}
#endif

	up_read(&mm->mmap_sem);
	return 1;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up_read(&mm->mmap_sem);

bad_area_nosemaphore:
	/* User mode accesses just cause a SIGSEGV */
	if (!is_kernel_mode) {
		/*
		 * It's possible to have interrupts off here.
		 */
		local_irq_enable();

		force_sig_info_fault("segfault", SIGSEGV, si_code, address,
				     fault_num, tsk, regs);
		return 0;
	}

no_context:
	/* Are we prepared to handle this kernel fault?  */
	if (fixup_exception(regs))
		return 0;

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */

	bust_spinlocks(1);

	/* FIXME: no lookup_address() yet */
#ifdef SUPPORT_LOOKUP_ADDRESS
	if (fault_num == INT_ITLB_MISS) {
		pte_t *pte = lookup_address(address);

		if (pte && pte_present(*pte) && !pte_exec_kernel(*pte))
			pr_crit("kernel tried to execute"
			       " non-executable page - exploit attempt?"
			       " (uid: %d)\n", current->uid);
	}
#endif
	if (address < PAGE_SIZE)
		pr_alert("Unable to handle kernel NULL pointer dereference\n");
	else
		pr_alert("Unable to handle kernel paging request\n");
	pr_alert(" at virtual address "REGFMT", pc "REGFMT"\n",
		 address, regs->pc);

	show_regs(regs);

	if (unlikely(tsk->pid < 2)) {
		panic("Kernel page fault running %s!",
		      is_idle_task(tsk) ? "the idle task" : "init");
	}

	/*
	 * More FIXME: we should probably copy the i386 here and
	 * implement a generic die() routine.  Not today.
	 */
#ifdef SUPPORT_DIE
	die("Oops", regs);
#endif
	bust_spinlocks(1);

	do_group_exit(SIGKILL);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (is_global_init(tsk)) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	pr_alert("VM: killing process %s\n", tsk->comm);
	if (!is_kernel_mode)
		do_group_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/* Kernel mode? Handle exceptions or die */
	if (is_kernel_mode)
		goto no_context;

	force_sig_info_fault("bus error", SIGBUS, BUS_ADRERR, address,
			     fault_num, tsk, regs);
	return 0;
}

#ifndef __tilegx__

/* We must release ICS before panicking or we won't get anywhere. */
#define ics_panic(fmt, ...) do { \
	__insn_mtspr(SPR_INTERRUPT_CRITICAL_SECTION, 0); \
	panic(fmt, __VA_ARGS__); \
} while (0)

/*
 * When we take an ITLB or DTLB fault or access violation in the
 * supervisor while the critical section bit is set, the hypervisor is
 * reluctant to write new values into the EX_CONTEXT_K_x registers,
 * since that might indicate we have not yet squirreled the SPR
 * contents away and can thus safely take a recursive interrupt.
 * Accordingly, the hypervisor passes us the PC via SYSTEM_SAVE_K_2.
 *
 * Note that this routine is called before homecache_tlb_defer_enter(),
 * which means that we can properly unlock any atomics that might
 * be used there (good), but also means we must be very sensitive
 * to not touch any data structures that might be located in memory
 * that could migrate, as we could be entering the kernel on a dataplane
 * cpu that has been deferring kernel TLB updates.  This means, for
 * example, that we can't migrate init_mm or its pgd.
 */
struct intvec_state do_page_fault_ics(struct pt_regs *regs, int fault_num,
				      unsigned long address,
				      unsigned long info)
{
	unsigned long pc = info & ~1;
	int write = info & 1;
	pgd_t *pgd = get_current_pgd();

	/* Retval is 1 at first since we will handle the fault fully. */
	struct intvec_state state = {
		do_page_fault, fault_num, address, write, 1
	};

	/* Validate that we are plausibly in the right routine. */
	if ((pc & 0x7) != 0 || pc < PAGE_OFFSET ||
	    (fault_num != INT_DTLB_MISS &&
	     fault_num != INT_DTLB_ACCESS)) {
		unsigned long old_pc = regs->pc;
		regs->pc = pc;
		ics_panic("Bad ICS page fault args:"
			  " old PC %#lx, fault %d/%d at %#lx\n",
			  old_pc, fault_num, write, address);
	}

	/* We might be faulting on a vmalloc page, so check that first. */
	if (fault_num != INT_DTLB_ACCESS && vmalloc_fault(pgd, address) >= 0)
		return state;

	/*
	 * If we faulted with ICS set in sys_cmpxchg, we are providing
	 * a user syscall service that should generate a signal on
	 * fault.  We didn't set up a kernel stack on initial entry to
	 * sys_cmpxchg, but instead had one set up by the fault, which
	 * (because sys_cmpxchg never releases ICS) came to us via the
	 * SYSTEM_SAVE_K_2 mechanism, and thus EX_CONTEXT_K_[01] are
	 * still referencing the original user code.  We release the
	 * atomic lock and rewrite pt_regs so that it appears that we
	 * came from user-space directly, and after we finish the
	 * fault we'll go back to user space and re-issue the swint.
	 * This way the backtrace information is correct if we need to
	 * emit a stack dump at any point while handling this.
	 *
	 * Must match register use in sys_cmpxchg().
	 */
	if (pc >= (unsigned long) sys_cmpxchg &&
	    pc < (unsigned long) __sys_cmpxchg_end) {
#ifdef CONFIG_SMP
		/* Don't unlock before we could have locked. */
		if (pc >= (unsigned long)__sys_cmpxchg_grab_lock) {
			int *lock_ptr = (int *)(regs->regs[ATOMIC_LOCK_REG]);
			__atomic_fault_unlock(lock_ptr);
		}
#endif
		regs->sp = regs->regs[27];
	}

	/*
	 * We can also fault in the atomic assembly, in which
	 * case we use the exception table to do the first-level fixup.
	 * We may re-fixup again in the real fault handler if it
	 * turns out the faulting address is just bad, and not,
	 * for example, migrating.
	 */
	else if (pc >= (unsigned long) __start_atomic_asm_code &&
		   pc < (unsigned long) __end_atomic_asm_code) {
		const struct exception_table_entry *fixup;
#ifdef CONFIG_SMP
		/* Unlock the atomic lock. */
		int *lock_ptr = (int *)(regs->regs[ATOMIC_LOCK_REG]);
		__atomic_fault_unlock(lock_ptr);
#endif
		fixup = search_exception_tables(pc);
		if (!fixup)
			ics_panic("ICS atomic fault not in table:"
				  " PC %#lx, fault %d", pc, fault_num);
		regs->pc = fixup->fixup;
		regs->ex1 = PL_ICS_EX1(KERNEL_PL, 0);
	}

	/*
	 * Now that we have released the atomic lock (if necessary),
	 * it's safe to spin if the PTE that caused the fault was migrating.
	 */
	if (fault_num == INT_DTLB_ACCESS)
		write = 1;
	if (handle_migrating_pte(pgd, fault_num, address, pc, 1, write))
		return state;

	/* Return zero so that we continue on with normal fault handling. */
	state.retval = 0;
	return state;
}

#endif /* !__tilegx__ */

/*
 * This routine handles page faults.  It determines the address, and the
 * problem, and then passes it handle_page_fault() for normal DTLB and
 * ITLB issues, and for DMA or SN processor faults when we are in user
 * space.  For the latter, if we're in kernel mode, we just save the
 * interrupt away appropriately and return immediately.  We can't do
 * page faults for user code while in kernel mode.
 */
void do_page_fault(struct pt_regs *regs, int fault_num,
		   unsigned long address, unsigned long write)
{
	int is_page_fault;

	/* This case should have been handled by do_page_fault_ics(). */
	BUG_ON(write & ~1);

#if CHIP_HAS_TILE_DMA()
	/*
	 * If it's a DMA fault, suspend the transfer while we're
	 * handling the miss; we'll restart after it's handled.  If we
	 * don't suspend, it's possible that this process could swap
	 * out and back in, and restart the engine since the DMA is
	 * still 'running'.
	 */
	if (fault_num == INT_DMATLB_MISS ||
	    fault_num == INT_DMATLB_ACCESS ||
	    fault_num == INT_DMATLB_MISS_DWNCL ||
	    fault_num == INT_DMATLB_ACCESS_DWNCL) {
		__insn_mtspr(SPR_DMA_CTR, SPR_DMA_CTR__SUSPEND_MASK);
		while (__insn_mfspr(SPR_DMA_USER_STATUS) &
		       SPR_DMA_STATUS__BUSY_MASK)
			;
	}
#endif

	/* Validate fault num and decide if this is a first-time page fault. */
	switch (fault_num) {
	case INT_ITLB_MISS:
	case INT_DTLB_MISS:
#if CHIP_HAS_TILE_DMA()
	case INT_DMATLB_MISS:
	case INT_DMATLB_MISS_DWNCL:
#endif
#if CHIP_HAS_SN_PROC()
	case INT_SNITLB_MISS:
	case INT_SNITLB_MISS_DWNCL:
#endif
		is_page_fault = 1;
		break;

	case INT_DTLB_ACCESS:
#if CHIP_HAS_TILE_DMA()
	case INT_DMATLB_ACCESS:
	case INT_DMATLB_ACCESS_DWNCL:
#endif
		is_page_fault = 0;
		break;

	default:
		panic("Bad fault number %d in do_page_fault", fault_num);
	}

#if CHIP_HAS_TILE_DMA() || CHIP_HAS_SN_PROC()
	if (EX1_PL(regs->ex1) != USER_PL) {
		struct async_tlb *async;
		switch (fault_num) {
#if CHIP_HAS_TILE_DMA()
		case INT_DMATLB_MISS:
		case INT_DMATLB_ACCESS:
		case INT_DMATLB_MISS_DWNCL:
		case INT_DMATLB_ACCESS_DWNCL:
			async = &current->thread.dma_async_tlb;
			break;
#endif
#if CHIP_HAS_SN_PROC()
		case INT_SNITLB_MISS:
		case INT_SNITLB_MISS_DWNCL:
			async = &current->thread.sn_async_tlb;
			break;
#endif
		default:
			async = NULL;
		}
		if (async) {

			/*
			 * No vmalloc check required, so we can allow
			 * interrupts immediately at this point.
			 */
			local_irq_enable();

			set_thread_flag(TIF_ASYNC_TLB);
			if (async->fault_num != 0) {
				panic("Second async fault %d;"
				      " old fault was %d (%#lx/%ld)",
				      fault_num, async->fault_num,
				      address, write);
			}
			BUG_ON(fault_num == 0);
			async->fault_num = fault_num;
			async->is_fault = is_page_fault;
			async->is_write = write;
			async->address = address;
			return;
		}
	}
#endif

	handle_page_fault(regs, fault_num, is_page_fault, address, write);
}


#if CHIP_HAS_TILE_DMA() || CHIP_HAS_SN_PROC()
/*
 * Check an async_tlb structure to see if a deferred fault is waiting,
 * and if so pass it to the page-fault code.
 */
static void handle_async_page_fault(struct pt_regs *regs,
				    struct async_tlb *async)
{
	if (async->fault_num) {
		/*
		 * Clear async->fault_num before calling the page-fault
		 * handler so that if we re-interrupt before returning
		 * from the function we have somewhere to put the
		 * information from the new interrupt.
		 */
		int fault_num = async->fault_num;
		async->fault_num = 0;
		handle_page_fault(regs, fault_num, async->is_fault,
				  async->address, async->is_write);
	}
}

/*
 * This routine effectively re-issues asynchronous page faults
 * when we are returning to user space.
 */
void do_async_page_fault(struct pt_regs *regs)
{
	/*
	 * Clear thread flag early.  If we re-interrupt while processing
	 * code here, we will reset it and recall this routine before
	 * returning to user space.
	 */
	clear_thread_flag(TIF_ASYNC_TLB);

#if CHIP_HAS_TILE_DMA()
	handle_async_page_fault(regs, &current->thread.dma_async_tlb);
#endif
#if CHIP_HAS_SN_PROC()
	handle_async_page_fault(regs, &current->thread.sn_async_tlb);
#endif
}
#endif /* CHIP_HAS_TILE_DMA() || CHIP_HAS_SN_PROC() */


void vmalloc_sync_all(void)
{
#ifdef __tilegx__
	/* Currently all L1 kernel pmd's are static and shared. */
	BUG_ON(pgd_index(VMALLOC_END) != pgd_index(VMALLOC_START));
#else
	/*
	 * Note that races in the updates of insync and start aren't
	 * problematic: insync can only get set bits added, and updates to
	 * start are only improving performance (without affecting correctness
	 * if undone).
	 */
	static DECLARE_BITMAP(insync, PTRS_PER_PGD);
	static unsigned long start = PAGE_OFFSET;
	unsigned long address;

	BUILD_BUG_ON(PAGE_OFFSET & ~PGDIR_MASK);
	for (address = start; address >= PAGE_OFFSET; address += PGDIR_SIZE) {
		if (!test_bit(pgd_index(address), insync)) {
			unsigned long flags;
			struct list_head *pos;

			spin_lock_irqsave(&pgd_lock, flags);
			list_for_each(pos, &pgd_list)
				if (!vmalloc_sync_one(list_to_pgd(pos),
								address)) {
					/* Must be at first entry in list. */
					BUG_ON(pos != pgd_list.next);
					break;
				}
			spin_unlock_irqrestore(&pgd_lock, flags);
			if (pos != pgd_list.next)
				set_bit(pgd_index(address), insync);
		}
		if (address == start && test_bit(pgd_index(address), insync))
			start = address + PGDIR_SIZE;
	}
#endif
}
