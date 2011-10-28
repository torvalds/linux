/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Copied from i386: Ross Biro 1/23/92
 */

#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/kprobes.h>
#include <linux/compat.h>
#include <linux/uaccess.h>
#include <asm/traps.h>

void user_enable_single_step(struct task_struct *child)
{
	set_tsk_thread_flag(child, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}

/*
 * Called by kernel/ptrace.c when detaching..
 */
void ptrace_disable(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);

	/*
	 * These two are currently unused, but will be set by arch_ptrace()
	 * and used in the syscall assembly when we do support them.
	 */
	clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	unsigned long __user *datap = (long __user __force *)data;
	unsigned long tmp;
	long ret = -EIO;
	char *childreg;
	struct pt_regs copyregs;
	int ex1_offset;

	switch (request) {

	case PTRACE_PEEKUSR:  /* Read register from pt_regs. */
		if (addr >= PTREGS_SIZE)
			break;
		childreg = (char *)task_pt_regs(child) + addr;
#ifdef CONFIG_COMPAT
		if (is_compat_task()) {
			if (addr & (sizeof(compat_long_t)-1))
				break;
			ret = put_user(*(compat_long_t *)childreg,
				       (compat_long_t __user *)datap);
		} else
#endif
		{
			if (addr & (sizeof(long)-1))
				break;
			ret = put_user(*(long *)childreg, datap);
		}
		break;

	case PTRACE_POKEUSR:  /* Write register in pt_regs. */
		if (addr >= PTREGS_SIZE)
			break;
		childreg = (char *)task_pt_regs(child) + addr;

		/* Guard against overwrites of the privilege level. */
		ex1_offset = PTREGS_OFFSET_EX1;
#if defined(CONFIG_COMPAT) && defined(__BIG_ENDIAN)
		if (is_compat_task())   /* point at low word */
			ex1_offset += sizeof(compat_long_t);
#endif
		if (addr == ex1_offset)
			data = PL_ICS_EX1(USER_PL, EX1_ICS(data));

#ifdef CONFIG_COMPAT
		if (is_compat_task()) {
			if (addr & (sizeof(compat_long_t)-1))
				break;
			*(compat_long_t *)childreg = data;
		} else
#endif
		{
			if (addr & (sizeof(long)-1))
				break;
			*(long *)childreg = data;
		}
		ret = 0;
		break;

	case PTRACE_GETREGS:  /* Get all registers from the child. */
		if (copy_to_user(datap, task_pt_regs(child),
				 sizeof(struct pt_regs)) == 0) {
			ret = 0;
		}
		break;

	case PTRACE_SETREGS:  /* Set all registers in the child. */
		if (copy_from_user(&copyregs, datap,
				   sizeof(struct pt_regs)) == 0) {
			copyregs.ex1 =
				PL_ICS_EX1(USER_PL, EX1_ICS(copyregs.ex1));
			*task_pt_regs(child) = copyregs;
			ret = 0;
		}
		break;

	case PTRACE_GETFPREGS:  /* Get the child FPU state. */
	case PTRACE_SETFPREGS:  /* Set the child FPU state. */
		break;

	case PTRACE_SETOPTIONS:
		/* Support TILE-specific ptrace options. */
		child->ptrace &= ~PT_TRACE_MASK_TILE;
		tmp = data & PTRACE_O_MASK_TILE;
		data &= ~PTRACE_O_MASK_TILE;
		ret = ptrace_request(child, request, addr, data);
		if (tmp & PTRACE_O_TRACEMIGRATE)
			child->ptrace |= PT_TRACE_MIGRATE;
		break;

	default:
#ifdef CONFIG_COMPAT
		if (task_thread_info(current)->status & TS_COMPAT) {
			ret = compat_ptrace_request(child, request,
						    addr, data);
			break;
		}
#endif
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
/* Not used; we handle compat issues in arch_ptrace() directly. */
long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			       compat_ulong_t addr, compat_ulong_t data)
{
	BUG();
}
#endif

void do_syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;

	if (!(current->ptrace & PT_PTRACED))
		return;

	/*
	 * The 0x80 provides a way for the tracing parent to distinguish
	 * between a syscall stop and SIGTRAP delivery
	 */
	ptrace_notify(SIGTRAP|((current->ptrace & PT_TRACESYSGOOD) ? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs, int error_code)
{
	struct siginfo info;

	memset(&info, 0, sizeof(info));
	info.si_signo = SIGTRAP;
	info.si_code  = TRAP_BRKPT;
	info.si_addr  = (void __user *) regs->pc;

	/* Send us the fakey SIGTRAP */
	force_sig_info(SIGTRAP, &info, tsk);
}

/* Handle synthetic interrupt delivered only by the simulator. */
void __kprobes do_breakpoint(struct pt_regs* regs, int fault_num)
{
	send_sigtrap(current, regs, fault_num);
}
