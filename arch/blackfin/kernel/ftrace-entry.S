/*
 * mcount and friends -- ftrace stuff
 *
 * Copyright (C) 2009-2010 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/linkage.h>
#include <asm/ftrace.h>

.text

#ifdef CONFIG_DYNAMIC_FTRACE

/* Simple stub so we can boot the kernel until runtime patching has
 * disabled all calls to this.  Then it'll be unused.
 */
ENTRY(__mcount)
# if ANOMALY_05000371
	nop; nop; nop; nop;
# endif
	rts;
ENDPROC(__mcount)

/* GCC will have called us before setting up the function prologue, so we
 * can clobber the normal scratch registers, but we need to make sure to
 * save/restore the registers used for argument passing (R0-R2) in case
 * the profiled function is using them.  With data registers, R3 is the
 * only one we can blow away.  With pointer registers, we have P0-P2.
 *
 * Upon entry, the RETS will point to the top of the current profiled
 * function.  And since GCC pushed the previous RETS for us, the previous
 * function will be waiting there.  mmmm pie.
 */
ENTRY(_ftrace_caller)
# ifdef CONFIG_HAVE_FUNCTION_TRACE_MCOUNT_TEST
	/* optional micro optimization: return if stopped */
	p1.l = _function_trace_stop;
	p1.h = _function_trace_stop;
	r3 = [p1];
	cc = r3 == 0;
	if ! cc jump _ftrace_stub (bp);
# endif

	/* save first/second/third function arg and the return register */
	[--sp] = r2;
	[--sp] = r0;
	[--sp] = r1;
	[--sp] = rets;

	/* function_trace_call(unsigned long ip, unsigned long parent_ip):
	 *  ip: this point was called by ...
	 *  parent_ip: ... this function
	 * the ip itself will need adjusting for the mcount call
	 */
	r0 = rets;
	r1 = [sp + 16];	/* skip the 4 local regs on stack */
	r0 += -MCOUNT_INSN_SIZE;

.globl _ftrace_call
_ftrace_call:
	call _ftrace_stub

# ifdef CONFIG_FUNCTION_GRAPH_TRACER
.globl _ftrace_graph_call
_ftrace_graph_call:
	nop;	/* jump _ftrace_graph_caller; */
# endif

	/* restore state and get out of dodge */
.Lfinish_trace:
	rets = [sp++];
	r1 = [sp++];
	r0 = [sp++];
	r2 = [sp++];

.globl _ftrace_stub
_ftrace_stub:
	rts;
ENDPROC(_ftrace_caller)

#else

/* See documentation for _ftrace_caller */
ENTRY(__mcount)
# ifdef CONFIG_HAVE_FUNCTION_TRACE_MCOUNT_TEST
	/* optional micro optimization: return if stopped */
	p1.l = _function_trace_stop;
	p1.h = _function_trace_stop;
	r3 = [p1];
	cc = r3 == 0;
	if ! cc jump _ftrace_stub (bp);
# endif

	/* save third function arg early so we can do testing below */
	[--sp] = r2;

	/* load the function pointer to the tracer */
	p0.l = _ftrace_trace_function;
	p0.h = _ftrace_trace_function;
	r3 = [p0];

	/* optional micro optimization: don't call the stub tracer */
	r2.l = _ftrace_stub;
	r2.h = _ftrace_stub;
	cc = r2 == r3;
	if ! cc jump .Ldo_trace;

# ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* if the ftrace_graph_return function pointer is not set to
	 * the ftrace_stub entry, call prepare_ftrace_return().
	 */
	p0.l = _ftrace_graph_return;
	p0.h = _ftrace_graph_return;
	r3 = [p0];
	cc = r2 == r3;
	if ! cc jump _ftrace_graph_caller;

	/* similarly, if the ftrace_graph_entry function pointer is not
	 * set to the ftrace_graph_entry_stub entry, ...
	 */
	p0.l = _ftrace_graph_entry;
	p0.h = _ftrace_graph_entry;
	r2.l = _ftrace_graph_entry_stub;
	r2.h = _ftrace_graph_entry_stub;
	r3 = [p0];
	cc = r2 == r3;
	if ! cc jump _ftrace_graph_caller;
# endif

	r2 = [sp++];
	rts;

.Ldo_trace:

	/* save first/second function arg and the return register */
	[--sp] = r0;
	[--sp] = r1;
	[--sp] = rets;

	/* setup the tracer function */
	p0 = r3;

	/* function_trace_call(unsigned long ip, unsigned long parent_ip):
	 *  ip: this point was called by ...
	 *  parent_ip: ... this function
	 * the ip itself will need adjusting for the mcount call
	 */
	r0 = rets;
	r1 = [sp + 16];	/* skip the 4 local regs on stack */
	r0 += -MCOUNT_INSN_SIZE;

	/* call the tracer */
	call (p0);

	/* restore state and get out of dodge */
.Lfinish_trace:
	rets = [sp++];
	r1 = [sp++];
	r0 = [sp++];
	r2 = [sp++];

.globl _ftrace_stub
_ftrace_stub:
	rts;
ENDPROC(__mcount)

#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/* The prepare_ftrace_return() function is similar to the trace function
 * except it takes a pointer to the location of the frompc.  This is so
 * the prepare_ftrace_return() can hijack it temporarily for probing
 * purposes.
 */
ENTRY(_ftrace_graph_caller)
# ifndef CONFIG_DYNAMIC_FTRACE
	/* save first/second function arg and the return register */
	[--sp] = r0;
	[--sp] = r1;
	[--sp] = rets;

	/* prepare_ftrace_return(parent, self_addr, frame_pointer) */
	r0 = sp;	/* unsigned long *parent */
	r1 = rets;	/* unsigned long self_addr */
# else
	r0 = sp;	/* unsigned long *parent */
	r1 = [sp];	/* unsigned long self_addr */
# endif
# ifdef CONFIG_HAVE_FUNCTION_GRAPH_FP_TEST
	r2 = fp;	/* unsigned long frame_pointer */
# endif
	r0 += 16;	/* skip the 4 local regs on stack */
	r1 += -MCOUNT_INSN_SIZE;
	call _prepare_ftrace_return;

	jump .Lfinish_trace;
ENDPROC(_ftrace_graph_caller)

/* Undo the rewrite caused by ftrace_graph_caller().  The common function
 * ftrace_return_to_handler() will return the original rets so we can
 * restore it and be on our way.
 */
ENTRY(_return_to_handler)
	/* make sure original return values are saved */
	[--sp] = p0;
	[--sp] = r0;
	[--sp] = r1;

	/* get original return address */
# ifdef CONFIG_HAVE_FUNCTION_GRAPH_FP_TEST
	r0 = fp;	/* Blackfin is sane, so omit this */
# endif
	call _ftrace_return_to_handler;
	rets = r0;

	/* anomaly 05000371 - make sure we have at least three instructions
	 * between rets setting and the return
	 */
	r1 = [sp++];
	r0 = [sp++];
	p0 = [sp++];
	rts;
ENDPROC(_return_to_handler)
#endif
