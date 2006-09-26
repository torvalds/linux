/*
 * arch/x86_64/kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/module.h>
#include <asm/stacktrace.h>

static void save_stack_warning(void *data, char *msg)
{
}

static void
save_stack_warning_symbol(void *data, char *msg, unsigned long symbol)
{
}

static int save_stack_stack(void *data, char *name)
{
	struct stack_trace *trace = (struct stack_trace *)data;
	return trace->all_contexts ? 0 : -1;
}

static void save_stack_address(void *data, unsigned long addr)
{
	struct stack_trace *trace = (struct stack_trace *)data;
	if (trace->skip > 0) {
		trace->skip--;
		return;
	}
	if (trace->nr_entries < trace->max_entries - 1)
		trace->entries[trace->nr_entries++] = addr;
}

static struct stacktrace_ops save_stack_ops = {
	.warning = save_stack_warning,
	.warning_symbol = save_stack_warning_symbol,
	.stack = save_stack_stack,
	.address = save_stack_address,
};

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace, struct task_struct *task)
{
	dump_trace(task, NULL, NULL, &save_stack_ops, trace);
	trace->entries[trace->nr_entries++] = ULONG_MAX;
}
EXPORT_SYMBOL(save_stack_trace);

