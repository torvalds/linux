/*
 *  linux/arch/arm/kernel/ptrace.h
 *
 *  Copyright (C) 2000-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
extern void ptrace_cancel_bpt(struct task_struct *);
extern void ptrace_set_bpt(struct task_struct *);
extern void ptrace_break(struct task_struct *, struct pt_regs *);
