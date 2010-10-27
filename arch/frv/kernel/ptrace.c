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
#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/tracehook.h>

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
 * retrieve the contents of FRV userspace general registers
 */
static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct user_int_regs *iregs = &target->thread.user->i;
	int ret;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  iregs, 0, sizeof(*iregs));
	if (ret < 0)
		return ret;

	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					sizeof(*iregs), -1);
}

/*
 * update the contents of the FRV userspace general registers
 */
static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct user_int_regs *iregs = &target->thread.user->i;
	unsigned int offs_gr0, offs_gr1;
	int ret;

	/* not allowed to set PSR or __status */
	if (pos < offsetof(struct user_int_regs, psr) + sizeof(long) &&
	    pos + count > offsetof(struct user_int_regs, psr))
		return -EIO;

	if (pos < offsetof(struct user_int_regs, __status) + sizeof(long) &&
	    pos + count > offsetof(struct user_int_regs, __status))
		return -EIO;

	/* set the control regs */
	offs_gr0 = offsetof(struct user_int_regs, gr[0]);
	offs_gr1 = offsetof(struct user_int_regs, gr[1]);
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 iregs, 0, offs_gr0);
	if (ret < 0)
		return ret;

	/* skip GR0/TBR */
	ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					offs_gr0, offs_gr1);
	if (ret < 0)
		return ret;

	/* set the general regs */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &iregs->gr[1], offs_gr1, sizeof(*iregs));
	if (ret < 0)
		return ret;

	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					sizeof(*iregs), -1);
}

/*
 * retrieve the contents of FRV userspace FP/Media registers
 */
static int fpmregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct user_fpmedia_regs *fpregs = &target->thread.user->f;
	int ret;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  fpregs, 0, sizeof(*fpregs));
	if (ret < 0)
		return ret;

	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					sizeof(*fpregs), -1);
}

/*
 * update the contents of the FRV userspace FP/Media registers
 */
static int fpmregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct user_fpmedia_regs *fpregs = &target->thread.user->f;
	int ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 fpregs, 0, sizeof(*fpregs));
	if (ret < 0)
		return ret;

	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					sizeof(*fpregs), -1);
}

/*
 * determine if the FP/Media registers have actually been used
 */
static int fpmregs_active(struct task_struct *target,
			  const struct user_regset *regset)
{
	return tsk_used_math(target) ? regset->n : 0;
}

/*
 * Define the register sets available on the FRV under Linux
 */
enum frv_regset {
	REGSET_GENERAL,
	REGSET_FPMEDIA,
};

static const struct user_regset frv_regsets[] = {
	/*
	 * General register format is:
	 *	PSR, ISR, CCR, CCCR, LR, LCR, PC, (STATUS), SYSCALLNO, ORIG_G8
	 *	GNER0-1, IACC0, TBR, GR1-63
	 */
	[REGSET_GENERAL] = {
		.core_note_type	= NT_PRSTATUS,
		.n		= ELF_NGREG,
		.size		= sizeof(long),
		.align		= sizeof(long),
		.get		= genregs_get,
		.set		= genregs_set,
	},
	/*
	 * FPU/Media register format is:
	 *	FR0-63, FNER0-1, MSR0-1, ACC0-7, ACCG0-8, FSR
	 */
	[REGSET_FPMEDIA] = {
		.core_note_type	= NT_PRFPREG,
		.n		= sizeof(struct user_fpmedia_regs) / sizeof(long),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.get		= fpmregs_get,
		.set		= fpmregs_set,
		.active		= fpmregs_active,
	},
};

static const struct user_regset_view user_frv_native_view = {
	.name		= "frv",
	.e_machine	= EM_FRV,
	.regsets	= frv_regsets,
	.n		= ARRAY_SIZE(frv_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_frv_native_view;
}

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
 * Called by kernel/ptrace.c when detaching..
 *
 * Control h/w single stepping
 */
void user_enable_single_step(struct task_struct *child)
{
	child->thread.frame0->__status |= REG__STATUS_STEP;
}

void user_disable_single_step(struct task_struct *child)
{
	child->thread.frame0->__status &= ~REG__STATUS_STEP;
}

void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	unsigned long tmp;
	int ret;
	int regno = addr >> 2;
	unsigned long __user *datap = (unsigned long __user *) data;

	switch (request) {
		/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		tmp = 0;
		ret = -EIO;
		if (addr & 3)
			break;

		ret = 0;
		switch (regno) {
		case 0 ... PT__END - 1:
			tmp = get_reg(child, regno);
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
			ret = put_user(tmp, datap);
		break;
	}

	case PTRACE_POKEUSR: /* write the word at location addr in the USER area */
		ret = -EIO;
		if (addr & 3)
			break;

		switch (regno) {
		case 0 ... PT__END - 1:
			ret = put_reg(child, regno, data);
			break;
		}
		break;

	case PTRACE_GETREGS:	/* Get all integer regs from the child. */
		return copy_regset_to_user(child, &user_frv_native_view,
					   REGSET_GENERAL,
					   0, sizeof(child->thread.user->i),
					   datap);

	case PTRACE_SETREGS:	/* Set all integer regs in the child. */
		return copy_regset_from_user(child, &user_frv_native_view,
					     REGSET_GENERAL,
					     0, sizeof(child->thread.user->i),
					     datap);

	case PTRACE_GETFPREGS:	/* Get the child FP/Media state. */
		return copy_regset_to_user(child, &user_frv_native_view,
					   REGSET_FPMEDIA,
					   0, sizeof(child->thread.user->f),
					   datap);

	case PTRACE_SETFPREGS:	/* Set the child FP/Media state. */
		return copy_regset_from_user(child, &user_frv_native_view,
					     REGSET_FPMEDIA,
					     0, sizeof(child->thread.user->f),
					     datap);

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

/*
 * handle tracing of system call entry
 * - return the revised system call number or ULONG_MAX to cause ENOSYS
 */
asmlinkage unsigned long syscall_trace_entry(void)
{
	__frame->__status |= REG__STATUS_SYSC_ENTRY;
	if (tracehook_report_syscall_entry(__frame)) {
		/* tracing decided this syscall should not happen, so
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in
		 * __frame->syscallno
		 */
		return ULONG_MAX;
	}

	return __frame->syscallno;
}

/*
 * handle tracing of system call exit
 */
asmlinkage void syscall_trace_exit(void)
{
	__frame->__status |= REG__STATUS_SYSC_EXIT;
	tracehook_report_syscall_exit(__frame, 0);
}
