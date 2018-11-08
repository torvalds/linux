// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/ptrace.h>

int kstack_depth_to_print = 48;

void show_trace(unsigned long *stack)
{
	unsigned long *endstack;
	unsigned long addr;
	int i;

	pr_info("Call Trace:\n");
	addr = (unsigned long)stack + THREAD_SIZE - 1;
	endstack = (unsigned long *)(addr & -THREAD_SIZE);
	i = 0;
	while (stack + 1 <= endstack) {
		addr = *stack++;
		/*
		 * If the address is either in the text segment of the
		 * kernel, or in the region which contains vmalloc'ed
		 * memory, it *may* be the address of a calling
		 * routine; if so, print it so that someone tracing
		 * down the cause of the crash will be able to figure
		 * out the call path that was taken.
		 */
		if (__kernel_text_address(addr)) {
#ifndef CONFIG_KALLSYMS
			if (i % 5 == 0)
				pr_cont("\n       ");
#endif
			pr_cont(" [<%08lx>] %pS\n", addr, (void *)addr);
			i++;
		}
	}
	pr_cont("\n");
}

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long *p;
	unsigned long *endstack;
	int i;

	if (!stack) {
		if (task)
			stack = (unsigned long *)task->thread.esp0;
		else
			stack = (unsigned long *)&stack;
	}
	endstack = (unsigned long *)
		(((unsigned long)stack + THREAD_SIZE - 1) & -THREAD_SIZE);

	pr_info("Stack from %08lx:", (unsigned long)stack);
	p = stack;
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (p + 1 > endstack)
			break;
		if (i % 8 == 0)
			pr_cont("\n       ");
		pr_cont(" %08lx", *p++);
	}
	pr_cont("\n");
	show_trace(stack);
}
