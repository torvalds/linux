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
extern void dump_backtrace(struct pt_regs *regs, struct task_struct *task,
			   const char *loglvl);

static inline bool on_thread_stack(void)
{
	return !(((unsigned long)(current->stack) ^ current_stack_pointer) & ~(THREAD_SIZE - 1));
}


#ifdef CONFIG_VMAP_STACK
DECLARE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack);
#endif /* CONFIG_VMAP_STACK */

#endif /* _ASM_RISCV_STACKTRACE_H */
