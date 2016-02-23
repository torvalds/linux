/*
 * Access to user system call parameters and results
 *
 * See asm-generic/syscall.h for function descriptions.
 *
 * Copyright (C) 2015 Mickaël Salaün <mic@digikod.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	const struct uml_pt_regs *r = &regs->regs;

	switch (i) {
	case 0:
		if (!n--)
			break;
		*args++ = UPT_SYSCALL_ARG1(r);
	case 1:
		if (!n--)
			break;
		*args++ = UPT_SYSCALL_ARG2(r);
	case 2:
		if (!n--)
			break;
		*args++ = UPT_SYSCALL_ARG3(r);
	case 3:
		if (!n--)
			break;
		*args++ = UPT_SYSCALL_ARG4(r);
	case 4:
		if (!n--)
			break;
		*args++ = UPT_SYSCALL_ARG5(r);
	case 5:
		if (!n--)
			break;
		*args++ = UPT_SYSCALL_ARG6(r);
	case 6:
		if (!n--)
			break;
	default:
		BUG();
		break;
	}
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	struct uml_pt_regs *r = &regs->regs;

	switch (i) {
	case 0:
		if (!n--)
			break;
		UPT_SYSCALL_ARG1(r) = *args++;
	case 1:
		if (!n--)
			break;
		UPT_SYSCALL_ARG2(r) = *args++;
	case 2:
		if (!n--)
			break;
		UPT_SYSCALL_ARG3(r) = *args++;
	case 3:
		if (!n--)
			break;
		UPT_SYSCALL_ARG4(r) = *args++;
	case 4:
		if (!n--)
			break;
		UPT_SYSCALL_ARG5(r) = *args++;
	case 5:
		if (!n--)
			break;
		UPT_SYSCALL_ARG6(r) = *args++;
	case 6:
		if (!n--)
			break;
	default:
		BUG();
		break;
	}
}

/* See arch/x86/um/asm/syscall.h for syscall_get_arch() definition. */

#endif	/* __UM_SYSCALL_GENERIC_H */
