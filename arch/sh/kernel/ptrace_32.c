/*
 * linux/arch/sh/kernel/ptrace.c
 *
 * Original x86 implementation:
 *	By Ross Biro 1/23/92
 *	edited by Linus Torvalds
 *
 * SuperH version:   Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 * Audit support: Yuichi Nakamura <ynakam@hitachisoft.jp>
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/io.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * This routine will get a word off of the process kernel stack.
 */
static inline int get_stack_long(struct task_struct *task, int offset)
{
	unsigned char *stack;

	stack = (unsigned char *)task_pt_regs(task);
	stack += offset;
	return (*((int *)stack));
}

/*
 * This routine will put a word on the process kernel stack.
 */
static inline int put_stack_long(struct task_struct *task, int offset,
				 unsigned long data)
{
	unsigned char *stack;

	stack = (unsigned char *)task_pt_regs(task);
	stack += offset;
	*(unsigned long *) stack = data;
	return 0;
}

void user_enable_single_step(struct task_struct *child)
{
	struct pt_regs *regs = task_pt_regs(child);
	long pc;

	pc = get_stack_long(child, (long)&regs->pc);

	/* Next scheduling will set up UBC */
	if (child->thread.ubc_pc == 0)
		ubc_usercnt += 1;

	child->thread.ubc_pc = pc;

	set_tsk_thread_flag(child, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);

	/*
	 * Ensure the UBC is not programmed at the next context switch.
	 *
	 * Normally this is not needed but there are sequences such as
	 * singlestep, signal delivery, and continue that leave the
	 * ubc_pc non-zero leading to spurious SIGTRAPs.
	 */
	if (child->thread.ubc_pc != 0) {
		ubc_usercnt -= 1;
		child->thread.ubc_pc = 0;
	}
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	struct user * dummy = NULL;
	int ret;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & 3) || addr < 0 ||
		    addr > sizeof(struct user) - 3)
			break;

		if (addr < sizeof(struct pt_regs))
			tmp = get_stack_long(child, addr);
		else if (addr >= (long) &dummy->fpu &&
			 addr < (long) &dummy->u_fpvalid) {
			if (!tsk_used_math(child)) {
				if (addr == (long)&dummy->fpu.fpscr)
					tmp = FPSCR_INIT;
				else
					tmp = 0;
			} else
				tmp = ((long *)&child->thread.fpu)
					[(addr - (long)&dummy->fpu) >> 2];
		} else if (addr == (long) &dummy->u_fpvalid)
			tmp = !!tsk_used_math(child);
		else
			tmp = 0;
		ret = put_user(tmp, (unsigned long __user *)data);
		break;
	}

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret = -EIO;
		if ((addr & 3) || addr < 0 ||
		    addr > sizeof(struct user) - 3)
			break;

		if (addr < sizeof(struct pt_regs))
			ret = put_stack_long(child, addr, data);
		else if (addr >= (long) &dummy->fpu &&
			 addr < (long) &dummy->u_fpvalid) {
			set_stopped_child_used_math(child);
			((long *)&child->thread.fpu)
				[(addr - (long)&dummy->fpu) >> 2] = data;
			ret = 0;
		} else if (addr == (long) &dummy->u_fpvalid) {
			conditional_stopped_child_used_math(data, child);
			ret = 0;
		}
		break;

#ifdef CONFIG_SH_DSP
	case PTRACE_GETDSPREGS: {
		unsigned long dp;

		ret = -EIO;
		dp = ((unsigned long) child) + THREAD_SIZE -
			 sizeof(struct pt_dspregs);
		if (*((int *) (dp - 4)) == SR_FD) {
			copy_to_user((void *)addr, (void *) dp,
				sizeof(struct pt_dspregs));
			ret = 0;
		}
		break;
	}

	case PTRACE_SETDSPREGS: {
		unsigned long dp;

		ret = -EIO;
		dp = ((unsigned long) child) + THREAD_SIZE -
			 sizeof(struct pt_dspregs);
		if (*((int *) (dp - 4)) == SR_FD) {
			copy_from_user((void *) dp, (void *)addr,
				sizeof(struct pt_dspregs));
			ret = 0;
		}
		break;
	}
#endif
#ifdef CONFIG_BINFMT_ELF_FDPIC
	case PTRACE_GETFDPIC: {
		unsigned long tmp = 0;

		switch (addr) {
		case PTRACE_GETFDPIC_EXEC:
			tmp = child->mm->context.exec_fdpic_loadmap;
			break;
		case PTRACE_GETFDPIC_INTERP:
			tmp = child->mm->context.interp_fdpic_loadmap;
			break;
		default:
			break;
		}

		ret = 0;
		if (put_user(tmp, (unsigned long *) data)) {
			ret = -EFAULT;
			break;
		}
		break;
	}
#endif
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage void do_syscall_trace(struct pt_regs *regs, int entryexit)
{
	struct task_struct *tsk = current;

	secure_computing(regs->regs[0]);

	if (unlikely(current->audit_context) && entryexit)
		audit_syscall_exit(AUDITSC_RESULT(regs->regs[0]),
				   regs->regs[0]);

	if (!test_thread_flag(TIF_SYSCALL_TRACE) &&
	    !test_thread_flag(TIF_SINGLESTEP))
		goto out;
	if (!(tsk->ptrace & PT_PTRACED))
		goto out;

	/* the 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD) &&
				 !test_thread_flag(TIF_SINGLESTEP) ? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (tsk->exit_code) {
		send_sig(tsk->exit_code, tsk, 1);
		tsk->exit_code = 0;
	}

out:
	if (unlikely(current->audit_context) && !entryexit)
		audit_syscall_entry(AUDIT_ARCH_SH, regs->regs[3],
				    regs->regs[4], regs->regs[5],
				    regs->regs[6], regs->regs[7]);

}
