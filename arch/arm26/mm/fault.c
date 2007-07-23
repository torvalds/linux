/*
 *  linux/arch/arm26/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h> //FIXME this header may be bogusly included

#include "fault.h"

#define FAULT_CODE_LDRSTRPOST   0x80
#define FAULT_CODE_LDRSTRPRE    0x40
#define FAULT_CODE_LDRSTRREG    0x20
#define FAULT_CODE_LDMSTM       0x10
#define FAULT_CODE_LDCSTC       0x08
#define FAULT_CODE_PREFETCH     0x04
#define FAULT_CODE_WRITE        0x02
#define FAULT_CODE_FORCECOW     0x01

#define DO_COW(m)               ((m) & (FAULT_CODE_WRITE|FAULT_CODE_FORCECOW))
#define READ_FAULT(m)           (!((m) & FAULT_CODE_WRITE))
#define DEBUG
/*
 * This is useful to dump out the page tables associated with
 * 'addr' in mm 'mm'.
 */
void show_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;

	if (!mm)
		mm = &init_mm;

	printk(KERN_ALERT "pgd = %p\n", mm->pgd);
	pgd = pgd_offset(mm, addr);
	printk(KERN_ALERT "[%08lx] *pgd=%08lx", addr, pgd_val(*pgd));

	do {
		pmd_t *pmd;
		pte_t *pte;

		pmd = pmd_offset(pgd, addr);

		if (pmd_none(*pmd))
			break;

		if (pmd_bad(*pmd)) {
			printk("(bad)");
			break;
		}

		/* We must not map this if we have highmem enabled */
		/* FIXME */
		pte = pte_offset_map(pmd, addr);
		printk(", *pte=%08lx", pte_val(*pte));
		pte_unmap(pte);
	} while(0);

	printk("\n");
}

/*
 * Oops.  The kernel tried to access some page that wasn't present.
 */
static void
__do_kernel_fault(struct mm_struct *mm, unsigned long addr, unsigned int fsr,
		  struct pt_regs *regs)
{
	/*
         * Are we prepared to handle this kernel fault?
         */
        if (fixup_exception(regs))
                return;

	/*
	 * No handler, we'll have to terminate things with extreme prejudice.
	 */
	bust_spinlocks(1);
	printk(KERN_ALERT
		"Unable to handle kernel %s at virtual address %08lx\n",
		(addr < PAGE_SIZE) ? "NULL pointer dereference" :
		"paging request", addr);

	show_pte(mm, addr);
	die("Oops", regs, fsr);
	bust_spinlocks(0);
	do_exit(SIGKILL);
}

/*
 * Something tried to access memory that isn't in our memory map..
 * User mode accesses just cause a SIGSEGV
 */
static void
__do_user_fault(struct task_struct *tsk, unsigned long addr,
		unsigned int fsr, int code, struct pt_regs *regs)
{
	struct siginfo si;

#ifdef CONFIG_DEBUG_USER
	printk("%s: unhandled page fault at 0x%08lx, code 0x%03x\n",
	       tsk->comm, addr, fsr);
	show_pte(tsk->mm, addr);
	show_regs(regs);
	//dump_backtrace(regs, tsk); // FIXME ARM32 dropped this - why?
	while(1); //FIXME - hack to stop debug going nutso
#endif

	tsk->thread.address = addr;
	tsk->thread.error_code = fsr;
	tsk->thread.trap_no = 14;
	si.si_signo = SIGSEGV;
	si.si_errno = 0;
	si.si_code = code;
	si.si_addr = (void *)addr;
	force_sig_info(SIGSEGV, &si, tsk);
}

static int
__do_page_fault(struct mm_struct *mm, unsigned long addr, unsigned int fsr,
		struct task_struct *tsk)
{
	struct vm_area_struct *vma;
	int fault, mask;

	vma = find_vma(mm, addr);
	fault = -2; /* bad map area */
	if (!vma)
		goto out;
	if (vma->vm_start > addr)
		goto check_stack;

