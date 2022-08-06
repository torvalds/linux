// SPDX-License-Identifier: GPL-2.0
/*
 * Stack trace management functions
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>

#include <asm/stacktrace.h>
#include <asm/unwind.h>

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	unsigned long addr;
	struct pt_regs dummyregs;
	struct unwind_state state;

	regs = &dummyregs;

	if (task == current) {
		regs->regs[3] = (unsigned long)__builtin_frame_address(0);
		regs->csr_era = (unsigned long)__builtin_return_address(0);
	} else {
		regs->regs[3] = thread_saved_fp(task);
		regs->csr_era = thread_saved_ra(task);
	}

	regs->regs[1] = 0;
	for (unwind_start(&state, task, regs);
	      !unwind_done(&state); unwind_next_frame(&state)) {
		addr = unwind_get_return_address(&state);
		if (!addr || !consume_entry(cookie, addr))
			break;
	}
}
