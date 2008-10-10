/*
 * Access to user system call parameters and results
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H	1

#include <asm/ptrace.h>

static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	if (regs->trap != __LC_SVC_OLD_PSW)
		return -1;
	return regs->gprs[2];
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->gprs[2] = regs->orig_gpr2;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return (regs->gprs[2] >= -4096UL) ? -regs->gprs[2] : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->gprs[2];
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->gprs[2] = error ? -error : val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	BUG_ON(i + n > 6);
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT)) {
		if (i + n == 6)
			args[--n] = (u32) regs->args[0];
		while (n-- > 0)
			args[n] = (u32) regs->gprs[2 + i + n];
	}
#endif
	if (i + n == 6)
		args[--n] = regs->args[0];
	memcpy(args, &regs->gprs[2 + i], n * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	BUG_ON(i + n > 6);
	if (i + n == 6)
		regs->args[0] = args[--n];
	memcpy(&regs->gprs[2 + i], args, n * sizeof(args[0]));
}

#endif	/* _ASM_SYSCALL_H */
