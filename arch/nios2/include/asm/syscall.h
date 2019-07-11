/*
 * Copyright Altera Corporation (C) <2014>. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ASM_NIOS2_SYSCALL_H__
#define __ASM_NIOS2_SYSCALL_H__

#include <linux/err.h>
#include <linux/sched.h>

static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->r2;
}

static inline void syscall_rollback(struct task_struct *task,
				struct pt_regs *regs)
{
	regs->r2 = regs->orig_r2;
	regs->r7 = regs->orig_r7;
}

static inline long syscall_get_error(struct task_struct *task,
				struct pt_regs *regs)
{
	return regs->r7 ? regs->r2 : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
	struct pt_regs *regs)
{
	return regs->r2;
}

static inline void syscall_set_return_value(struct task_struct *task,
	struct pt_regs *regs, int error, long val)
{
	if (error) {
		/* error < 0, but nios2 uses > 0 return value */
		regs->r2 = -error;
		regs->r7 = 1;
	} else {
		regs->r2 = val;
		regs->r7 = 0;
	}
}

static inline void syscall_get_arguments(struct task_struct *task,
	struct pt_regs *regs, unsigned long *args)
{
	*args++ = regs->r4;
	*args++ = regs->r5;
	*args++ = regs->r6;
	*args++ = regs->r7;
	*args++ = regs->r8;
	*args   = regs->r9;
}

static inline void syscall_set_arguments(struct task_struct *task,
	struct pt_regs *regs, const unsigned long *args)
{
	regs->r4 = *args++;
	regs->r5 = *args++;
	regs->r6 = *args++;
	regs->r7 = *args++;
	regs->r8 = *args++;
	regs->r9 = *args;
}

#endif
