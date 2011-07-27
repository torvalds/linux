/*
 * SuperH process tracing
 *
 * Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 * Copyright (C) 2002 - 2009  Paul Mundt
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
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/io.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/tracehook.h>
#include <linux/elf.h>
#include <linux/regset.h>
#include <linux/hw_breakpoint.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mmu_context.h>
#include <asm/syscalls.h>
#include <asm/fpu.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

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

void ptrace_triggered(struct perf_event *bp, int nmi,
		      struct perf_sample_data *data, struct pt_regs *regs)
{
	struct perf_event_attr attr;

	/*
	 * Disable the breakpoint request here since ptrace has defined a
	 * one-shot behaviour for breakpoint exceptions.
	 */
	attr = bp->attr;
	attr.disabled = true;
	modify_user_hw_breakpoint(bp, &attr);
}

static int set_single_step(struct task_struct *tsk, unsigned long addr)
{
	struct thread_struct *thread = &tsk->thread;
	struct perf_event *bp;
	struct perf_event_attr attr;

	bp = thread->ptrace_bps[0];
	if (!bp) {
		ptrace_breakpoint_init(&attr);

		attr.bp_addr = addr;
		attr.bp_len = HW_BREAKPOINT_LEN_2;
		attr.bp_type = HW_BREAKPOINT_R;

		bp = register_user_hw_breakpoint(&attr, ptrace_triggered, tsk);
		if (IS_ERR(bp))
			return PTR_ERR(bp);

		thread->ptrace_bps[0] = bp;
	} else {
		int err;

		attr = bp->attr;
		attr.bp_addr = addr;
		/* reenable breakpoint */
		attr.disabled = false;
		err = modify_user_hw_breakpoint(bp, &attr);
		if (unlikely(err))
			return err;
	}

	return 0;
}

void user_enable_single_step(struct task_struct *child)
{
	unsigned long pc = get_stack_long(child, offsetof(struct pt_regs, pc));

	set_tsk_thread_flag(child, TIF_SINGLESTEP);

	if (ptrace_get_breakpoints(child) < 0)
		return;

	set_single_step(child, pc);
	ptrace_put_breakpoints(child);
}

void user_disable_single_step(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
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

#ifdef CONFIG_SH_FPU
int fpregs_get(struct task_struct *target,
	       const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       void *kbuf, void __user *ubuf)
{
	int ret;

	ret = init_fpu(target);
	if (ret)
		return ret;

	if ((boot_cpu_data.flags & CPU_HAS_FPU))
		return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					   &target->thread.xstate->hardfpu, 0, -1);

	return user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				   &target->thread.xstate->softfpu, 0, -1);
}

static int fpregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	int ret;

	ret = init_fpu(target);
	if (ret)
		return ret;

	set_stopped_child_used_math(target);

	if ((boot_cpu_data.flags & CPU_HAS_FPU))
		return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					  &target->thread.xstate->hardfpu, 0, -1);

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.xstate->softfpu, 0, -1);
}

static int fpregs_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	return tsk_used_math(target) ? regset->n : 0;
}
#endif

