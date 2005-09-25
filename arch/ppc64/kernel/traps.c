/*
 *  linux/arch/ppc64/kernel/traps.c
 *
 *  Copyright (C) 1995-1996  Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  and Paul Mackerras (paulus@cs.anu.edu.au)
 */

/*
 * This file handles the architecture-dependent parts of hardware exceptions
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kprobes.h>
#include <asm/kdebug.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/ppcdebug.h>
#include <asm/rtas.h>
#include <asm/systemcfg.h>
#include <asm/machdep.h>
#include <asm/pmc.h>

#ifdef CONFIG_DEBUGGER
int (*__debugger)(struct pt_regs *regs);
int (*__debugger_ipi)(struct pt_regs *regs);
int (*__debugger_bpt)(struct pt_regs *regs);
int (*__debugger_sstep)(struct pt_regs *regs);
int (*__debugger_iabr_match)(struct pt_regs *regs);
int (*__debugger_dabr_match)(struct pt_regs *regs);
int (*__debugger_fault_handler)(struct pt_regs *regs);

EXPORT_SYMBOL(__debugger);
EXPORT_SYMBOL(__debugger_ipi);
EXPORT_SYMBOL(__debugger_bpt);
EXPORT_SYMBOL(__debugger_sstep);
EXPORT_SYMBOL(__debugger_iabr_match);
EXPORT_SYMBOL(__debugger_dabr_match);
EXPORT_SYMBOL(__debugger_fault_handler);
#endif

struct notifier_block *ppc64_die_chain;
static DEFINE_SPINLOCK(die_notifier_lock);

int register_die_notifier(struct notifier_block *nb)
{
	int err = 0;
	unsigned long flags;

	spin_lock_irqsave(&die_notifier_lock, flags);
	err = notifier_chain_register(&ppc64_die_chain, nb);
	spin_unlock_irqrestore(&die_notifier_lock, flags);
	return err;
}

/*
 * Trap & Exception support
 */

static DEFINE_SPINLOCK(die_lock);

int die(const char *str, struct pt_regs *regs, long err)
{
	static int die_counter;
	int nl = 0;

	if (debugger(regs))
		return 1;

	console_verbose();
	spin_lock_irq(&die_lock);
	bust_spinlocks(1);
	printk("Oops: %s, sig: %ld [#%d]\n", str, err, ++die_counter);
#ifdef CONFIG_PREEMPT
	printk("PREEMPT ");
	nl = 1;
#endif
#ifdef CONFIG_SMP
	printk("SMP NR_CPUS=%d ", NR_CPUS);
	nl = 1;
#endif
#ifdef CONFIG_DEBUG_PAGEALLOC
	printk("DEBUG_PAGEALLOC ");
	nl = 1;
#endif
#ifdef CONFIG_NUMA
	printk("NUMA ");
	nl = 1;
#endif
	switch(systemcfg->platform) {
		case PLATFORM_PSERIES:
			printk("PSERIES ");
			nl = 1;
			break;
		case PLATFORM_PSERIES_LPAR:
			printk("PSERIES LPAR ");
			nl = 1;
			break;
		case PLATFORM_ISERIES_LPAR:
			printk("ISERIES LPAR ");
			nl = 1;
			break;
		case PLATFORM_POWERMAC:
			printk("POWERMAC ");
			nl = 1;
			break;
		case PLATFORM_BPA:
			printk("BPA ");
			nl = 1;
			break;
	}
	if (nl)
		printk("\n");
	print_modules();
	show_regs(regs);
	bust_spinlocks(0);
	spin_unlock_irq(&die_lock);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops) {
		printk(KERN_EMERG "Fatal exception: panic in 5 seconds\n");
		ssleep(5);
		panic("Fatal exception");
	}
	do_exit(SIGSEGV);

	return 0;
}

void _exception(int signr, struct pt_regs *regs, int code, unsigned long addr)
{
	siginfo_t info;

	if (!user_mode(regs)) {
		if (die("Exception in kernel mode", regs, signr))
			return;
	}

	memset(&info, 0, sizeof(info));
	info.si_signo = signr;
	info.si_code = code;
	info.si_addr = (void __user *) addr;
	force_sig_info(signr, &info, current);
}

