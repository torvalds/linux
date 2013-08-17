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
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>

#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/audit.h>
#include <linux/regset.h>
#include <linux/tracehook.h>
#include <linux/elf.h>

#include <asm/thread_info.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>

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
		       unsigned int pos, unsigned int count,
		       void *kbuf, void __user * ubuf)
{
	const struct pt_regs *regs = task_pt_regs(target);
	int ret;

	/* r0 */
	ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf, 0, 4);

	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  regs->gpr+1, 4, 4*32);
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &regs->pc, 4*32, 4*33);
	if (!ret)
		ret = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
					  &regs->sr, 4*33, 4*34);
	if (!ret)
		ret = user_regset_copyout_zero(&pos, &count, &kbuf, &ubuf,
					       4*34, -1);

	return ret;
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
	ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf, 0, 4);
	/* r1 - r31 */
	if (!ret)
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
		ret = user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
						4*33, -1);

	return ret;
}

/*
 * Define the register sets available on OpenRISC under Linux
 */
enum or1k_regset {
	REGSET_GENERAL,
};

static const struct user_regset or1k_regsets[] = {
	[REGSET_GENERAL] = {
			    .core_note_type = NT_PRSTATUS,
			    .n = ELF_NGREG,
			    .size = sizeof(long),
			    .align = sizeof(long),
			    .get = genregs_get,
			    .set = genregs_set,
			    },
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
	    tracehook_report_syscall_entry(regs))
		/*
		 * Tracing decided this syscall should not happen.
		 * We'll return a bogus call number to get an ENOSYS
		 * error, but leave the original number in <something>.
		 */
		ret = -1L;

	audit_syscall_entry(audit_arch(), regs->gpr[11],
			    regs->gpr[3], regs->gpr[4],
			    regs->gpr[5], regs->gpr[6]);

	return ret ? : regs->gpr[11];
}

asmlinkage void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	audit_syscall_exit(regs);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
