/*
 * Copyright IBM Corp. 2008,2009
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

#include <asm/asm-offsets.h>

	.section .kprobes.text, "ax"

	.globl ftrace_stub
ftrace_stub:
	br	%r14

	.globl _mcount
_mcount:
#ifdef CONFIG_DYNAMIC_FTRACE
	br	%r14

	.globl ftrace_caller
ftrace_caller:
#endif
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
	l	%r2,100(%r15)
	l	%r3,152(%r15)
	.globl	ftrace_graph_caller
ftrace_graph_caller:
# The bras instruction gets runtime patched to call prepare_ftrace_return.
# See ftrace_enable_ftrace_graph_caller. The patched instruction is:
#	bras	%r14,prepare_ftrace_return
	bras	%r14,0f
0:	st	%r2,100(%r15)
#endif
	ahi	%r15,96
	l	%r14,56(%r15)
3:	lm	%r2,%r5,16(%r15)
	br	%r14

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

#endif
