// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright (C) 2013 Richard Weinberger <richrd@nod.at>
 */

#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>

#include <asm/sysrq.h>
#include <asm/stacktrace.h>
#include <os.h>

static void _print_addr(void *data, unsigned long address, int reliable)
{
	const char *loglvl = data;

	printk("%s [<%08lx>] %s%pS\n", loglvl, address, reliable ? "" : "? ",
		(void *)address);
}

static const struct stacktrace_ops stackops = {
	.address = _print_addr
};

void show_stack(struct task_struct *task, unsigned long *stack,
		       const char *loglvl)
{
	struct pt_regs *segv_regs = current->thread.segv_regs;
	int i;

	if (!segv_regs && os_is_signal_stack()) {
		pr_err("Received SIGSEGV in SIGSEGV handler,"
				" aborting stack trace!\n");
		return;
	}

	if (!stack)
		stack = get_stack_pointer(task, segv_regs);

	printk("%sStack:\n", loglvl);
	for (i = 0; i < 3 * STACKSLOTS_PER_LINE; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % STACKSLOTS_PER_LINE) == 0))
			printk("%s\n", loglvl);
		pr_cont(" %08lx", *stack++);
	}
	printk("%s\n", loglvl);

	printk("%sCall Trace:\n", loglvl);
	dump_trace(current, &stackops, (void *)loglvl);
	printk("%s\n", loglvl);
}
