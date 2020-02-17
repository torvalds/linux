// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stacktrace support for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <linux/module.h>

struct stackframe {
	unsigned long fp;
	unsigned long rets;
};

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace)
{
	unsigned long low, high;
	unsigned long fp;
	struct stackframe *frame;
	int skip = trace->skip;

	low = (unsigned long)task_stack_page(current);
	high = low + THREAD_SIZE;
	fp = (unsigned long)__builtin_frame_address(0);

	while (fp >= low && fp <= (high - sizeof(*frame))) {
		frame = (struct stackframe *)fp;

		if (skip) {
			skip--;
		} else {
			trace->entries[trace->nr_entries++] = frame->rets;
			if (trace->nr_entries >= trace->max_entries)
				break;
		}

		/*
		 * The next frame must be at a higher address than the
		 * current frame.
		 */
		low = fp + sizeof(*frame);
		fp = frame->fp;
	}
}
EXPORT_SYMBOL_GPL(save_stack_trace);
