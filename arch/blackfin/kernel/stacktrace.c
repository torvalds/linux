/*
 * Blackfin stacktrace code (mostly copied from avr32)
 *
 * Copyright 2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <linux/module.h>

register unsigned long current_frame_pointer asm("FP");

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
	fp = current_frame_pointer;

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
