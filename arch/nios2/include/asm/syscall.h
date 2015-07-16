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
	struct pt_regs *regs, unsigned int i, unsigned int n,
	unsigned long *args)
{
	BUG_ON(i + n > 6);

	switch (i) {
	case 0:
		if (!n--)
			break;
		*args++ = regs->r4;
	case 1:
		if (!n--)
			break;
		*args++ = regs->r5;
	case 2:
		if (!n--)
			break;
		*args++ = regs->r6;
	case 3:
		if (!n--)
			break;
		*args++ = regs->r7;
	case 4:
		if (!n--)
			break;
		*args++ = regs->r8;
	case 5:
		if (!n--)
			break;
		*args++ = regs->r9;
	case 6:
		if (!n--)
			break;
	default:
		BUG();
	}
}

static inline void syscall_set_arguments(struct task_struct *task,
	struct pt_regs *regs, unsigned int i, unsigned int n,
	const unsigned long *args)
{
	BUG_ON(i + n > 6);

	switch (i) {
	case 0:
		if (!n--)
			break;
		regs->r4 = *args++;
	case 1:
		if (!n--)
			break;
		regs->r5 = *args++;
	case 2:
		if (!n--)
			break;
		regs->r6 = *args++;
	case 3:
		if (!n--)
			break;
		regs->r7 = *args++;
	case 4:
		if (!n--)
			break;
		regs->r8 = *args++;
	case 5:
		if (!n--)
			break;
		regs->r9 = *args++;
	case 6:
		if (!n)
			break;
	default:
		BUG();
	}
}

#endif