void system_reset_exception(struct pt_regs *regs)
{
	/* See if any machine dependent calls */
	if (ppc_md.system_reset_exception)
		ppc_md.system_reset_exception(regs);

	die("System Reset", regs, 0);

	/* Must die if the interrupt is not recoverable */
	if (!(regs->msr & MSR_RI))
		panic("Unrecoverable System Reset");

	/* What should we do here? We could issue a shutdown or hard reset. */
}

void machine_check_exception(struct pt_regs *regs)
{
	int recover = 0;

	/* See if any machine dependent calls */
	if (ppc_md.machine_check_exception)
		recover = ppc_md.machine_check_exception(regs);

	if (recover)
		return;

	if (debugger_fault_handler(regs))
		return;
	die("Machine check", regs, 0);

	/* Must die if the interrupt is not recoverable */
	if (!(regs->msr & MSR_RI))
		panic("Unrecoverable Machine check");
}

void unknown_exception(struct pt_regs *regs)
{
	printk("Bad trap at PC: %lx, SR: %lx, vector=%lx\n",
	       regs->nip, regs->msr, regs->trap);

	_exception(SIGTRAP, regs, 0, 0);
}

void instruction_breakpoint_exception(struct pt_regs *regs)
{
	if (notify_die(DIE_IABR_MATCH, "iabr_match", regs, 5,
					5, SIGTRAP) == NOTIFY_STOP)
		return;
	if (debugger_iabr_match(regs))
		return;
	_exception(SIGTRAP, regs, TRAP_BRKPT, regs->nip);
}

void __kprobes single_step_exception(struct pt_regs *regs)
{
	regs->msr &= ~MSR_SE;  /* Turn off 'trace' bit */

	if (notify_die(DIE_SSTEP, "single_step", regs, 5,
					5, SIGTRAP) == NOTIFY_STOP)
		return;
	if (debugger_sstep(regs))
		return;

	_exception(SIGTRAP, regs, TRAP_TRACE, regs->nip);
}

/*
 * After we have successfully emulated an instruction, we have to
 * check if the instruction was being single-stepped, and if so,
 * pretend we got a single-step exception.  This was pointed out
 * by Kumar Gala.  -- paulus
 */
static inline void emulate_single_step(struct pt_regs *regs)
{
	if (regs->msr & MSR_SE)
		single_step_exception(regs);
}

static void parse_fpe(struct pt_regs *regs)
{
	int code = 0;
	unsigned long fpscr;

	flush_fp_to_thread(current);

	fpscr = current->thread.fpscr;

	/* Invalid operation */
	if ((fpscr & FPSCR_VE) && (fpscr & FPSCR_VX))
		code = FPE_FLTINV;

	/* Overflow */
	else if ((fpscr & FPSCR_OE) && (fpscr & FPSCR_OX))
		code = FPE_FLTOVF;

	/* Underflow */
	else if ((fpscr & FPSCR_UE) && (fpscr & FPSCR_UX))
		code = FPE_FLTUND;

	/* Divide by zero */
	else if ((fpscr & FPSCR_ZE) && (fpscr & FPSCR_ZX))
		code = FPE_FLTDIV;

	/* Inexact result */
	else if ((fpscr & FPSCR_XE) && (fpscr & FPSCR_XX))
		code = FPE_FLTRES;

	_exception(SIGFPE, regs, code, regs->nip);
}

/*
 * Illegal instruction emulation support.  Return non-zero if we can't
 * emulate, or -EFAULT if the associated memory access caused an access
 * fault.  Return zero on success.
 */

#define INST_MFSPR_PVR		0x7c1f42a6
#define INST_MFSPR_PVR_MASK	0xfc1fffff

#define INST_DCBA		0x7c0005ec
#define INST_DCBA_MASK		0x7c0007fe

#define INST_MCRXR		0x7c000400
#define INST_MCRXR_MASK		0x7c0007fe

