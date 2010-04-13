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
	brnz,pn		%g2, 1f
	 sethi		%hi(ftrace_trace_function), %g1
	sethi		%hi(ftrace_stub), %g2
	ldx		[%g1 + %lo(ftrace_trace_function)], %g1
	or		%g2, %lo(ftrace_stub), %g2
	cmp		%g1, %g2
	be,pn		%icc, 1f
	 mov		%i7, %o1
	jmpl		%g1, %g0
	 mov		%o7, %o0
	/* not reached */
1:
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
	mov		%i7, %o1
	lduw		[%g1 + %lo(function_trace_stop)], %g2
	brnz,pn		%g2, ftrace_stub
	 mov		%o7, %o0
	.globl		ftrace_call
ftrace_call:
	/* If the final kernel link ever turns on relaxation, we'll need
	 * to do something about this tail call.  Otherwise the linker
	 * will rewrite the call into a branch and nop out the move
	 * instruction.
	 */
	call		ftrace_stub
	 mov		%o0, %o7
	retl
	 nop
	.size		ftrace_call,.-ftrace_call
	.size		ftrace_caller,.-ftrace_caller
#endif
#endif
