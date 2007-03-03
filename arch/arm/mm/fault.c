/*
 *  linux/arch/arm/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-2004 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>

#include "fault.h"

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

		if (pgd_none(*pgd))
			break;

		if (pgd_bad(*pgd)) {
			printk("(bad)");
			break;
		}

		pmd = pmd_offset(pgd, addr);
#if PTRS_PER_PMD != 1
		printk(", *pmd=%08lx", pmd_val(*pmd));
#endif

		if (pmd_none(*pmd))
			break;

		if (pmd_bad(*pmd)) {
			printk("(bad)");
			break;
		}

#ifndef CONFIG_HIGHMEM
		/* We must not map this if we have highmem enabled */
		pte = pte_offset_map(pmd, addr);
		printk(", *pte=%08lx", pte_val(*pte));
		printk(", *ppte=%08lx", pte_val(pte[-PTRS_PER_PTE]));
		pte_unmap(pte);
#endif
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
		unsigned int fsr, unsigned int sig, int code,
		struct pt_regs *regs)
{
	struct siginfo si;

#ifdef CONFIG_DEBUG_USER
	if (user_debug & UDBG_SEGV) {
		printk(KERN_DEBUG "%s: unhandled page fault (%d) at 0x%08lx, code 0x%03x\n",
		       tsk->comm, sig, addr, fsr);
		show_pte(tsk->mm, addr);
		show_regs(regs);
	}
#endif

	tsk->thread.address = addr;
	tsk->thread.error_code = fsr;
	tsk->thread.trap_no = 14;
	si.si_signo = sig;
	si.si_errno = 0;
	si.si_code = code;
	si.si_addr = (void __user *)addr;
	force_sig_info(sig, &si, tsk);
}

void do_bad_area(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->active_mm;

	/*
	 * If we are in kernel mode at this point, we
	 * have no context to handle this fault with.
	 */
	if (user_mode(regs))
		__do_user_fault(tsk, addr, fsr, SIGSEGV, SEGV_MAPERR, regs);
	else
		__do_kernel_fault(mm, addr, fsr, regs);
}

#define VM_FAULT_BADMAP		(-20)
#define VM_FAULT_BADACCESS	(-21)

static int
__do_page_fault(struct mm_struct *mm, unsigned long addr, unsigned int fsr,
		struct task_struct *tsk)
{
	struct vm_area_struct *vma;
	int fault, mask;

	vma = find_vma(mm, addr);
	fault = VM_FAULT_BADMAP;
	if (!vma)
		goto out;
	if (vma->vm_start > addr)
		goto check_stack;

	/*
	 * Ok, we have a good vm_area for this
	 * memory access, so we can handle it.
	 */
good_area:
	if (fsr & (1 << 11)) /* write? */
		mask = VM_WRITE;
	else
		mask = VM_READ|VM_EXEC|VM_WRITE;

	fault = VM_FAULT_BADACCESS;
	if (!(vma->vm_flags & mask))
		goto out;

	/*
	 * If for any reason at all we couldn't handle
	 * the fault, make sure we exit gracefully rather
	 * than endlessly redo the fault.
	 */
survive:
	fault = handle_mm_fault(mm, vma, addr & PAGE_MASK, fsr & (1 << 11));

	/*
	 * Handle the "normal" cases first - successful and sigbus
	 */
	switch (fault) {
	case VM_FAULT_MAJOR:
		tsk->maj_flt++;
		return fault;
	case VM_FAULT_MINOR:
		tsk->min_flt++;
	case VM_FAULT_SIGBUS:
		return fault;
	}

	if (!is_init(tsk))
		goto out;

	/*
	 * If we are out of memory for pid1, sleep for a while and retry
	 */
	up_read(&mm->mmap_sem);
	yield();
	down_read(&mm->mmap_sem);
	goto survive;

check_stack:
	if (vma->vm_flags & VM_GROWSDOWN && !expand_stack(vma, addr))
		goto good_area;
out:
	return fault;
}

