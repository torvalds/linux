/*
 * Copyright IBM Corp. 2008,2009
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

#include <asm/asm-offsets.h>

	.globl ftrace_stub
ftrace_stub:
	br	%r14

#ifdef CONFIG_64BIT

#ifdef CONFIG_DYNAMIC_FTRACE

	.globl _mcount
_mcount:
	br	%r14

	.globl ftrace_caller
ftrace_caller:
	larl	%r1,function_trace_stop
	icm	%r1,0xf,0(%r1)
	bnzr	%r14
	stmg	%r2,%r5,32(%r15)
	stg	%r14,112(%r15)
	lgr	%r1,%r15
	aghi	%r15,-160
	stg	%r1,__SF_BACKCHAIN(%r15)
	lgr	%r2,%r14
	lg	%r3,168(%r15)
	larl	%r14,ftrace_dyn_func
	lg	%r14,0(%r14)
	basr	%r14,%r14
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl	ftrace_graph_caller
ftrace_graph_caller:
	# This unconditional branch gets runtime patched. Change only if
	# you know what you are doing. See ftrace_enable_graph_caller().
	j	0f
	lg	%r2,272(%r15)
	lg	%r3,168(%r15)
	brasl	%r14,prepare_ftrace_return
	stg	%r2,168(%r15)
0:
#endif
	aghi	%r15,160
	lmg	%r2,%r5,32(%r15)
	lg	%r14,112(%r15)
	br	%r14

	.data
	.globl	ftrace_dyn_func
ftrace_dyn_func:
	.quad	ftrace_stub
	.previous

#else /* CONFIG_DYNAMIC_FTRACE */

	.globl _mcount
_mcount:
	larl	%r1,function_trace_stop
	icm	%r1,0xf,0(%r1)
	bnzr	%r14
	stmg	%r2,%r5,32(%r15)
	stg	%r14,112(%r15)
	lgr	%r1,%r15
	aghi	%r15,-160
	stg	%r1,__SF_BACKCHAIN(%r15)
	lgr	%r2,%r14
	lg	%r3,168(%r15)
	larl	%r14,ftrace_trace_function
	lg	%r14,0(%r14)
	basr	%r14,%r14
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	lg	%r2,272(%r15)
	lg	%r3,168(%r15)
	brasl	%r14,prepare_ftrace_return
	stg	%r2,168(%r15)
#endif
	aghi	%r15,160
	lmg	%r2,%r5,32(%r15)
	lg	%r14,112(%r15)
	br	%r14

#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

	.globl	return_to_handler
return_to_handler:
	stmg	%r2,%r5,32(%r15)
	lgr	%r1,%r15
	aghi	%r15,-160
	stg	%r1,__SF_BACKCHAIN(%r15)
	brasl	%r14,ftrace_return_to_handler
	aghi	%r15,160
	lgr	%r14,%r2
	lmg	%r2,%r5,32(%r15)
	br	%r14

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#else /* CONFIG_64BIT */

#ifdef CONFIG_DYNAMIC_FTRACE

	.globl _mcount
_mcount:
	br	%r14

	.globl ftrace_caller
ftrace_caller:
	stm	%r2,%r5,16(%r15)
	bras	%r1,2f
0:	.long	ftrace_trace_function
1:	.long	function_trace_stop
2:	l	%r2,1b-0b(%r1)
	icm	%r2,0xf,0(%r2)
	jnz	3f
	st	%r14,56(%r15)
	lr	%r0,%r15
	ahi	%r15,-96
	l	%r3,100(%r15)
	la	%r2,0(%r14)
	st	%r0,__SF_BACKCHAIN(%r15)
	la	%r3,0(%r3)
	l	%r14,0b-0b(%r1)
	l	%r14,0(%r14)
	basr	%r14,%r14
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl	ftrace_graph_caller
ftrace_graph_caller:
	# This unconditional branch gets runtime patched. Change only if
	# you know what you are doing. See ftrace_enable_graph_caller().
	j	1f
	bras	%r1,0f
	.long	prepare_ftrace_return
0:	l	%r2,152(%r15)
	l	%r4,0(%r1)
	l	%r3,100(%r15)
	basr	%r14,%r4
	st	%r2,100(%r15)
1:
#endif
	ahi	%r15,96
	l	%r14,56(%r15)
3:	lm	%r2,%r5,16(%r15)
	br	%r14

	.data
	.globl	ftrace_dyn_func
ftrace_dyn_func:
	.long	ftrace_stub
	.previous

#else /* CONFIG_DYNAMIC_FTRACE */

	.globl _mcount
_mcount:
	stm	%r2,%r5,16(%r15)
	bras	%r1,2f
0:	.long	ftrace_trace_function
1:	.long	function_trace_stop
2:	l	%r2,1b-0b(%r1)
	icm	%r2,0xf,0(%r2)
	jnz	3f
	st	%r14,56(%r15)
	lr	%r0,%r15
	ahi	%r15,-96
	l	%r3,100(%r15)
	la	%r2,0(%r14)
	st	%r0,__SF_BACKCHAIN(%r15)
	la	%r3,0(%r3)
	l	%r14,0b-0b(%r1)
	l	%r14,0(%r14)
	basr	%r14,%r14
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	bras	%r1,0f
	.long	prepare_ftrace_return
0:	l	%r2,152(%r15)
	l	%r4,0(%r1)
	l	%r3,100(%r15)
	basr	%r14,%r4
	st	%r2,100(%r15)
#endif
	ahi	%r15,96
	l	%r14,56(%r15)
3:	lm	%r2,%r5,16(%r15)
	br	%r14

#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

	.globl	return_to_handler
return_to_handler:
	stm	%r2,%r5,16(%r15)
	st	%r14,56(%r15)
	lr	%r0,%r15
	ahi	%r15,-96
	st	%r0,__SF_BACKCHAIN(%r15)
	bras	%r1,0f
	.long	ftrace_return_to_handler
0:	l	%r2,0b-0b(%r1)
	basr	%r14,%r2
	lr	%r14,%r2
	ahi	%r15,96
	lm	%r2,%r5,16(%r15)
	br	%r14

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#endif /* CONFIG_64BIT */
