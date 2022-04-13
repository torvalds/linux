// SPDX-License-Identifier: GPL-2.0
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *
 *  Derived from "arch/i386/kernel/traps.c"
 *    Copyright (C) 1991, 1992 Linus Torvalds
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */
#include "asm/irqflags.h"
#include "asm/ptrace.h"
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/randomize_kstack.h>
#include <linux/extable.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cpu.h>
#include <linux/entry-common.h>
#include <asm/asm-extable.h>
#include <asm/fpu/api.h>
#include <asm/vtime.h>
#include "entry.h"

static inline void __user *get_trap_ip(struct pt_regs *regs)
{
	unsigned long address;

	if (regs->int_code & 0x200)
		address = current->thread.trap_tdb.data[3];
	else
		address = regs->psw.addr;
	return (void __user *) (address - (regs->int_code >> 16));
}

int is_valid_bugaddr(unsigned long addr)
{
	return 1;
}

void do_report_trap(struct pt_regs *regs, int si_signo, int si_code, char *str)
{
	if (user_mode(regs)) {
		force_sig_fault(si_signo, si_code, get_trap_ip(regs));
		report_user_fault(regs, si_signo, 0);
        } else {
		if (!fixup_exception(regs))
			die(regs, str);
        }
}

static void do_trap(struct pt_regs *regs, int si_signo, int si_code, char *str)
{
	if (notify_die(DIE_TRAP, str, regs, 0,
		       regs->int_code, si_signo) == NOTIFY_STOP)
		return;
	do_report_trap(regs, si_signo, si_code, str);
}
NOKPROBE_SYMBOL(do_trap);

void do_per_trap(struct pt_regs *regs)
{
	if (notify_die(DIE_SSTEP, "sstep", regs, 0, 0, SIGTRAP) == NOTIFY_STOP)
		return;
	if (!current->ptrace)
		return;
	force_sig_fault(SIGTRAP, TRAP_HWBKPT,
		(void __force __user *) current->thread.per_event.address);
}
NOKPROBE_SYMBOL(do_per_trap);

static void default_trap_handler(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		report_user_fault(regs, SIGSEGV, 0);
		force_exit_sig(SIGSEGV);
	} else
		die(regs, "Unknown program exception");
}

#define DO_ERROR_INFO(name, signr, sicode, str) \
static void name(struct pt_regs *regs)		\
{						\
	do_trap(regs, signr, sicode, str);	\
}

DO_ERROR_INFO(addressing_exception, SIGILL, ILL_ILLADR,
	      "addressing exception")
DO_ERROR_INFO(execute_exception, SIGILL, ILL_ILLOPN,
	      "execute exception")
DO_ERROR_INFO(divide_exception, SIGFPE, FPE_INTDIV,
	      "fixpoint divide exception")
DO_ERROR_INFO(overflow_exception, SIGFPE, FPE_INTOVF,
	      "fixpoint overflow exception")
DO_ERROR_INFO(hfp_overflow_exception, SIGFPE, FPE_FLTOVF,
	      "HFP overflow exception")
DO_ERROR_INFO(hfp_underflow_exception, SIGFPE, FPE_FLTUND,
	      "HFP underflow exception")
DO_ERROR_INFO(hfp_significance_exception, SIGFPE, FPE_FLTRES,
	      "HFP significance exception")
DO_ERROR_INFO(hfp_divide_exception, SIGFPE, FPE_FLTDIV,
	      "HFP divide exception")
DO_ERROR_INFO(hfp_sqrt_exception, SIGFPE, FPE_FLTINV,
	      "HFP square root exception")
DO_ERROR_INFO(operand_exception, SIGILL, ILL_ILLOPN,
	      "operand exception")
DO_ERROR_INFO(privileged_op, SIGILL, ILL_PRVOPC,
	      "privileged operation")
DO_ERROR_INFO(special_op_exception, SIGILL, ILL_ILLOPN,
	      "special operation exception")
DO_ERROR_INFO(transaction_exception, SIGILL, ILL_ILLOPN,
	      "transaction constraint exception")

static inline void do_fp_trap(struct pt_regs *regs, __u32 fpc)
{
	int si_code = 0;
	/* FPC[2] is Data Exception Code */
	if ((fpc & 0x00000300) == 0) {
		/* bits 6 and 7 of DXC are 0 iff IEEE exception */
		if (fpc & 0x8000) /* invalid fp operation */
			si_code = FPE_FLTINV;
		else if (fpc & 0x4000) /* div by 0 */
			si_code = FPE_FLTDIV;
		else if (fpc & 0x2000) /* overflow */
			si_code = FPE_FLTOVF;
		else if (fpc & 0x1000) /* underflow */
			si_code = FPE_FLTUND;
		else if (fpc & 0x0800) /* inexact */
			si_code = FPE_FLTRES;
	}
	do_trap(regs, SIGFPE, si_code, "floating point exception");
}

