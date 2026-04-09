/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_SYSCALL_H
#define _ASM_ALPHA_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/audit.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <asm/ptrace.h>

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_ALPHA;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->r19 ? -(long)regs->r0 : (long)regs->r0;
}

/*
 * Alpha syscall ABI / kernel conventions:
 *  - PAL provides syscall number in r0 on entry.
 *  - The kernel tracks the active syscall number in regs->r1 (mutable) and
 *    preserves the original syscall number in regs->r2 for rollback/restart.
 *  - Return value is in regs->r0, with regs->r19 ("a3") as the error flag
 *    (0=success, 1=error; on error regs->r0 holds positive errno).
 */

static inline long syscall_get_nr(struct task_struct *task,
				struct pt_regs *regs)
{
	return (long)regs->r1;
}

static inline void syscall_set_nr(struct task_struct *task,
				struct pt_regs *regs,
				long nr)
{
	regs->r1 = (unsigned long)nr;
}

/*
 * Syscall arguments:
 *   regs->r16..regs->r21 carry up to 6 syscall arguments on entry.
 *   Note: regs->r19 is also used as "a3" (error flag) on syscall return.
 */

static inline void syscall_get_arguments(struct task_struct *task,
					struct pt_regs *regs,
					unsigned long *args)
{
	args[0] = regs->r16;
	args[1] = regs->r17;
	args[2] = regs->r18;
	args[3] = regs->r19;
	args[4] = regs->r20;
	args[5] = regs->r21;
}

static inline void syscall_set_arguments(struct task_struct *task,
					struct pt_regs *regs,
					const unsigned long *args)
{
	regs->r16 = args[0];
	regs->r17 = args[1];
	regs->r18 = args[2];
	regs->r19 = args[3];
	regs->r20 = args[4];
	regs->r21 = args[5];
}
/*
 * Set return value for a syscall.
 * Alpha uses r0 for return value and r19 ("a3") as the error indicator:
 *   a3 = 0 => success
 *   a3 = 1 => error, and userspace interprets r0 as errno (positive).
 *
 * The kernel reports errors to userspace by setting a3=1 and placing a
 * positive errno value in r0. Some syscall paths do this in entry.S,
 * while others (e.g. seccomp/ptrace helpers) use syscall_set_return_value().
 */

static inline void syscall_set_return_value(struct task_struct *task,
					struct pt_regs *regs,
					int error, long val)
{

	if (error) {
		/* error is negative errno in this tree */
		regs->r0  = (unsigned long)(-error);  /* positive errno */
		regs->r19 = 1;                        /* a3 = error */
	} else {
		regs->r0  = (unsigned long)val;
		regs->r19 = 0;                        /* a3 = success */
	}
}

/* Restore the original syscall nr after seccomp/ptrace modified regs->r1. */
static inline void syscall_rollback(struct task_struct *task,
					struct pt_regs *regs)
{
	regs->r1 = regs->r2;
}

#endif	/* _ASM_ALPHA_SYSCALL_H */
