// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/ftrace.h>
#include <linux/uaccess.h>

extern void (*ftrace_trace_function)(unsigned long, unsigned long,
				     struct ftrace_ops*, struct pt_regs*);


noinline void __naked ftrace_stub(unsigned long ip, unsigned long parent_ip,
				  struct ftrace_ops *op, struct pt_regs *regs)
{
	asm volatile ("\n");
}

noinline void csky_mcount(unsigned long from_pc, unsigned long self_pc)
{
	if (ftrace_trace_function != ftrace_stub)
		ftrace_trace_function(self_pc, from_pc, NULL, NULL);
}

/* _mcount is defined in abi's mcount.S */
EXPORT_SYMBOL(_mcount);
