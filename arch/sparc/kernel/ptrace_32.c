// SPDX-License-Identifier: GPL-2.0
/* ptrace.c: Sparc process tracing support.
 *
 * Copyright (C) 1996, 2008 David S. Miller (davem@davemloft.net)
 *
 * Based upon code written by Ross Biro, Linus Torvalds, Bob Manson,
 * and David Mosberger.
 *
 * Added Linux support -miguel (weird, eh?, the original code was meant
 * to emulate SunOS).
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/regset.h>
#include <linux/elf.h>
#include <linux/tracehook.h>

#include <asm/pgtable.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>

#include "kernel.h"

/* #define ALLOW_INIT_TRACING */

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* nothing to do */
}

enum sparc_regset {
	REGSET_GENERAL,
	REGSET_FP,
};

static int regwindow32_get(struct task_struct *target,
			   const struct pt_regs *regs,
			   u32 *uregs)
{
	unsigned long reg_window = regs->u_regs[UREG_I6];
	int size = 16 * sizeof(u32);

	if (target == current) {
		if (copy_from_user(uregs, (void __user *)reg_window, size))
			return -EFAULT;
	} else {
		if (access_process_vm(target, reg_window, uregs, size,
				      FOLL_FORCE) != size)
			return -EFAULT;
	}
	return 0;
}

static int regwindow32_set(struct task_struct *target,
			   const struct pt_regs *regs,
			   u32 *uregs)
{
	unsigned long reg_window = regs->u_regs[UREG_I6];
	int size = 16 * sizeof(u32);

	if (target == current) {
		if (copy_to_user((void __user *)reg_window, uregs, size))
			return -EFAULT;
	} else {
		if (access_process_vm(target, reg_window, uregs, size,
				      FOLL_FORCE | FOLL_WRITE) != size)
			return -EFAULT;
	}
	return 0;
}

static int genregs32_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = target->thread.kregs;
	u32 uregs[16];
	int ret;

	if (target == current)
		flush_user_windows();

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  regs->u_regs,
				  0, 16 * sizeof(u32));
	if (ret || !count)
		return ret;

	if (pos < 32 * sizeof(u32)) {
		if (regwindow32_get(target, regs, uregs))
			return -EFAULT;
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  uregs,
					  16 * sizeof(u32), 32 * sizeof(u32));
		if (ret || !count)
			return ret;
	}

	uregs[0] = regs->psr;
	uregs[1] = regs->pc;
	uregs[2] = regs->npc;
	uregs[3] = regs->y;
	uregs[4] = 0;	/* WIM */
	uregs[5] = 0;	/* TBR */
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  uregs,
				  32 * sizeof(u32), 38 * sizeof(u32));
}

static int genregs32_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = target->thread.kregs;
	u32 uregs[16];
	u32 psr;
	int ret;

	if (target == current)
		flush_user_windows();

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 regs->u_regs,
				 0, 16 * sizeof(u32));
	if (ret || !count)
		return ret;

	if (pos < 32 * sizeof(u32)) {
		if (regwindow32_get(target, regs, uregs))
			return -EFAULT;
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 uregs,
					 16 * sizeof(u32), 32 * sizeof(u32));
		if (ret)
			return ret;
		if (regwindow32_set(target, regs, uregs))
			return -EFAULT;
		if (!count)
			return 0;
	}
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &psr,
				 32 * sizeof(u32), 33 * sizeof(u32));
	if (ret)
		return ret;
	regs->psr = (regs->psr & ~(PSR_ICC | PSR_SYSCALL)) |
		    (psr & (PSR_ICC | PSR_SYSCALL));
	if (!count)
		return 0;
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->pc,
				 33 * sizeof(u32), 34 * sizeof(u32));
	if (ret || !count)
		return ret;
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->npc,
				 34 * sizeof(u32), 35 * sizeof(u32));
	if (ret || !count)
		return ret;
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->y,
				 35 * sizeof(u32), 36 * sizeof(u32));
	if (ret || !count)
		return ret;
	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 36 * sizeof(u32), 38 * sizeof(u32));
}

static int fpregs32_get(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			void *kbuf, void __user *ubuf)
{
	const unsigned long *fpregs = target->thread.float_regs;
	int ret = 0;

#if 0
	if (target == current)
		save_and_clear_fpu();
#endif

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  fpregs,
				  0, 32 * sizeof(u32));

	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       32 * sizeof(u32),
					       33 * sizeof(u32));
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &target->thread.fsr,
					  33 * sizeof(u32),
					  34 * sizeof(u32));

	if (!ret) {
		unsigned long val;

		val = (1 << 8) | (8 << 16);
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &val,
					  34 * sizeof(u32),
					  35 * sizeof(u32));
	}

	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       35 * sizeof(u32), -1);

	return ret;
}

