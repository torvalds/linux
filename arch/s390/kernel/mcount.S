/*
 * Copyright IBM Corp. 2008, 2009
 *
 *   Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>,
 *
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>
#include <asm/ptrace.h>

	.section .kprobes.text, "ax"

ENTRY(ftrace_stub)
	br	%r14

#define STACK_FRAME_SIZE  (STACK_FRAME_OVERHEAD + __PT_SIZE)
#define STACK_PTREGS	  (STACK_FRAME_OVERHEAD)
#define STACK_PTREGS_GPRS (STACK_PTREGS + __PT_GPRS)
#define STACK_PTREGS_PSW  (STACK_PTREGS + __PT_PSW)

ENTRY(_mcount)
	br	%r14

ENTRY(ftrace_caller)
	.globl	ftrace_regs_caller
	.set	ftrace_regs_caller,ftrace_caller
	lgr	%r1,%r15
#ifndef CC_USING_HOTPATCH
	aghi	%r0,MCOUNT_RETURN_FIXUP
#endif
	aghi	%r15,-STACK_FRAME_SIZE
	stg	%r1,__SF_BACKCHAIN(%r15)
	stg	%r1,(STACK_PTREGS_GPRS+15*8)(%r15)
	stg	%r0,(STACK_PTREGS_PSW+8)(%r15)
	stmg	%r2,%r14,(STACK_PTREGS_GPRS+2*8)(%r15)
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	aghik	%r2,%r0,-MCOUNT_INSN_SIZE
	lgrl	%r4,function_trace_op
	lgrl	%r1,ftrace_trace_function
#else
	lgr	%r2,%r0
	aghi	%r2,-MCOUNT_INSN_SIZE
	larl	%r4,function_trace_op
	lg	%r4,0(%r4)
	larl	%r1,ftrace_trace_function
	lg	%r1,0(%r1)
#endif
	lgr	%r3,%r14
	la	%r5,STACK_PTREGS(%r15)
	basr	%r14,%r1
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
# The j instruction gets runtime patched to a nop instruction.
# See ftrace_enable_ftrace_graph_caller.
ENTRY(ftrace_graph_caller)
	j	ftrace_graph_caller_end
	lg	%r2,(STACK_PTREGS_GPRS+14*8)(%r15)
	lg	%r3,(STACK_PTREGS_PSW+8)(%r15)
	brasl	%r14,prepare_ftrace_return
	stg	%r2,(STACK_PTREGS_GPRS+14*8)(%r15)
ftrace_graph_caller_end:
	.globl	ftrace_graph_caller_end
#endif
	lg	%r1,(STACK_PTREGS_PSW+8)(%r15)
	lmg	%r2,%r15,(STACK_PTREGS_GPRS+2*8)(%r15)
	br	%r1

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

ENTRY(return_to_handler)
	stmg	%r2,%r5,32(%r15)
	lgr	%r1,%r15
	aghi	%r15,-STACK_FRAME_OVERHEAD
	stg	%r1,__SF_BACKCHAIN(%r15)
	brasl	%r14,ftrace_return_to_handler
	aghi	%r15,STACK_FRAME_OVERHEAD
	lgr	%r14,%r2
	lmg	%r2,%r5,32(%r15)
	br	%r14

#endif
