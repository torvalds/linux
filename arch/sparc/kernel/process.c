// SPDX-License-Identifier: GPL-2.0

/*
 * This file handles the architecture independent parts of process handling..
 */

#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/signal.h>
#include <linux/syscalls.h>

#include "kernel.h"

asmlinkage long sparc_fork(struct pt_regs *regs)
{
	unsigned long orig_i1;
	long ret;
	struct kernel_clone_args args = {
		.exit_signal	= SIGCHLD,
	};

	synchronize_user_stack();

	orig_i1 = regs->u_regs[UREG_I1];
	/* Reuse the parent's stack for the child. */
	args.stack = regs->u_regs[UREG_FP];

	ret = kernel_clone(&args);

	/* If we get an error and potentially restart the system
	 * call, we're screwed because copy_thread() clobbered
	 * the parent's %o1.  So detect that case and restore it
	 * here.
	 */
	if ((unsigned long)ret >= -ERESTART_RESTARTBLOCK)
		regs->u_regs[UREG_I1] = orig_i1;

	return ret;
}

asmlinkage long sparc_vfork(struct pt_regs *regs)
{
	unsigned long orig_i1;
	long ret;
	struct kernel_clone_args args = {
		.flags		= CLONE_VFORK | CLONE_VM,
		.exit_signal	= SIGCHLD,
	};

	synchronize_user_stack();

	orig_i1 = regs->u_regs[UREG_I1];
	/* Reuse the parent's stack for the child. */
	args.stack = regs->u_regs[UREG_FP];

	ret = kernel_clone(&args);

	/* If we get an error and potentially restart the system
	 * call, we're screwed because copy_thread() clobbered
	 * the parent's %o1.  So detect that case and restore it
	 * here.
	 */
	if ((unsigned long)ret >= -ERESTART_RESTARTBLOCK)
		regs->u_regs[UREG_I1] = orig_i1;

	return ret;
}

asmlinkage long sparc_clone(struct pt_regs *regs)
{
	unsigned long orig_i1;
	unsigned int flags;
	long ret;
	struct kernel_clone_args args = {0};

	synchronize_user_stack();

	orig_i1 = regs->u_regs[UREG_I1];
	flags = lower_32_bits(regs->u_regs[UREG_I0]);
	args.flags		= (flags & ~CSIGNAL);
	args.exit_signal	= (flags & CSIGNAL);
	args.tls		= regs->u_regs[UREG_I3];

#ifdef CONFIG_COMPAT
	if (test_thread_flag(TIF_32BIT)) {
		args.pidfd	= compat_ptr(regs->u_regs[UREG_I2]);
		args.child_tid	= compat_ptr(regs->u_regs[UREG_I4]);
		args.parent_tid	= compat_ptr(regs->u_regs[UREG_I2]);
	} else
#endif
	{
		args.pidfd	= (int __user *)regs->u_regs[UREG_I2];
		args.child_tid	= (int __user *)regs->u_regs[UREG_I4];
		args.parent_tid	= (int __user *)regs->u_regs[UREG_I2];
	}

	/* Did userspace give setup a separate stack for the child or are we
	 * reusing the parent's?
	 */
	if (regs->u_regs[UREG_I1])
		args.stack = regs->u_regs[UREG_I1];
	else
		args.stack = regs->u_regs[UREG_FP];

	ret = kernel_clone(&args);

	/* If we get an error and potentially restart the system
	 * call, we're screwed because copy_thread() clobbered
	 * the parent's %o1.  So detect that case and restore it
	 * here.
	 */
	if ((unsigned long)ret >= -ERESTART_RESTARTBLOCK)
		regs->u_regs[UREG_I1] = orig_i1;

	return ret;
}

asmlinkage long sparc_clone3(struct pt_regs *regs)
{
	unsigned long sz;
	struct clone_args __user *cl_args;

	synchronize_user_stack();

	cl_args = (struct clone_args __user *)regs->u_regs[UREG_I0];
	sz = regs->u_regs[UREG_I1];

	return sys_clone3(cl_args, sz);
}
