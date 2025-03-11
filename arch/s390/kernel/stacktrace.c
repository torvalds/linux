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

static inline bool ip_invalid(unsigned long ip)
{
	/*
	 * Perform some basic checks if an instruction address taken
	 * from unreliable source is invalid.
	 */
	if (ip & 1)
		return true;
	if (ip < mmap_min_addr)
		return true;
	if (ip >= current->mm->context.asce_limit)
		return true;
	return false;
}

static inline bool ip_within_vdso(unsigned long ip)
{
	return in_range(ip, current->mm->context.vdso_base, vdso_text_size());
}

void arch_stack_walk_user_common(stack_trace_consume_fn consume_entry, void *cookie,
				 struct perf_callchain_entry_ctx *entry,
				 const struct pt_regs *regs, bool perf)
{
	struct stack_frame_vdso_wrapper __user *sf_vdso;
	struct stack_frame_user __user *sf;
	unsigned long ip, sp;
	bool first = true;

	if (is_compat_task())
		return;
	if (!current->mm)
		return;
	ip = instruction_pointer(regs);
	if (!store_ip(consume_entry, cookie, entry, perf, ip))
		return;
	sf = (void __user *)user_stack_pointer(regs);
	pagefault_disable();
	while (1) {
		if (__get_user(sp, &sf->back_chain))
			break;
		/*
		 * VDSO entry code has a non-standard stack frame layout.
		 * See VDSO user wrapper code for details.
		 */
		if (!sp && ip_within_vdso(ip)) {
			sf_vdso = (void __user *)sf;
			if (__get_user(ip, &sf_vdso->return_address))
				break;
			sp = (unsigned long)sf + STACK_FRAME_VDSO_OVERHEAD;
			sf = (void __user *)sp;
			if (__get_user(sp, &sf->back_chain))
				break;
		} else {
			sf = (void __user *)sp;
			if (__get_user(ip, &sf->gprs[8]))
				break;
		}
		/* Sanity check: ABI requires SP to be 8 byte aligned. */
		if (sp & 0x7)
			break;
		if (ip_invalid(ip)) {
			/*
			 * If the instruction address is invalid, and this
			 * is the first stack frame, assume r14 has not
			 * been written to the stack yet. Otherwise exit.
			 */
			if (!first)
				break;
			ip = regs->gprs[14];
			if (ip_invalid(ip))
				break;
		}
		if (!store_ip(consume_entry, cookie, entry, perf, ip))
			break;
		first = false;
	}
	pagefault_enable();
}

void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs)
{
	arch_stack_walk_user_common(consume_entry, cookie, NULL, regs, false);
}
