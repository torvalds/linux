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
#include <linux/smp_lock.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/regset.h>
#include <linux/elf.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

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

static int genregs32_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = target->thread.kregs;
	unsigned long __user *reg_window;
	unsigned long *k = kbuf;
	unsigned long __user *u = ubuf;
	unsigned long reg;

	if (target == current)
		flush_user_windows();

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf) {
		for (; count > 0 && pos < 16; count--)
			*k++ = regs->u_regs[pos++];

		reg_window = (unsigned long __user *) regs->u_regs[UREG_I6];
		for (; count > 0 && pos < 32; count--) {
			if (get_user(*k++, &reg_window[pos++]))
				return -EFAULT;
		}
	} else {
		for (; count > 0 && pos < 16; count--) {
			if (put_user(regs->u_regs[pos++], u++))
				return -EFAULT;
		}

		reg_window = (unsigned long __user *) regs->u_regs[UREG_I6];
		for (; count > 0 && pos < 32; count--) {
			if (get_user(reg, &reg_window[pos++]) ||
			    put_user(reg, u++))
				return -EFAULT;
		}
	}
	while (count > 0) {
		switch (pos) {
		case 32: /* PSR */
			reg = regs->psr;
			break;
		case 33: /* PC */
			reg = regs->pc;
			break;
		case 34: /* NPC */
			reg = regs->npc;
			break;
		case 35: /* Y */
			reg = regs->y;
			break;
		case 36: /* WIM */
		case 37: /* TBR */
			reg = 0;
			break;
		default:
			goto finish;
		}

		if (kbuf)
			*k++ = reg;
		else if (put_user(reg, u++))
			return -EFAULT;
		pos++;
		count--;
	}
finish:
	pos *= sizeof(reg);
	count *= sizeof(reg);

	return user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					38 * sizeof(reg), -1);
}

static int genregs32_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = target->thread.kregs;
	unsigned long __user *reg_window;
	const unsigned long *k = kbuf;
	const unsigned long __user *u = ubuf;
	unsigned long reg;

	if (target == current)
		flush_user_windows();

	pos /= sizeof(reg);
	count /= sizeof(reg);

	if (kbuf) {
		for (; count > 0 && pos < 16; count--)
			regs->u_regs[pos++] = *k++;

		reg_window = (unsigned long __user *) regs->u_regs[UREG_I6];
		for (; count > 0 && pos < 32; count--) {
			if (put_user(*k++, &reg_window[pos++]))
				return -EFAULT;
		}
	} else {
		for (; count > 0 && pos < 16; count--) {
			if (get_user(reg, u++))
				return -EFAULT;
			regs->u_regs[pos++] = reg;
		}

		reg_window = (unsigned long __user *) regs->u_regs[UREG_I6];
		for (; count > 0 && pos < 32; count--) {
			if (get_user(reg, u++) ||
			    put_user(reg, &reg_window[pos++]))
				return -EFAULT;
		}
	}
	while (count > 0) {
		unsigned long psr;

		if (kbuf)
			reg = *k++;
		else if (get_user(reg, u++))
			return -EFAULT;

		switch (pos) {
		case 32: /* PSR */
			psr = regs->psr;
			psr &= ~PSR_ICC;
			psr |= (reg & PSR_ICC);
			regs->psr = psr;
			break;
		case 33: /* PC */
			regs->pc = reg;
			break;
		case 34: /* NPC */
			regs->npc = reg;
			break;
		case 35: /* Y */
			regs->y = reg;
			break;
		case 36: /* WIM */
		case 37: /* TBR */
			break;
		default:
			goto finish;
		}

		pos++;
		count--;
	}
finish:
	pos *= sizeof(reg);
	count *= sizeof(reg);

	return user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					 38 * sizeof(reg), -1);
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
		.n = 38 * sizeof(u32),
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
		.n = 99 * sizeof(u32),
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

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	unsigned long addr2 = current->thread.kregs->u_regs[UREG_I4];
	const struct user_regset_view *view;
	int ret;

	view = task_user_regset_view(current);

	switch(request) {
	case PTRACE_GETREGS: {
		struct pt_regs __user *pregs = (struct pt_regs __user *) addr;

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
		struct pt_regs __user *pregs = (struct pt_regs __user *) addr;

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
		struct fps __user *fps = (struct fps __user *) addr;

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
		struct fps __user *fps = (struct fps __user *) addr;

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
		ret = ptrace_readdata(child, addr,
				      (void __user *) addr2, data);

		if (ret == data)
			ret = 0;
		else if (ret >= 0)
			ret = -EIO;
		break;

	case PTRACE_WRITETEXT:
	case PTRACE_WRITEDATA:
		ret = ptrace_writedata(child, (void __user *) addr2,
				       addr, data);

		if (ret == data)
			ret = 0;
		else if (ret >= 0)
			ret = -EIO;
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

asmlinkage void syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig (current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
