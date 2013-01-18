/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ptrace.h>
#include <linux/export.h>

/*-------------------------------------------------------------------------
 *              APIs expected by various kernel sub-systems
 *-------------------------------------------------------------------------
 */

noinline void show_stacktrace(struct task_struct *tsk, struct pt_regs *regs)
{
	pr_info("\nStack Trace: NOT Available\n");
}
EXPORT_SYMBOL(show_stacktrace);

/* Expected by sched Code */
void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	show_stacktrace(tsk, NULL);
}

/* Expected by Rest of kernel code */
void dump_stack(void)
{
	show_stacktrace(NULL, NULL);
}
EXPORT_SYMBOL(dump_stack);

/* Another API expected by schedular, shows up in "ps" as Wait Channel
 * Ofcourse just returning schedule( ) would be pointless so unwind until
 * the function is not in schedular code
 */
unsigned int get_wchan(struct task_struct *tsk)
{
	return 0;
}
