/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_SYSCALL_H__
#define __ASM_OPENRISC_SYSCALL_H__

#include <linux/err.h>
#include <linux/sched.h>

static inline int
syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->syscallno ? regs->syscallno : -1;
}

static inline void
syscall_rollback(struct task_struct *task, struct pt_regs *regs)
{
	regs->gpr[11] = regs->orig_gpr11;
}

static inline long
syscall_get_error(struct task_struct *task, struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->gpr[11]) ? regs->gpr[11] : 0;
}

static inline long
syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return regs->gpr[11];
}

static inline void
syscall_set_return_value(struct task_struct *task, struct pt_regs *regs,
			 int error, long val)
{
	if (error)
		regs->gpr[11] = -error;
	else
		regs->gpr[11] = val;
}

static inline void
syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned int i, unsigned int n, unsigned long *args)
{
	BUG_ON(i + n > 6);

	memcpy(args, &regs->gpr[3 + i], n * sizeof(args[0]));
}

static inline void
syscall_set_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned int i, unsigned int n, const unsigned long *args)
{
	BUG_ON(i + n > 6);

	memcpy(&regs->gpr[3 + i], args, n * sizeof(args[0]));
}

#endif
