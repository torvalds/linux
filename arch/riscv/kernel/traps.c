/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/kdebug.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/irq.h>

#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/csr.h>

int show_unhandled_signals = 1;

extern asmlinkage void handle_exception(void);

static DEFINE_SPINLOCK(die_lock);

void die(struct pt_regs *regs, const char *str)
{
	static int die_counter;
	int ret;

	oops_enter();

	spin_lock_irq(&die_lock);
	console_verbose();
	bust_spinlocks(1);

	pr_emerg("%s [#%d]\n", str, ++die_counter);
	print_modules();
	show_regs(regs);

	ret = notify_die(DIE_OOPS, str, regs, 0, regs->scause, SIGSEGV);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irq(&die_lock);
	oops_exit();

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	if (ret != NOTIFY_STOP)
		do_exit(SIGSEGV);
}

void do_trap(struct pt_regs *regs, int signo, int code,
	unsigned long addr, struct task_struct *tsk)
{
	if (show_unhandled_signals && unhandled_signal(tsk, signo)
	    && printk_ratelimit()) {
		pr_info("%s[%d]: unhandled signal %d code 0x%x at 0x" REG_FMT,
			tsk->comm, task_pid_nr(tsk), signo, code, addr);
		print_vma_addr(KERN_CONT " in ", GET_IP(regs));
		pr_cont("\n");
		show_regs(regs);
	}

	force_sig_fault(signo, code, (void __user *)addr, tsk);
}

static void do_trap_error(struct pt_regs *regs, int signo, int code,
	unsigned long addr, const char *str)
{
	if (user_mode(regs)) {
		do_trap(regs, signo, code, addr, current);
	} else {
		if (!fixup_exception(regs))
			die(regs, str);
	}
}

#define DO_ERROR_INFO(name, signo, code, str)				\
asmlinkage void name(struct pt_regs *regs)				\
{									\
	do_trap_error(regs, signo, code, regs->sepc, "Oops - " str);	\
}

DO_ERROR_INFO(do_trap_unknown,
	SIGILL, ILL_ILLTRP, "unknown exception");
DO_ERROR_INFO(do_trap_insn_misaligned,
	SIGBUS, BUS_ADRALN, "instruction address misaligned");
DO_ERROR_INFO(do_trap_insn_fault,
	SIGSEGV, SEGV_ACCERR, "instruction access fault");
DO_ERROR_INFO(do_trap_insn_illegal,
	SIGILL, ILL_ILLOPC, "illegal instruction");
DO_ERROR_INFO(do_trap_load_misaligned,
	SIGBUS, BUS_ADRALN, "load address misaligned");
DO_ERROR_INFO(do_trap_load_fault,
	SIGSEGV, SEGV_ACCERR, "load access fault");
DO_ERROR_INFO(do_trap_store_misaligned,
	SIGBUS, BUS_ADRALN, "store (or AMO) address misaligned");
DO_ERROR_INFO(do_trap_store_fault,
	SIGSEGV, SEGV_ACCERR, "store (or AMO) access fault");
DO_ERROR_INFO(do_trap_ecall_u,
	SIGILL, ILL_ILLTRP, "environment call from U-mode");
DO_ERROR_INFO(do_trap_ecall_s,
	SIGILL, ILL_ILLTRP, "environment call from S-mode");
DO_ERROR_INFO(do_trap_ecall_m,
	SIGILL, ILL_ILLTRP, "environment call from M-mode");

asmlinkage void do_trap_break(struct pt_regs *regs)
{
#ifdef CONFIG_GENERIC_BUG
	if (!user_mode(regs)) {
		enum bug_trap_type type;

		type = report_bug(regs->sepc, regs);
		switch (type) {
		case BUG_TRAP_TYPE_NONE:
			break;
		case BUG_TRAP_TYPE_WARN:
			regs->sepc += sizeof(bug_insn_t);
			return;
		case BUG_TRAP_TYPE_BUG:
			die(regs, "Kernel BUG");
		}
	}
#endif /* CONFIG_GENERIC_BUG */

	force_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *)(regs->sepc), current);
	regs->sepc += 0x4;
}

#ifdef CONFIG_GENERIC_BUG
int is_valid_bugaddr(unsigned long pc)
{
	bug_insn_t insn;

	if (pc < PAGE_OFFSET)
		return 0;
	if (probe_kernel_address((bug_insn_t __user *)pc, insn))
		return 0;
	return (insn == __BUG_INSN);
}
#endif /* CONFIG_GENERIC_BUG */

void __init trap_init(void)
{
	/*
	 * Set sup0 scratch register to 0, indicating to exception vector
	 * that we are presently executing in the kernel
	 */
	csr_write(sscratch, 0);
	/* Set the exception vector address */
	csr_write(stvec, &handle_exception);
	/* Enable all interrupts */
	csr_write(sie, -1);
}