static int emulate_instruction(struct pt_regs *regs)
{
	unsigned int instword;

	if (!user_mode(regs))
		return -EINVAL;

	CHECK_FULL_REGS(regs);

	if (get_user(instword, (unsigned int __user *)(regs->nip)))
		return -EFAULT;

	/* Emulate the mfspr rD, PVR. */
	if ((instword & INST_MFSPR_PVR_MASK) == INST_MFSPR_PVR) {
		unsigned int rd;

		rd = (instword >> 21) & 0x1f;
		regs->gpr[rd] = mfspr(SPRN_PVR);
		return 0;
	}

	/* Emulating the dcba insn is just a no-op.  */
	if ((instword & INST_DCBA_MASK) == INST_DCBA) {
		static int warned;

		if (!warned) {
			printk(KERN_WARNING
			       "process %d (%s) uses obsolete 'dcba' insn\n",
			       current->pid, current->comm);
			warned = 1;
		}
		return 0;
	}

	/* Emulate the mcrxr insn.  */
	if ((instword & INST_MCRXR_MASK) == INST_MCRXR) {
		static int warned;
		unsigned int shift;

		if (!warned) {
			printk(KERN_WARNING
			       "process %d (%s) uses obsolete 'mcrxr' insn\n",
			       current->pid, current->comm);
			warned = 1;
		}

		shift = (instword >> 21) & 0x1c;
		regs->ccr &= ~(0xf0000000 >> shift);
		regs->ccr |= (regs->xer & 0xf0000000) >> shift;
		regs->xer &= ~0xf0000000;
		return 0;
	}

	return -EINVAL;
}

/*
 * Look through the list of trap instructions that are used for BUG(),
 * BUG_ON() and WARN_ON() and see if we hit one.  At this point we know
 * that the exception was caused by a trap instruction of some kind.
 * Returns 1 if we should continue (i.e. it was a WARN_ON) or 0
 * otherwise.
 */
extern struct bug_entry __start___bug_table[], __stop___bug_table[];

#ifndef CONFIG_MODULES
#define module_find_bug(x)	NULL
#endif

struct bug_entry *find_bug(unsigned long bugaddr)
{
	struct bug_entry *bug;

	for (bug = __start___bug_table; bug < __stop___bug_table; ++bug)
		if (bugaddr == bug->bug_addr)
			return bug;
	return module_find_bug(bugaddr);
}

static int
check_bug_trap(struct pt_regs *regs)
{
	struct bug_entry *bug;
	unsigned long addr;

	if (regs->msr & MSR_PR)
		return 0;	/* not in kernel */
	addr = regs->nip;	/* address of trap instruction */
	if (addr < PAGE_OFFSET)
		return 0;
	bug = find_bug(regs->nip);
	if (bug == NULL)
		return 0;
	if (bug->line & BUG_WARNING_TRAP) {
		/* this is a WARN_ON rather than BUG/BUG_ON */
		printk(KERN_ERR "Badness in %s at %s:%d\n",
		       bug->function, bug->file,
		      (unsigned int)bug->line & ~BUG_WARNING_TRAP);
		show_stack(current, (void *)regs->gpr[1]);
		return 1;
	}
	printk(KERN_CRIT "kernel BUG in %s at %s:%d!\n",
	       bug->function, bug->file, (unsigned int)bug->line);
	return 0;
}

void __kprobes program_check_exception(struct pt_regs *regs)
{
	if (debugger_fault_handler(regs))
		return;

	if (regs->msr & 0x100000) {
		/* IEEE FP exception */
		parse_fpe(regs);
	} else if (regs->msr & 0x20000) {
		/* trap exception */

		if (notify_die(DIE_BPT, "breakpoint", regs, 5,
					5, SIGTRAP) == NOTIFY_STOP)
			return;
		if (debugger_bpt(regs))
			return;

		if (check_bug_trap(regs)) {
			regs->nip += 4;
			return;
		}
		_exception(SIGTRAP, regs, TRAP_BRKPT, regs->nip);

	} else {
		/* Privileged or illegal instruction; try to emulate it. */
		switch (emulate_instruction(regs)) {
		case 0:
			regs->nip += 4;
			emulate_single_step(regs);
			break;

		case -EFAULT:
			_exception(SIGSEGV, regs, SEGV_MAPERR, regs->nip);
			break;

		default:
			if (regs->msr & 0x40000)
				/* priveleged */
				_exception(SIGILL, regs, ILL_PRVOPC, regs->nip);
			else
				/* illegal */
				_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
			break;
		}
	}
}

