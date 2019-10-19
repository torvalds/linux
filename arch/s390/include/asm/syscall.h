/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Access to user system call parameters and results
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H	1

#include <uapi/linux/audit.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <asm/ptrace.h>

extern const unsigned long sys_call_table[];
extern const unsigned long sys_call_table_emu[];

static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return test_pt_regs_flag(regs, PIF_SYSCALL) ?
		(regs->int_code & 0xffff) : -1;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->gprs[2] = regs->orig_gpr2;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->gprs[2]) ? regs->gprs[2] : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->gprs[2];
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->gprs[2] = error ? error : val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	unsigned long mask = -1UL;
	unsigned int n = 6;

#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT))
		mask = 0xffffffff;
#endif
	while (n-- > 0)
		if (n > 0)
			args[n] = regs->gprs[2 + n] & mask;

	args[0] = regs->orig_gpr2 & mask;
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	unsigned int n = 6;

	while (n-- > 0)
		if (n > 0)
			regs->gprs[2 + n] = args[n];
	regs->orig_gpr2 = args[0];
}

static inline int syscall_get_arch(struct task_struct *task)
{
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT))
		return AUDIT_ARCH_S390;
#endif
	return AUDIT_ARCH_S390X;
}
#endif	/* _ASM_SYSCALL_H */
