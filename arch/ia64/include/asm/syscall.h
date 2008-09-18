/*
 * Access to user system call parameters and results
 *
 * Copyright (C) 2008 Intel Corp.  Shaohua Li <shaohua.li@intel.com>
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */

#ifndef _ASM_SYSCALL_H
#define _ASM_SYSCALL_H	1

#include <linux/sched.h>
#include <linux/err.h>

static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	if ((long)regs->cr_ifs < 0) /* Not a syscall */
		return -1;

#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(regs))
		return regs->r1;
#endif

	return regs->r15;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(regs))
		regs->r8 = regs->r1;
#endif

	/* do nothing */
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(regs))
		return regs->r8;
#endif

	return regs->r10 == -1 ? regs->r8:0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->r8;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(regs)) {
		regs->r8 = (long) error ? error : val;
		return;
	}
#endif

	if (error) {
		/* error < 0, but ia64 uses > 0 return value */
		regs->r8 = -error;
		regs->r10 = -1;
	} else {
		regs->r8 = val;
		regs->r10 = 0;
	}
}

extern void ia64_syscall_get_set_arguments(struct task_struct *task,
	struct pt_regs *regs, unsigned int i, unsigned int n,
	unsigned long *args, int rw);
static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	BUG_ON(i + n > 6);

#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(regs)) {
		switch (i + n) {
		case 6:
			if (!n--) break;
			*args++ = regs->r13;
		case 5:
			if (!n--) break;
			*args++ = regs->r15;
		case 4:
			if (!n--) break;
			*args++ = regs->r14;
		case 3:
			if (!n--) break;
			*args++ = regs->r10;
		case 2:
			if (!n--) break;
			*args++ = regs->r9;
		case 1:
			if (!n--) break;
			*args++ = regs->r11;
		case 0:
			if (!n--) break;
		default:
			BUG();
			break;
		}

		return;
	}
#endif
	ia64_syscall_get_set_arguments(task, regs, i, n, args, 0);
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	BUG_ON(i + n > 6);

#ifdef CONFIG_IA32_SUPPORT
	if (IS_IA32_PROCESS(regs)) {
		switch (i + n) {
		case 6:
			if (!n--) break;
			regs->r13 = *args++;
		case 5:
			if (!n--) break;
			regs->r15 = *args++;
		case 4:
			if (!n--) break;
			regs->r14 = *args++;
		case 3:
			if (!n--) break;
			regs->r10 = *args++;
		case 2:
			if (!n--) break;
			regs->r9 = *args++;
		case 1:
			if (!n--) break;
			regs->r11 = *args++;
		case 0:
			if (!n--) break;
		}

		return;
	}
#endif
	ia64_syscall_get_set_arguments(task, regs, i, n, args, 1);
}
#endif	/* _ASM_SYSCALL_H */
