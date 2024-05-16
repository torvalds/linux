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

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS

/**
* SAVE_ABI_REGS - save regs against the pt_regs struct
*
* @all: tell if saving all the regs
*
* If all is set, all the regs will be saved, otherwise only ABI
* related regs (a0-a7,epc,ra and optional s0) will be saved.
*
* After the stack is established,
*
* 0(sp) stores the PC of the traced function which can be accessed
* by &(fregs)->regs->epc in tracing function. Note that the real
* function entry address should be computed with -FENTRY_RA_OFFSET.
*
* 8(sp) stores the function return address (i.e. parent IP) that
* can be accessed by &(fregs)->regs->ra in tracing function.
*
* The other regs are saved at the respective localtion and accessed
* by the respective pt_regs member.
*
* Here is the layout of stack for your reference.
*
* PT_SIZE_ON_STACK  ->  +++++++++
*                       + ..... +
*                       + t3-t6 +
*                       + s2-s11+
*                       + a0-a7 + --++++-> ftrace_caller saved
*                       + s1    +   +
*                       + s0    + --+
*                       + t0-t2 +   +
*                       + tp    +   +
*                       + gp    +   +
*                       + sp    +   +
*                       + ra    + --+ // parent IP
*               sp  ->  + epc   + --+ // PC
*                       +++++++++
**/
	.macro SAVE_ABI_REGS, all=0
	addi	sp, sp, -PT_SIZE_ON_STACK

	REG_S	t0,  PT_EPC(sp)
	REG_S	x1,  PT_RA(sp)

	// save the ABI regs

	REG_S	x10, PT_A0(sp)
	REG_S	x11, PT_A1(sp)
	REG_S	x12, PT_A2(sp)
	REG_S	x13, PT_A3(sp)
	REG_S	x14, PT_A4(sp)
	REG_S	x15, PT_A5(sp)
	REG_S	x16, PT_A6(sp)
	REG_S	x17, PT_A7(sp)

	// save the leftover regs

	.if \all == 1
	REG_S	x2, PT_SP(sp)
	REG_S	x3, PT_GP(sp)
	REG_S	x4, PT_TP(sp)
	REG_S	x5, PT_T0(sp)
	REG_S	x6, PT_T1(sp)
	REG_S	x7, PT_T2(sp)
	REG_S	x8, PT_S0(sp)
	REG_S	x9, PT_S1(sp)
	REG_S	x18, PT_S2(sp)
	REG_S	x19, PT_S3(sp)
	REG_S	x20, PT_S4(sp)
	REG_S	x21, PT_S5(sp)
	REG_S	x22, PT_S6(sp)
	REG_S	x23, PT_S7(sp)
	REG_S	x24, PT_S8(sp)
	REG_S	x25, PT_S9(sp)
	REG_S	x26, PT_S10(sp)
	REG_S	x27, PT_S11(sp)
	REG_S	x28, PT_T3(sp)
	REG_S	x29, PT_T4(sp)
	REG_S	x30, PT_T5(sp)
	REG_S	x31, PT_T6(sp)

	// save s0 if FP_TEST defined

	.else
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	REG_S	x8, PT_S0(sp)
#endif
	.endif
	.endm

	.macro RESTORE_ABI_REGS, all=0
	REG_L	t0, PT_EPC(sp)
	REG_L	x1, PT_RA(sp)
	REG_L	x10, PT_A0(sp)
	REG_L	x11, PT_A1(sp)
	REG_L	x12, PT_A2(sp)
	REG_L	x13, PT_A3(sp)
	REG_L	x14, PT_A4(sp)
	REG_L	x15, PT_A5(sp)
	REG_L	x16, PT_A6(sp)
	REG_L	x17, PT_A7(sp)

	.if \all == 1
	REG_L	x2, PT_SP(sp)
	REG_L	x3, PT_GP(sp)
	REG_L	x4, PT_TP(sp)
	REG_L	x6, PT_T1(sp)
	REG_L	x7, PT_T2(sp)
	REG_L	x8, PT_S0(sp)
	REG_L	x9, PT_S1(sp)
	REG_L	x18, PT_S2(sp)
	REG_L	x19, PT_S3(sp)
	REG_L	x20, PT_S4(sp)
	REG_L	x21, PT_S5(sp)
	REG_L	x22, PT_S6(sp)
	REG_L	x23, PT_S7(sp)
	REG_L	x24, PT_S8(sp)
	REG_L	x25, PT_S9(sp)
	REG_L	x26, PT_S10(sp)
	REG_L	x27, PT_S11(sp)
	REG_L	x28, PT_T3(sp)
	REG_L	x29, PT_T4(sp)
	REG_L	x30, PT_T5(sp)
	REG_L	x31, PT_T6(sp)

	.else
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	REG_L	x8, PT_S0(sp)
#endif
	.endif
	addi	sp, sp, PT_SIZE_ON_STACK
	.endm

	.macro PREPARE_ARGS
	addi	a0, t0, -FENTRY_RA_OFFSET
	la	a1, function_trace_op
	REG_L	a2, 0(a1)
	mv	a1, ra
	mv	a3, sp
	.endm

#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */

#ifndef CONFIG_DYNAMIC_FTRACE_WITH_REGS
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

#else /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */
SYM_FUNC_START(ftrace_regs_caller)
	mv	t1, zero
	SAVE_ABI_REGS 1
	PREPARE_ARGS

SYM_INNER_LABEL(ftrace_regs_call, SYM_L_GLOBAL)
	call	ftrace_stub

	RESTORE_ABI_REGS 1
	bnez	t1, .Ldirect
	jr	t0
.Ldirect:
	jr	t1
SYM_FUNC_END(ftrace_regs_caller)

SYM_FUNC_START(ftrace_caller)
	SAVE_ABI_REGS 0
	PREPARE_ARGS

SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)
	call	ftrace_stub

	RESTORE_ABI_REGS 0
	jr	t0
SYM_FUNC_END(ftrace_caller)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
SYM_CODE_START(ftrace_stub_direct_tramp)
	jr	t0
SYM_CODE_END(ftrace_stub_direct_tramp)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */
