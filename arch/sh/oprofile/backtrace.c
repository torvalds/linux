/*
 * SH specific backtracing code for oprofile
 *
 * Copyright 2007 STMicroelectronics Ltd.
 *
 * Author: Dave Peverley <dpeverley@mpc-data.co.uk>
 *
 * Based on ARM oprofile backtrace code by Richard Purdie and in turn, i386
 * oprofile backtrace code by John Levon, David Smith
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <asm/unwinder.h>
#include <asm/ptrace.h>
#include <linux/uaccess.h>
#include <asm/sections.h>
#include <asm/stacktrace.h>

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
	.stack = backtrace_stack,
	.address = backtrace_address,
};

/* Limit to stop backtracing too far. */
static int backtrace_limit = 20;

static unsigned long *
user_backtrace(unsigned long *stackaddr, struct pt_regs *regs)
{
	unsigned long buf_stack;

	/* Also check accessibility of address */
	if (!access_ok(VERIFY_READ, stackaddr, sizeof(unsigned long)))
		return NULL;

	if (__copy_from_user_inatomic(&buf_stack, stackaddr, sizeof(unsigned long)))
		return NULL;

	/* Quick paranoia check */
	if (buf_stack & 3)
		return NULL;

	oprofile_add_trace(buf_stack);

	stackaddr++;

	return stackaddr;
}

void sh_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	unsigned long *stackaddr;

	/*
	 * Paranoia - clip max depth as we could get lost in the weeds.
	 */
	if (depth > backtrace_limit)
		depth = backtrace_limit;

	stackaddr = (unsigned long *)kernel_stack_pointer(regs);
	if (!user_mode(regs)) {
		if (depth)
			unwind_stack(NULL, regs, stackaddr,
				     &backtrace_ops, &depth);
		return;
	}

	while (depth-- && (stackaddr != NULL))
		stackaddr = user_backtrace(stackaddr, regs);
}
