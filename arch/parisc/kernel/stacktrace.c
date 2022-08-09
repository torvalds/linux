// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack trace management functions
 *
 *  Copyright (C) 2009-2021 Helge Deller <deller@gmx.de>
 *  based on arch/x86/kernel/stacktrace.c by Ingo Molnar <mingo@redhat.com>
 *  and parisc unwind functions by Randolph Chung <tausq@debian.org>
 *
 *  TODO: Userspace stacktrace (CONFIG_USER_STACKTRACE_SUPPORT)
 */
#include <linux/kernel.h>
#include <linux/stacktrace.h>

#include <asm/unwind.h>

static void notrace walk_stackframe(struct task_struct *task,
	struct pt_regs *regs, bool (*fn)(void *, unsigned long), void *cookie)
{
	struct unwind_frame_info info;

	unwind_frame_init_task(&info, task, NULL);
	while (1) {
		if (unwind_once(&info) < 0 || info.ip == 0)
			break;

		if (__kernel_text_address(info.ip))
			if (!fn(cookie, info.ip))
				break;
	}
}

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	walk_stackframe(task, regs, consume_entry, cookie);
}

int arch_stack_walk_reliable(stack_trace_consume_fn consume_entry, void *cookie,
			     struct task_struct *task)
{
	walk_stackframe(task, NULL, consume_entry, cookie);
	return 1;
}
