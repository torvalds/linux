// SPDX-License-Identifier: GPL-2.0

#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>

extern void (*ftrace_trace_function)(unsigned long, unsigned long,
				     struct ftrace_ops*, struct pt_regs*);

noinline void __naked ftrace_stub(unsigned long ip, unsigned long parent_ip,
				  struct ftrace_ops *op, struct pt_regs *regs)
{
	__asm__ ("");  /* avoid to optimize as pure function */
}

noinline void _mcount(unsigned long parent_ip)
{
	/* save all state by the compiler prologue */

	unsigned long ip = (unsigned long)__builtin_return_address(0);

	if (ftrace_trace_function != ftrace_stub)
		ftrace_trace_function(ip - MCOUNT_INSN_SIZE, parent_ip,
				      NULL, NULL);

	/* restore all state by the compiler epilogue */
}
EXPORT_SYMBOL(_mcount);
