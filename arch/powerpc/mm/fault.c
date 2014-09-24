/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Modified by Cort Dougan and Paul Mackerras.
 *
 *  Modified for PPC64 by Dave Engebretsen (engebret@ibm.com)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
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
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/perf_event.h>
#include <linux/magic.h>
#include <linux/ratelimit.h>
#include <linux/context_tracking.h>
#include <linux/hugetlb.h>

#include <asm/firmware.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/uaccess.h>
#include <asm/tlbflush.h>
#include <asm/siginfo.h>
#include <asm/debug.h>
#include <mm/mmu_decl.h>

#include "icswx.h"

#ifdef CONFIG_KPROBES
static inline int notify_page_fault(struct pt_regs *regs)
{
	int ret = 0;

	/* kprobe_running() needs smp_processor_id() */
	if (!user_mode(regs)) {
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, 11))
			ret = 1;
		preempt_enable();
	}

	return ret;
}
#else
static inline int notify_page_fault(struct pt_regs *regs)
{
	return 0;
}
#endif

/*
 * Check whether the instruction at regs->nip is a store using
 * an update addressing form which will update r1.
 */
static int store_updates_sp(struct pt_regs *regs)
{
	unsigned int inst;

	if (get_user(inst, (unsigned int __user *)regs->nip))
		return 0;
	/* check for 1 in the rA field */
	if (((inst >> 16) & 0x1f) != 1)
		return 0;
	/* check major opcode */
	switch (inst >> 26) {
	case 37:	/* stwu */
	case 39:	/* stbu */
	case 45:	/* sthu */
	case 53:	/* stfsu */
	case 55:	/* stfdu */
		return 1;
	case 62:	/* std or stdu */
		return (inst & 3) == 1;
	case 31:
		/* check minor opcode */
		switch ((inst >> 1) & 0x3ff) {
		case 181:	/* stdux */
		case 183:	/* stwux */
		case 247:	/* stbux */
		case 439:	/* sthux */
		case 695:	/* stfsux */
		case 759:	/* stfdux */
			return 1;
		}
	}
	return 0;
}
/*
 * do_page_fault error handling helpers
 */

#define MM_FAULT_RETURN		0
#define MM_FAULT_CONTINUE	-1
#define MM_FAULT_ERR(sig)	(sig)

static int do_sigbus(struct pt_regs *regs, unsigned long address,
		     unsigned int fault)
{
	siginfo_t info;
	unsigned int lsb = 0;

	up_read(&current->mm->mmap_sem);

	if (!user_mode(regs))
		return MM_FAULT_ERR(SIGBUS);

	current->thread.trap_nr = BUS_ADRERR;
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void __user *)address;
#ifdef CONFIG_MEMORY_FAILURE
	if (fault & (VM_FAULT_HWPOISON|VM_FAULT_HWPOISON_LARGE)) {
		pr_err("MCE: Killing %s:%d due to hardware memory corruption fault at %lx\n",
			current->comm, current->pid, address);
		info.si_code = BUS_MCEERR_AR;
	}

	if (fault & VM_FAULT_HWPOISON_LARGE)
		lsb = hstate_index_to_shift(VM_FAULT_GET_HINDEX(fault));
	if (fault & VM_FAULT_HWPOISON)
		lsb = PAGE_SHIFT;
#endif
	info.si_addr_lsb = lsb;
	force_sig_info(SIGBUS, &info, current);
	return MM_FAULT_RETURN;
}

static int mm_fault_error(struct pt_regs *regs, unsigned long addr, int fault)
{
	/*
	 * Pagefault was interrupted by SIGKILL. We have no reason to
	 * continue the pagefault.
	 */
	if (fatal_signal_pending(current)) {
		/*
		 * If we have retry set, the mmap semaphore will have
		 * alrady been released in __lock_page_or_retry(). Else
		 * we release it now.
		 */
		if (!(fault & VM_FAULT_RETRY))
			up_read(&current->mm->mmap_sem);
		/* Coming from kernel, we need to deal with uaccess fixups */
		if (user_mode(regs))
			return MM_FAULT_RETURN;
		return MM_FAULT_ERR(SIGKILL);
	}

	/* No fault: be happy */
	if (!(fault & VM_FAULT_ERROR))
		return MM_FAULT_CONTINUE;

	/* Out of memory */
	if (fault & VM_FAULT_OOM) {
		up_read(&current->mm->mmap_sem);

		/*
		 * We ran out of memory, or some other thing happened to us that
		 * made us unable to handle the page fault gracefully.
		 */
		if (!user_mode(regs))
			return MM_FAULT_ERR(SIGKILL);
		pagefault_out_of_memory();
		return MM_FAULT_RETURN;
	}

	if (fault & (VM_FAULT_SIGBUS|VM_FAULT_HWPOISON|VM_FAULT_HWPOISON_LARGE))
		return do_sigbus(regs, addr, fault);

	/* We don't understand the fault code, this is fatal */
	BUG();
	return MM_FAULT_CONTINUE;
}

