/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_SYSCALL_32_H
#define __ASM_SH_SYSCALL_32_H

#include <uapi/linux/audit.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <asm/ptrace.h>

/* The system call number is given by the user in R3 */
static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return (regs->tra >= 0) ? regs->regs[3] : -1L;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/*
	 * XXX: This needs some thought. On SH we don't
	 * save away the original r0 value anywhere.
	 */
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->regs[0]) ? regs->regs[0] : 0;
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
		regs->regs[0] = -error;
	else
		regs->regs[0] = val;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{

	/* Argument pattern is: R4, R5, R6, R7, R0, R1 */
	args[5] = regs->regs[1];
	args[4] = regs->regs[0];
	args[3] = regs->regs[7];
	args[2] = regs->regs[6];
	args[1] = regs->regs[5];
	args[0] = regs->regs[4];
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	regs->regs[1] = args[5];
	regs->regs[0] = args[4];
	regs->regs[7] = args[3];
	regs->regs[6] = args[2];
	regs->regs[5] = args[1];
	regs->regs[4] = args[0];
}

static inline int syscall_get_arch(void)
{
	int arch = AUDIT_ARCH_SH;

#ifdef CONFIG_CPU_LITTLE_ENDIAN
	arch |= __AUDIT_ARCH_LE;
#endif
	return arch;
}
#endif /* __ASM_SH_SYSCALL_32_H */
