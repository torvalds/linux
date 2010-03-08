/*
 * Stack trace support for Microblaze.
 *
 * Copyright (C) 2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <linux/ptrace.h>
#include <linux/module.h>

/* FIXME initial support */
void save_stack_trace(struct stack_trace *trace)
{
	unsigned long *sp;
	unsigned long addr;
	asm("addik %0, r1, 0" : "=r" (sp));

	while (!kstack_end(sp)) {
		addr = *sp++;
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
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned int *sp;
	unsigned long addr;

	struct thread_info *ti = task_thread_info(tsk);

	if (tsk == current)
		asm("addik %0, r1, 0" : "=r" (sp));
	else
		sp = (unsigned int *)ti->cpu_context.r1;

	while (!kstack_end(sp)) {
		addr = *sp++;
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
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);
