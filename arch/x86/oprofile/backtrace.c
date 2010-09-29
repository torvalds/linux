/**
 * @file backtrace.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author David Smith
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <asm/stacktrace.h>

static void backtrace_warning_symbol(void *data, char *msg,
				     unsigned long symbol)
{
	/* Ignore warnings */
}

static void backtrace_warning(void *data, char *msg)
{
	/* Ignore warnings */
}

static int backtrace_stack(void *data, char *name)
{
	/* Yes, we want all stacks */
	return 0;
}

static void backtrace_address(void *data, unsigned long addr, int reliable)
{
	unsigned int *depth = data;

	if ((*depth)--)
		oprofile_add_trace(addr);
}

static struct stacktrace_ops backtrace_ops = {
	.warning	= backtrace_warning,
	.warning_symbol	= backtrace_warning_symbol,
	.stack		= backtrace_stack,
	.address	= backtrace_address,
	.walk_stack	= print_context_stack,
};

static struct stack_frame *dump_user_backtrace(struct stack_frame *head)
{
	struct stack_frame bufhead[2];

	/* Also check accessibility of one struct stack_frame beyond */
	if (!access_ok(VERIFY_READ, head, sizeof(bufhead)))
		return NULL;
	if (__copy_from_user_inatomic(bufhead, head, sizeof(bufhead)))
		return NULL;

	oprofile_add_trace(bufhead[0].return_address);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= bufhead[0].next_frame)
		return NULL;

	return bufhead[0].next_frame;
}

void
x86_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	struct stack_frame *head = (struct stack_frame *)frame_pointer(regs);

	if (!user_mode_vm(regs)) {
		unsigned long stack = kernel_stack_pointer(regs);
		if (depth)
			dump_trace(NULL, regs, (unsigned long *)stack, 0,
				   &backtrace_ops, &depth);
		return;
	}

	while (depth-- && head)
		head = dump_user_backtrace(head);
}