static void translation_specification_exception(struct pt_regs *regs)
{
	/* May never happen. */
	panic("Translation-Specification Exception");
}

static void illegal_op(struct pt_regs *regs)
{
        __u8 opcode[6];
	__u16 __user *location;
	int is_uprobe_insn = 0;
	int signal = 0;

	location = get_trap_ip(regs);

	if (user_mode(regs)) {
		if (get_user(*((__u16 *) opcode), (__u16 __user *) location))
			return;
		if (*((__u16 *) opcode) == S390_BREAKPOINT_U16) {
			if (current->ptrace)
				force_sig_fault(SIGTRAP, TRAP_BRKPT, location);
			else
				signal = SIGILL;
#ifdef CONFIG_UPROBES
		} else if (*((__u16 *) opcode) == UPROBE_SWBP_INSN) {
			is_uprobe_insn = 1;
#endif
		} else
			signal = SIGILL;
	}
	/*
	 * We got either an illegal op in kernel mode, or user space trapped
	 * on a uprobes illegal instruction. See if kprobes or uprobes picks
	 * it up. If not, SIGILL.
	 */
	if (is_uprobe_insn || !user_mode(regs)) {
		if (notify_die(DIE_BPT, "bpt", regs, 0,
			       3, SIGTRAP) != NOTIFY_STOP)
			signal = SIGILL;
	}
	if (signal)
		do_trap(regs, signal, ILL_ILLOPC, "illegal operation");
}
NOKPROBE_SYMBOL(illegal_op);

DO_ERROR_INFO(specification_exception, SIGILL, ILL_ILLOPN,
	      "specification exception");

static void vector_exception(struct pt_regs *regs)
{
	int si_code, vic;

	if (!MACHINE_HAS_VX) {
		do_trap(regs, SIGILL, ILL_ILLOPN, "illegal operation");
		return;
	}

	/* get vector interrupt code from fpc */
	save_fpu_regs();
	vic = (current->thread.fpu.fpc & 0xf00) >> 8;
	switch (vic) {
	case 1: /* invalid vector operation */
		si_code = FPE_FLTINV;
		break;
	case 2: /* division by zero */
		si_code = FPE_FLTDIV;
		break;
	case 3: /* overflow */
		si_code = FPE_FLTOVF;
		break;
	case 4: /* underflow */
		si_code = FPE_FLTUND;
		break;
	case 5:	/* inexact */
		si_code = FPE_FLTRES;
		break;
	default: /* unknown cause */
		si_code = 0;
	}
	do_trap(regs, SIGFPE, si_code, "vector exception");
}

static void data_exception(struct pt_regs *regs)
{
	save_fpu_regs();
	if (current->thread.fpu.fpc & FPC_DXC_MASK)
		do_fp_trap(regs, current->thread.fpu.fpc);
	else
		do_trap(regs, SIGILL, ILL_ILLOPN, "data exception");
}

static void space_switch_exception(struct pt_regs *regs)
{
	/* Set user psw back to home space mode. */
	if (user_mode(regs))
		regs->psw.mask |= PSW_ASC_HOME;
	/* Send SIGILL. */
	do_trap(regs, SIGILL, ILL_PRVOPC, "space switch event");
}

static void monitor_event_exception(struct pt_regs *regs)
{
	if (user_mode(regs))
		return;

	switch (report_bug(regs->psw.addr - (regs->int_code >> 16), regs)) {
	case BUG_TRAP_TYPE_NONE:
		fixup_exception(regs);
		break;
	case BUG_TRAP_TYPE_WARN:
		break;
	case BUG_TRAP_TYPE_BUG:
		die(regs, "monitor event");
		break;
	}
}

void kernel_stack_overflow(struct pt_regs *regs)
{
	bust_spinlocks(1);
	printk("Kernel stack overflow.\n");
	show_regs(regs);
	bust_spinlocks(0);
	panic("Corrupt kernel stack, can't continue.");
}
NOKPROBE_SYMBOL(kernel_stack_overflow);

