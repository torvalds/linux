/*
 * Traps/Non-MMU Exception handling for ARC
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: May 2011
 *  -user-space unaligned access emulation
 *
 * Rahul Trivedi: Codito Technologies 2004
 */

#include <linux/sched.h>
#include <linux/kdebug.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <linux/kprobes.h>
#include <linux/kgdb.h>
#include <asm/setup.h>
#include <asm/unaligned.h>
#include <asm/kprobes.h>

void __init trap_init(void)
{
	return;
}

void die(const char *str, struct pt_regs *regs, unsigned long address)
{
	show_kernel_fault_diag(str, regs, address);

	/* DEAD END */
	__asm__("flag 1");
}

/*
 * Helper called for bulk of exceptions NOT needing specific handling
 *  -for user faults enqueues requested signal
 *  -for kernel, chk if due to copy_(to|from)_user, otherwise die()
 */
static noinline int
unhandled_exception(const char *str, struct pt_regs *regs, siginfo_t *info)
{
	if (user_mode(regs)) {
		struct task_struct *tsk = current;

		tsk->thread.fault_address = (__force unsigned int)info->si_addr;

		force_sig_info(info->si_signo, info, tsk);

	} else {
		/* If not due to copy_(to|from)_user, we are doomed */
		if (fixup_exception(regs))
			return 0;

		die(str, regs, (unsigned long)info->si_addr);
	}

	return 1;
}

#define DO_ERROR_INFO(signr, str, name, sicode) \
int name(unsigned long address, struct pt_regs *regs) \
{						\
	siginfo_t info = {			\
		.si_signo = signr,		\
		.si_errno = 0,			\
		.si_code  = sicode,		\
		.si_addr = (void __user *)address,	\
	};					\
	return unhandled_exception(str, regs, &info);\
}

/*
 * Entry points for exceptions NOT needing specific handling
 */
DO_ERROR_INFO(SIGILL, "Priv Op/Disabled Extn", do_privilege_fault, ILL_PRVOPC)
DO_ERROR_INFO(SIGILL, "Invalid Extn Insn", do_extension_fault, ILL_ILLOPC)
DO_ERROR_INFO(SIGILL, "Illegal Insn (or Seq)", insterror_is_error, ILL_ILLOPC)
DO_ERROR_INFO(SIGBUS, "Invalid Mem Access", do_memory_error, BUS_ADRERR)
DO_ERROR_INFO(SIGTRAP, "Breakpoint Set", trap_is_brkpt, TRAP_BRKPT)
DO_ERROR_INFO(SIGBUS, "Misaligned Access", do_misaligned_error, BUS_ADRALN)

/*
 * Entry Point for Misaligned Data access Exception, for emulating in software
 */
int do_misaligned_access(unsigned long address, struct pt_regs *regs,
			 struct callee_regs *cregs)
{
	/* If emulation not enabled, or failed, kill the task */
	if (misaligned_fixup(address, regs, cregs) != 0)
		return do_misaligned_error(address, regs);

	return 0;
}

/*
 * Entry point for miscll errors such as Nested Exceptions
 *  -Duplicate TLB entry is handled seperately though
 */
void do_machine_check_fault(unsigned long address, struct pt_regs *regs)
{
	die("Machine Check Exception", regs, address);
}


/*
 * Entry point for traps induced by ARCompact TRAP_S <n> insn
 * This is same family as TRAP0/SWI insn (use the same vector).
 * The only difference being SWI insn take no operand, while TRAP_S does
 * which reflects in ECR Reg as 8 bit param.
 * Thus TRAP_S <n> can be used for specific purpose
 *  -1 used for software breakpointing (gdb)
 *  -2 used by kprobes
 */
void do_non_swi_trap(unsigned long address, struct pt_regs *regs)
{
	unsigned int param = regs->ecr_param;

	switch (param) {
	case 1:
		trap_is_brkpt(address, regs);
		break;

	case 2:
		trap_is_kprobe(address, regs);
		break;

	case 3:
	case 4:
		kgdb_trap(regs);
		break;

	default:
		break;
	}
}

/*
 * Entry point for Instruction Error Exception
 *  -For a corner case, ARC kprobes implementation resorts to using
 *   this exception, hence the check
 */
void do_insterror_or_kprobe(unsigned long address, struct pt_regs *regs)
{
	int rc;

	/* Check if this exception is caused by kprobes */
	rc = notify_die(DIE_IERR, "kprobe_ierr", regs, address, 0, SIGILL);
	if (rc == NOTIFY_STOP)
		return;

	insterror_is_error(address, regs);
}
