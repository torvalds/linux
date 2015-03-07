/*
 *  arch/cris/mm/fault.c
 *
 *  Copyright (C) 2000-2010  Axis Communications AB
 */

#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <arch/system.h>

extern int find_fixup_code(struct pt_regs *);
extern void die_if_kernel(const char *, struct pt_regs *, long);
extern void show_registers(struct pt_regs *regs);

/* debug of low-level TLB reload */
#undef DEBUG

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

/* debug of higher-level faults */
#define DPG(x)

/* current active page directory */

DEFINE_PER_CPU(pgd_t *, current_pgd);
unsigned long cris_signal_return_page;

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * Notice that the address we're given is aligned to the page the fault
 * occurred in, since we only get the PFN in R_MMU_CAUSE not the complete
 * address.
 *
 * error_code:
 *      bit 0 == 0 means no page found, 1 means protection fault
 *      bit 1 == 0 means read, 1 means write
 *
 * If this routine detects a bad access, it returns 1, otherwise it
 * returns 0.
 */

asmlinkage void
do_page_fault(unsigned long address, struct pt_regs *regs,
	      int protection, int writeaccess)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	siginfo_t info;
	int fault;
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

	D(printk(KERN_DEBUG
		 "Page fault for %lX on %X at %lX, prot %d write %d\n",
		 address, smp_processor_id(), instruction_pointer(regs),
		 protection, writeaccess));

	tsk = current;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 *
	 * NOTE2: This is done so that, when updating the vmalloc
	 * mappings we don't have to walk all processes pgdirs and
	 * add the high mappings all at once. Instead we do it as they
	 * are used. However vmalloc'ed page entries have the PAGE_GLOBAL
	 * bit set so sometimes the TLB can use a lingering entry.
	 *
	 * This verifies that the fault happens in kernel space
	 * and that the fault was not a protection error (error_code & 1).
	 */

	if (address >= VMALLOC_START &&
	    !protection &&
	    !user_mode(regs))
		goto vmalloc_fault;

	/* When stack execution is not allowed we store the signal
	 * trampolines in the reserved cris_signal_return_page.
	 * Handle this in the exact same way as vmalloc (we know
	 * that the mapping is there and is valid so no need to
	 * call handle_mm_fault).
	 */
	if (cris_signal_return_page &&
	    address == cris_signal_return_page &&
	    !protection && user_mode(regs))
		goto vmalloc_fault;

	/* we can and should enable interrupts at this point */
	local_irq_enable();

	mm = tsk->mm;
	info.si_code = SEGV_MAPERR;

	/*
	 * If we're in an interrupt or "atomic" operation or have no
	 * user context, we must not take the fault.
	 */

	if (in_atomic() || !mm)
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
	if (user_mode(regs)) {
		/*
		 * accessing the stack below usp is always a bug.
		 * we get page-aligned addresses so we can only check
		 * if we're within a page from usp, but that might be
		 * enough to catch brutal errors at least.
		 */
		if (address + PAGE_SIZE < rdusp())
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */

 good_area:
	info.si_code = SEGV_ACCERR;

	/* first do some preliminary protection checks */

	if (writeaccess == 2){
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
	} else if (writeaccess == 1) {
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

	fault = handle_mm_fault(mm, vma, address, flags);

	if ((fault & VM_FAULT_RETRY) && fatal_signal_pending(current))
		return;

	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGSEGV)
			goto bad_area;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}

	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR)
			tsk->maj_flt++;
		else
			tsk->min_flt++;
		if (fault & VM_FAULT_RETRY) {
			flags &= ~FAULT_FLAG_ALLOW_RETRY;
			flags |= FAULT_FLAG_TRIED;

			/*
			 * No need to up_read(&mm->mmap_sem) as we would
			 * have already released it in __lock_page_or_retry
			 * in mm/filemap.c.
			 */

			goto retry;
		}
	}

	up_read(&mm->mmap_sem);
	return;

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */

 bad_area:
	up_read(&mm->mmap_sem);

 bad_area_nosemaphore:
	DPG(show_registers(regs));

	/* User mode accesses just cause a SIGSEGV */

	if (user_mode(regs)) {
#ifdef CONFIG_NO_SEGFAULT_TERMINATION
		DECLARE_WAIT_QUEUE_HEAD(wq);
#endif
		printk(KERN_NOTICE "%s (pid %d) segfaults for page "
			"address %08lx at pc %08lx\n",
			tsk->comm, tsk->pid,
			address, instruction_pointer(regs));

		/* With DPG on, we've already dumped registers above.  */
		DPG(if (0))
			show_registers(regs);

#ifdef CONFIG_NO_SEGFAULT_TERMINATION
		wait_event_interruptible(wq, 0 == 1);
#else
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void *)address;
		force_sig_info(SIGSEGV, &info, tsk);