/*
 * For 600- and 800-family processors, the error_code parameter is DSISR
 * for a data fault, SRR1 for an instruction fault. For 400-family processors
 * the error_code parameter is ESR for a data fault, 0 for an instruction
 * fault.
 * For 64-bit processors, the error_code parameter is
 *  - DSISR for a non-SLB data access fault,
 *  - SRR1 & 0x08000000 for a non-SLB instruction access fault
 *  - 0 any SLB fault.
 *
 * The return value is 0 if the fault was handled, or the signal
 * number if this is a kernel fault that can't be handled here.
 */
int __kprobes do_page_fault(struct pt_regs *regs, unsigned long address,
			    unsigned long error_code)
{
	enum ctx_state prev_state = exception_enter();
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
	int code = SEGV_MAPERR;
	int is_write = 0;
	int trap = TRAP(regs);
 	int is_exec = trap == 0x400;
	int fault;
	int rc = 0, store_update_sp = 0;

#if !(defined(CONFIG_4xx) || defined(CONFIG_BOOKE))
	/*
	 * Fortunately the bit assignments in SRR1 for an instruction
	 * fault and DSISR for a data fault are mostly the same for the
	 * bits we are interested in.  But there are some bits which
	 * indicate errors in DSISR but can validly be set in SRR1.
	 */
	if (trap == 0x400)
		error_code &= 0x48200000;
	else
		is_write = error_code & DSISR_ISSTORE;
#else
	is_write = error_code & ESR_DST;
#endif /* CONFIG_4xx || CONFIG_BOOKE */

#ifdef CONFIG_PPC_ICSWX
	/*
	 * we need to do this early because this "data storage
	 * interrupt" does not update the DAR/DEAR so we don't want to
	 * look at it
	 */
	if (error_code & ICSWX_DSI_UCT) {
		rc = acop_handle_fault(regs, address, error_code);
		if (rc)
			goto bail;
	}
#endif /* CONFIG_PPC_ICSWX */

	if (notify_page_fault(regs))
		goto bail;

	if (unlikely(debugger_fault_handler(regs)))
		goto bail;

	/* On a kernel SLB miss we can only check for a valid exception entry */
	if (!user_mode(regs) && (address >= TASK_SIZE)) {
		rc = SIGSEGV;
		goto bail;
	}

#if !(defined(CONFIG_4xx) || defined(CONFIG_BOOKE) || \
			     defined(CONFIG_PPC_BOOK3S_64))
  	if (error_code & DSISR_DABRMATCH) {
		/* breakpoint match */
		do_break(regs, address, error_code);
		goto bail;
	}
#endif

	/* We restore the interrupt state now */
	if (!arch_irq_disabled_regs(regs))
		local_irq_enable();

	if (in_atomic() || mm == NULL) {
		if (!user_mode(regs)) {
			rc = SIGSEGV;
			goto bail;
		}
		/* in_atomic() in user mode is really bad,
		   as is current->mm == NULL. */
		printk(KERN_EMERG "Page fault in user mode with "
		       "in_atomic() = %d mm = %p\n", in_atomic(), mm);
		printk(KERN_EMERG "NIP = %lx  MSR = %lx\n",
		       regs->nip, regs->msr);
		die("Weird page fault", regs, SIGSEGV);
	}

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);

	/*
	 * We want to do this outside mmap_sem, because reading code around nip
	 * can result in fault, which will cause a deadlock when called with
	 * mmap_sem held
	 */
	if (user_mode(regs))
		store_update_sp = store_updates_sp(regs);

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
	if (!down_read_trylock(&mm->mmap_sem)) {
		if (!user_mode(regs) && !search_exception_tables(regs->nip))
			goto bad_area_nosemaphore;

retry:
		down_read(&mm->mmap_sem);
	} else {
		/*
		 * The above down_read_trylock() might have succeeded in
		 * which case we'll have missed the might_sleep() from
		 * down_read():
		 */
		might_sleep();
	}

	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;

	/*
	 * N.B. The POWER/Open ABI allows programs to access up to
	 * 288 bytes below the stack pointer.
	 * The kernel signal delivery code writes up to about 1.5kB
	 * below the stack pointer (r1) before decrementing it.
	 * The exec code can write slightly over 640kB to the stack
	 * before setting the user r1.  Thus we allow the stack to
	 * expand to 1MB without further checks.
	 */
	if (address + 0x100000 < vma->vm_end) {
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
		if (address + 2048 < uregs->gpr[1] && !store_update_sp)
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;

good_area:
	code = SEGV_ACCERR;
#if defined(CONFIG_6xx)
	if (error_code & 0x95700000)
		/* an error such as lwarx to I/O controller space,
		   address matching DABR, eciwx, etc. */
		goto bad_area;
#endif /* CONFIG_6xx */
#if defined(CONFIG_8xx)
	/* 8xx sometimes need to load a invalid/non-present TLBs.
	 * These must be invalidated separately as linux mm don't.
	 */
	if (error_code & 0x40000000) /* no translation? */
		_tlbil_va(address, 0, 0, 0);

        /* The MPC8xx seems to always set 0x80000000, which is
         * "undefined".  Of those that can be set, this is the only
         * one which seems bad.
         */
	if (error_code & 0x10000000)
                /* Guarded storage error. */
		goto bad_area;
#endif /* CONFIG_8xx */

	if (is_exec) {
#ifdef CONFIG_PPC_STD_MMU
		/* Protection fault on exec go straight to failure on
		 * Hash based MMUs as they either don't support per-page
		 * execute permission, or if they do, it's handled already
		 * at the hash level. This test would probably have to
		 * be removed if we change the way this works to make hash
		 * processors use the same I/D cache coherency mechanism
		 * as embedded.
		 */
		if (error_code & DSISR_PROTFAULT)
			goto bad_area;
#endif /* CONFIG_PPC_STD_MMU */

		/*
		 * Allow execution from readable areas if the MMU does not
		 * provide separate controls over reading and executing.
		 *
		 * Note: That code used to not be enabled for 4xx/BookE.
		 * It is now as I/D cache coherency for these is done at
		 * set_pte_at() time and I see no reason why the test
		 * below wouldn't be valid on those processors. This -may-
		 * break programs compiled with a really old ABI though.
		 */
		if (!(vma->vm_flags & VM_EXEC) &&
		    (cpu_has_feature(CPU_FTR_NOEXECUTE) ||
		     !(vma->vm_flags & (VM_READ | VM_WRITE))))
			goto bad_area;
	/* a write */
	} else if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
		flags |= FAULT_FLAG_WRITE;
	/* a read */
	} else {
		/* protection fault */
		if (error_code & 0x08000000)
			goto bad_area;
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(mm, vma, address, flags);
	if (unlikely(fault & (VM_FAULT_RETRY|VM_FAULT_ERROR))) {
		rc = mm_fault_error(regs, address, fault);
		if (rc >= MM_FAULT_RETURN)
			goto bail;
		else
			rc = 0;
	}

	/*
	 * Major/minor page fault accounting is only done on the
	 * initial attempt. If we go through a retry, it is extremely
	 * likely that the page will be found in page cache at that point.
	 */
	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR) {
			current->maj_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1,
				      regs, address);