#ifdef CONFIG_SH_DSP
static int dspregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user *ubuf)
{
	const struct pt_dspregs *regs =
		(struct pt_dspregs *)&target->thread.dsp_status.dsp_regs;
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
	struct pt_dspregs *regs =
		(struct pt_dspregs *)&target->thread.dsp_status.dsp_regs;
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

const struct pt_regs_offset regoffset_table[] = {
	REGS_OFFSET_NAME(0),
	REGS_OFFSET_NAME(1),
	REGS_OFFSET_NAME(2),
	REGS_OFFSET_NAME(3),
	REGS_OFFSET_NAME(4),
	REGS_OFFSET_NAME(5),
	REGS_OFFSET_NAME(6),
	REGS_OFFSET_NAME(7),
	REGS_OFFSET_NAME(8),
	REGS_OFFSET_NAME(9),
	REGS_OFFSET_NAME(10),
	REGS_OFFSET_NAME(11),
	REGS_OFFSET_NAME(12),
	REGS_OFFSET_NAME(13),
	REGS_OFFSET_NAME(14),
	REGS_OFFSET_NAME(15),
	REG_OFFSET_NAME(pc),
	REG_OFFSET_NAME(pr),
	REG_OFFSET_NAME(sr),
	REG_OFFSET_NAME(gbr),
	REG_OFFSET_NAME(mach),
	REG_OFFSET_NAME(macl),
	REG_OFFSET_NAME(tra),
	REG_OFFSET_END,
};

/*
 * These are our native regset flavours.
 */
enum sh_regset {
	REGSET_GENERAL,
#ifdef CONFIG_SH_FPU
	REGSET_FPU,
#endif
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

#ifdef CONFIG_SH_FPU
	[REGSET_FPU] = {
		.core_note_type	= NT_PRFPREG,
		.n		= sizeof(struct user_fpu_struct) / sizeof(long),
		.size		= sizeof(long),
		.align		= sizeof(long),
		.get		= fpregs_get,
		.set		= fpregs_set,
		.active		= fpregs_active,
	},
#endif

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

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
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
		else if (addr >= offsetof(struct user, fpu) &&
			 addr < offsetof(struct user, u_fpvalid)) {
			if (!tsk_used_math(child)) {
				if (addr == offsetof(struct user, fpu.fpscr))
					tmp = FPSCR_INIT;
				else
					tmp = 0;
			} else {
				unsigned long index;
				ret = init_fpu(child);
				if (ret)
					break;
				index = addr - offsetof(struct user, fpu);
				tmp = ((unsigned long *)child->thread.xstate)
					[index >> 2];
			}
		} else if (addr == offsetof(struct user, u_fpvalid))
			tmp = !!tsk_used_math(child);
		else if (addr == PT_TEXT_ADDR)
			tmp = child->mm->start_code;
		else if (addr == PT_DATA_ADDR)
			tmp = child->mm->start_data;
		else if (addr == PT_TEXT_END_ADDR)
			tmp = child->mm->end_code;
		else if (addr == PT_TEXT_LEN)
			tmp = child->mm->end_code - child->mm->start_code;
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
		else if (addr >= offsetof(struct user, fpu) &&
			 addr < offsetof(struct user, u_fpvalid)) {
			unsigned long index;
			ret = init_fpu(child);
			if (ret)
				break;
			index = addr - offsetof(struct user, fpu);
			set_stopped_child_used_math(child);
			((unsigned long *)child->thread.xstate)
				[index >> 2] = data;
			ret = 0;
		} else if (addr == offsetof(struct user, u_fpvalid)) {
			conditional_stopped_child_used_math(data, child);
			ret = 0;
		}
		break;

	case PTRACE_GETREGS:
		return copy_regset_to_user(child, &user_sh_native_view,
					   REGSET_GENERAL,
					   0, sizeof(struct pt_regs),
					   datap);
	case PTRACE_SETREGS:
		return copy_regset_from_user(child, &user_sh_native_view,
					     REGSET_GENERAL,
					     0, sizeof(struct pt_regs),
					     datap);
#ifdef CONFIG_SH_FPU
	case PTRACE_GETFPREGS:
		return copy_regset_to_user(child, &user_sh_native_view,
					   REGSET_FPU,
					   0, sizeof(struct user_fpu_struct),
					   datap);
	case PTRACE_SETFPREGS:
		return copy_regset_from_user(child, &user_sh_native_view,
					     REGSET_FPU,
					     0, sizeof(struct user_fpu_struct),
					     datap);
#endif
#ifdef CONFIG_SH_DSP
	case PTRACE_GETDSPREGS:
		return copy_regset_to_user(child, &user_sh_native_view,
					   REGSET_DSP,
					   0, sizeof(struct pt_dspregs),
					   datap);
	case PTRACE_SETDSPREGS:
		return copy_regset_from_user(child, &user_sh_native_view,
					     REGSET_DSP,
					     0, sizeof(struct pt_dspregs),
					     datap);
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

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->regs[0]);

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

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->regs[0]);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
