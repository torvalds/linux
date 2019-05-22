/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_SYSCALL_64_H
#define __ASM_SH_SYSCALL_64_H

#include <uapi/linux/audit.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/ptrace.h>

/* The system call number is given by the user in R9 */
static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return (regs->syscall_nr >= 0) ? regs->regs[9] : -1L;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/*
	 * XXX: This needs some thought. On SH we don't
	 * save away the original R9 value anywhere.
	 */
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->regs[9]) ? regs->regs[9] : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->regs[9];
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	if (error)
		regs->regs[9] = -error;
	else
		regs->regs[9] = val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	memcpy(args, &regs->regs[2], 6 * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	memcpy(&regs->regs[2], args, 6 * sizeof(args[0]));
}

static inline int syscall_get_arch(struct task_struct *task)
{
	int arch = AUDIT_ARCH_SH;

#ifdef CONFIG_64BIT
	arch |= __AUDIT_ARCH_64BIT;
#endif
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	arch |= __AUDIT_ARCH_LE;
#endif

	return arch;
}
#endif /* __ASM_SH_SYSCALL_64_H */
