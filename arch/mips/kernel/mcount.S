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
	lw	t1, function_trace_stop
	bnez	t1, ftrace_stub
	 nop

	MCOUNT_SAVE_REGS
#ifdef KBUILD_MCOUNT_RA_ADDRESS
	PTR_S	t0, PT_R12(sp)	/* t0 saved the location of the return address(at) by -mmcount-ra-address */
#endif

	move	a0, ra		/* arg1: next ip, selfaddr */
	.globl ftrace_call
ftrace_call:
	nop	/* a placeholder for the call to a real tracing function */
	 move	a1, AT		/* arg2: the caller's next ip, parent */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl ftrace_graph_call
ftrace_graph_call:
	nop
	 nop
#endif

	MCOUNT_RESTORE_REGS
	.globl ftrace_stub
ftrace_stub:
	RETURN_BACK
	END(ftrace_caller)

#else	/* ! CONFIG_DYNAMIC_FTRACE */

NESTED(_mcount, PT_SIZE, ra)
	lw	t1, function_trace_stop
	bnez	t1, ftrace_stub
	 nop
	PTR_LA	t1, ftrace_stub
	PTR_L	t2, ftrace_trace_function /* Prepare t2 for (1) */
	bne	t1, t2, static_trace
	 nop

#ifdef	CONFIG_FUNCTION_GRAPH_TRACER
	PTR_L	t3, ftrace_graph_return
	bne	t1, t3, ftrace_graph_caller
	 nop
	PTR_LA	t1, ftrace_graph_entry_stub
	PTR_L	t3, ftrace_graph_entry
	bne	t1, t3, ftrace_graph_caller
	 nop
#endif
	b	ftrace_stub
	 nop

static_trace:
	MCOUNT_SAVE_REGS

	move	a0, ra		/* arg1: next ip, selfaddr */
	jalr	t2		/* (1) call *ftrace_trace_function */
	 move	a1, AT		/* arg2: the caller's next ip, parent */

	MCOUNT_RESTORE_REGS
	.globl ftrace_stub
ftrace_stub:
	RETURN_BACK
	END(_mcount)

#endif	/* ! CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

NESTED(ftrace_graph_caller, PT_SIZE, ra)
#ifdef CONFIG_DYNAMIC_FTRACE
	PTR_L	a1, PT_R31(sp)	/* load the original ra from the stack */
#ifdef KBUILD_MCOUNT_RA_ADDRESS
	PTR_L	t0, PT_R12(sp)	/* load the original t0 from the stack */
#endif
#else
	MCOUNT_SAVE_REGS
	move	a1, ra		/* arg2: next ip, selfaddr */
#endif

#ifdef KBUILD_MCOUNT_RA_ADDRESS
	bnez	t0, 1f		/* non-leaf func: t0 saved the location of the return address */
	 nop
	PTR_LA	t0, PT_R1(sp)	/* leaf func: get the location of at(old ra) from our own stack */
1:	move	a0, t0		/* arg1: the location of the return address */
#else
	PTR_LA	a0, PT_R1(sp)	/* arg1: &AT -> a0 */
#endif
	jal	prepare_ftrace_return
#ifdef CONFIG_FRAME_POINTER
	 move	a2, fp		/* arg3: frame pointer */
#else
#ifdef CONFIG_64BIT
	 PTR_LA	a2, PT_SIZE(sp)
#else
	 PTR_LA	a2, (PT_SIZE+8)(sp)
#endif
#endif

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
