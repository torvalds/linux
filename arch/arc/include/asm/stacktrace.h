/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 * Copyright (C) 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_STACKTRACE_H
#define __ASM_STACKTRACE_H

#include <linux/sched.h>

/**
 * arc_unwind_core - Unwind the kernel mode stack for an execution context
 * @tsk:		NULL for current task, specific task otherwise
 * @regs:		pt_regs used to seed the unwinder {SP, FP, BLINK, PC}
 * 			If NULL, use pt_regs of @tsk (if !NULL) otherwise
 * 			use the current values of {SP, FP, BLINK, PC}
 * @consumer_fn:	Callback invoked for each frame unwound
 * 			Returns 0 to continue unwinding, -1 to stop
 * @arg:		Arg to callback
 *
 * Returns the address of first function in stack
 *
 * Semantics:
 *  - synchronous unwinding (e.g. dump_stack): @tsk  NULL, @regs  NULL
 *  - Asynchronous unwinding of sleeping task: @tsk !NULL, @regs  NULL
 *  - Asynchronous unwinding of intr/excp etc: @tsk !NULL, @regs !NULL
 */
notrace noinline unsigned int arc_unwind_core(
	struct task_struct *tsk, struct pt_regs *regs,
	int (*consumer_fn) (unsigned int, void *),
	void *arg);

#endif /* __ASM_STACKTRACE_H */
