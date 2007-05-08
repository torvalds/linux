/*
 * arch/sh/kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <asm/ptrace.h>

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace, struct task_struct *task)
{
	unsigned long *sp = (unsigned long *)current_stack_pointer;

	while (!kstack_end(sp)) {
		unsigned long addr = *sp++;

		if (__kernel_text_address(addr)) {
			if (trace->skip > 0)
				trace->skip--;
			else
				trace->entries[trace->nr_entries++] = addr;
			if (trace->nr_entries >= trace->max_entries)
				break;
		}
	}
}
