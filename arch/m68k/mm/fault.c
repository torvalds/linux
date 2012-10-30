/*
 *  linux/arch/m68k/mm/fault.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/setup.h>
#include <asm/traps.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>

extern void die_if_kernel(char *, struct pt_regs *, long);

int send_fault_sig(struct pt_regs *regs)
{
	siginfo_t siginfo = { 0, 0, 0, };

	siginfo.si_signo = current->thread.signo;
	siginfo.si_code = current->thread.code;
	siginfo.si_addr = (void *)current->thread.faddr;
#ifdef DEBUG
	printk("send_fault_sig: %p,%d,%d\n", siginfo.si_addr, siginfo.si_signo, siginfo.si_code);
#endif

	if (user_mode(regs)) {
		force_sig_info(siginfo.si_signo,
			       &siginfo, current);
	} else {
		if (handle_kernel_fault(regs))
			return -1;

		//if (siginfo.si_signo == SIGBUS)
		//	force_sig_info(siginfo.si_signo,
		//		       &siginfo, current);

		/*
		 * Oops. The kernel tried to access some bad page. We'll have to
		 * terminate things with extreme prejudice.
		 */
		if ((unsigned long)siginfo.si_addr < PAGE_SIZE)
			printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
		else
			printk(KERN_ALERT "Unable to handle kernel access");
		printk(" at virtual address %p\n", siginfo.si_addr);
		die_if_kernel("Oops", regs, 0 /*error_code*/);
		do_exit(SIGKILL);
	}

	return 1;
}

/*
 * This routine handles page faults.  It determines the problem, and
 * then passes it off to one of the appropriate routines.
 *
 * error_code:
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *
 * If this routine detects a bad access, it returns 1, otherwise it
 * returns 0.
 */
int do_page_fault(struct pt_regs *regs, unsigned long address,
			      unsigned long error_code)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct * vma;
	int fault;
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

#ifdef DEBUG
	printk ("do page fault:\nregs->sr=%#x, regs->pc=%#lx, address=%#lx, %ld, %p\n",
		regs->sr, regs->pc, address, error_code,
		current->mm->pgd);
#endif

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_atomic() || !mm)
		goto no_context;

retry:
	down_read(&mm->mmap_sem);

	vma = find_vma(mm, address);
	if (!vma)
		goto map_err;
	if (vma->vm_flags & VM_IO)
		goto acc_err;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto map_err;
	if (user_mode(regs)) {
		/* Accessing the stack below usp is always a bug.  The
		   "+ 256" is there due to some instructions doing
		   pre-decrement on the stack and that doesn't show up
		   until later.  */
		if (address + 256 < rdusp())
			goto map_err;
	}
	if (expand_stack(vma, address))
		goto map_err;

/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
#ifdef DEBUG
	printk("do_page_fault: good_area\n");
#endif
	switch (error_code & 3) {
		default:	/* 3: write, present */
			/* fall through */
		case 2:		/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto acc_err;
			flags |= FAULT_FLAG_WRITE;
			break;
		case 1:		/* read, present */
			goto acc_err;
		case 0:		/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
				goto acc_err;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */

	fault = handle_mm_fault(mm, vma, address, flags);
#ifdef DEBUG
	printk("handle_mm_fault returns %d\n",fault);
#endif

	if ((fault & VM_FAULT_RETRY) && fatal_signal_pending(current))
		return 0;

	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			goto bus_err;
		BUG();
	}

	/*
	 * Major/minor page fault accounting is only done on the
	 * initial attempt. If we go through a retry, it is extremely
	 * likely that the page will be found in page cache at that point.
	 */
	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR)
			current->maj_flt++;
		else
			current->min_flt++;
		if (fault & VM_FAULT_RETRY) {
			/* Clear FAULT_FLAG_ALLOW_RETRY to avoid any risk
			 * of starvation. */
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
	return 0;

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (!user_mode(regs))
		goto no_context;
	pagefault_out_of_memory();
	return 0;

no_context:
	current->thread.signo = SIGBUS;
	current->thread.faddr = address;
	return send_fault_sig(regs);

bus_err:
	current->thread.signo = SIGBUS;
	current->thread.code = BUS_ADRERR;
	current->thread.faddr = address;
	goto send_sig;

map_err:
	current->thread.signo = SIGSEGV;
	current->thread.code = SEGV_MAPERR;
	current->thread.faddr = address;
	goto send_sig;

acc_err:
	current->thread.signo = SIGSEGV;
	current->thread.code = SEGV_ACCERR;
	current->thread.faddr = address;

send_sig:
	up_read(&mm->mmap_sem);
	return send_fault_sig(regs);
}