static void __init test_monitor_call(void)
{
	int val = 1;

	if (!IS_ENABLED(CONFIG_BUG))
		return;
	asm volatile(
		"	mc	0,0\n"
		"0:	xgr	%0,%0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (val));
	if (!val)
		panic("Monitor call doesn't work!\n");
}

void __init trap_init(void)
{
	local_mcck_enable();
	test_monitor_call();
}

static void (*pgm_check_table[128])(struct pt_regs *regs);

void noinstr __do_pgm_check(struct pt_regs *regs)
{
	unsigned int trapnr;
	irqentry_state_t state;

	regs->int_code = S390_lowcore.pgm_int_code;
	regs->int_parm_long = S390_lowcore.trans_exc_code;

	state = irqentry_enter(regs);

	if (user_mode(regs)) {
		update_timer_sys();
		if (!static_branch_likely(&cpu_has_bear)) {
			if (regs->last_break < 4096)
				regs->last_break = 1;
		}
		current->thread.last_break = regs->last_break;
	}

	if (S390_lowcore.pgm_code & 0x0200) {
		/* transaction abort */
		current->thread.trap_tdb = S390_lowcore.pgm_tdb;
	}

	if (S390_lowcore.pgm_code & PGM_INT_CODE_PER) {
		if (user_mode(regs)) {
			struct per_event *ev = &current->thread.per_event;

			set_thread_flag(TIF_PER_TRAP);
			ev->address = S390_lowcore.per_address;
			ev->cause = S390_lowcore.per_code_combined;
			ev->paid = S390_lowcore.per_access_id;
		} else {
			/* PER event in kernel is kprobes */
			__arch_local_irq_ssm(regs->psw.mask & ~PSW_MASK_PER);
			do_per_trap(regs);
			goto out;
		}
	}

	if (!irqs_disabled_flags(regs->psw.mask))
		trace_hardirqs_on();
	__arch_local_irq_ssm(regs->psw.mask & ~PSW_MASK_PER);

	trapnr = regs->int_code & PGM_INT_CODE_MASK;
	if (trapnr)
		pgm_check_table[trapnr](regs);
out:
	local_irq_disable();
	irqentry_exit(regs, state);
}

/*
 * The program check table contains exactly 128 (0x00-0x7f) entries. Each
 * line defines the function to be called corresponding to the program check
 * interruption code.
 */
static void (*pgm_check_table[128])(struct pt_regs *regs) = {
	[0x00]		= default_trap_handler,
	[0x01]		= illegal_op,
	[0x02]		= privileged_op,
	[0x03]		= execute_exception,
	[0x04]		= do_protection_exception,
	[0x05]		= addressing_exception,
	[0x06]		= specification_exception,
	[0x07]		= data_exception,
	[0x08]		= overflow_exception,
	[0x09]		= divide_exception,
	[0x0a]		= overflow_exception,
	[0x0b]		= divide_exception,
	[0x0c]		= hfp_overflow_exception,
	[0x0d]		= hfp_underflow_exception,
	[0x0e]		= hfp_significance_exception,
	[0x0f]		= hfp_divide_exception,
	[0x10]		= do_dat_exception,
	[0x11]		= do_dat_exception,
	[0x12]		= translation_specification_exception,
	[0x13]		= special_op_exception,
	[0x14]		= default_trap_handler,
	[0x15]		= operand_exception,
	[0x16]		= default_trap_handler,
	[0x17]		= default_trap_handler,
	[0x18]		= transaction_exception,
	[0x19]		= default_trap_handler,
	[0x1a]		= default_trap_handler,
	[0x1b]		= vector_exception,
	[0x1c]		= space_switch_exception,
	[0x1d]		= hfp_sqrt_exception,
	[0x1e ... 0x37] = default_trap_handler,
	[0x38]		= do_dat_exception,
	[0x39]		= do_dat_exception,
	[0x3a]		= do_dat_exception,
	[0x3b]		= do_dat_exception,
	[0x3c]		= default_trap_handler,
	[0x3d]		= do_secure_storage_access,
	[0x3e]		= do_non_secure_storage_access,
	[0x3f]		= do_secure_storage_violation,
	[0x40]		= monitor_event_exception,
	[0x41 ... 0x7f] = default_trap_handler,
};

#define COND_TRAP(x) asm(			\
	".weak " __stringify(x) "\n\t"		\
	".set  " __stringify(x) ","		\
	__stringify(default_trap_handler))

COND_TRAP(do_secure_storage_access);
COND_TRAP(do_non_secure_storage_access);
COND_TRAP(do_secure_storage_violation);
