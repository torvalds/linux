/*
 * Copyright (C) 2011 Texas Instruments Incorporated
 * Author: Mark Salter <msalter@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_C6X_SYSCALL_H
#define __ASM_C6X_SYSCALL_H

#include <linux/err.h>
#include <linux/sched.h>

static inline int syscall_get_nr(struct task_struct *task,
				 struct pt_regs *regs)
{
	return regs->b0;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/* do nothing */
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->a4) ? regs->a4 : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->a4;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->a4 = error ?: val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs, unsigned int i,
					 unsigned int n, unsigned long *args)
{
	switch (i) {
	case 0:
		if (!n--)
			break;
		*args++ = regs->a4;
	case 1:
		if (!n--)
			break;
		*args++ = regs->b4;
	case 2:
		if (!n--)
			break;
		*args++ = regs->a6;
	case 3:
		if (!n--)
			break;
		*args++ = regs->b6;
	case 4:
		if (!n--)
			break;
		*args++ = regs->a8;
	case 5:
		if (!n--)
			break;
		*args++ = regs->b8;
	case 6:
		if (!n--)
			break;
	default:
		BUG();
	}
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	switch (i) {
	case 0:
		if (!n--)
			break;
		regs->a4 = *args++;
	case 1:
		if (!n--)
			break;
		regs->b4 = *args++;
	case 2:
		if (!n--)
			break;
		regs->a6 = *args++;
	case 3:
		if (!n--)
			break;
		regs->b6 = *args++;
	case 4:
		if (!n--)
			break;
		regs->a8 = *args++;
	case 5:
		if (!n--)
			break;
		regs->a9 = *args++;
	case 6:
		if (!n)
			break;
	default:
		BUG();
	}
}

#endif /* __ASM_C6X_SYSCALLS_H */
