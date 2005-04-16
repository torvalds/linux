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
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>

extern void die_if_kernel(char *, struct pt_regs *, long);
extern const int frame_extra_sizes[]; /* in m68k/kernel/signal.c */

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
		const struct exception_table_entry *fixup;

		/* Are we prepared to handle this kernel fault? */
		if ((fixup = search_exception_tables(regs->pc))) {
			struct pt_regs *tregs;
			/* Create a new four word stack frame, discarding the old
			   one.  */
			regs->stkadj = frame_extra_sizes[regs->format];
			tregs =	(struct pt_regs *)((ulong)regs + regs->stkadj);
			tregs->vector = regs->vector;
			tregs->format = 0;
			tregs->pc = fixup->fixup;
			tregs->sr = regs->sr;
			return -1;
		}

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
	int write, fault;

#ifdef DEBUG
	printk ("do page fault:\nregs->sr=%#x, regs->pc=%#lx, address=%#lx, %ld, %p\n",
		regs->sr, regs->pc, address, error_code,
		current->mm->pgd);
#endif

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_interrupt() || !mm)
		goto no_context;

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
	write = 0;
	switch (error_code & 3) {
		default:	/* 3: write, present */
			/* fall through */
		case 2:		/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto acc_err;
			write++;
			break;
		case 1:		/* read, present */
			goto acc_err;
		case 0:		/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
				goto acc_err;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */

 survive:
	fault = handle_mm_fault(mm, vma, address, write);
#ifdef DEBUG
	printk("handle_mm_fault returns %d\n",fault);
#endif
	switch (fault) {
	case 1:
		current->min_flt++;
		break;
	case 2:
		current->maj_flt++;
		break;
	case 0:
		goto bus_err;
	default:
		goto out_of_memory;
	}

	up_read(&mm->mmap_sem);
	return 0;

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (current->pid == 1) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}

	printk("VM: killing process %s\n", current->comm);
	if (user_mode(regs))
		do_exit(SIGKILL);

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
