/*
 * MIPS specific _mcount support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive for
 * more details.
 *
 * Copyright (C) 2009 Lemote Inc. & DSLab, Lanzhou University, China
 * Author: Wu Zhangjin <wuzj@lemote.com>
 */

#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/ftrace.h>

	.text
	.set noreorder
	.set noat

	.macro MCOUNT_SAVE_REGS
	PTR_SUBU	sp, PT_SIZE
	PTR_S	ra, PT_R31(sp)
	PTR_S	AT, PT_R1(sp)
	PTR_S	a0, PT_R4(sp)
	PTR_S	a1, PT_R5(sp)
	PTR_S	a2, PT_R6(sp)
	PTR_S	a3, PT_R7(sp)
#ifdef CONFIG_64BIT
	PTR_S	a4, PT_R8(sp)
	PTR_S	a5, PT_R9(sp)
	PTR_S	a6, PT_R10(sp)
	PTR_S	a7, PT_R11(sp)
#endif
	.endm

	.macro MCOUNT_RESTORE_REGS
	PTR_L	ra, PT_R31(sp)
	PTR_L	AT, PT_R1(sp)
	PTR_L	a0, PT_R4(sp)
	PTR_L	a1, PT_R5(sp)
	PTR_L	a2, PT_R6(sp)
	PTR_L	a3, PT_R7(sp)
#ifdef CONFIG_64BIT
	PTR_L	a4, PT_R8(sp)
	PTR_L	a5, PT_R9(sp)
	PTR_L	a6, PT_R10(sp)
	PTR_L	a7, PT_R11(sp)
#endif
#ifdef CONFIG_64BIT
	PTR_ADDIU	sp, PT_SIZE
#else
	PTR_ADDIU	sp, (PT_SIZE + 8)
#endif
.endm

	.macro RETURN_BACK
	jr ra
	 move ra, AT
	.endm

#ifdef CONFIG_DYNAMIC_FTRACE

NESTED(ftrace_caller, PT_SIZE, ra)
	.globl _mcount
_mcount:
	b	ftrace_stub
	 nop
	lw	t0, function_trace_stop
	bnez	t0, ftrace_stub
	 nop

	MCOUNT_SAVE_REGS

	move	a0, ra		/* arg1: next ip, selfaddr */
	.globl ftrace_call
ftrace_call:
	nop	/* a placeholder for the call to a real tracing function */
	 move	a1, AT		/* arg2: the caller's next ip, parent */

	MCOUNT_RESTORE_REGS
	.globl ftrace_stub
ftrace_stub:
	RETURN_BACK
	END(ftrace_caller)

#else	/* ! CONFIG_DYNAMIC_FTRACE */

NESTED(_mcount, PT_SIZE, ra)
	lw	t0, function_trace_stop
	bnez	t0, ftrace_stub
	 nop
	PTR_LA	t0, ftrace_stub
	PTR_L	t1, ftrace_trace_function /* Prepare t1 for (1) */
	bne	t0, t1, static_trace
	 nop

#ifdef	CONFIG_FUNCTION_GRAPH_TRACER
	PTR_L	t2, ftrace_graph_return
	bne	t0, t2, ftrace_graph_caller
	 nop
	PTR_LA	t0, ftrace_graph_entry_stub
	PTR_L	t2, ftrace_graph_entry
	bne	t0, t2, ftrace_graph_caller
	 nop
#endif
	b	ftrace_stub
	 nop

static_trace:
	MCOUNT_SAVE_REGS

	move	a0, ra		/* arg1: next ip, selfaddr */
	jalr	t1	/* (1) call *ftrace_trace_function */
	 move	a1, AT		/* arg2: the caller's next ip, parent */

	MCOUNT_RESTORE_REGS
	.globl ftrace_stub
ftrace_stub:
	RETURN_BACK
	END(_mcount)

#endif	/* ! CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

NESTED(ftrace_graph_caller, PT_SIZE, ra)
	MCOUNT_SAVE_REGS

	PTR_LA	a0, PT_R1(sp)	/* arg1: &AT -> a0 */
	move	a1, ra		/* arg2: next ip, selfaddr */
	jal	prepare_ftrace_return
	 move	a2, fp		/* arg3: frame pointer */

	MCOUNT_RESTORE_REGS
	RETURN_BACK
	END(ftrace_graph_caller)

	.align	2
	.globl	return_to_handler
return_to_handler:
	PTR_SUBU	sp, PT_SIZE
	PTR_S	v0, PT_R2(sp)

	jal	ftrace_return_to_handler
	 PTR_S	v1, PT_R3(sp)

	/* restore the real parent address: v0 -> ra */
	move	ra, v0

	PTR_L	v0, PT_R2(sp)
	PTR_L	v1, PT_R3(sp)
	jr	ra
	 PTR_ADDIU	sp, PT_SIZE
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	.set at
	.set reorder
