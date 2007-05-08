/*
 *  linux/arch/arm/kernel/ptrace.h
 *
 *  Copyright (C) 2000-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/ptrace.h>

extern void ptrace_cancel_bpt(struct task_struct *);
extern void ptrace_set_bpt(struct task_struct *);
extern void ptrace_break(struct task_struct *, struct pt_regs *);

/*
 * make sure single-step breakpoint is gone.
 */
static inline void single_step_disable(struct task_struct *task)
{
	task->ptrace &= ~PT_SINGLESTEP;
	ptrace_cancel_bpt(task);
}

static inline void single_step_enable(struct task_struct *task)
{
	task->ptrace |= PT_SINGLESTEP;
}

/*
 * Send SIGTRAP if we're single-stepping
 */
static inline void single_step_trap(struct task_struct *task)
{
	if (task->ptrace & PT_SINGLESTEP) {
		ptrace_cancel_bpt(task);
		send_sig(SIGTRAP, task, 1);
	}
}

static inline void single_step_clear(struct task_struct *task)
{
	if (task->ptrace & PT_SINGLESTEP)
		ptrace_cancel_bpt(task);
}

static inline void single_step_set(struct task_struct *task)
{
	if (task->ptrace & PT_SINGLESTEP)
		ptrace_set_bpt(task);
}
