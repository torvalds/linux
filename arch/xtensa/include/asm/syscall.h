/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2007 Tensilica Inc.
 * Copyright (C) 2018 Cadence Design Systems Inc.
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H

#include <linux/err.h>
#include <asm/ptrace.h>
#include <uapi/linux/audit.h>

static inline int syscall_get_arch(void)
{
	return AUDIT_ARCH_XTENSA;
}

typedef void (*syscall_t)(void);
extern syscall_t sys_call_table[];

static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	return regs->syscall;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/* Do nothing. */
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	/* 0 if syscall succeeded, otherwise -Errorcode */
	return IS_ERR_VALUE(regs->areg[2]) ? regs->areg[2] : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->areg[2];
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->areg[0] = (long) error ? error : val;
}

#define SYSCALL_MAX_ARGS 6
#define XTENSA_SYSCALL_ARGUMENT_REGS {6, 3, 4, 5, 8, 9}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	static const unsigned int reg[] = XTENSA_SYSCALL_ARGUMENT_REGS;
	unsigned int i;

	for (i = 0; i < 6; ++i)
		args[i] = regs->areg[reg[i]];
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	static const unsigned int reg[] = XTENSA_SYSCALL_ARGUMENT_REGS;
	unsigned int i;

	for (i = 0; i < 6; ++i)
		regs->areg[reg[i]] = args[i];
}

asmlinkage long xtensa_rt_sigreturn(struct pt_regs*);
asmlinkage long xtensa_shmat(int, char __user *, int);
asmlinkage long xtensa_fadvise64_64(int, int,
				    unsigned long long, unsigned long long);

#endif
