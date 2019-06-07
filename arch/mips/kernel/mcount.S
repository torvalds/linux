/*
 * MIPS specific _mcount support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive for
 * more details.
 *
 * Copyright (C) 2009 Lemote Inc. & DSLab, Lanzhou University, China
 * Copyright (C) 2010 DSLab, Lanzhou University, China
 * Author: Wu Zhangjin <wuzhangjin@gmail.com>
 */

#include <asm/export.h>
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
	PTR_ADDIU	sp, PT_SIZE
	.endm

	.macro RETURN_BACK
	jr ra
	 move ra, AT
	.endm

/*
 * The -mmcount-ra-address option of gcc 4.5 uses register $12 to pass
 * the location of the parent's return address.
 */
#define MCOUNT_RA_ADDRESS_REG	$12

#ifdef CONFIG_DYNAMIC_FTRACE

NESTED(ftrace_caller, PT_SIZE, ra)
	.globl _mcount
_mcount:
EXPORT_SYMBOL(_mcount)
	b	ftrace_stub
#ifdef CONFIG_32BIT
	 addiu sp,sp,8
#else
	 nop
#endif

	/* When tracing is activated, it calls ftrace_caller+8 (aka here) */
	MCOUNT_SAVE_REGS
#ifdef KBUILD_MCOUNT_RA_ADDRESS
	PTR_S	MCOUNT_RA_ADDRESS_REG, PT_R12(sp)
#endif

	PTR_SUBU a0, ra, 8	/* arg1: self address */
	PTR_LA   t1, _stext
	sltu     t2, a0, t1	/* t2 = (a0 < _stext) */
	PTR_LA   t1, _etext
	sltu     t3, t1, a0	/* t3 = (a0 > _etext) */
	or       t1, t2, t3
	beqz     t1, ftrace_call
	 nop
#if defined(KBUILD_MCOUNT_RA_ADDRESS) && defined(CONFIG_32BIT)
	PTR_SUBU a0, a0, 16	/* arg1: adjust to module's recorded callsite */
#else
	PTR_SUBU a0, a0, 12
#endif

	.globl ftrace_call
ftrace_call:
	nop	/* a placeholder for the call to a real tracing function */
	 move	a1, AT		/* arg2: parent's return address */

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
EXPORT_SYMBOL(_mcount)
	PTR_LA	t1, ftrace_stub
	PTR_L	t2, ftrace_trace_function /* Prepare t2 for (1) */
	beq	t1, t2, fgraph_trace
	 nop

	MCOUNT_SAVE_REGS

	move	a0, ra		/* arg1: self return address */
	jalr	t2		/* (1) call *ftrace_trace_function */
	 move	a1, AT		/* arg2: parent's return address */

	MCOUNT_RESTORE_REGS

fgraph_trace:
#ifdef	CONFIG_FUNCTION_GRAPH_TRACER
	PTR_LA	t1, ftrace_stub
	PTR_L	t3, ftrace_graph_return
	bne	t1, t3, ftrace_graph_caller
	 nop
	PTR_LA	t1, ftrace_graph_entry_stub
	PTR_L	t3, ftrace_graph_entry
	bne	t1, t3, ftrace_graph_caller
	 nop
#endif

#ifdef CONFIG_32BIT
	addiu sp, sp, 8
#endif

	.globl ftrace_stub
ftrace_stub:
	RETURN_BACK
	END(_mcount)

#endif	/* ! CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

NESTED(ftrace_graph_caller, PT_SIZE, ra)
#ifndef CONFIG_DYNAMIC_FTRACE
	MCOUNT_SAVE_REGS
#endif

	/* arg1: Get the location of the parent's return address */
#ifdef KBUILD_MCOUNT_RA_ADDRESS
#ifdef CONFIG_DYNAMIC_FTRACE
	PTR_L	a0, PT_R12(sp)
#else
	move	a0, MCOUNT_RA_ADDRESS_REG
#endif
	bnez	a0, 1f	/* non-leaf func: stored in MCOUNT_RA_ADDRESS_REG */
	 nop
#endif
	PTR_LA	a0, PT_R1(sp)	/* leaf func: the location in current stack */
1:

	/* arg2: Get self return address */
#ifdef CONFIG_DYNAMIC_FTRACE
	PTR_L	a1, PT_R31(sp)
#else
	move	a1, ra
#endif

	/* arg3: Get frame pointer of current stack */
#ifdef CONFIG_64BIT
	PTR_LA	a2, PT_SIZE(sp)
#else
	PTR_LA	a2, (PT_SIZE+8)(sp)
#endif

	jal	prepare_ftrace_return
	 nop
	MCOUNT_RESTORE_REGS
#ifndef CONFIG_DYNAMIC_FTRACE
#ifdef CONFIG_32BIT
	addiu sp, sp, 8
#endif
#endif
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
