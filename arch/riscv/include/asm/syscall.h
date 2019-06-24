/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2008-2009 Red Hat, Inc.  All rights reserved.
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 * Copyright 2015 Regents of the University of California, Berkeley
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */

#ifndef _ASM_RISCV_SYSCALL_H
#define _ASM_RISCV_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/sched.h>
#include <linux/err.h>

/* The array of function pointers for syscalls. */
extern void *sys_call_table[];

/*
 * Only the low 32 bits of orig_r0 are meaningful, so we return int.
 * This importantly ignores the high bits on 64-bit, so comparisons
 * sign-extend the low 32 bits.
 */
static inline int syscall_get_nr(struct task_struct *task,
				 struct pt_regs *regs)
{
	return regs->a7;
}

static inline void syscall_set_nr(struct task_struct *task,
				  struct pt_regs *regs,
				  int sysno)
{
	regs->a7 = sysno;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
        regs->a0 = regs->orig_a0;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->a0;

	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->a0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->a0 = (long) error ?: val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	args[0] = regs->orig_a0;
	args++;
	memcpy(args, &regs->a1, 5 * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	regs->orig_a0 = args[0];
	args++;
	memcpy(&regs->a1, args, 5 * sizeof(regs->a1));
}

static inline int syscall_get_arch(struct task_struct *task)
{
#ifdef CONFIG_64BIT
	return AUDIT_ARCH_RISCV64;
#else
	return AUDIT_ARCH_RISCV32;
#endif
}

#endif	/* _ASM_RISCV_SYSCALL_H */