#endif
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

	if (find_fixup_code(regs))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */

	if (!oops_in_progress) {
		oops_in_progress = 1;
		if ((unsigned long) (address) < PAGE_SIZE)
			printk(KERN_ALERT "Unable to handle kernel NULL "
				"pointer dereference");
		else
			printk(KERN_ALERT "Unable to handle kernel access"
				" at virtual address %08lx\n", address);

		die_if_kernel("Oops", regs, (writeaccess << 1) | protection);
		oops_in_progress = 0;
	}

	do_exit(SIGKILL);

	/*
	 * We ran out of memory, or some other thing happened to us that made
	 * us unable to handle the page fault gracefully.
	 */

 out_of_memory:
	up_read(&mm->mmap_sem);
	if (!user_mode(regs))
		goto no_context;
	pagefault_out_of_memory();
	return;

 do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void *)address;
	force_sig_info(SIGBUS, &info, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;
	return;

vmalloc_fault:
	{
		/*
		 * Synchronize this task's top level page-table
		 * with the 'reference' page table.
		 *
		 * Use current_pgd instead of tsk->active_mm->pgd
		 * since the latter might be unavailable if this
		 * code is executed in a misfortunately run irq
		 * (like inside schedule() between switch_mm and
		 *  switch_to...).
		 */

		int offset = pgd_index(address);
		pgd_t *pgd, *pgd_k;
		pud_t *pud, *pud_k;
		pmd_t *pmd, *pmd_k;
		pte_t *pte_k;

		pgd = (pgd_t *)per_cpu(current_pgd, smp_processor_id()) + offset;
		pgd_k = init_mm.pgd + offset;

		/* Since we're two-level, we don't need to do both
		 * set_pgd and set_pmd (they do the same thing). If
		 * we go three-level at some point, do the right thing
		 * with pgd_present and set_pgd here.
		 *
		 * Also, since the vmalloc area is global, we don't
		 * need to copy individual PTE's, it is enough to
		 * copy the pgd pointer into the pte page of the
		 * root task. If that is there, we'll find our pte if
		 * it exists.
		 */

		pud = pud_offset(pgd, address);
		pud_k = pud_offset(pgd_k, address);
		if (!pud_present(*pud_k))
			goto no_context;

		pmd = pmd_offset(pud, address);
		pmd_k = pmd_offset(pud_k, address);

		if (!pmd_present(*pmd_k))
			goto bad_area_nosemaphore;

		set_pmd(pmd, *pmd_k);

		/* Make sure the actual PTE exists as well to
		 * catch kernel vmalloc-area accesses to non-mapped
		 * addresses. If we don't do this, this will just
		 * silently loop forever.
		 */

		pte_k = pte_offset_kernel(pmd_k, address);
		if (!pte_present(*pte_k))
			goto no_context;

		return;
	}
}

/* Find fixup code. */
int
find_fixup_code(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;
	/* in case of delay slot fault (v32) */
	unsigned long ip = (instruction_pointer(regs) & ~0x1);

	fixup = search_exception_tables(ip);
	if (fixup != 0) {
		/* Adjust the instruction pointer in the stackframe. */
		instruction_pointer(regs) = fixup->fixup;
		arch_fixup(regs);
		return 1;
	}

	return 0;
}
