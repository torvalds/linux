// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2008-2009 Red Hat, Inc.  All rights reserved.
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASM_NDS32_SYSCALL_H
#define _ASM_NDS32_SYSCALL_H	1

#include <linux/err.h>
struct task_struct;
struct pt_regs;

/**
 * syscall_get_nr - find what system call a task is executing
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 *
 * If @task is executing a system call or is at system call
 * tracing about to attempt one, returns the system call number.
 * If @task is not executing a system call, i.e. it's blocked
 * inside the kernel for a fault or signal, returns -1.
 *
 * Note this returns int even on 64-bit machines.  Only 32 bits of
 * system call number can be meaningful.  If the actual arch value
 * is 64 bits, this truncates to 32 bits so 0xffffffff means -1.
 *
 * It's only valid to call this when @task is known to be blocked.
 */
int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->syscallno;
}

/**
 * syscall_rollback - roll back registers after an aborted system call
 * @task:	task of interest, must be in system call exit tracing
 * @regs:	task_pt_regs() of @task
 *
 * It's only valid to call this when @task is stopped for system
 * call exit tracing (due to TIF_SYSCALL_TRACE or TIF_SYSCALL_AUDIT),
 * after tracehook_report_syscall_entry() returned nonzero to prevent
 * the system call from taking place.
 *
 * This rolls back the register state in @regs so it's as if the
 * system call instruction was a no-op.  The registers containing
 * the system call number and arguments are as they were before the
 * system call instruction.  This may not be the same as what the
 * register state looked like at system call entry tracing.
 */
void syscall_rollback(struct task_struct *task, struct pt_regs *regs)
{
	regs->uregs[0] = regs->orig_r0;
}

/**
 * syscall_get_error - check result of traced system call
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 *
 * Returns 0 if the system call succeeded, or -ERRORCODE if it failed.
 *
 * It's only valid to call this when @task is stopped for tracing on exit
 * from a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
long syscall_get_error(struct task_struct *task, struct pt_regs *regs)
{
	unsigned long error = regs->uregs[0];
	return IS_ERR_VALUE(error) ? error : 0;
}

/**
 * syscall_get_return_value - get the return value of a traced system call
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 *
 * Returns the return value of the successful system call.
 * This value is meaningless if syscall_get_error() returned nonzero.
 *
 * It's only valid to call this when @task is stopped for tracing on exit
 * from a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
long syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return regs->uregs[0];
}

/**
 * syscall_set_return_value - change the return value of a traced system call
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 * @error:	negative error code, or zero to indicate success
 * @val:	user return value if @error is zero
 *
 * This changes the results of the system call that user mode will see.
 * If @error is zero, the user sees a successful system call with a
 * return value of @val.  If @error is nonzero, it's a negated errno
 * code; the user sees a failed system call with this errno code.
 *
 * It's only valid to call this when @task is stopped for tracing on exit
 * from a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
void syscall_set_return_value(struct task_struct *task, struct pt_regs *regs,
			      int error, long val)
{
	regs->uregs[0] = (long)error ? error : val;
}

/**
 * syscall_get_arguments - extract system call parameter values
 * @task:	task of interest, must be blocked
 * @regs:	task_pt_regs() of @task
 * @args:	array filled with argument values
 *
 * Fetches 6 arguments to the system call (from 0 through 5). The first
 * argument is stored in @args[0], and so on.
 *
 * It's only valid to call this when @task is stopped for tracing on
 * entry to a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 */
#define SYSCALL_MAX_ARGS 6
void syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
			   unsigned long *args)
{
	args[0] = regs->orig_r0;
	args++;
	memcpy(args, &regs->uregs[0] + 1, 5 * sizeof(args[0]));
}

/**
 * syscall_set_arguments - change system call parameter value
 * @task:	task of interest, must be in system call entry tracing
 * @regs:	task_pt_regs() of @task
 * @i:		argument index [0,5]
 * @n:		number of arguments; n+i must be [1,6].
 * @args:	array of argument values to store
 *
 * Changes @n arguments to the system call starting with the @i'th argument.
 * Argument @i gets value @args[0], and so on.
 * An arch inline version is probably optimal when @i and @n are constants.
 *
 * It's only valid to call this when @task is stopped for tracing on
 * entry to a system call, due to %TIF_SYSCALL_TRACE or %TIF_SYSCALL_AUDIT.
 * It's invalid to call this with @i + @n > 6; we only support system calls
 * taking up to 6 arguments.
 */
void syscall_set_arguments(struct task_struct *task, struct pt_regs *regs,
			   unsigned int i, unsigned int n,
			   const unsigned long *args)
{
	if (n == 0)
		return;

	if (i + n > SYSCALL_MAX_ARGS) {
		pr_warn("%s called with max args %d, handling only %d\n",
			__func__, i + n, SYSCALL_MAX_ARGS);
		n = SYSCALL_MAX_ARGS - i;
	}

	if (i == 0) {
		regs->orig_r0 = args[0];
		args++;
		i++;
		n--;
	}

	memcpy(&regs->uregs[0] + i, args, n * sizeof(args[0]));
}
#endif /* _ASM_NDS32_SYSCALL_H */
