/**
 * @file backtrace.c
 *
 * @remark Copyright 2008 Tensilica Inc.
 * Copyright (C) 2015 Cadence Design Systems Inc.
 * @remark Read the file COPYING
 *
 */

#include <linux/oprofile.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

static int xtensa_backtrace_cb(struct stackframe *frame, void *data)
{
	oprofile_add_trace(frame->pc);
	return 0;
}

void xtensa_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	if (user_mode(regs))
		xtensa_backtrace_user(regs, depth, xtensa_backtrace_cb, NULL);
	else
		xtensa_backtrace_kernel(regs, depth, xtensa_backtrace_cb,
					xtensa_backtrace_cb, NULL);
}