#ifdef CONFIG_PPC_SMLPAR
			if (firmware_has_feature(FW_FEATURE_CMO)) {
				u32 page_ins;

				preempt_disable();
				page_ins = be32_to_cpu(get_lppaca()->page_ins);
				page_ins += 1 << PAGE_FACTOR;
				get_lppaca()->page_ins = cpu_to_be32(page_ins);
				preempt_enable();
			}
#endif /* CONFIG_PPC_SMLPAR */
		} else {
			current->min_flt++;
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1,
				      regs, address);
		}
		if (fault & VM_FAULT_RETRY) {
			/* Clear FAULT_FLAG_ALLOW_RETRY to avoid any risk
			 * of starvation. */
			flags &= ~FAULT_FLAG_ALLOW_RETRY;
			flags |= FAULT_FLAG_TRIED;
			goto retry;
		}
	}

	up_read(&mm->mmap_sem);
	goto bail;

bad_area:
	up_read(&mm->mmap_sem);

bad_area_nosemaphore:
	/* User mode accesses cause a SIGSEGV */
	if (user_mode(regs)) {
		_exception(SIGSEGV, regs, code, address);
		goto bail;
	}

	if (is_exec && (error_code & DSISR_PROTFAULT))
		printk_ratelimited(KERN_CRIT "kernel tried to execute NX-protected"
				   " page (%lx) - exploit attempt? (uid: %d)\n",
				   address, from_kuid(&init_user_ns, current_uid()));

	rc = SIGSEGV;

bail:
	exception_exit(prev_state);
	return rc;

}

/*
 * bad_page_fault is called when we have a bad access from the kernel.
 * It is called from the DSI and ISI handlers in head.S and from some
 * of the procedures in traps.c.
 */
void bad_page_fault(struct pt_regs *regs, unsigned long address, int sig)
{
	const struct exception_table_entry *entry;
	unsigned long *stackend;

	/* Are we prepared to handle this fault?  */
	if ((entry = search_exception_tables(regs->nip)) != NULL) {
		regs->nip = entry->fixup;
		return;
	}

	/* kernel has accessed a bad area */

	switch (regs->trap) {
	case 0x300:
	case 0x380:
		printk(KERN_ALERT "Unable to handle kernel paging request for "
			"data at address 0x%08lx\n", regs->dar);
		break;
	case 0x400:
	case 0x480:
		printk(KERN_ALERT "Unable to handle kernel paging request for "
			"instruction fetch\n");
		break;
	default:
		printk(KERN_ALERT "Unable to handle kernel paging request for "
			"unknown fault\n");
		break;
	}
	printk(KERN_ALERT "Faulting instruction address: 0x%08lx\n",
		regs->nip);

	stackend = end_of_stack(current);
	if (current != &init_task && *stackend != STACK_END_MAGIC)
		printk(KERN_ALERT "Thread overran stack, or stack corrupted\n");

	die("Kernel access of bad area", regs, sig);
}
