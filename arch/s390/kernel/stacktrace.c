// SPDX-License-Identifier: GPL-2.0
/*
 * Stack trace management functions
 *
 *  Copyright IBM Corp. 2006
 */

#include <linux/perf_event.h>
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

static inline bool store_ip(stack_trace_consume_fn consume_entry, void *cookie,
			    struct perf_callchain_entry_ctx *entry, bool perf,
			    unsigned long ip)
{
#ifdef CONFIG_PERF_EVENTS
	if (perf) {
		if (perf_callchain_store(entry, ip))
			return false;
		return true;
	}
#endif
	return consume_entry(cookie, ip);
}

void arch_stack_walk_user_common(stack_trace_consume_fn consume_entry, void *cookie,
				 struct perf_callchain_entry_ctx *entry,
				 const struct pt_regs *regs, bool perf)
{
	struct stack_frame_user __user *sf;
	unsigned long ip, sp;
	bool first = true;

	if (is_compat_task())
		return;
	ip = instruction_pointer(regs);
	if (!store_ip(consume_entry, cookie, entry, perf, ip))
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
		if (!store_ip(consume_entry, cookie, entry, perf, ip))
			return;
		/* Sanity check: ABI requires SP to be aligned 8 bytes. */
		if (!sp || sp & 0x7)
			break;
		sf = (void __user *)sp;
		first = false;
	}
	pagefault_enable();
}

void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs)
{
	arch_stack_walk_user_common(consume_entry, cookie, NULL, regs, false);
}

unsigned long return_address(unsigned int n)
{
	struct unwind_state state;
	unsigned long addr;

	/* Increment to skip current stack entry */
	n++;

	unwind_for_each_frame(&state, NULL, NULL, 0) {
		addr = unwind_get_return_address(&state);
		if (!addr)
			break;
		if (!n--)
			return addr;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(return_address);
