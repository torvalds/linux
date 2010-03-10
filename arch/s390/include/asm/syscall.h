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

#include <linux/sched.h>
#include <asm/ptrace.h>

/*
 * The syscall table always contains 32 bit pointers since we know that the
 * address of the function to be called is (way) below 4GB.  So the "int"
 * type here is what we want [need] for both 32 bit and 64 bit systems.
 */
extern const unsigned int sys_call_table[];

static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return regs->svcnr ? regs->svcnr : -1;
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
	unsigned long mask = -1UL;

	BUG_ON(i + n > 6);
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT))
		mask = 0xffffffff;
#endif
	if (i + n == 6)
		args[--n] = regs->args[0] & mask;
	while (n-- > 0)
		if (i + n > 0)
			args[n] = regs->gprs[2 + i + n] & mask;
	if (i == 0)
		args[0] = regs->orig_gpr2 & mask;
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	BUG_ON(i + n > 6);
	if (i + n == 6)
		regs->args[0] = args[--n];
	while (n-- > 0)
		if (i + n > 0)
			regs->gprs[2 + i + n] = args[n];
	if (i == 0)
		regs->orig_gpr2 = args[0];
}

#endif	/* _ASM_SYSCALL_H */
