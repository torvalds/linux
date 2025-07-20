// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC ptrace.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2005 Gyorgy Jeney <nog@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/string.h>

#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/audit.h>
#include <linux/regset.h>
#include <linux/elf.h>

#include <asm/thread_info.h>
#include <asm/page.h>

asmlinkage long do_syscall_trace_enter(struct pt_regs *regs);

asmlinkage void do_syscall_trace_leave(struct pt_regs *regs);

/*
 * Copy the thread state to a regset that can be interpreted by userspace.
 *
 * It doesn't matter what our internal pt_regs structure looks like.  The
 * important thing is that we export a consistent view of the thread state
 * to userspace.  As such, we need to make sure that the regset remains
 * ABI compatible as defined by the struct user_regs_struct:
 *
 * (Each item is a 32-bit word)
 * r0 = 0 (exported for clarity)
 * 31 GPRS r1-r31
 * PC (Program counter)
 * SR (Supervision register)
 */
static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	const struct pt_regs *regs = task_pt_regs(target);

	/* r0 */
	membuf_zero(&to, 4);
	membuf_write(&to, regs->gpr + 1, 31 * 4);
	membuf_store(&to, regs->pc);
	return membuf_store(&to, regs->sr);
}

/*
 * Set the thread state from a regset passed in via ptrace
 */
static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user * ubuf)
{
	struct pt_regs *regs = task_pt_regs(target);
	int ret;

	/* ignore r0 */
	user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf, 0, 4);
	/* r1 - r31 */
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
					 regs->gpr+1, 4, 4*32);
	/* PC */
	if (!ret)
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->pc, 4*32, 4*33);
	/*
	 * Skip SR and padding... userspace isn't allowed to changes bits in
	 * the Supervision register
	 */
	if (!ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf, 4*33, -1);

	return ret;
}

#ifdef CONFIG_FPU
/*
 * As OpenRISC shares GPRs and floating point registers we don't need to export
 * the floating point registers again.  So here we only export the fpcsr special
 * purpose register.
 */
static int fpregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	return membuf_store(&to, target->thread.fpcsr);
}

static int fpregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	/* FPCSR */
	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  &target->thread.fpcsr, 0, 4);
}
#endif

/*
 * Define the register sets available on OpenRISC under Linux
 */
enum or1k_regset {
	REGSET_GENERAL,
#ifdef CONFIG_FPU
	REGSET_FPU,
#endif
};

static const struct user_regset or1k_regsets[] = {
	[REGSET_GENERAL] = {
			    .core_note_type = NT_PRSTATUS,
			    .n = ELF_NGREG,
			    .size = sizeof(long),
			    .align = sizeof(long),
			    .regset_get = genregs_get,
			    .set = genregs_set,
			    },
#ifdef CONFIG_FPU
	[REGSET_FPU] = {
			    .core_note_type = NT_PRFPREG,
			    .n = sizeof(struct __or1k_fpu_state) / sizeof(long),
			    .size = sizeof(long),
			    .align = sizeof(long),
			    .regset_get = fpregs_get,
			    .set = fpregs_set,
			    },
#endif
};

static const struct user_regset_view user_or1k_native_view = {
	.name = "or1k",
	.e_machine = EM_OPENRISC,
	.regsets = or1k_regsets,
	.n = ARRAY_SIZE(or1k_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &user_or1k_native_view;
}

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_NAME(sr),
	REG_OFFSET_NAME(sp),
	REG_OFFSET_NAME(gpr2),
	REG_OFFSET_NAME(gpr3),
	REG_OFFSET_NAME(gpr4),
	REG_OFFSET_NAME(gpr5),
	REG_OFFSET_NAME(gpr6),
	REG_OFFSET_NAME(gpr7),
	REG_OFFSET_NAME(gpr8),
	REG_OFFSET_NAME(gpr9),
	REG_OFFSET_NAME(gpr10),
	REG_OFFSET_NAME(gpr11),
	REG_OFFSET_NAME(gpr12),
	REG_OFFSET_NAME(gpr13),
	REG_OFFSET_NAME(gpr14),
	REG_OFFSET_NAME(gpr15),
	REG_OFFSET_NAME(gpr16),
	REG_OFFSET_NAME(gpr17),
	REG_OFFSET_NAME(gpr18),
	REG_OFFSET_NAME(gpr19),
	REG_OFFSET_NAME(gpr20),
	REG_OFFSET_NAME(gpr21),
	REG_OFFSET_NAME(gpr22),
	REG_OFFSET_NAME(gpr23),
	REG_OFFSET_NAME(gpr24),
	REG_OFFSET_NAME(gpr25),
	REG_OFFSET_NAME(gpr26),
	REG_OFFSET_NAME(gpr27),
	REG_OFFSET_NAME(gpr28),
	REG_OFFSET_NAME(gpr29),
	REG_OFFSET_NAME(gpr30),
	REG_OFFSET_NAME(gpr31),
	REG_OFFSET_NAME(pc),
	REG_OFFSET_NAME(orig_gpr11),
	REG_OFFSET_END,
};

/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/**
 * regs_within_kernel_stack() - check the address in the stack
 * @regs:      pt_regs which contains kernel stack pointer.
 * @addr:      address which is checked.
 *
 * regs_within_kernel_stack() checks @addr is within the kernel stack page(s).
 * If @addr is within the kernel stack, it returns true. If not, returns false.
 */
static bool regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	return (addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:	pt_regs which contains kernel stack pointer.
 * @n:		stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);

	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{
	pr_debug("ptrace_disable(): TODO\n");

	user_disable_single_step(child);
	clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
}

long arch_ptrace(struct task_struct *child, long request, unsigned long addr,
		 unsigned long data)
{
	int ret;

	switch (request) {
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

/*
 * Notification of system call entry/exit
 * - triggered by current->work.syscall_trace
 */
asmlinkage long do_syscall_trace_enter(struct pt_regs *regs)
{
	long ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    ptrace_report_syscall_entry(regs))
		/*
		 * Tracing decided this syscall should not happen.
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in <something>.
		 */
		ret = -1L;

	audit_syscall_entry(regs->gpr[11], regs->gpr[3], regs->gpr[4],
			    regs->gpr[5], regs->gpr[6]);

	return ret ? : regs->gpr[11];
}

asmlinkage void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	audit_syscall_exit(regs);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		ptrace_report_syscall_exit(regs, step);
}
