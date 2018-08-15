// SPDX-License-Identifier: GPL-2.0

#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>

extern void (*ftrace_trace_function)(unsigned long, unsigned long,
				     struct ftrace_ops*, struct pt_regs*);
extern int ftrace_graph_entry_stub(struct ftrace_graph_ent *trace);
extern void ftrace_graph_caller(void);

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

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (ftrace_graph_return != (trace_func_graph_ret_t)ftrace_stub
	    || ftrace_graph_entry != ftrace_graph_entry_stub)
		ftrace_graph_caller();
#endif

	/* restore all state by the compiler epilogue */
}
EXPORT_SYMBOL(_mcount);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	struct ftrace_graph_ent trace;
	unsigned long old;
	int err;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;

	trace.func = self_addr;
	trace.depth = current->curr_ret_stack + 1;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace))
		return;

	err = ftrace_push_return_trace(old, self_addr, &trace.depth,
				       frame_pointer, NULL);

	if (err == -EBUSY)
		return;

	*parent = return_hooker;
}

noinline void ftrace_graph_caller(void)
{
	unsigned long *parent_ip =
		(unsigned long *)(__builtin_frame_address(2) - 4);

	unsigned long selfpc =
		(unsigned long)(__builtin_return_address(1) - MCOUNT_INSN_SIZE);

	unsigned long frame_pointer =
		(unsigned long)__builtin_frame_address(3);

	prepare_ftrace_return(parent_ip, selfpc, frame_pointer);
}

extern unsigned long ftrace_return_to_handler(unsigned long frame_pointer);
void __naked return_to_handler(void)
{
	__asm__ __volatile__ (
		/* save state needed by the ABI     */
		"smw.adm $r0,[$sp],$r1,#0x0  \n\t"

		/* get original return address      */
		"move $r0, $fp               \n\t"
		"bal ftrace_return_to_handler\n\t"
		"move $lp, $r0               \n\t"

		/* restore state nedded by the ABI  */
		"lmw.bim $r0,[$sp],$r1,#0x0  \n\t");
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
