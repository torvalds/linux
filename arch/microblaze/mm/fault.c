/*
 *  arch/microblaze/mm/fault.c
 *
 *    Copyright (C) 2007 Xilinx, Inc.  All rights reserved.
 *
 *  Derived from "arch/ppc/mm/fault.c"
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Modified by Cort Dougan and Paul Mackerras.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#include <linux/extable.h>
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

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <linux/mmu_context.h>
#include <linux/uaccess.h>
#include <asm/exceptions.h>

static unsigned long pte_misses;	/* updated by do_page_fault() */
static unsigned long pte_errors;	/* updated by do_page_fault() */

/*
 * Check whether the instruction at regs->pc is a store using
 * an update addressing form which will update r1.
 */
static int store_updates_sp(struct pt_regs *regs)
{
	unsigned int inst;

	if (get_user(inst, (unsigned int __user *)regs->pc))
		return 0;
	/* check for 1 in the rD field */
	if (((inst >> 21) & 0x1f) != 1)
		return 0;
	/* check for store opcodes */
	if ((inst & 0xd0000000) == 0xd0000000)
		return 1;
	return 0;
}


/*
 * bad_page_fault is called when we have a bad access from the kernel.
 * It is called from do_page_fault above and from some of the procedures
 * in traps.c.
 */
void bad_page_fault(struct pt_regs *regs, unsigned long address, int sig)
{
	const struct exception_table_entry *fixup;
/* MS: no context */
	/* Are we prepared to handle this fault?  */
	fixup = search_exception_tables(regs->pc);
	if (fixup) {
		regs->pc = fixup->fixup;
		return;
	}

	/* kernel has accessed a bad area */
	die("kernel access of bad area", regs, sig);
}

/*
 * The error_code parameter is ESR for a data fault,
 * 0 for an instruction fault.
 */
void do_page_fault(struct pt_regs *regs, unsigned long address,
		   unsigned long error_code)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	siginfo_t info;
	int code = SEGV_MAPERR;
	int is_write = error_code & ESR_S;
	int fault;
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

	regs->ear = address;
	regs->esr = error_code;

	/* On a kernel SLB miss we can only check for a valid exception entry */
	if (unlikely(kernel_mode(regs) && (address >= TASK_SIZE))) {
		pr_warn("kernel task_size exceed");
		_exception(SIGSEGV, regs, code, address);
	}

	/* for instr TLB miss and instr storage exception ESR_S is undefined */
	if ((error_code & 0x13) == 0x13 || (error_code & 0x11) == 0x11)
		is_write = 0;

	if (unlikely(faulthandler_disabled() || !mm)) {
		if (kernel_mode(regs))
			goto bad_area_nosemaphore;

		/* faulthandler_disabled() in user mode is really bad,
		   as is current->mm == NULL. */
		pr_emerg("Page fault in user mode with faulthandler_disabled(), mm = %p\n",
			 mm);
		pr_emerg("r15 = %lx  MSR = %lx\n",
		       regs->r15, regs->msr);
		die("Weird page fault", regs, SIGSEGV);
	}

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;

	/* When running in the kernel we expect faults to occur only to
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
	if (unlikely(!down_read_trylock(&mm->mmap_sem))) {
		if (kernel_mode(regs) && !search_exception_tables(regs->pc))
			goto bad_area_nosemaphore;

retry:
		down_read(&mm->mmap_sem);
	}

	vma = find_vma(mm, address);
	if (unlikely(!vma))
		goto bad_area;

	if (vma->vm_start <= address)
		goto good_area;

	if (unlikely(!(vma->vm_flags & VM_GROWSDOWN)))
		goto bad_area;

	if (unlikely(!is_write))
		goto bad_area;

	/*
	 * N.B. The ABI allows programs to access up to
	 * a few hundred bytes below the stack pointer (TBD).
	 * The kernel signal delivery code writes up to about 1.5kB
	 * below the stack pointer (r1) before decrementing it.
	 * The exec code can write slightly over 640kB to the stack
	 * before setting the user r1.  Thus we allow the stack to
	 * expand to 1MB without further checks.
	 */
	if (unlikely(address + 0x100000 < vma->vm_end)) {

		/* get user regs even if this fault is in kernel mode */
		struct pt_regs *uregs = current->thread.regs;
		if (uregs == NULL)
			goto bad_area;

		/*
		 * A user-mode access to an address a long way below
		 * the stack pointer is only valid if the instruction
		 * is one which would update the stack pointer to the
		 * address accessed if the instruction completed,
		 * i.e. either stwu rs,n(r1) or stwux rs,r1,rb
		 * (or the byte, halfword, float or double forms).
		 *
		 * If we don't check this then any write to the area
		 * between the last mapped region and the stack will
		 * expand the stack rather than segfaulting.
		 */
		if (address + 2048 < uregs->r1
			&& (kernel_mode(regs) || !store_updates_sp(regs)))
				goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;

good_area:
	code = SEGV_ACCERR;

	/* a write */
	if (unlikely(is_write)) {
		if (unlikely(!(vma->vm_flags & VM_WRITE)))
			goto bad_area;
		flags |= FAULT_FLAG_WRITE;
	/* a read */
	} else {
		/* protection fault */
		if (unlikely(error_code & 0x08000000))
			goto bad_area;
		if (unlikely(!(vma->vm_flags & (VM_READ | VM_EXEC))))
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(vma, address, flags);

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
		if (unlikely(fault & VM_FAULT_MAJOR))
			current->maj_flt++;
		else
			current->min_flt++;
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

	/*
	 * keep track of tlb+htab misses that are good addrs but
	 * just need pte's created via handle_mm_fault()
	 * -- Cort
	 */
	pte_misses++;
	return;

bad_area:
	up_read(&mm->mmap_sem);

bad_area_nosemaphore:
	pte_errors++;

	/* User mode accesses cause a SIGSEGV */
	if (user_mode(regs)) {
		_exception(SIGSEGV, regs, code, address);
/*		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		info.si_code = code;
		info.si_addr = (void *) address;
		force_sig_info(SIGSEGV, &info, current);*/
		return;
	}

	bad_page_fault(regs, address, SIGSEGV);
	return;

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (!user_mode(regs))
		bad_page_fault(regs, address, SIGKILL);
	else
		pagefault_out_of_memory();
	return;

do_sigbus:
	up_read(&mm->mmap_sem);
	if (user_mode(regs)) {
		info.si_signo = SIGBUS;
		info.si_errno = 0;
		info.si_code = BUS_ADRERR;
		info.si_addr = (void __user *)address;
		force_sig_info(SIGBUS, &info, current);
		return;
	}
	bad_page_fault(regs, address, SIGBUS);
}