static int
do_page_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	int fault, sig, code;

	tsk = current;
	mm  = tsk->mm;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_atomic() || !mm)
		goto no_context;

	/*
	 * As per x86, we may deadlock here.  However, since the kernel only
	 * validly references user space from well defined areas of the code,
	 * we can bug out early if this is from code which shouldn't.
	 */
	if (!down_read_trylock(&mm->mmap_sem)) {
		if (!user_mode(regs) && !search_exception_tables(regs->ARM_pc))
			goto no_context;
		down_read(&mm->mmap_sem);
	}

	fault = __do_page_fault(mm, addr, fsr, tsk);
	up_read(&mm->mmap_sem);

	/*
	 * Handle the "normal" case first - VM_FAULT_MAJOR / VM_FAULT_MINOR
	 */
	if (fault >= VM_FAULT_MINOR)
		return 0;

	/*
	 * If we are in kernel mode at this point, we
	 * have no context to handle this fault with.
	 */
	if (!user_mode(regs))
		goto no_context;

	switch (fault) {
	case VM_FAULT_OOM:
		/*
		 * We ran out of memory, or some other thing
		 * happened to us that made us unable to handle
		 * the page fault gracefully.
		 */
		printk("VM: killing process %s\n", tsk->comm);
		do_exit(SIGKILL);
		return 0;

	case VM_FAULT_SIGBUS:
		/*
		 * We had some memory, but were unable to
		 * successfully fix up this page fault.
		 */
		sig = SIGBUS;
		code = BUS_ADRERR;
		break;

	default:
		/*
		 * Something tried to access memory that
		 * isn't in our memory map..
		 */
		sig = SIGSEGV;
		code = fault == VM_FAULT_BADACCESS ?
			SEGV_ACCERR : SEGV_MAPERR;
		break;
	}

	__do_user_fault(tsk, addr, fsr, sig, code, regs);
	return 0;

no_context:
	__do_kernel_fault(mm, addr, fsr, regs);
	return 0;
}

/*
 * First Level Translation Fault Handler
 *
 * We enter here because the first level page table doesn't contain
 * a valid entry for the address.
 *
 * If the address is in kernel space (>= TASK_SIZE), then we are
 * probably faulting in the vmalloc() area.
 *
 * If the init_task's first level page tables contains the relevant
 * entry, we copy the it to this task.  If not, we send the process
 * a signal, fixup the exception, or oops the kernel.
 *
 * NOTE! We MUST NOT take any locks for this case. We may be in an
 * interrupt or a critical region, and should only copy the information
 * from the master page table, nothing more.
 */
static int
do_translation_fault(unsigned long addr, unsigned int fsr,
		     struct pt_regs *regs)
{
	unsigned int index;
	pgd_t *pgd, *pgd_k;
	pmd_t *pmd, *pmd_k;

	if (addr < TASK_SIZE)
		return do_page_fault(addr, fsr, regs);

	index = pgd_index(addr);

	/*
	 * FIXME: CP15 C1 is write only on ARMv3 architectures.
	 */
	pgd = cpu_get_pgd() + index;
	pgd_k = init_mm.pgd + index;

	if (pgd_none(*pgd_k))
		goto bad_area;

	if (!pgd_present(*pgd))
		set_pgd(pgd, *pgd_k);

	pmd_k = pmd_offset(pgd_k, addr);
	pmd   = pmd_offset(pgd, addr);

	if (pmd_none(*pmd_k))
		goto bad_area;

	copy_pmd(pmd, pmd_k);
	return 0;

bad_area:
	do_bad_area(addr, fsr, regs);
	return 0;
}

/*
 * Some section permission faults need to be handled gracefully.
 * They can happen due to a __{get,put}_user during an oops.
 */
static int
do_sect_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	do_bad_area(addr, fsr, regs);
	return 0;
}

/*
 * This abort handler always returns "fault".
 */
static int
do_bad(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	return 1;
}

