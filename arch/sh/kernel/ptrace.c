/*
 * linux/arch/sh/kernel/ptrace.c
 *
 * Original x86 implementation:
 *	By Ross Biro 1/23/92
 *	edited by Linus Torvalds
 *
 * SuperH version:   Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 *
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

static void ptrace_disable_singlestep(struct task_struct *child)
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
	ptrace_disable_singlestep(child);
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	struct user * dummy = NULL;
	int ret;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);

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

	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
			break;
		ret = -EIO;
		break;

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

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);

		ptrace_disable_singlestep(child);

		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill.
 * perhaps it should be put in the status that it wants to
 * exit.
 */
	case PTRACE_KILL: {
		ret = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		ptrace_disable_singlestep(child);
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;
	}

	case PTRACE_SINGLESTEP: {  /* set the trap flag. */
		long pc;
		struct pt_regs *regs = NULL;

		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		if ((child->ptrace & PT_DTRACE) == 0) {
			/* Spurious delayed TF traps may occur */
			child->ptrace |= PT_DTRACE;
		}

		pc = get_stack_long(child, (long)&regs->pc);

		/* Next scheduling will set up UBC */
		if (child->thread.ubc_pc == 0)
			ubc_usercnt += 1;
		child->thread.ubc_pc = pc;

		set_tsk_thread_flag(child, TIF_SINGLESTEP);
		child->exit_code = data;
		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		break;
	}

	case PTRACE_DETACH: /* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

#ifdef CONFIG_SH_DSP
	case PTRACE_GETDSPREGS: {
		unsigned long dp;

		ret = -EIO;
		dp = ((unsigned long) child) + THREAD_SIZE -
			 sizeof(struct pt_dspregs);
		if (*((int *) (dp - 4)) == SR_FD) {
			copy_to_user(addr, (void *) dp,
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
			copy_from_user((void *) dp, addr,
				sizeof(struct pt_dspregs));
			ret = 0;
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

asmlinkage void do_syscall_trace(void)
{
	struct task_struct *tsk = current;

	if (!test_thread_flag(TIF_SYSCALL_TRACE) &&
	    !test_thread_flag(TIF_SINGLESTEP))
		return;
	if (!(tsk->ptrace & PT_PTRACED))
		return;
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
}
