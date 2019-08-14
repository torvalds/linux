// SPDX-License-Identifier: GPL-2.0
/*
 * Stack trace management functions
 *
 *  Copyright IBM Corp. 2006
 *  Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/stacktrace.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	struct unwind_state state;
	unsigned long addr;

	unwind_for_each_frame(&state, task, regs, 0) {
		addr = unwind_get_return_address(&state);
		if (!addr || !consume_entry(cookie, addr, false))
			break;
	}
}