static struct fsr_info {
	int	(*fn)(unsigned long addr, unsigned int fsr, struct pt_regs *regs);
	int	sig;
	int	code;
	const char *name;
} fsr_info[] = {
	/*
	 * The following are the standard ARMv3 and ARMv4 aborts.  ARMv5
	 * defines these to be "precise" aborts.
	 */
	{ do_bad,		SIGSEGV, 0,		"vector exception"		   },
	{ do_bad,		SIGILL,	 BUS_ADRALN,	"alignment exception"		   },
	{ do_bad,		SIGKILL, 0,		"terminal exception"		   },
	{ do_bad,		SIGILL,	 BUS_ADRALN,	"alignment exception"		   },
	{ do_bad,		SIGBUS,	 0,		"external abort on linefetch"	   },
	{ do_translation_fault,	SIGSEGV, SEGV_MAPERR,	"section translation fault"	   },
	{ do_bad,		SIGBUS,	 0,		"external abort on linefetch"	   },
	{ do_page_fault,	SIGSEGV, SEGV_MAPERR,	"page translation fault"	   },
	{ do_bad,		SIGBUS,	 0,		"external abort on non-linefetch"  },
	{ do_bad,		SIGSEGV, SEGV_ACCERR,	"section domain fault"		   },
	{ do_bad,		SIGBUS,	 0,		"external abort on non-linefetch"  },
	{ do_bad,		SIGSEGV, SEGV_ACCERR,	"page domain fault"		   },
	{ do_bad,		SIGBUS,	 0,		"external abort on translation"	   },
	{ do_sect_fault,	SIGSEGV, SEGV_ACCERR,	"section permission fault"	   },
	{ do_bad,		SIGBUS,	 0,		"external abort on translation"	   },
	{ do_page_fault,	SIGSEGV, SEGV_ACCERR,	"page permission fault"		   },
	/*
	 * The following are "imprecise" aborts, which are signalled by bit
	 * 10 of the FSR, and may not be recoverable.  These are only
	 * supported if the CPU abort handler supports bit 10.
	 */
	{ do_bad,		SIGBUS,  0,		"unknown 16"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 17"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 18"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 19"			   },
	{ do_bad,		SIGBUS,  0,		"lock abort"			   }, /* xscale */
	{ do_bad,		SIGBUS,  0,		"unknown 21"			   },
	{ do_bad,		SIGBUS,  BUS_OBJERR,	"imprecise external abort"	   }, /* xscale */
	{ do_bad,		SIGBUS,  0,		"unknown 23"			   },
	{ do_bad,		SIGBUS,  0,		"dcache parity error"		   }, /* xscale */
	{ do_bad,		SIGBUS,  0,		"unknown 25"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 26"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 27"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 28"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 29"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 30"			   },
	{ do_bad,		SIGBUS,  0,		"unknown 31"			   }
};

void __init
hook_fault_code(int nr, int (*fn)(unsigned long, unsigned int, struct pt_regs *),
		int sig, const char *name)
{
	if (nr >= 0 && nr < ARRAY_SIZE(fsr_info)) {
		fsr_info[nr].fn   = fn;
		fsr_info[nr].sig  = sig;
		fsr_info[nr].name = name;
	}
}

/*
 * Dispatch a data abort to the relevant handler.
 */
asmlinkage void __exception
do_DataAbort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	const struct fsr_info *inf = fsr_info + (fsr & 15) + ((fsr & (1 << 10)) >> 6);
	struct siginfo info;

	if (!inf->fn(addr, fsr, regs))
		return;

	printk(KERN_ALERT "Unhandled fault: %s (0x%03x) at 0x%08lx\n",
		inf->name, fsr, addr);

	info.si_signo = inf->sig;
	info.si_errno = 0;
	info.si_code  = inf->code;
	info.si_addr  = (void __user *)addr;
	notify_die("", regs, &info, fsr, 0);
}

asmlinkage void __exception
do_PrefetchAbort(unsigned long addr, struct pt_regs *regs)
{
	do_translation_fault(addr, 0, regs);
}