void kernel_fp_unavailable_exception(struct pt_regs *regs)
{
	printk(KERN_EMERG "Unrecoverable FP Unavailable Exception "
			  "%lx at %lx\n", regs->trap, regs->nip);
	die("Unrecoverable FP Unavailable Exception", regs, SIGABRT);
}

void altivec_unavailable_exception(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		/* A user program has executed an altivec instruction,
		   but this kernel doesn't support altivec. */
		_exception(SIGILL, regs, ILL_ILLOPC, regs->nip);
		return;
	}
	printk(KERN_EMERG "Unrecoverable VMX/Altivec Unavailable Exception "
			  "%lx at %lx\n", regs->trap, regs->nip);
	die("Unrecoverable VMX/Altivec Unavailable Exception", regs, SIGABRT);
}

extern perf_irq_t perf_irq;

void performance_monitor_exception(struct pt_regs *regs)
{
	perf_irq(regs);
}

void alignment_exception(struct pt_regs *regs)
{
	int fixed;

	fixed = fix_alignment(regs);

	if (fixed == 1) {
		regs->nip += 4;	/* skip over emulated instruction */
		emulate_single_step(regs);
		return;
	}

	/* Operand address was bad */	
	if (fixed == -EFAULT) {
		if (user_mode(regs)) {
			_exception(SIGSEGV, regs, SEGV_MAPERR, regs->dar);
		} else {
			/* Search exception table */
			bad_page_fault(regs, regs->dar, SIGSEGV);
		}

		return;
	}

	_exception(SIGBUS, regs, BUS_ADRALN, regs->nip);
}

#ifdef CONFIG_ALTIVEC
void altivec_assist_exception(struct pt_regs *regs)
{
	int err;
	siginfo_t info;

	if (!user_mode(regs)) {
		printk(KERN_EMERG "VMX/Altivec assist exception in kernel mode"
		       " at %lx\n", regs->nip);
		die("Kernel VMX/Altivec assist exception", regs, SIGILL);
	}

	flush_altivec_to_thread(current);

	err = emulate_altivec(regs);
	if (err == 0) {
		regs->nip += 4;		/* skip emulated instruction */
		emulate_single_step(regs);
		return;
	}

	if (err == -EFAULT) {
		/* got an error reading the instruction */
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		info.si_code = SEGV_MAPERR;
		info.si_addr = (void __user *) regs->nip;
		force_sig_info(SIGSEGV, &info, current);
	} else {
		/* didn't recognize the instruction */
		/* XXX quick hack for now: set the non-Java bit in the VSCR */
		if (printk_ratelimit())
			printk(KERN_ERR "Unrecognized altivec instruction "
			       "in %s at %lx\n", current->comm, regs->nip);
		current->thread.vscr.u[3] |= 0x10000;
	}
}
#endif /* CONFIG_ALTIVEC */

/*
 * We enter here if we get an unrecoverable exception, that is, one
 * that happened at a point where the RI (recoverable interrupt) bit
 * in the MSR is 0.  This indicates that SRR0/1 are live, and that
 * we therefore lost state by taking this exception.
 */
void unrecoverable_exception(struct pt_regs *regs)
{
	printk(KERN_EMERG "Unrecoverable exception %lx at %lx\n",
	       regs->trap, regs->nip);
	die("Unrecoverable exception", regs, SIGABRT);
}

/*
 * We enter here if we discover during exception entry that we are
 * running in supervisor mode with a userspace value in the stack pointer.
 */
void kernel_bad_stack(struct pt_regs *regs)
{
	printk(KERN_EMERG "Bad kernel stack pointer %lx at %lx\n",
	       regs->gpr[1], regs->nip);
	die("Bad kernel stack pointer", regs, SIGABRT);
}

void __init trap_init(void)
{
}
