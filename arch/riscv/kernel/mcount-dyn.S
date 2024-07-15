/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/export.h>
#include <asm/asm.h>
#include <asm/csr.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>

	.text

#define FENTRY_RA_OFFSET	8
#define ABI_SIZE_ON_STACK	80
#define ABI_A0			0
#define ABI_A1			8
#define ABI_A2			16
#define ABI_A3			24
#define ABI_A4			32
#define ABI_A5			40
#define ABI_A6			48
#define ABI_A7			56
#define ABI_T0			64
#define ABI_RA			72

	.macro SAVE_ABI
	addi	sp, sp, -ABI_SIZE_ON_STACK

	REG_S	a0, ABI_A0(sp)
	REG_S	a1, ABI_A1(sp)
	REG_S	a2, ABI_A2(sp)
	REG_S	a3, ABI_A3(sp)
	REG_S	a4, ABI_A4(sp)
	REG_S	a5, ABI_A5(sp)
	REG_S	a6, ABI_A6(sp)
	REG_S	a7, ABI_A7(sp)
	REG_S	t0, ABI_T0(sp)
	REG_S	ra, ABI_RA(sp)
	.endm

	.macro RESTORE_ABI
	REG_L	a0, ABI_A0(sp)
	REG_L	a1, ABI_A1(sp)
	REG_L	a2, ABI_A2(sp)
	REG_L	a3, ABI_A3(sp)
	REG_L	a4, ABI_A4(sp)
	REG_L	a5, ABI_A5(sp)
	REG_L	a6, ABI_A6(sp)
	REG_L	a7, ABI_A7(sp)
	REG_L	t0, ABI_T0(sp)
	REG_L	ra, ABI_RA(sp)

	addi	sp, sp, ABI_SIZE_ON_STACK
	.endm

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS

/**
* SAVE_ABI_REGS - save regs against the ftrace_regs struct
*
* After the stack is established,
*
* 0(sp) stores the PC of the traced function which can be accessed
* by &(fregs)->epc in tracing function. Note that the real
* function entry address should be computed with -FENTRY_RA_OFFSET.
*
* 8(sp) stores the function return address (i.e. parent IP) that
* can be accessed by &(fregs)->ra in tracing function.
*
* The other regs are saved at the respective localtion and accessed
* by the respective ftrace_regs member.
*
* Here is the layout of stack for your reference.
*
* PT_SIZE_ON_STACK  ->  +++++++++
*                       + ..... +
*                       + a0-a7 + --++++-> ftrace_caller saved
*                       + t1    + --++++-> direct tramp address
*                       + s0    + --+ // frame pointer
*                       + sp    +   +
*                       + ra    + --+ // parent IP
*               sp  ->  + epc   + --+ // PC
*                       +++++++++
**/
	.macro SAVE_ABI_REGS
	mv	t4, sp			// Save original SP in T4
	addi	sp, sp, -FREGS_SIZE_ON_STACK

	REG_S	t0,  FREGS_EPC(sp)
	REG_S	x1,  FREGS_RA(sp)
	REG_S	t4,  FREGS_SP(sp)	// Put original SP on stack
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	REG_S	x8,  FREGS_S0(sp)
#endif
	REG_S	x6,  FREGS_T1(sp)

	// save the arguments
	REG_S	x10, FREGS_A0(sp)
	REG_S	x11, FREGS_A1(sp)
	REG_S	x12, FREGS_A2(sp)
	REG_S	x13, FREGS_A3(sp)
	REG_S	x14, FREGS_A4(sp)
	REG_S	x15, FREGS_A5(sp)
	REG_S	x16, FREGS_A6(sp)
	REG_S	x17, FREGS_A7(sp)
	.endm

	.macro RESTORE_ABI_REGS, all=0
	REG_L	t0, FREGS_EPC(sp)
	REG_L	x1, FREGS_RA(sp)
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	REG_L	x8, FREGS_S0(sp)
#endif
	REG_L	x6,  FREGS_T1(sp)

	// restore the arguments
	REG_L	x10, FREGS_A0(sp)
	REG_L	x11, FREGS_A1(sp)
	REG_L	x12, FREGS_A2(sp)
	REG_L	x13, FREGS_A3(sp)
	REG_L	x14, FREGS_A4(sp)
	REG_L	x15, FREGS_A5(sp)
	REG_L	x16, FREGS_A6(sp)
	REG_L	x17, FREGS_A7(sp)

	addi	sp, sp, FREGS_SIZE_ON_STACK
	.endm

	.macro PREPARE_ARGS
	addi	a0, t0, -FENTRY_RA_OFFSET
	la	a1, function_trace_op
	REG_L	a2, 0(a1)
	mv	a1, ra
	mv	a3, sp
	.endm

#endif /* CONFIG_DYNAMIC_FTRACE_WITH_ARGS */

#ifndef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
SYM_FUNC_START(ftrace_caller)
	SAVE_ABI

	addi	a0, t0, -FENTRY_RA_OFFSET
	la	a1, function_trace_op
	REG_L	a2, 0(a1)
	mv	a1, ra
	mv	a3, sp

SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)
	call	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	addi	a0, sp, ABI_RA
	REG_L	a1, ABI_T0(sp)
	addi	a1, a1, -FENTRY_RA_OFFSET
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	mv	a2, s0
#endif
SYM_INNER_LABEL(ftrace_graph_call, SYM_L_GLOBAL)
	call	ftrace_stub
#endif
	RESTORE_ABI
	jr	t0
SYM_FUNC_END(ftrace_caller)

#else /* CONFIG_DYNAMIC_FTRACE_WITH_ARGS */
SYM_FUNC_START(ftrace_caller)
	mv	t1, zero
	SAVE_ABI_REGS
	PREPARE_ARGS

SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)
	call	ftrace_stub

	RESTORE_ABI_REGS
	bnez	t1, .Ldirect
	jr	t0
.Ldirect:
	jr	t1
SYM_FUNC_END(ftrace_caller)

#endif /* CONFIG_DYNAMIC_FTRACE_WITH_ARGS */

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
SYM_CODE_START(ftrace_stub_direct_tramp)
	jr	t0
SYM_CODE_END(ftrace_stub_direct_tramp)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */
