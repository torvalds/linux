/*
 * Copyright IBM Corp. 2008
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

#include <asm/asm-offsets.h>

#ifndef CONFIG_64BIT
.globl _mcount
_mcount:
	stm	%r0,%r5,8(%r15)
	st	%r14,56(%r15)
	lr	%r1,%r15
	ahi	%r15,-96
	l	%r3,100(%r15)
	la	%r2,0(%r14)
	st	%r1,__SF_BACKCHAIN(%r15)
	la	%r3,0(%r3)
	bras	%r14,0f
	.long	ftrace_trace_function
0:	l	%r14,0(%r14)
	l	%r14,0(%r14)
	basr	%r14,%r14
	ahi	%r15,96
	lm	%r0,%r5,8(%r15)
	l	%r14,56(%r15)
	br	%r14

.globl ftrace_stub
ftrace_stub:
	br	%r14

#else /* CONFIG_64BIT */

.globl _mcount
_mcount:
	stmg	%r0,%r5,16(%r15)
	stg	%r14,112(%r15)
	lgr	%r1,%r15
	aghi	%r15,-160
	stg	%r1,__SF_BACKCHAIN(%r15)
	lgr	%r2,%r14
	lg	%r3,168(%r15)
	larl	%r14,ftrace_trace_function
	lg	%r14,0(%r14)
	basr	%r14,%r14
	aghi	%r15,160
	lmg	%r0,%r5,16(%r15)
	lg	%r14,112(%r15)
	br	%r14

.globl ftrace_stub
ftrace_stub:
	br	%r14

#endif /* CONFIG_64BIT */
