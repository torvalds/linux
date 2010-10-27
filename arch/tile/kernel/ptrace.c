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
 * This routine will put a word on the process's privileged stack.
 */
static void putreg(struct task_struct *task,
		   unsigned long addr, unsigned long value)
{
	unsigned int regno = addr / sizeof(unsigned long);
	struct pt_regs *childregs = task_pt_regs(task);
	childregs->regs[regno] = value;
	childregs->flags |= PT_FLAGS_RESTORE_REGS;
}

static unsigned long getreg(struct task_struct *task, unsigned long addr)
{
	unsigned int regno = addr / sizeof(unsigned long);
	struct pt_regs *childregs = task_pt_regs(task);
	return childregs->regs[regno];
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

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	unsigned long __user *datap;
	unsigned long tmp;
	int i;
	long ret = -EIO;

#ifdef CONFIG_COMPAT
	if (task_thread_info(current)->status & TS_COMPAT)
		data = (u32)data;
	if (task_thread_info(child)->status & TS_COMPAT)
		addr = (u32)addr;
#endif
	datap = (unsigned long __user __force *)data;

	switch (request) {

	case PTRACE_PEEKUSR:  /* Read register from pt_regs. */
		if (addr & (sizeof(data)-1))
			break;
		if (addr < 0 || addr >= PTREGS_SIZE)
			break;
		tmp = getreg(child, addr);   /* Read register */
		ret = put_user(tmp, datap);
		break;

	case PTRACE_POKEUSR:  /* Write register in pt_regs. */
		if (addr & (sizeof(data)-1))
			break;
		if (addr < 0 || addr >= PTREGS_SIZE)
			break;
		putreg(child, addr, data);   /* Write register */
		ret = 0;
		break;

	case PTRACE_GETREGS:  /* Get all registers from the child. */
		if (!access_ok(VERIFY_WRITE, datap, PTREGS_SIZE))
			break;
		for (i = 0; i < PTREGS_SIZE; i += sizeof(long)) {
			ret = __put_user(getreg(child, i), datap);
			if (ret != 0)
				break;
			datap++;
		}
		break;

	case PTRACE_SETREGS:  /* Set all registers in the child. */
		if (!access_ok(VERIFY_READ, datap, PTREGS_SIZE))
			break;
		for (i = 0; i < PTREGS_SIZE; i += sizeof(long)) {
			ret = __get_user(tmp, datap);
			if (ret != 0)
				break;
			putreg(child, i, tmp);
			datap++;
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
