/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_SYSCALL_H
#define __ASM_SYSCALL_H

#include <linux/sched.h>
#include <linux/err.h>
#include <abi/regdef.h>
#include <uapi/linux/audit.h>

static inline int
syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs_syscallid(regs);
}

static inline void
syscall_rollback(struct task_struct *task, struct pt_regs *regs)
{
	regs->a0 = regs->orig_a0;
}

static inline long
syscall_get_error(struct task_struct *task, struct pt_regs *regs)
{
	unsigned long error = regs->a0;

	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long
syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return regs->a0;
}

static inline void
syscall_set_return_value(struct task_struct *task, struct pt_regs *regs,
		int error, long val)
{
	regs->a0 = (long) error ?: val;
}

static inline void
syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned int i, unsigned int n, unsigned long *args)
{
	BUG_ON(i + n > 6);
	if (i == 0) {
		args[0] = regs->orig_a0;
		args++;
		n--;
	} else {
		i--;
	}
	memcpy(args, &regs->a1 + i, n * sizeof(args[0]));
}

static inline void
syscall_set_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned int i, unsigned int n, const unsigned long *args)
{
	BUG_ON(i + n > 6);
	if (i == 0) {
		regs->orig_a0 = args[0];
		args++;
		n--;
	} else {
		i--;
	}
	memcpy(&regs->a1 + i, args, n * sizeof(regs->a1));
}

static inline int
syscall_get_arch(void)
{
	return AUDIT_ARCH_CSKY;
}

#endif	/* __ASM_SYSCALL_H */
