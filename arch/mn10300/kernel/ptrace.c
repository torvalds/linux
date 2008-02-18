/* MN10300 Process tracing
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Modified by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/fpu.h>
#include <asm/asm-offsets.h>

/*
 * translate ptrace register IDs into struct pt_regs offsets
 */
static const u8 ptrace_regid_to_frame[] = {
	[PT_A3 << 2]		= REG_A3,
	[PT_A2 << 2]		= REG_A2,
	[PT_D3 << 2]		= REG_D3,
	[PT_D2 << 2]		= REG_D2,
	[PT_MCVF << 2]		= REG_MCVF,
	[PT_MCRL << 2]		= REG_MCRL,
	[PT_MCRH << 2]		= REG_MCRH,
	[PT_MDRQ << 2]		= REG_MDRQ,
	[PT_E1 << 2]		= REG_E1,
	[PT_E0 << 2]		= REG_E0,
	[PT_E7 << 2]		= REG_E7,
	[PT_E6 << 2]		= REG_E6,
	[PT_E5 << 2]		= REG_E5,
	[PT_E4 << 2]		= REG_E4,
	[PT_E3 << 2]		= REG_E3,
	[PT_E2 << 2]		= REG_E2,
	[PT_SP << 2]		= REG_SP,
	[PT_LAR << 2]		= REG_LAR,
	[PT_LIR << 2]		= REG_LIR,
	[PT_MDR << 2]		= REG_MDR,
	[PT_A1 << 2]		= REG_A1,
	[PT_A0 << 2]		= REG_A0,
	[PT_D1 << 2]		= REG_D1,
	[PT_D0 << 2]		= REG_D0,
	[PT_ORIG_D0 << 2]	= REG_ORIG_D0,
	[PT_EPSW << 2]		= REG_EPSW,
	[PT_PC << 2]		= REG_PC,
};

static inline int get_stack_long(struct task_struct *task, int offset)
{
	return *(unsigned long *)
		((unsigned long) task->thread.uregs + offset);
}

/*
 * this routine will put a word on the processes privileged stack.
 * the offset is how far from the base addr as stored in the TSS.
 * this routine assumes that all the privileged stacks are in our
 * data space.
 */
static inline
int put_stack_long(struct task_struct *task, int offset, unsigned long data)
{
	unsigned long stack;

	stack = (unsigned long) task->thread.uregs + offset;
	*(unsigned long *) stack = data;
	return 0;
}

static inline unsigned long get_fpregs(struct fpu_state_struct *buf,
				       struct task_struct *tsk)
{
	return __copy_to_user(buf, &tsk->thread.fpu_state,
			      sizeof(struct fpu_state_struct));
}

static inline unsigned long set_fpregs(struct task_struct *tsk,
				       struct fpu_state_struct *buf)
{
	return __copy_from_user(&tsk->thread.fpu_state, buf,
				sizeof(struct fpu_state_struct));
}

static inline void fpsave_init(struct task_struct *task)
{
	memset(&task->thread.fpu_state, 0, sizeof(struct fpu_state_struct));
}

/*
 * make sure the single step bit is not set
 */
void ptrace_disable(struct task_struct *child)
{
#ifndef CONFIG_MN10300_USING_JTAG
	struct user *dummy = NULL;
	long tmp;

	tmp = get_stack_long(child, (unsigned long) &dummy->regs.epsw);
	tmp &= ~EPSW_T;
	put_stack_long(child, (unsigned long) &dummy->regs.epsw, tmp);
#endif
}

/*
 * set the single step bit
 */
void ptrace_enable(struct task_struct *child)
{
#ifndef CONFIG_MN10300_USING_JTAG
	struct user *dummy = NULL;
	long tmp;

	tmp = get_stack_long(child, (unsigned long) &dummy->regs.epsw);
	tmp |= EPSW_T;
	put_stack_long(child, (unsigned long) &dummy->regs.epsw, tmp);
#endif
}

/*
 * handle the arch-specific side of process tracing
 */