static int fpregs32_set(struct task_struct *target,
			const struct user_regset *regset,
			unsigned int pos, unsigned int count,
			const void *kbuf, const void __user *ubuf)
{
	unsigned long *fpregs = target->thread.float_regs;
	int ret;

#if 0
	if (target == current)
		save_and_clear_fpu();
#endif
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 fpregs,
				 0, 32 * sizeof(u32));
	if (!ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  32 * sizeof(u32),
					  33 * sizeof(u32));
	if (!ret && count > 0) {
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &target->thread.fsr,
					 33 * sizeof(u32),
					 34 * sizeof(u32));
	}

	if (!ret)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						34 * sizeof(u32), -1);
	return ret;
}

static const struct user_regset sparc32_regsets[] = {
	/* Format is:
	 * 	G0 --> G7
	 *	O0 --> O7
	 *	L0 --> L7
	 *	I0 --> I7
	 *	PSR, PC, nPC, Y, WIM, TBR
	 */
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = 38,
		.size = sizeof(u32), .align = sizeof(u32),
		.get = genregs32_get, .set = genregs32_set
	},
	/* Format is:
	 *	F0 --> F31
	 *	empty 32-bit word
	 *	FSR (32--bit word)
	 *	FPU QUEUE COUNT (8-bit char)
	 *	FPU QUEUE ENTRYSIZE (8-bit char)
	 *	FPU ENABLED (8-bit char)
	 *	empty 8-bit char
	 *	FPU QUEUE (64 32-bit ints)
	 */
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG,
		.n = 99,
		.size = sizeof(u32), .align = sizeof(u32),
		.get = fpregs32_get, .set = fpregs32_set
	},
};

static const struct user_regset_view user_sparc32_view = {
	.name = "sparc", .e_machine = EM_SPARC,
	.regsets = sparc32_regsets, .n = ARRAY_SIZE(sparc32_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_sparc32_view;
}

struct fps {
	unsigned long regs[32];
	unsigned long fsr;
	unsigned long flags;
	unsigned long extra;
	unsigned long fpqd;
	struct fq {
		unsigned long *insnaddr;
		unsigned long insn;
	} fpq[16];
};

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	unsigned long addr2 = current->thread.kregs->u_regs[UREG_I4];
	void __user *addr2p;
	const struct user_regset_view *view;
	struct pt_regs __user *pregs;
	struct fps __user *fps;
	int ret;

	view = task_user_regset_view(current);
	addr2p = (void __user *) addr2;
	pregs = (struct pt_regs __user *) addr;
	fps = (struct fps __user *) addr;

	switch(request) {
	case PTRACE_GETREGS: {
		ret = copy_regset_to_user(child, view, REGSET_GENERAL,
					  32 * sizeof(u32),
					  4 * sizeof(u32),
					  &pregs->psr);
		if (!ret)
			copy_regset_to_user(child, view, REGSET_GENERAL,
					    1 * sizeof(u32),
					    15 * sizeof(u32),
					    &pregs->u_regs[0]);
		break;
	}

	case PTRACE_SETREGS: {
		ret = copy_regset_from_user(child, view, REGSET_GENERAL,
					    32 * sizeof(u32),
					    4 * sizeof(u32),
					    &pregs->psr);
		if (!ret)
			copy_regset_from_user(child, view, REGSET_GENERAL,
					      1 * sizeof(u32),
					      15 * sizeof(u32),
					      &pregs->u_regs[0]);
		break;
	}

	case PTRACE_GETFPREGS: {
		ret = copy_regset_to_user(child, view, REGSET_FP,
					  0 * sizeof(u32),
					  32 * sizeof(u32),
					  &fps->regs[0]);
		if (!ret)
			ret = copy_regset_to_user(child, view, REGSET_FP,
						  33 * sizeof(u32),
						  1 * sizeof(u32),
						  &fps->fsr);

		if (!ret) {
			if (__put_user(0, &fps->fpqd) ||
			    __put_user(0, &fps->flags) ||
			    __put_user(0, &fps->extra) ||
			    clear_user(fps->fpq, sizeof(fps->fpq)))
				ret = -EFAULT;
		}
		break;
	}

	case PTRACE_SETFPREGS: {
		ret = copy_regset_from_user(child, view, REGSET_FP,
					    0 * sizeof(u32),
					    32 * sizeof(u32),
					    &fps->regs[0]);
		if (!ret)
			ret = copy_regset_from_user(child, view, REGSET_FP,
						    33 * sizeof(u32),
						    1 * sizeof(u32),
						    &fps->fsr);
		break;
	}

	case PTRACE_READTEXT:
	case PTRACE_READDATA:
		ret = ptrace_readdata(child, addr, addr2p, data);

		if (ret == data)
			ret = 0;
		else if (ret >= 0)
			ret = -EIO;
		break;

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA:
		ret = ptrace_writedata(child, addr2p, addr, data);

		if (ret == data)
			ret = 0;
		else if (ret >= 0)
			ret = -EIO;
		break;

	default:
		if (request == PTRACE_SPARC_DETACH)
			request = PTRACE_DETACH;
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage int syscall_trace(struct pt_regs *regs, int syscall_exit_p)
{
	int ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE)) {
		if (syscall_exit_p)
			tracehook_report_syscall_exit(regs, 0);
		else
			ret = tracehook_report_syscall_entry(regs);
	}

	return ret;
}
