/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __UM_PTRACE_GENERIC_H
#define __UM_PTRACE_GENERIC_H

#ifndef __ASSEMBLER__

#include <sysdep/ptrace.h>

struct pt_regs {
	struct uml_pt_regs regs;
};

#define arch_has_single_step()	(1)

#define EMPTY_REGS { .regs = EMPTY_UML_PT_REGS }

#define PT_REGS_IP(r) UPT_IP(&(r)->regs)
#define PT_REGS_SP(r) UPT_SP(&(r)->regs)

#define PT_REGS_RESTART_SYSCALL(r) UPT_RESTART_SYSCALL(&(r)->regs)

#define PT_REGS_SYSCALL_NR(r) UPT_SYSCALL_NR(&(r)->regs)

#define instruction_pointer(regs) PT_REGS_IP(regs)

#define PTRACE_OLDSETOPTIONS 21

struct task_struct;

extern long subarch_ptrace(struct task_struct *child, long request,
	unsigned long addr, unsigned long data);
extern unsigned long getreg(struct task_struct *child, int regno);
extern int putreg(struct task_struct *child, int regno, unsigned long value);

extern int poke_user(struct task_struct *child, long addr, long data);
extern int peek_user(struct task_struct *child, long addr, long data);

extern int arch_set_tls(struct task_struct *new, unsigned long tls);
extern void clear_flushed_tls(struct task_struct *task);
extern int syscall_trace_enter(struct pt_regs *regs);
extern void syscall_trace_leave(struct pt_regs *regs);

#endif

#endif
