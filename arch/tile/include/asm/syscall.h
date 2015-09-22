/*
 * Copyright (C) 2008-2009 Red Hat, Inc.  All rights reserved.
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */

#ifndef _ASM_TILE_SYSCALL_H
#define _ASM_TILE_SYSCALL_H

#include <linux/sched.h>
#include <linux/err.h>
#include <linux/audit.h>
#include <linux/compat.h>
#include <arch/abi.h>

/* The array of function pointers for syscalls. */
extern void *sys_call_table[];
#ifdef CONFIG_COMPAT
extern void *compat_sys_call_table[];
#endif

/*
 * Only the low 32 bits of orig_r0 are meaningful, so we return int.
 * This importantly ignores the high bits on 64-bit, so comparisons
 * sign-extend the low 32 bits.
 */
static inline int syscall_get_nr(struct task_struct *t, struct pt_regs *regs)
{
	return regs->regs[TREG_SYSCALL_NR];
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->regs[0] = regs->orig_r0;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->regs[0];
	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->regs[0];
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	if (error) {
		/* R0 is the passed-in negative error, R1 is positive. */
		regs->regs[0] = error;
		regs->regs[1] = -error;
	} else {
		/* R1 set to zero to indicate no error. */
		regs->regs[0] = val;
		regs->regs[1] = 0;
	}
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	BUG_ON(i + n > 6);
	memcpy(args, &regs[i], n * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	BUG_ON(i + n > 6);
	memcpy(&regs[i], args, n * sizeof(args[0]));
}

/*
 * We don't care about endianness (__AUDIT_ARCH_LE bit) here because
 * tile has the same system calls both on little- and big- endian.
 */
static inline int syscall_get_arch(void)
{
	if (is_compat_task())
		return AUDIT_ARCH_TILEGX32;

#ifdef CONFIG_TILEGX
	return AUDIT_ARCH_TILEGX;
#else
	return AUDIT_ARCH_TILEPRO;
#endif
}

#endif	/* _ASM_TILE_SYSCALL_H */
