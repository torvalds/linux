/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M68K_SYSCALL_H
#define _ASM_M68K_SYSCALL_H

#include <uapi/linux/audit.h>

#include <asm/unistd.h>

extern const unsigned long sys_call_table[];

static inline int syscall_get_nr(struct task_struct *task,
				 struct pt_regs *regs)
{
	return regs->orig_d0;
}

static inline void syscall_set_nr(struct task_struct *task,
				  struct pt_regs *regs,
				  int nr)
{
	regs->orig_d0 = nr;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->d0 = regs->orig_d0;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->d0;

	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->d0;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->d0 = (long)error ?: val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	args[0] = regs->orig_d0;
	args++;

	memcpy(args, &regs->d1, 5 * sizeof(args[0]));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	regs->orig_d0 = args[0];
	args++;

	memcpy(&regs->d1, args, 5 * sizeof(args[0]));
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_M68K;
}

#endif	/* _ASM_M68K_SYSCALL_H */
