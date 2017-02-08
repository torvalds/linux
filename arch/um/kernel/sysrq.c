/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright (C) 2013 Richard Weinberger <richrd@nod.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>

#include <asm/sysrq.h>
#include <asm/stacktrace.h>
#include <os.h>

static void _print_addr(void *data, unsigned long address, int reliable)
{
	pr_info(" [<%08lx>]", address);
	pr_cont(" %s", reliable ? "" : "? ");
	print_symbol("%s", address);
	pr_cont("\n");
}

static const struct stacktrace_ops stackops = {
	.address = _print_addr
};

void show_stack(struct task_struct *task, unsigned long *stack)
{
	unsigned long *sp = stack;
	struct pt_regs *segv_regs = current->thread.segv_regs;
	int i;

	if (!segv_regs && os_is_signal_stack()) {
		pr_err("Received SIGSEGV in SIGSEGV handler,"
				" aborting stack trace!\n");
		return;
	}

	if (!stack)
		sp = get_stack_pointer(task, segv_regs);

	pr_info("Stack:\n");
	stack = sp;
	for (i = 0; i < 3 * STACKSLOTS_PER_LINE; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % STACKSLOTS_PER_LINE) == 0))
			pr_cont("\n");
		pr_cont(" %08lx", *stack++);
	}
	pr_cont("\n");

	pr_info("Call Trace:\n");
	dump_trace(current, &stackops, NULL);
	pr_info("\n");
}
