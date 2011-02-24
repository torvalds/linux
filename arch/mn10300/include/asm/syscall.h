/* Access to user system call parameters and results
 *
 * See asm-generic/syscall.h for function descriptions.
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H

#include <linux/sched.h>
#include <linux/err.h>

extern const unsigned long sys_call_table[];

static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->orig_d0;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->d0 = regs->orig_d0;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->d0;
	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->d0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->d0 = (long) error ?: val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	switch (i) {
	case 0:
		if (!n--) break;
		*args++ = regs->a0;
	case 1:
		if (!n--) break;
		*args++ = regs->d1;
	case 2:
		if (!n--) break;
		*args++ = regs->a3;
	case 3:
		if (!n--) break;
		*args++ = regs->a2;
	case 4:
		if (!n--) break;
		*args++ = regs->d3;
	case 5:
		if (!n--) break;
		*args++ = regs->d2;
	case 6:
		if (!n--) break;
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
	switch (i) {
	case 0:
		if (!n--) break;
		regs->a0 = *args++;
	case 1:
		if (!n--) break;
		regs->d1 = *args++;
	case 2:
		if (!n--) break;
		regs->a3 = *args++;
	case 3:
		if (!n--) break;
		regs->a2 = *args++;
	case 4:
		if (!n--) break;
		regs->d3 = *args++;
	case 5:
		if (!n--) break;
		regs->d2 = *args++;
	case 6:
		if (!n--) break;
	default:
		BUG();
		break;
	}
}

#endif /* _ASM_SYSCALL_H */
