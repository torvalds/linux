/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_H8300_SYSCALLS_32_H
#define __ASM_H8300_SYSCALLS_32_H

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/ptrace.h>

static inline int
syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->orig_er0;
}

static inline void
syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned long *args)
{
	*args++ = regs->er1;
	*args++ = regs->er2;
	*args++ = regs->er3;
	*args++ = regs->er4;
	*args++ = regs->er5;
	*args   = regs->er6;
}



/* Misc syscall related bits */
asmlinkage long do_syscall_trace_enter(struct pt_regs *regs);
asmlinkage void do_syscall_trace_leave(struct pt_regs *regs);

#endif /* __KERNEL__ */
#endif /* __ASM_H8300_SYSCALLS_32_H */
