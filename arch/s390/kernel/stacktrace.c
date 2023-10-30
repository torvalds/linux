// SPDX-License-Identifier: GPL-2.0
/*
 * Stack trace management functions
 *
 *  Copyright IBM Corp. 2006
 */

#include <linux/stacktrace.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>
#include <asm/kprobes.h>
#include <asm/ptrace.h>

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	struct unwind_state state;
	unsigned long addr;

	unwind_for_each_frame(&state, task, regs, 0) {
		addr = unwind_get_return_address(&state);
		if (!addr || !consume_entry(cookie, addr))
			break;
	}
}

int arch_stack_walk_reliable(stack_trace_consume_fn consume_entry,
			     void *cookie, struct task_struct *task)
{
	struct unwind_state state;
	unsigned long addr;

	unwind_for_each_frame(&state, task, NULL, 0) {
		if (state.stack_info.type != STACK_TYPE_TASK)
			return -EINVAL;

		if (state.regs)
			return -EINVAL;

		addr = unwind_get_return_address(&state);
		if (!addr)
			return -EINVAL;

#ifdef CONFIG_RETHOOK
		/*
		 * Mark stacktraces with krethook functions on them
		 * as unreliable.
		 */
		if (state.ip == (unsigned long)arch_rethook_trampoline)
			return -EINVAL;
#endif

		if (!consume_entry(cookie, addr))
			return -EINVAL;
	}

	/* Check for stack corruption */
	if (unwind_error(&state))
		return -EINVAL;
	return 0;
}

void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs)
{
	struct stack_frame_user __user *sf;
	unsigned long ip, sp;
	bool first = true;

	if (is_compat_task())
		return;
	if (!consume_entry(cookie, instruction_pointer(regs)))
		return;
	sf = (void __user *)user_stack_pointer(regs);
	pagefault_disable();
	while (1) {
		if (__get_user(sp, &sf->back_chain))
			break;
		if (__get_user(ip, &sf->gprs[8]))
			break;
		if (ip & 0x1) {
			/*
			 * If the instruction address is invalid, and this
			 * is the first stack frame, assume r14 has not
			 * been written to the stack yet. Otherwise exit.
			 */
			if (first && !(regs->gprs[14] & 0x1))
				ip = regs->gprs[14];
			else
				break;
		}
		if (!consume_entry(cookie, ip))
			break;
		/* Sanity check: ABI requires SP to be aligned 8 bytes. */
		if (!sp || sp & 0x7)
			break;
		sf = (void __user *)sp;
		first = false;
	}
	pagefault_enable();
}
