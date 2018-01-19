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
		      unsigned int i, unsigned int n, unsigned long *args)
{
	BUG_ON(i + n > 6);

	while (n > 0) {
		switch (i) {
		case 0:
			*args++ = regs->er1;
			break;
		case 1:
			*args++ = regs->er2;
			break;
		case 2:
			*args++ = regs->er3;
			break;
		case 3:
			*args++ = regs->er4;
			break;
		case 4:
			*args++ = regs->er5;
			break;
		case 5:
			*args++ = regs->er6;
			break;
		}
		i++;
		n--;
	}
}



/* Misc syscall related bits */
asmlinkage long do_syscall_trace_enter(struct pt_regs *regs);
asmlinkage void do_syscall_trace_leave(struct pt_regs *regs);

#endif /* __KERNEL__ */
#endif /* __ASM_H8300_SYSCALLS_32_H */
