/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Syscall support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_HEXAGON_SYSCALL_H
#define _ASM_HEXAGON_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/err.h>
#include <asm/ptrace.h>

typedef long (*syscall_fn)(unsigned long, unsigned long,
	unsigned long, unsigned long,
	unsigned long, unsigned long);

#include <asm-generic/syscalls.h>

extern void *sys_call_table[];

static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return regs->r06;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	memcpy(args, &(&regs->r00)[0], 6 * sizeof(args[0]));
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->r00) ? regs->r00 : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->r00;
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_HEXAGON;
}

#endif
