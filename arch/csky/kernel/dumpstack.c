// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/ptrace.h>

int kstack_depth_to_print = 48;

void show_trace(unsigned long *stack)
{
	unsigned long *stack_end;
	unsigned long *stack_start;
	unsigned long *fp;
	unsigned long addr;

	addr = (unsigned long) stack & THREAD_MASK;
	stack_start = (unsigned long *) addr;
	stack_end = (unsigned long *) (addr + THREAD_SIZE);

	fp = stack;
	pr_info("\nCall Trace:");

	while (fp > stack_start && fp < stack_end) {
#ifdef CONFIG_STACKTRACE
		addr	= fp[1];
		fp	= (unsigned long *) fp[0];
#else
		addr	= *fp++;
#endif
		if (__kernel_text_address(addr))
			pr_cont("\n[<%08lx>] %pS", addr, (void *)addr);
	}
	pr_cont("\n");
}

void show_stack(struct task_struct *task, unsigned long *stack)
{
	if (!stack) {
		if (task)
			stack = (unsigned long *)thread_saved_fp(task);
		else
#ifdef CONFIG_STACKTRACE
			asm volatile("mov %0, r8\n":"=r"(stack)::"memory");
#else
			stack = (unsigned long *)&stack;
#endif
	}

	show_trace(stack);
}
