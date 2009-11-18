/*
 * ftrace graph code
 *
 * Copyright (C) 2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/ftrace.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/atomic.h>

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr)
{
	struct ftrace_graph_ent trace;
	unsigned long return_hooker = (unsigned long)&return_to_handler;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	if (ftrace_push_return_trace(*parent, self_addr, &trace.depth, 0) == -EBUSY)
		return;

	trace.func = self_addr;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		current->curr_ret_stack--;
		return;
	}

	/* all is well in the world !  hijack RETS ... */
	*parent = return_hooker;
}

#endif
