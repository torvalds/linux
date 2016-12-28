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
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/tracehook.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
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

static inline
int put_stack_long(struct task_struct *task, int offset, unsigned long data)
{
	unsigned long stack;

	stack = (unsigned long) task->thread.uregs + offset;
	*(unsigned long *) stack = data;
	return 0;
}

/*
 * retrieve the contents of MN10300 userspace general registers
 */
static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	int ret;

	/* we need to skip regs->next */
	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  regs, 0, PT_ORIG_D0 * sizeof(long));
	if (ret < 0)
		return ret;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &regs->orig_d0, PT_ORIG_D0 * sizeof(long),
				  NR_PTREGS * sizeof(long));
	if (ret < 0)
		return ret;

	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					NR_PTREGS * sizeof(long), -1);
}

/*
 * update the contents of the MN10300 userspace general registers
 */
static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	unsigned long tmp;
	int ret;

	/* we need to skip regs->next */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 regs, 0, PT_ORIG_D0 * sizeof(long));
	if (ret < 0)
		return ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->orig_d0, PT_ORIG_D0 * sizeof(long),
				 PT_EPSW * sizeof(long));
	if (ret < 0)
		return ret;

	/* we need to mask off changes to EPSW */
	tmp = regs->epsw;
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &tmp, PT_EPSW * sizeof(long),
				 PT_PC * sizeof(long));
	tmp &= EPSW_FLAG_V | EPSW_FLAG_C | EPSW_FLAG_N | EPSW_FLAG_Z;
	tmp |= regs->epsw & ~(EPSW_FLAG_V | EPSW_FLAG_C | EPSW_FLAG_N |
			      EPSW_FLAG_Z);
	regs->epsw = tmp;

	if (ret < 0)
		return ret;

	/* and finally load the PC */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->pc, PT_PC * sizeof(long),
				 NR_PTREGS * sizeof(long));

	if (ret < 0)
		return ret;

	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 NR_PTREGS * sizeof(long), -1);
}

/*
 * retrieve the contents of MN10300 userspace FPU registers
 */
static int fpuregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct fpu_state_struct *fpregs = &target->thread.fpu_state;
	int ret;

	unlazy_fpu(target);

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  fpregs, 0, sizeof(*fpregs));
	if (ret < 0)
		return ret;

	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					sizeof(*fpregs), -1);
}

/*
 * update the contents of the MN10300 userspace FPU registers
 */
static int fpuregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct fpu_state_struct fpu_state = target->thread.fpu_state;
	int ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &fpu_state, 0, sizeof(fpu_state));
	if (ret < 0)
		return ret;

	fpu_kill_state(target);
	target->thread.fpu_state = fpu_state;
	set_using_fpu(target);

	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 sizeof(fpu_state), -1);
}

/*
 * determine if the FPU registers have actually been used
 */
static int fpuregs_active(struct task_struct *target,
			  const struct user_regset *regset)
{
	return is_using_fpu(target) ? regset->n : 0;
}

/*
 * Define the register sets available on the MN10300 under Linux
 */
enum mn10300_regset {
	REGSET_GENERAL,
	REGSET_FPU,
};

static const struct user_regset mn10300_regsets[] = {
	/*
	 * General register format is:
	 *	A3, A2, D3, D2, MCVF, MCRL, MCRH, MDRQ
	 *	E1, E0, E7...E2, SP, LAR, LIR, MDR
	 *	A1, A0, D1, D0, ORIG_D0, EPSW, PC
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
	 * FPU register format is:
	 *	FS0-31, FPCR
	 */
	[REGSET_FPU] = {
		.core_note_type	= NT_PRFPREG,
		.n		= sizeof(struct fpu_state_struct) / sizeof(long),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.get		= fpuregs_get,
		.set		= fpuregs_set,
		.active		= fpuregs_active,
	},
};

static const struct user_regset_view user_mn10300_native_view = {
	.name		= "mn10300",
	.e_machine	= EM_MN10300,
	.regsets	= mn10300_regsets,
	.n		= ARRAY_SIZE(mn10300_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_mn10300_native_view;
}

/*
 * set the single-step bit
 */
void user_enable_single_step(struct task_struct *child)
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
 * make sure the single-step bit is not set
 */
void user_disable_single_step(struct task_struct *child)
{
#ifndef CONFIG_MN10300_USING_JTAG
	struct user *dummy = NULL;
	long tmp;

	tmp = get_stack_long(child, (unsigned long) &dummy->regs.epsw);
	tmp &= ~EPSW_T;
	put_stack_long(child, (unsigned long) &dummy->regs.epsw, tmp);
#endif
}

void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

/*
 * handle the arch-specific side of process tracing
 */
long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	unsigned long tmp;
	int ret;
	unsigned long __user *datap = (unsigned long __user *) data;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR:
		ret = -EIO;
		if ((addr & 3) || addr > sizeof(struct user) - 3)
			break;

		tmp = 0;  /* Default return condition */
		if (addr < NR_PTREGS << 2)
			tmp = get_stack_long(child,
					     ptrace_regid_to_frame[addr]);
		ret = put_user(tmp, datap);
		break;

		/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR:
		ret = -EIO;
		if ((addr & 3) || addr > sizeof(struct user) - 3)
			break;

		ret = 0;
		if (addr < NR_PTREGS << 2)
			ret = put_stack_long(child, ptrace_regid_to_frame[addr],
					     data);
		break;

	case PTRACE_GETREGS:	/* Get all integer regs from the child. */
		return copy_regset_to_user(child, &user_mn10300_native_view,
					   REGSET_GENERAL,
					   0, NR_PTREGS * sizeof(long),
					   datap);

	case PTRACE_SETREGS:	/* Set all integer regs in the child. */
		return copy_regset_from_user(child, &user_mn10300_native_view,
					     REGSET_GENERAL,
					     0, NR_PTREGS * sizeof(long),
					     datap);

	case PTRACE_GETFPREGS:	/* Get the child FPU state. */
		return copy_regset_to_user(child, &user_mn10300_native_view,
					   REGSET_FPU,
					   0, sizeof(struct fpu_state_struct),
					   datap);

	case PTRACE_SETFPREGS:	/* Set the child FPU state. */
		return copy_regset_from_user(child, &user_mn10300_native_view,
					     REGSET_FPU,
					     0, sizeof(struct fpu_state_struct),
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
asmlinkage unsigned long syscall_trace_entry(struct pt_regs *regs)
{
	if (tracehook_report_syscall_entry(regs))
		/* tracing decided this syscall should not happen, so
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in
		 * regs->orig_d0
		 */
		return ULONG_MAX;

	return regs->orig_d0;
}

/*
 * handle tracing of system call exit
 */
asmlinkage void syscall_trace_exit(struct pt_regs *regs)
{
	tracehook_report_syscall_exit(regs, 0);
}
