/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * TILE-Gx specific __mcount support
 */

#include <linux/linkage.h>
#include <asm/ftrace.h>

#define REGSIZE 8

	.text
	.global __mcount

	.macro	MCOUNT_SAVE_REGS
	addli	sp, sp, -REGSIZE
	{
	 st     sp, lr
	 addli	r29, sp, - (12 * REGSIZE)
	}
	{
	 addli	sp, sp, - (13 * REGSIZE)
	 st     r29, sp
	}
	addli	r29, r29, REGSIZE
	{ st	r29, r0; addli	r29, r29, REGSIZE }
	{ st	r29, r1; addli	r29, r29, REGSIZE }
	{ st	r29, r2; addli	r29, r29, REGSIZE }
	{ st	r29, r3; addli	r29, r29, REGSIZE }
	{ st	r29, r4; addli	r29, r29, REGSIZE }
	{ st	r29, r5; addli	r29, r29, REGSIZE }
	{ st	r29, r6; addli	r29, r29, REGSIZE }
	{ st	r29, r7; addli	r29, r29, REGSIZE }
	{ st	r29, r8; addli	r29, r29, REGSIZE }
	{ st	r29, r9; addli	r29, r29, REGSIZE }
	{ st	r29, r10; addli	r29, r29, REGSIZE }
	.endm

	.macro	MCOUNT_RESTORE_REGS
	addli	r29, sp, (2 * REGSIZE)
	{ ld	r0, r29; addli	r29, r29, REGSIZE }
	{ ld	r1, r29; addli	r29, r29, REGSIZE }
	{ ld	r2, r29; addli	r29, r29, REGSIZE }
	{ ld	r3, r29; addli	r29, r29, REGSIZE }
	{ ld	r4, r29; addli	r29, r29, REGSIZE }
	{ ld	r5, r29; addli	r29, r29, REGSIZE }
	{ ld	r6, r29; addli	r29, r29, REGSIZE }
	{ ld	r7, r29; addli	r29, r29, REGSIZE }
	{ ld	r8, r29; addli	r29, r29, REGSIZE }
	{ ld	r9, r29; addli	r29, r29, REGSIZE }
	{ ld	r10, r29; addli	lr, sp, (13 * REGSIZE) }
	{ ld	lr, lr;  addli	sp, sp, (14 * REGSIZE) }
	.endm

	.macro  RETURN_BACK
	{ move	r12, lr; move	lr, r10 }
	jrp	r12
	.endm

#ifdef CONFIG_DYNAMIC_FTRACE

	.align	64
STD_ENTRY(__mcount)
__mcount:
	j	ftrace_stub
STD_ENDPROC(__mcount)

	.align	64
STD_ENTRY(ftrace_caller)
	MCOUNT_SAVE_REGS

	/* arg1: self return address */
	/* arg2: parent's return address */
	{ move	r0, lr; move	r1, r10 }

	.global	ftrace_call
ftrace_call:
	/*
	 * a placeholder for the call to a real tracing function, i.e.
	 * ftrace_trace_function()
	 */
	nop

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.global	ftrace_graph_call
ftrace_graph_call:
	/*
	 * a placeholder for the call to a real tracing function, i.e.
	 * ftrace_graph_caller()
	 */
	nop
#endif
	MCOUNT_RESTORE_REGS
	.global	ftrace_stub
ftrace_stub:
	RETURN_BACK
STD_ENDPROC(ftrace_caller)

#else /* ! CONFIG_DYNAMIC_FTRACE */

	.align	64
STD_ENTRY(__mcount)
	{
	 moveli	r11, hw2_last(ftrace_trace_function)
	 moveli	r13, hw2_last(ftrace_stub)
	}
	{
	 shl16insli	r11, r11, hw1(ftrace_trace_function)
	 shl16insli	r13, r13, hw1(ftrace_stub)
	}
	{
	 shl16insli	r11, r11, hw0(ftrace_trace_function)
	 shl16insli	r13, r13, hw0(ftrace_stub)
	}

	ld	r11, r11
	sub	r14, r13, r11
	bnez	r14, static_trace

#ifdef	CONFIG_FUNCTION_GRAPH_TRACER
	moveli	r15, hw2_last(ftrace_graph_return)
	shl16insli	r15, r15, hw1(ftrace_graph_return)
	shl16insli	r15, r15, hw0(ftrace_graph_return)
	ld	r15, r15
	sub	r15, r15, r13
	bnez	r15, ftrace_graph_caller

	{
	 moveli	r16, hw2_last(ftrace_graph_entry)
	 moveli	r17, hw2_last(ftrace_graph_entry_stub)
	}
	{
	 shl16insli	r16, r16, hw1(ftrace_graph_entry)
	 shl16insli	r17, r17, hw1(ftrace_graph_entry_stub)
	}
	{
	 shl16insli	r16, r16, hw0(ftrace_graph_entry)
	 shl16insli	r17, r17, hw0(ftrace_graph_entry_stub)
	}
	ld	r16, r16
	sub	r17, r16, r17
	bnez	r17, ftrace_graph_caller

#endif
	RETURN_BACK

static_trace:
	MCOUNT_SAVE_REGS

	/* arg1: self return address */
	/* arg2: parent's return address */
	{ move	r0, lr; move	r1, r10 }

	/* call ftrace_trace_function() */
	jalr	r11

	MCOUNT_RESTORE_REGS

	.global ftrace_stub
ftrace_stub:
	RETURN_BACK
STD_ENDPROC(__mcount)

#endif	/* ! CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

STD_ENTRY(ftrace_graph_caller)
ftrace_graph_caller:
#ifndef CONFIG_DYNAMIC_FTRACE
	MCOUNT_SAVE_REGS
#endif

	/* arg1: Get the location of the parent's return address */
	addi	r0, sp, 12 * REGSIZE
	/* arg2: Get self return address */
	move	r1, lr

	jal prepare_ftrace_return

	MCOUNT_RESTORE_REGS
	RETURN_BACK
STD_ENDPROC(ftrace_graph_caller)

	.global return_to_handler
return_to_handler:
	MCOUNT_SAVE_REGS

	jal	ftrace_return_to_handler
	/* restore the real parent address */
	move	r11, r0

	MCOUNT_RESTORE_REGS
	jr	r11

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
