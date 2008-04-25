/*
 * Stack trace utility
 *
 * Copyright 2008 Christoph Hellwig, IBM Corp.
 *
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <asm/ptrace.h>

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace)
{
	unsigned long sp;

	asm("mr %0,1" : "=r" (sp));

	for (;;) {
		unsigned long *stack = (unsigned long *) sp;
		unsigned long newsp, ip;

		if (!validate_sp(sp, current, STACK_FRAME_OVERHEAD))
			return;

		newsp = stack[0];
		ip = stack[STACK_FRAME_LR_SAVE];

		if (!trace->skip)
			trace->entries[trace->nr_entries++] = ip;
		else
			trace->skip--;

		if (trace->nr_entries >= trace->max_entries)
			return;

		sp = newsp;
	}
}
