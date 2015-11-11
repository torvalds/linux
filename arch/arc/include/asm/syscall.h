/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_SYSCALL_H
#define _ASM_ARC_SYSCALL_H  1

#include <linux/err.h>
#include <linux/sched.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>		/* in_syscall() */

static inline long
syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	if (user_mode(regs) && in_syscall(regs))
		return regs->r8;
	else
		return -1;
}

static inline void
syscall_rollback(struct task_struct *task, struct pt_regs *regs)
{
	regs->r0 = regs->orig_r0;
}

static inline long
syscall_get_error(struct task_struct *task, struct pt_regs *regs)
{
	/* 0 if syscall succeeded, otherwise -Errorcode */
	return IS_ERR_VALUE(regs->r0) ? regs->r0 : 0;
}

static inline long
syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return regs->r0;
}

static inline void
syscall_set_return_value(struct task_struct *task, struct pt_regs *regs,
			 int error, long val)
{
	regs->r0 = (long) error ?: val;
}

/*
 * @i:      argument index [0,5]
 * @n:      number of arguments; n+i must be [1,6].
 */
static inline void
syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned int i, unsigned int n, unsigned long *args)
{
	unsigned long *inside_ptregs = &(regs->r0);
	inside_ptregs -= i;

	BUG_ON((i + n) > 6);

	while (n--) {
		args[i++] = (*inside_ptregs);
		inside_ptregs--;
	}
}

#endif
