/*
 * Code for tracing calls in Linux kernel.
 * Copyright (C) 2009 Helge Deller <deller@gmx.de>
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

#include <asm/sections.h>
#include <asm/ftrace.h>



#ifdef CONFIG_FUNCTION_GRAPH_TRACER

/* Add a function return address to the trace stack on thread info.*/
static int push_return_trace(unsigned long ret, unsigned long long time,
				unsigned long func, int *depth)
{
	int index;

	if (!current->ret_stack)
		return -EBUSY;

	/* The return trace stack is full */
	if (current->curr_ret_stack == FTRACE_RETFUNC_DEPTH - 1) {
		atomic_inc(&current->trace_overrun);
		return -EBUSY;
	}

	index = ++current->curr_ret_stack;
	barrier();
	current->ret_stack[index].ret = ret;
	current->ret_stack[index].func = func;
	current->ret_stack[index].calltime = time;
	*depth = index;

	return 0;
}

/* Retrieve a function return address to the trace stack on thread info.*/
static void pop_return_trace(struct ftrace_graph_ret *trace, unsigned long *ret)
{
	int index;

	index = current->curr_ret_stack;

	if (unlikely(index < 0)) {
		ftrace_graph_stop();
		WARN_ON(1);
		/* Might as well panic, otherwise we have no where to go */
		*ret = (unsigned long)
			dereference_function_descriptor(&panic);
		return;
	}

	*ret = current->ret_stack[index].ret;
	trace->func = current->ret_stack[index].func;
	trace->calltime = current->ret_stack[index].calltime;
	trace->overrun = atomic_read(&current->trace_overrun);
	trace->depth = index;
	barrier();
	current->curr_ret_stack--;

}

/*
 * Send the trace to the ring-buffer.
 * @return the original return address.
 */
unsigned long ftrace_return_to_handler(unsigned long retval0,
				       unsigned long retval1)
{
	struct ftrace_graph_ret trace;
	unsigned long ret;

	pop_return_trace(&trace, &ret);
	trace.rettime = cpu_clock(raw_smp_processor_id());
	ftrace_graph_return(&trace);

	if (unlikely(!ret)) {
		ftrace_graph_stop();
		WARN_ON(1);
		/* Might as well panic. What else to do? */
		ret = (unsigned long)
			dereference_function_descriptor(&panic);
	}

	/* HACK: we hand over the old functions' return values
	   in %r23 and %r24. Assembly in entry.S will take care
	   and move those to their final registers %ret0 and %ret1 */
	asm( "copy %0, %%r23 \n\t"
	     "copy %1, %%r24 \n" : : "r" (retval0), "r" (retval1) );

	return ret;
}

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr)
{
	unsigned long old;
	unsigned long long calltime;
	struct ftrace_graph_ent trace;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;
	*parent = (unsigned long)
		  dereference_function_descriptor(&return_to_handler);

	if (unlikely(!__kernel_text_address(old))) {
		ftrace_graph_stop();
		*parent = old;
		WARN_ON(1);
		return;
	}

	calltime = cpu_clock(raw_smp_processor_id());

	if (push_return_trace(old, calltime,
				self_addr, &trace.depth) == -EBUSY) {
		*parent = old;
		return;
	}

	trace.func = self_addr;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		current->curr_ret_stack--;
		*parent = old;
	}
}

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */


void ftrace_function_trampoline(unsigned long parent,
				unsigned long self_addr,
				unsigned long org_sp_gr3)
{
	extern ftrace_func_t ftrace_trace_function;

	if (function_trace_stop)
		return;

	if (ftrace_trace_function != ftrace_stub) {
		ftrace_trace_function(parent, self_addr);
		return;
	}
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (ftrace_graph_entry && ftrace_graph_return) {
		unsigned long sp;
		unsigned long *parent_rp;

                asm volatile ("copy %%r30, %0" : "=r"(sp));
		/* sanity check: is stack pointer which we got from
		   assembler function in entry.S in a reasonable
		   range compared to current stack pointer? */
		if ((sp - org_sp_gr3) > 0x400)
			return;

		/* calculate pointer to %rp in stack */
		parent_rp = (unsigned long *) org_sp_gr3 - 0x10;
		/* sanity check: parent_rp should hold parent */
		if (*parent_rp != parent)
			return;
		
		prepare_ftrace_return(parent_rp, self_addr);
		return;
	}
#endif
}

