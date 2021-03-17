/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_SYSCALL_H
#define __ASM_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/compat.h>
#include <linux/err.h>

typedef long (*syscall_fn_t)(const struct pt_regs *regs);

extern const syscall_fn_t sys_call_table[];

#ifdef CONFIG_COMPAT
extern const syscall_fn_t compat_sys_call_table[];
#endif

static inline int syscall_get_nr(struct task_struct *task,
				 struct pt_regs *regs)
{
	return regs->syscallno;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->regs[0] = regs->orig_x0;
}


static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->regs[0];

	if (is_compat_thread(task_thread_info(task)))
		error = sign_extend64(error, 31);

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
	if (error)
		val = error;

	if (is_compat_thread(task_thread_info(task)))
		val = lower_32_bits(val);

	regs->regs[0] = val;
}

#define SYSCALL_MAX_ARGS 6

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	args[0] = regs->orig_x0;
	args++;

	memcpy(args, &regs->regs[1], 5 * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	regs->orig_x0 = args[0];
	args++;

	memcpy(&regs->regs[1], args, 5 * sizeof(args[0]));
}

/*
 * We don't care about endianness (__AUDIT_ARCH_LE bit) here because
 * AArch64 has the same system calls both on little- and big- endian.
 */
static inline int syscall_get_arch(struct task_struct *task)
{
	if (is_compat_thread(task_thread_info(task)))
		return AUDIT_ARCH_ARM;

	return AUDIT_ARCH_AARCH64;
}

#endif	/* __ASM_SYSCALL_H */