long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	struct fpu_state_struct fpu_state;
	int i, ret;

	switch (request) {
	/* read the word at location addr. */
	case PTRACE_PEEKTEXT: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp, (unsigned long *) data);
		break;
	}

	/* read the word at location addr. */
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp, (unsigned long *) data);
		break;
	}

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long tmp;

		ret = -EIO;
		if ((addr & 3) || addr < 0 ||
		    addr > sizeof(struct user) - 3)
			break;

		tmp = 0;  /* Default return condition */
		if (addr < NR_PTREGS << 2)
			tmp = get_stack_long(child,
					     ptrace_regid_to_frame[addr]);
		ret = put_user(tmp, (unsigned long *) data);
		break;
	}

	/* write the word at location addr. */
	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
		if (access_process_vm(child, addr, &data, sizeof(data), 1) ==
		    sizeof(data))
			ret = 0;
		else
			ret = -EIO;
		break;

		/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR:
		ret = -EIO;
		if ((addr & 3) || addr < 0 ||
		    addr > sizeof(struct user) - 3)
			break;

		ret = 0;
		if (addr < NR_PTREGS << 2)
			ret = put_stack_long(child, ptrace_regid_to_frame[addr],
					     data);
		break;

		/* continue and stop at next (return from) syscall */
	case PTRACE_SYSCALL:
		/* restart after signal. */
	case PTRACE_CONT:
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		ptrace_disable(child);
		wake_up_process(child);
		ret = 0;
		break;

		/*
		 * make the child exit
		 * - the best I can do is send it a sigkill
		 * - perhaps it should be put in the status that it wants to
		 *   exit
		 */
	case PTRACE_KILL:
		ret = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		clear_tsk_thread_flag(child, TIF_SINGLESTEP);
		ptrace_disable(child);
		wake_up_process(child);
		break;

	case PTRACE_SINGLESTEP: /* set the trap flag. */
#ifndef CONFIG_MN10300_USING_JTAG
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		ptrace_enable(child);
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
#else
		ret = -EINVAL;
#endif
		break;

	case PTRACE_DETACH:	/* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

		/* Get all gp regs from the child. */
	case PTRACE_GETREGS: {
		unsigned long tmp;

		if (!access_ok(VERIFY_WRITE, (unsigned *) data, NR_PTREGS << 2)) {
			ret = -EIO;
			break;
		}

		for (i = 0; i < NR_PTREGS << 2; i += 4) {
			tmp = get_stack_long(child, ptrace_regid_to_frame[i]);
			__put_user(tmp, (unsigned long *) data);
			data += sizeof(tmp);
		}
		ret = 0;
		break;
	}

	case PTRACE_SETREGS: { /* Set all gp regs in the child. */
		unsigned long tmp;

		if (!access_ok(VERIFY_READ, (unsigned long *)data,
			       sizeof(struct pt_regs))) {
			ret = -EIO;
			break;
		}

		for (i = 0; i < NR_PTREGS << 2; i += 4) {
			__get_user(tmp, (unsigned long *) data);
			put_stack_long(child, ptrace_regid_to_frame[i], tmp);
			data += sizeof(tmp);
		}
		ret = 0;
		break;
	}

	case PTRACE_GETFPREGS: { /* Get the child FPU state. */
		if (is_using_fpu(child)) {
			unlazy_fpu(child);
			fpu_state = child->thread.fpu_state;
		} else {
			memset(&fpu_state, 0, sizeof(fpu_state));
		}

		ret = -EIO;
		if (copy_to_user((void *) data, &fpu_state,
				 sizeof(fpu_state)) == 0)
			ret = 0;
		break;
	}

	case PTRACE_SETFPREGS: { /* Set the child FPU state. */
		ret = -EFAULT;
		if (copy_from_user(&fpu_state, (const void *) data,
				   sizeof(fpu_state)) == 0) {
			fpu_kill_state(child);
			child->thread.fpu_state = fpu_state;
			set_using_fpu(child);
			ret = 0;
		}
		break;
	}

	case PTRACE_SETOPTIONS: {
		if (data & PTRACE_O_TRACESYSGOOD)
			child->ptrace |= PT_TRACESYSGOOD;
		else
			child->ptrace &= ~PT_TRACESYSGOOD;
		ret = 0;
		break;
	}

	default:
		ret = -EIO;
		break;
	}

	return ret;
}

/*
 * notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
asmlinkage void do_syscall_trace(struct pt_regs *regs, int entryexit)
{
#if 0
	/* just in case... */
	printk(KERN_DEBUG "[%d] syscall_%lu(%lx,%lx,%lx,%lx) = %lx\n",
	       current->pid,
	       regs->orig_d0,
	       regs->a0,
	       regs->d1,
	       regs->a3,
	       regs->a2,
	       regs->d0);
	return;
#endif

	if (!test_thread_flag(TIF_SYSCALL_TRACE) &&
	    !test_thread_flag(TIF_SINGLESTEP))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;

	/* the 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP |
		      ((current->ptrace & PT_TRACESYSGOOD) &&
		       !test_thread_flag(TIF_SINGLESTEP) ? 0x80 : 0));

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
