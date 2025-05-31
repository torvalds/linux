/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Access to user system call parameters and results
 *
 * See asm-generic/syscall.h for function descriptions.
 *
 * Copyright (C) 2015 Mickaël Salaün <mic@digikod.net>
 */

#ifndef __UM_SYSCALL_GENERIC_H
#define __UM_SYSCALL_GENERIC_H

#include <asm/ptrace.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <sysdep/ptrace.h>

static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{

	return PT_REGS_SYSCALL_NR(regs);
}

static inline void syscall_set_nr(struct task_struct *task, struct pt_regs *regs, int nr)
{
	PT_REGS_SYSCALL_NR(regs) = nr;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/* do nothing */
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	const long error = regs_return_value(regs);

	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs_return_value(regs);
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	PT_REGS_SET_SYSCALL_RETURN(regs, (long) error ?: val);
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	const struct uml_pt_regs *r = &regs->regs;

	*args++ = UPT_SYSCALL_ARG1(r);
	*args++ = UPT_SYSCALL_ARG2(r);
	*args++ = UPT_SYSCALL_ARG3(r);
	*args++ = UPT_SYSCALL_ARG4(r);
	*args++ = UPT_SYSCALL_ARG5(r);
	*args   = UPT_SYSCALL_ARG6(r);
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	struct uml_pt_regs *r = &regs->regs;

	UPT_SYSCALL_ARG1(r) = *args++;
	UPT_SYSCALL_ARG2(r) = *args++;
	UPT_SYSCALL_ARG3(r) = *args++;
	UPT_SYSCALL_ARG4(r) = *args++;
	UPT_SYSCALL_ARG5(r) = *args++;
	UPT_SYSCALL_ARG6(r) = *args;
}

/* See arch/x86/um/asm/syscall.h for syscall_get_arch() definition. */

#endif	/* __UM_SYSCALL_GENERIC_H */
