/* ptrace.c: FRV specific parts of process tracing
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/m68k/kernel/ptrace.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/security.h>
#include <linux/signal.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/unistd.h>

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long get_reg(struct task_struct *task, int regno)
{
	struct user_context *user = task->thread.user;

	if (regno < 0 || regno >= PT__END)
		return 0;

	return ((unsigned long *) user)[regno];
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int put_reg(struct task_struct *task, int regno,
			  unsigned long data)
{
	struct user_context *user = task->thread.user;

	if (regno < 0 || regno >= PT__END)
		return -EIO;

	switch (regno) {
	case PT_GR(0):
		return 0;
	case PT_PSR:
	case PT__STATUS:
		return -EIO;
	default:
		((unsigned long *) user)[regno] = data;
		return 0;
	}
}

/*
 * check that an address falls within the bounds of the target process's memory
 * mappings
 */
static inline int is_user_addr_valid(struct task_struct *child,
				     unsigned long start, unsigned long len)
{
#ifdef CONFIG_MMU
	if (start >= PAGE_OFFSET || len > PAGE_OFFSET - start)
		return -EIO;
	return 0;
#else
	struct vm_area_struct *vma;

	vma = find_vma(child->mm, start);
	if (vma && start >= vma->vm_start && start + len <= vma->vm_end)
		return 0;

	return -EIO;
#endif
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Control h/w single stepping
 */
void ptrace_disable(struct task_struct *child)
{
	child->thread.frame0->__status &= ~REG__STATUS_STEP;
}

void ptrace_enable(struct task_struct *child)
{
	child->thread.frame0->__status |= REG__STATUS_STEP;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	unsigned long tmp;
	int ret;

	switch (request) {
		/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = -EIO;
		if (is_user_addr_valid(child, addr, sizeof(tmp)) < 0)
			break;
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

		/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		tmp = 0;
		ret = -EIO;
		if ((addr & 3) || addr < 0)
			break;

		ret = 0;
		switch (addr >> 2) {
		case 0 ... PT__END - 1:
			tmp = get_reg(child, addr >> 2);
			break;

		case PT__END + 0:
			tmp = child->mm->end_code - child->mm->start_code;
			break;

		case PT__END + 1:
			tmp = child->mm->end_data - child->mm->start_data;
			break;

		case PT__END + 2:
			tmp = child->mm->start_stack - child->mm->start_brk;
			break;

		case PT__END + 3:
			tmp = child->mm->start_code;
			break;

		case PT__END + 4:
			tmp = child->mm->start_stack;
			break;

		default:
			ret = -EIO;
			break;
		}

		if (ret == 0)
			ret = put_user(tmp, (unsigned long *) data);
		break;
	}

		/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = -EIO;
		if (is_user_addr_valid(child, addr, sizeof(tmp)) < 0)
			break;
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret = -EIO;
		if ((addr & 3) || addr < 0)
			break;

		ret = 0;
		switch (addr >> 2) {
		case 0 ... PT__END-1:
			ret = put_reg(child, addr >> 2, data);
			break;

		default:
			ret = -EIO;
			break;
		}
		break;

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: /* restart after signal. */
		ret = -EIO;
		if (!valid_signal(data))
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

		/* make the child exit.  Best I can do is send it a sigkill.
		 * perhaps it should be put in the status that it wants to
		 * exit.
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

	case PTRACE_SINGLESTEP:  /* set the trap flag. */
		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		ptrace_enable(child);
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;

	case PTRACE_DETACH:	/* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	case PTRACE_GETREGS: { /* Get all integer regs from the child. */
		int i;
		for (i = 0; i < PT__GPEND; i++) {
			tmp = get_reg(child, i);
			if (put_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			}
			data += sizeof(long);
		}
		ret = 0;
		break;
	}

	case PTRACE_SETREGS: { /* Set all integer regs in the child. */
		int i;
		for (i = 0; i < PT__GPEND; i++) {
			if (get_user(tmp, (unsigned long *) data)) {
				ret = -EFAULT;
				break;
			}
			put_reg(child, i, tmp);
			data += sizeof(long);
		}
		ret = 0;
		break;
	}

	case PTRACE_GETFPREGS: { /* Get the child FP/Media state. */
		ret = 0;
		if (copy_to_user((void *) data,
				 &child->thread.user->f,
				 sizeof(child->thread.user->f)))
			ret = -EFAULT;
		break;
	}

	case PTRACE_SETFPREGS: { /* Set the child FP/Media state. */
		ret = 0;
		if (copy_from_user(&child->thread.user->f,
				   (void *) data,
				   sizeof(child->thread.user->f)))
			ret = -EFAULT;
		break;
	}

	case PTRACE_GETFDPIC:
		tmp = 0;
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

	default:
		ret = -EIO;
		break;
	}
	return ret;
}

asmlinkage void do_syscall_trace(int leaving)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;

	if (!(current->ptrace & PT_PTRACED))
		return;

	/* we need to indicate entry or exit to strace */
	if (leaving)
		__frame->__status |= REG__STATUS_SYSC_EXIT;
	else
		__frame->__status |= REG__STATUS_SYSC_ENTRY;

	ptrace_notify(SIGTRAP);

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
