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

/*
 * The syscall table always contains 32 bit pointers since we know that the
 * address of the function to be called is (way) below 4GB.  So the "int"
 * type here is what we want [need] for both 32 bit and 64 bit systems.
 */
extern const unsigned int sys_call_table[];
extern const unsigned int sys_call_table_emu[];

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
	unsigned long error = regs->gprs[2];
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT)) {
		/*
		 * Sign-extend the value so (int)-EFOO becomes (long)-EFOO
		 * and will match correctly in comparisons.
		 */
		error = (long)(int)error;
	}
#endif
	return IS_ERR_VALUE(error) ? error : 0;
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
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	unsigned long mask = -1UL;

	/*
	 * No arguments for this syscall, there's nothing to do.
	 */
	if (!n)
		return;

	BUG_ON(i + n > 6);
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT))
		mask = 0xffffffff;
#endif
	while (n-- > 0)
		if (i + n > 0)
			args[n] = regs->gprs[2 + i + n] & mask;
	if (i == 0)
		args[0] = regs->orig_gpr2 & mask;
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	BUG_ON(i + n > 6);
	while (n-- > 0)
		if (i + n > 0)
			regs->gprs[2 + i + n] = args[n];
	if (i == 0)
		regs->orig_gpr2 = args[0];
}

static inline int syscall_get_arch(void)
{
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(current, TIF_31BIT))
		return AUDIT_ARCH_S390;
#endif
	return AUDIT_ARCH_S390X;
}
#endif	/* _ASM_SYSCALL_H */
