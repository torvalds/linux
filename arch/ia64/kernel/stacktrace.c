// SPDX-License-Identifier: GPL-2.0
/*
 * arch/ia64/kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/module.h>

static void
ia64_do_save_stack(struct unw_frame_info *info, void *arg)
{
	struct stack_trace *trace = arg;
	unsigned long ip;
	int skip = trace->skip;

	trace->nr_entries = 0;
	do {
		unw_get_ip(info, &ip);
		if (ip == 0)
			break;
		if (skip == 0) {
			trace->entries[trace->nr_entries++] = ip;
			if (trace->nr_entries == trace->max_entries)
				break;
		} else
			skip--;
	} while (unw_unwind(info) >= 0);
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace)
{
	unw_init_running(ia64_do_save_stack, trace);
}
EXPORT_SYMBOL(save_stack_trace);
