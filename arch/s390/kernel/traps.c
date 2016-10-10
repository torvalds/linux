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
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/extable.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/fpu/api.h>
#include "entry.h"

static inline void __user *get_trap_ip(struct pt_regs *regs)
{
	unsigned long address;

	if (regs->int_code & 0x200)
		address = *(unsigned long *)(current->thread.trap_tdb + 24);
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
	siginfo_t info;

	if (user_mode(regs)) {
		info.si_signo = si_signo;
		info.si_errno = 0;
		info.si_code = si_code;
		info.si_addr = get_trap_ip(regs);
		force_sig_info(si_signo, &info, current);
		report_user_fault(regs, si_signo, 0);
        } else {
                const struct exception_table_entry *fixup;
		fixup = search_exception_tables(regs->psw.addr);
                if (fixup)
			regs->psw.addr = extable_fixup(fixup);
		else {
			enum bug_trap_type btt;

			btt = report_bug(regs->psw.addr, regs);
			if (btt == BUG_TRAP_TYPE_WARN)
				return;
			die(regs, str);
		}
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
	siginfo_t info;

	if (notify_die(DIE_SSTEP, "sstep", regs, 0, 0, SIGTRAP) == NOTIFY_STOP)
		return;
	if (!current->ptrace)
		return;
	info.si_signo = SIGTRAP;
	info.si_errno = 0;
	info.si_code = TRAP_HWBKPT;
	info.si_addr =
		(void __force __user *) current->thread.per_event.address;
	force_sig_info(SIGTRAP, &info, current);
}
NOKPROBE_SYMBOL(do_per_trap);

void default_trap_handler(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		report_user_fault(regs, SIGSEGV, 0);
		do_exit(SIGSEGV);
	} else
		die(regs, "Unknown program exception");
}

#define DO_ERROR_INFO(name, signr, sicode, str) \
void name(struct pt_regs *regs)			\
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

void translation_exception(struct pt_regs *regs)
{
	/* May never happen. */
	panic("Translation exception");
}

void illegal_op(struct pt_regs *regs)
{
	siginfo_t info;
        __u8 opcode[6];
	__u16 __user *location;
	int is_uprobe_insn = 0;
	int signal = 0;

	location = get_trap_ip(regs);

	if (user_mode(regs)) {
		if (get_user(*((__u16 *) opcode), (__u16 __user *) location))
			return;
		if (*((__u16 *) opcode) == S390_BREAKPOINT_U16) {
			if (current->ptrace) {
				info.si_signo = SIGTRAP;
				info.si_errno = 0;
				info.si_code = TRAP_BRKPT;
				info.si_addr = location;
				force_sig_info(SIGTRAP, &info, current);
			} else
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

void vector_exception(struct pt_regs *regs)
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

void data_exception(struct pt_regs *regs)
{
	int signal = 0;

	save_fpu_regs();
	if (current->thread.fpu.fpc & FPC_DXC_MASK)
		signal = SIGFPE;
	else
		signal = SIGILL;
	if (signal == SIGFPE)
		do_fp_trap(regs, current->thread.fpu.fpc);
	else if (signal)
		do_trap(regs, signal, ILL_ILLOPN, "data exception");
}

void space_switch_exception(struct pt_regs *regs)
{
	/* Set user psw back to home space mode. */
	if (user_mode(regs))
		regs->psw.mask |= PSW_ASC_HOME;
	/* Send SIGILL. */
	do_trap(regs, SIGILL, ILL_PRVOPC, "space switch event");
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

void __init trap_init(void)
{
	local_mcck_enable();
}
