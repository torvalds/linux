/*
 * Code for tracing calls in Linux kernel.
 * Copyright (C) 2009-2016 Helge Deller <deller@gmx.de>
 *
 * based on code for x86 which is:
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 *
 * future possible enhancements:
 * 	- add CONFIG_DYNAMIC_FTRACE
 *	- add CONFIG_STACK_TRACER
 */

#include <linux/init.h>
#include <linux/ftrace.h>

#include <asm/assembly.h>
#include <asm/sections.h>
#include <asm/ftrace.h>


#define __hot __attribute__ ((__section__ (".text.hot")))

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
static void __hot prepare_ftrace_return(unsigned long *parent,
					unsigned long self_addr)
{
	unsigned long old;
	struct ftrace_graph_ent trace;
	extern int parisc_return_to_handler;

	if (unlikely(ftrace_graph_is_dead()))
		return;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;

	trace.func = self_addr;
	trace.depth = current->curr_ret_stack + 1;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace))
		return;

        if (ftrace_push_return_trace(old, self_addr, &trace.depth,
			0 ) == -EBUSY)
                return;

	/* activate parisc_return_to_handler() as return point */
	*parent = (unsigned long) &parisc_return_to_handler;
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

void notrace __hot ftrace_function_trampoline(unsigned long parent,
				unsigned long self_addr,
				unsigned long org_sp_gr3)
{
	extern ftrace_func_t ftrace_trace_function;  /* depends on CONFIG_DYNAMIC_FTRACE */
	extern int ftrace_graph_entry_stub(struct ftrace_graph_ent *trace);

	if (ftrace_trace_function != ftrace_stub) {
		/* struct ftrace_ops *op, struct pt_regs *regs); */
		ftrace_trace_function(parent, self_addr, NULL, NULL);
		return;
	}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (ftrace_graph_return != (trace_func_graph_ret_t) ftrace_stub ||
		ftrace_graph_entry != ftrace_graph_entry_stub) {
		unsigned long *parent_rp;

		/* calculate pointer to %rp in stack */
		parent_rp = (unsigned long *) (org_sp_gr3 - RP_OFFSET);
		/* sanity check: parent_rp should hold parent */
		if (*parent_rp != parent)
			return;

		prepare_ftrace_return(parent_rp, self_addr);
		return;
	}
#endif
}