	/*
	 * Ok, we have a good vm_area for this
	 * memory access, so we can handle it.
	 */
good_area:
	if (READ_FAULT(fsr)) /* read? */
		mask = VM_READ|VM_EXEC|VM_WRITE;
	else
		mask = VM_WRITE;

	fault = -1; /* bad access type */
	if (!(vma->vm_flags & mask))
		goto out;

	/*
	 * If for any reason at all we couldn't handle
	 * the fault, make sure we exit gracefully rather
	 * than endlessly redo the fault.
	 */
survive:
	fault = handle_mm_fault(mm, vma, addr & PAGE_MASK, DO_COW(fsr));
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			return fault;
		BUG();
	}
	if (fault & VM_FAULT_MAJOR)
		tsk->maj_flt++;
	else
		tsk->min_flt++;
	return fault;

out_of_memory:
	fault = -3; /* out of memory */
	if (!is_init(tsk))
		goto out;

	/*
	 * If we are out of memory for pid1,
	 * sleep for a while and retry
	 */
	yield();
	goto survive;

check_stack:
	if (vma->vm_flags & VM_GROWSDOWN && !expand_stack(vma, addr))
		goto good_area;
out:
	return fault;
}

int do_page_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	int fault;

	tsk = current;
	mm  = tsk->mm;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_atomic() || !mm)
		goto no_context;

	down_read(&mm->mmap_sem);
	fault = __do_page_fault(mm, addr, fsr, tsk);
	up_read(&mm->mmap_sem);

	/*
	 * Handle the "normal" case first
	 */
	if (likely(!(fault & VM_FAULT_ERROR)))
		return 0;
	if (fault & VM_FAULT_SIGBUS)
		goto do_sigbus;
	/* else VM_FAULT_OOM */

	/*
	 * If we are in kernel mode at this point, we
	 * have no context to handle this fault with.
         * FIXME - is this test right?
	 */
	if (!user_mode(regs)){
		goto no_context;
	}

	if (fault == -3) {
		/*
		 * We ran out of memory, or some other thing happened to
		 * us that made us unable to handle the page fault gracefully.
		 */
		printk("VM: killing process %s\n", tsk->comm);
		do_exit(SIGKILL);
	}
	else{
		__do_user_fault(tsk, addr, fsr, fault == -1 ? SEGV_ACCERR : SEGV_MAPERR, regs);
	}

	return 0;


/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
do_sigbus:
	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	tsk->thread.address = addr;  //FIXME - need other bits setting?
	tsk->thread.error_code = fsr;
	tsk->thread.trap_no = 14;
	force_sig(SIGBUS, tsk);
#ifdef CONFIG_DEBUG_USER
	printk(KERN_DEBUG "%s: sigbus at 0x%08lx, pc=0x%08lx\n",
		current->comm, addr, instruction_pointer(regs));
#endif

	/* Kernel mode? Handle exceptions or die */
	if (user_mode(regs))
		return 0;

no_context:
	__do_kernel_fault(mm, addr, fsr, regs);
	return 0;
}

/*
 * Handle a data abort.  Note that we have to handle a range of addresses
 * on ARM2/3 for ldm.  If both pages are zero-mapped, then we have to force
 * a copy-on-write.  However, on the second page, we always force COW.
 */
asmlinkage void
do_DataAbort(unsigned long min_addr, unsigned long max_addr, int mode, struct pt_regs *regs)
{
        do_page_fault(min_addr, mode, regs);

        if ((min_addr ^ max_addr) >> PAGE_SHIFT){
               do_page_fault(max_addr, mode | FAULT_CODE_FORCECOW, regs);
	}
}

asmlinkage int
do_PrefetchAbort(unsigned long addr, struct pt_regs *regs)
{
#if 0
        if (the memc mapping for this page exists) {
                printk ("Page in, but got abort (undefined instruction?)\n");
                return 0;
        }
#endif
        do_page_fault(addr, FAULT_CODE_PREFETCH, regs);
        return 1;
}

