/*
 * Copyright (C) 2000 Anton Blanchard (anton@linuxcare.com)
 *
 * This file implements mcount(), which is used to collect profiling data.
 * This can also be tweaked for kernel stack overflow detection.
 */

#include <linux/linkage.h>

/*
 * This is the main variant and is called by C code.  GCC's -pg option
 * automatically instruments every C function with a call to this.
 */

	.text
	.align		32
	.globl		_mcount
	.type		_mcount,#function
	.globl		mcount
	.type		mcount,#function
_mcount:
mcount:
#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE
	/* Do nothing, the retl/nop below is all we need.  */
#else
	sethi		%hi(function_trace_stop), %g1
	lduw		[%g1 + %lo(function_trace_stop)], %g2
	brnz,pn		%g2, 2f
	 sethi		%hi(ftrace_trace_function), %g1
	sethi		%hi(ftrace_stub), %g2
	ldx		[%g1 + %lo(ftrace_trace_function)], %g1
	or		%g2, %lo(ftrace_stub), %g2
	cmp		%g1, %g2
	be,pn		%icc, 1f
	 mov		%i7, %g3
	save		%sp, -176, %sp
	mov		%g3, %o1
	jmpl		%g1, %o7
	 mov		%i7, %o0
	ret
	 restore
	/* not reached */
1:
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	sethi		%hi(ftrace_graph_return), %g1
	ldx		[%g1 + %lo(ftrace_graph_return)], %g3
	cmp		%g2, %g3
	bne,pn		%xcc, 5f
	 sethi		%hi(ftrace_graph_entry_stub), %g2
	sethi		%hi(ftrace_graph_entry), %g1
	or		%g2, %lo(ftrace_graph_entry_stub), %g2
	ldx		[%g1 + %lo(ftrace_graph_entry)], %g1
	cmp		%g1, %g2
	be,pt		%xcc, 2f
	 nop
5:	mov		%i7, %g2
	mov		%fp, %g3
	save		%sp, -176, %sp
	mov		%g2, %l0
	ba,pt		%xcc, ftrace_graph_caller
	 mov		%g3, %l1
#endif
2:
#endif
#endif
	retl
	 nop
	.size		_mcount,.-_mcount
	.size		mcount,.-mcount

#ifdef CONFIG_FUNCTION_TRACER
	.globl		ftrace_stub
	.type		ftrace_stub,#function
ftrace_stub:
	retl
	 nop
	.size		ftrace_stub,.-ftrace_stub
#ifdef CONFIG_DYNAMIC_FTRACE
	.globl		ftrace_caller
	.type		ftrace_caller,#function
ftrace_caller:
	sethi		%hi(function_trace_stop), %g1
	mov		%i7, %g2
	lduw		[%g1 + %lo(function_trace_stop)], %g1
	brnz,pn		%g1, ftrace_stub
	 mov		%fp, %g3
	save		%sp, -176, %sp
	mov		%g2, %o1
	mov		%g2, %l0
	mov		%g3, %l1
	.globl		ftrace_call
ftrace_call:
	call		ftrace_stub
	 mov		%i7, %o0
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl		ftrace_graph_call
ftrace_graph_call:
	call		ftrace_stub
	 nop
#endif
	ret
	 restore
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.size		ftrace_graph_call,.-ftrace_graph_call
#endif
	.size		ftrace_call,.-ftrace_call
	.size		ftrace_caller,.-ftrace_caller
#endif
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	mov		%l0, %o0
	mov		%i7, %o1
	call		prepare_ftrace_return
	 mov		%l1, %o2
	ret
	 restore	%o0, -8, %i7
END(ftrace_graph_caller)

ENTRY(return_to_handler)
	save		%sp, -176, %sp
	call		ftrace_return_to_handler
	 mov		%fp, %o0
	jmpl		%o0 + 8, %g0
	 restore
END(return_to_handler)
#endif
