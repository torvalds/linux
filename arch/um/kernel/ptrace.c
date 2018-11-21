/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/audit.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/tracehook.h>
#include <linux/uaccess.h>
#include <asm/ptrace-abi.h>

void user_enable_single_step(struct task_struct *child)
{
	child->ptrace |= PT_DTRACE;
	child->thread.singlestep_syscall = 0;

#ifdef SUBARCH_SET_SINGLESTEPPING
	SUBARCH_SET_SINGLESTEPPING(child, 1);
#endif
}

void user_disable_single_step(struct task_struct *child)
{
	child->ptrace &= ~PT_DTRACE;
	child->thread.singlestep_syscall = 0;

#ifdef SUBARCH_SET_SINGLESTEPPING
	SUBARCH_SET_SINGLESTEPPING(child, 0);
#endif
}

/*
 * Called by kernel/ptrace.c when detaching..
 */
void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

extern int peek_user(struct task_struct * child, long addr, long data);
extern int poke_user(struct task_struct * child, long addr, long data);

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int i, ret;
	unsigned long __user *p = (void __user *)data;
	void __user *vp = p;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR:
		ret = peek_user(child, addr, data);
		break;

	/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR:
		ret = poke_user(child, addr, data);
		break;

	case PTRACE_SYSEMU:
	case PTRACE_SYSEMU_SINGLESTEP:
		ret = -EIO;
		break;

#ifdef PTRACE_GETREGS
	case PTRACE_GETREGS: { /* Get all gp regs from the child. */
		if (!access_ok(VERIFY_WRITE, p, MAX_REG_OFFSET)) {
			ret = -EIO;
			break;
		}
		for ( i = 0; i < MAX_REG_OFFSET; i += sizeof(long) ) {
			__put_user(getreg(child, i), p);
			p++;
		}
		ret = 0;
		break;
	}
#endif
#ifdef PTRACE_SETREGS
	case PTRACE_SETREGS: { /* Set all gp regs in the child. */
		unsigned long tmp = 0;
		if (!access_ok(VERIFY_READ, p, MAX_REG_OFFSET)) {
			ret = -EIO;
			break;
		}
		for ( i = 0; i < MAX_REG_OFFSET; i += sizeof(long) ) {
			__get_user(tmp, p);
			putreg(child, i, tmp);
			p++;
		}
		ret = 0;
		break;
	}
#endif
	case PTRACE_GET_THREAD_AREA:
		ret = ptrace_get_thread_area(child, addr, vp);
		break;

	case PTRACE_SET_THREAD_AREA:
		ret = ptrace_set_thread_area(child, addr, vp);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		if (ret == -EIO)
			ret = subarch_ptrace(child, request, addr, data);
		break;
	}

	return ret;
}

static void send_sigtrap(struct task_struct *tsk, struct uml_pt_regs *regs,
		  int error_code)
{
	/* Send us the fake SIGTRAP */
	force_sig_fault(SIGTRAP, TRAP_BRKPT,
			/* User-mode eip? */
			UPT_IS_USER(regs) ? (void __user *) UPT_IP(regs) : NULL, tsk);
}

/*
 * XXX Check PT_DTRACE vs TIF_SINGLESTEP for singlestepping check and
 * PT_PTRACED vs TIF_SYSCALL_TRACE for syscall tracing check
 */
int syscall_trace_enter(struct pt_regs *regs)
{
	audit_syscall_entry(UPT_SYSCALL_NR(&regs->regs),
			    UPT_SYSCALL_ARG1(&regs->regs),
			    UPT_SYSCALL_ARG2(&regs->regs),
			    UPT_SYSCALL_ARG3(&regs->regs),
			    UPT_SYSCALL_ARG4(&regs->regs));

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return 0;

	return tracehook_report_syscall_entry(regs);
}

void syscall_trace_leave(struct pt_regs *regs)
{
	int ptraced = current->ptrace;

	audit_syscall_exit(regs);

	/* Fake a debug trap */
	if (ptraced & PT_DTRACE)
		send_sigtrap(current, &regs->regs, 0);

	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;

	tracehook_report_syscall_exit(regs, 0);
	/* force do_signal() --> is_syscall() */
	if (ptraced & PT_PTRACED)
		set_thread_flag(TIF_SIGPENDING);
}
