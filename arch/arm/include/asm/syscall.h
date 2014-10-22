/*
 * Access to user system call parameters and results
 *
 * Copyright (C) 2012 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */
#ifndef _ASM_ARM_SYSCALL_H
#define _ASM_ARM_SYSCALL_H

#include <linux/audit.h> /* for AUDIT_ARCH_* */
#include <linux/elf.h> /* for ELF_EM */
#include <linux/sched.h>
#include <linux/thread_info.h> /* for task_thread_info */
#include <linux/err.h>

static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return task_thread_info(task)->syscall;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->ARM_r0 = regs->ARM_ORIG_r0;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->ARM_r0;
	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->ARM_r0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->ARM_r0 = (long) error ?: val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	BUG_ON(i + n > 6);
	memcpy(args, &regs->ARM_r0 + i, n * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	BUG_ON(i + n > 6);
	memcpy(&regs->ARM_r0 + i, args, n * sizeof(args[0]));
}

static inline int syscall_get_arch(struct task_struct *task,
				    struct pt_regs *regs)
{
	/* ARM tasks don't change audit architectures on the fly. */
#ifdef __ARMEB__
	return AUDIT_ARCH_ARMEB;
#else
	return AUDIT_ARCH_ARM;
#endif
}
#endif	/* _ASM_ARM_SYSCALL_H */
