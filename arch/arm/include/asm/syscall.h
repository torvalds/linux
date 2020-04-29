/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Access to user system call parameters and results
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */

#ifndef _ASM_ARM_SYSCALL_H
#define _ASM_ARM_SYSCALL_H

#include <uapi/linux/audit.h> /* for AUDIT_ARCH_* */
#include <linux/elf.h> /* for ELF_EM */
#include <linux/err.h>
#include <linux/sched.h>

#include <asm/unistd.h>

#define NR_syscalls (__NR_syscalls)

extern const unsigned long sys_call_table[];

static inline int syscall_get_nr(struct task_struct *task,
				 struct pt_regs *regs)
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
	regs->ARM_r0 = (long) error ? error : val;
}

#define SYSCALL_MAX_ARGS 7

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	args[0] = regs->ARM_ORIG_r0;
	args++;

	memcpy(args, &regs->ARM_r0 + 1, 5 * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	regs->ARM_ORIG_r0 = args[0];
	args++;

	memcpy(&regs->ARM_r0 + 1, args, 5 * sizeof(args[0]));
}

static inline int syscall_get_arch(struct task_struct *task)
{
	/* ARM tasks don't change audit architectures on the fly. */
	return AUDIT_ARCH_ARM;
}

#endif /* _ASM_ARM_SYSCALL_H */
