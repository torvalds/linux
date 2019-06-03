/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2011 Texas Instruments Incorporated
 * Author: Mark Salter <msalter@redhat.com>
 */

#ifndef __ASM_C6X_SYSCALL_H
#define __ASM_C6X_SYSCALL_H

#include <uapi/linux/audit.h>
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
					 struct pt_regs *regs,
					 unsigned long *args)
{
	*args++ = regs->a4;
	*args++ = regs->b4;
	*args++ = regs->a6;
	*args++ = regs->b6;
	*args++ = regs->a8;
	*args   = regs->b8;
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	regs->a4 = *args++;
	regs->b4 = *args++;
	regs->a6 = *args++;
	regs->b6 = *args++;
	regs->a8 = *args++;
	regs->a9 = *args;
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)
		? AUDIT_ARCH_C6XBE : AUDIT_ARCH_C6X;
}

#endif /* __ASM_C6X_SYSCALLS_H */
