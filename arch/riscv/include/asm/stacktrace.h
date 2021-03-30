/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_STACKTRACE_H
#define _ASM_RISCV_STACKTRACE_H

#include <linux/sched.h>
#include <asm/ptrace.h>

struct stackframe {
	unsigned long fp;
	unsigned long ra;
};

extern void notrace walk_stackframe(struct task_struct *task, struct pt_regs *regs,
				    bool (*fn)(void *, unsigned long), void *arg);

#endif /* _ASM_RISCV_STACKTRACE_H */
