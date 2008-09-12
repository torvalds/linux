/*
 * SuperH process tracing
 *
 * Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 * Copyright (C) 2002 - 2008  Paul Mundt
 *
 * Audit support by Yuichi Nakamura <ynakam@hitachisoft.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
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
#include <linux/tracehook.h>
#include <linux/elf.h>
#include <linux/regset.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/syscalls.h>

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
	/* Next scheduling will set up UBC */
	if (child->thread.ubc_pc == 0)
		ubc_usercnt += 1;

	child->thread.ubc_pc = get_stack_long(child,
				offsetof(struct pt_regs, pc));

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

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	int ret;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  regs->regs,
				  0, 16 * sizeof(unsigned long));
	if (!ret)
		/* PC, PR, SR, GBR, MACH, MACL, TRA */
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &regs->pc,
					  offsetof(struct pt_regs, pc),
					  sizeof(struct pt_regs));
	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       sizeof(struct pt_regs), -1);

	return ret;
}

static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	int ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 regs->regs,
				 0, 16 * sizeof(unsigned long));
	if (!ret && count > 0)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 &regs->pc,
					 offsetof(struct pt_regs, pc),
					 sizeof(struct pt_regs));
	if (!ret)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						sizeof(struct pt_regs), -1);

	return ret;
}

#ifdef CONFIG_SH_DSP
static int dspregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct pt_dspregs *regs = task_pt_dspregs(target);
	int ret;

	ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf, regs,
				  0, sizeof(struct pt_dspregs));
	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       sizeof(struct pt_dspregs), -1);

	return ret;
}

static int dspregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	struct pt_dspregs *regs = task_pt_dspregs(target);
	int ret;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, regs,
				 0, sizeof(struct pt_dspregs));
	if (!ret)
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						sizeof(struct pt_dspregs), -1);

	return ret;
}

static int dspregs_active(struct task_struct *target,
			  const struct user_regset *regset)
{
	struct pt_regs *regs = task_pt_regs(target);

	return regs->sr & SR_DSP ? regset->n : 0;
}
#endif

/*
 * These are our native regset flavours.
 */
enum sh_regset {
	REGSET_GENERAL,
#ifdef CONFIG_SH_DSP
	REGSET_DSP,
#endif
};

static const struct user_regset sh_regsets[] = {
	/*
	 * Format is:
	 *	R0 --> R15
	 *	PC, PR, SR, GBR, MACH, MACL, TRA
	 */
	[REGSET_GENERAL] = {
		.core_note_type	= NT_PRSTATUS,
		.n		= ELF_NGREG,
		.size		= sizeof(long),
		.align		= sizeof(long),
		.get		= genregs_get,
		.set		= genregs_set,
	},

#ifdef CONFIG_SH_DSP
	[REGSET_DSP] = {
		.n		= sizeof(struct pt_dspregs) / sizeof(long),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.get		= dspregs_get,
		.set		= dspregs_set,
		.active		= dspregs_active,
	},
#endif
};

static const struct user_regset_view user_sh_native_view = {
	.name		= "sh",
	.e_machine	= EM_SH,
	.regsets	= sh_regsets,
	.n		= ARRAY_SIZE(sh_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_sh_native_view;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	struct user * dummy = NULL;
	unsigned long __user *datap = (unsigned long __user *)data;
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
		ret = put_user(tmp, datap);
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

	case PTRACE_GETREGS:
		return copy_regset_to_user(child, &user_sh_native_view,
					   REGSET_GENERAL,
					   0, sizeof(struct pt_regs),
					   (void __user *)data);
	case PTRACE_SETREGS:
		return copy_regset_from_user(child, &user_sh_native_view,
					     REGSET_GENERAL,
					     0, sizeof(struct pt_regs),
					     (const void __user *)data);
#ifdef CONFIG_SH_DSP
	case PTRACE_GETDSPREGS:
		return copy_regset_to_user(child, &user_sh_native_view,
					   REGSET_DSP,
					   0, sizeof(struct pt_dspregs),
					   (void __user *)data);
	case PTRACE_SETDSPREGS:
		return copy_regset_from_user(child, &user_sh_native_view,
					     REGSET_DSP,
					     0, sizeof(struct pt_dspregs),
					     (const void __user *)data);
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
		if (put_user(tmp, datap)) {
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

static inline int audit_arch(void)
{
	int arch = EM_SH;

#ifdef CONFIG_CPU_LITTLE_ENDIAN
	arch |= __AUDIT_ARCH_LE;
#endif

	return arch;
}

asmlinkage long do_syscall_trace_enter(struct pt_regs *regs)
{
	long ret = 0;

	secure_computing(regs->regs[0]);

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		/*
		 * Tracing decided this syscall should not happen.
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in regs->regs[0].
		 */
		ret = -1L;

	if (unlikely(current->audit_context))
		audit_syscall_entry(audit_arch(), regs->regs[3],
				    regs->regs[4], regs->regs[5],
				    regs->regs[6], regs->regs[7]);

	return ret ?: regs->regs[0];
}

asmlinkage void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	if (unlikely(current->audit_context))
		audit_syscall_exit(AUDITSC_RESULT(regs->regs[0]),
				   regs->regs[0]);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
