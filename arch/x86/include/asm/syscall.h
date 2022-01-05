/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Access to user system call parameters and results
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.  All rights reserved.
 *
 * See asm-generic/syscall.h for descriptions of what we must do here.
 */

#ifndef _ASM_X86_SYSCALL_H
#define _ASM_X86_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <asm/thread_info.h>	/* for TS_COMPAT */
#include <asm/unistd.h>

typedef long (*sys_call_ptr_t)(const struct pt_regs *);
extern const sys_call_ptr_t sys_call_table[];

#if defined(CONFIG_X86_32)
#define ia32_sys_call_table sys_call_table
#else
/*
 * These may not exist, but still put the prototypes in so we
 * can use IS_ENABLED().
 */
extern const sys_call_ptr_t ia32_sys_call_table[];
extern const sys_call_ptr_t x32_sys_call_table[];
#endif

/*
 * Only the low 32 bits of orig_ax are meaningful, so we return int.
 * This importantly ignores the high bits on 64-bit, so comparisons
 * sign-extend the low 32 bits.
 */
static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->orig_ax;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->ax = regs->orig_ax;
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	unsigned long error = regs->ax;
#ifdef CONFIG_IA32_EMULATION
	/*
	 * TS_COMPAT is set for 32-bit syscall entries and then
	 * remains set until we return to user mode.
	 */
	if (task->thread_info.status & (TS_COMPAT|TS_I386_REGS_POKED))
		/*
		 * Sign-extend the value so (int)-EFOO becomes (long)-EFOO
		 * and will match correctly in comparisons.
		 */
		error = (long) (int) error;
#endif
	return IS_ERR_VALUE(error) ? error : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->ax;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	regs->ax = (long) error ?: val;
}

#ifdef CONFIG_X86_32

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	memcpy(args, &regs->bx, 6 * sizeof(args[0]));
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_I386;
}

#else	 /* CONFIG_X86_64 */

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
# ifdef CONFIG_IA32_EMULATION
	if (task->thread_info.status & TS_COMPAT) {
		*args++ = regs->bx;
		*args++ = regs->cx;
		*args++ = regs->dx;
		*args++ = regs->si;
		*args++ = regs->di;
		*args   = regs->bp;
	} else
# endif
	{
		*args++ = regs->di;
		*args++ = regs->si;
		*args++ = regs->dx;
		*args++ = regs->r10;
		*args++ = regs->r8;
		*args   = regs->r9;
	}
}

static inline int syscall_get_arch(struct task_struct *task)
{
	/* x32 tasks should be considered AUDIT_ARCH_X86_64. */
	return (IS_ENABLED(CONFIG_IA32_EMULATION) &&
		task->thread_info.status & TS_COMPAT)
		? AUDIT_ARCH_I386 : AUDIT_ARCH_X86_64;
}

void do_syscall_64(struct pt_regs *regs, int nr);
void do_int80_syscall_32(struct pt_regs *regs);
long do_fast_syscall_32(struct pt_regs *regs);

#endif	/* CONFIG_X86_32 */

#endif	/* _ASM_X86_SYSCALL_H */
