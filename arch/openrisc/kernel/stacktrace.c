/*
 * Stack trace utility for OpenRISC
 *
 * Copyright (C) 2017 Stafford Horne <shorne@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 * Losely based on work from sh and powerpc.
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/stacktrace.h>

#include <asm/processor.h>
#include <asm/unwinder.h>

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
static void
save_stack_address(void *data, unsigned long addr, int reliable)
{
	struct stack_trace *trace = data;

	if (!reliable)
		return;

	if (trace->skip > 0) {
		trace->skip--;
		return;
	}

	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = addr;
}

void save_stack_trace(struct stack_trace *trace)
{
	unwind_stack(trace, (unsigned long *) &trace, save_stack_address);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

static void
save_stack_address_nosched(void *data, unsigned long addr, int reliable)
{
	struct stack_trace *trace = (struct stack_trace *)data;

	if (!reliable)
		return;

	if (in_sched_functions(addr))
		return;

	if (trace->skip > 0) {
		trace->skip--;
		return;
	}

	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = addr;
}

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned long *sp = NULL;

	if (tsk == current)
		sp = (unsigned long *) &sp;
	else
		sp = (unsigned long *) KSTK_ESP(tsk);

	unwind_stack(trace, sp, save_stack_address_nosched);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void
save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	unwind_stack(trace, (unsigned long *) regs->sp,
		     save_stack_address_nosched);
}
EXPORT_SYMBOL_GPL(save_stack_trace_regs);
