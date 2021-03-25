/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/csr.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm-generic/export.h>
#include <asm/ftrace.h>

	.text

#define FENTRY_RA_OFFSET	12
#define ABI_SIZE_ON_STACK	72
#define ABI_A0			0
#define ABI_A1			8
#define ABI_A2			16
#define ABI_A3			24
#define ABI_A4			32
#define ABI_A5			40
#define ABI_A6			48
#define ABI_A7			56
#define ABI_RA			64

	.macro SAVE_ABI
	addi	sp, sp, -SZREG
	addi	sp, sp, -ABI_SIZE_ON_STACK

	REG_S	a0, ABI_A0(sp)
	REG_S	a1, ABI_A1(sp)
	REG_S	a2, ABI_A2(sp)
	REG_S	a3, ABI_A3(sp)
	REG_S	a4, ABI_A4(sp)
	REG_S	a5, ABI_A5(sp)
	REG_S	a6, ABI_A6(sp)
	REG_S	a7, ABI_A7(sp)
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
	REG_L	ra, ABI_RA(sp)

	addi	sp, sp, ABI_SIZE_ON_STACK
	addi	sp, sp, SZREG
	.endm

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	.macro SAVE_ALL
	addi	sp, sp, -SZREG
	addi	sp, sp, -PT_SIZE_ON_STACK

	REG_S x1,  PT_EPC(sp)
	addi	sp, sp, PT_SIZE_ON_STACK
	REG_L x1,  (sp)
	addi	sp, sp, -PT_SIZE_ON_STACK
	REG_S x1,  PT_RA(sp)
	REG_L x1,  PT_EPC(sp)

	REG_S x2,  PT_SP(sp)
	REG_S x3,  PT_GP(sp)
	REG_S x4,  PT_TP(sp)
	REG_S x5,  PT_T0(sp)
	REG_S x6,  PT_T1(sp)
	REG_S x7,  PT_T2(sp)
	REG_S x8,  PT_S0(sp)
	REG_S x9,  PT_S1(sp)
	REG_S x10, PT_A0(sp)
	REG_S x11, PT_A1(sp)
	REG_S x12, PT_A2(sp)
	REG_S x13, PT_A3(sp)
	REG_S x14, PT_A4(sp)
	REG_S x15, PT_A5(sp)
	REG_S x16, PT_A6(sp)
	REG_S x17, PT_A7(sp)
	REG_S x18, PT_S2(sp)
	REG_S x19, PT_S3(sp)
	REG_S x20, PT_S4(sp)
	REG_S x21, PT_S5(sp)
	REG_S x22, PT_S6(sp)
	REG_S x23, PT_S7(sp)
	REG_S x24, PT_S8(sp)
	REG_S x25, PT_S9(sp)
	REG_S x26, PT_S10(sp)
	REG_S x27, PT_S11(sp)
	REG_S x28, PT_T3(sp)
	REG_S x29, PT_T4(sp)
	REG_S x30, PT_T5(sp)
	REG_S x31, PT_T6(sp)
	.endm

	.macro RESTORE_ALL
	REG_L x1,  PT_RA(sp)
	addi	sp, sp, PT_SIZE_ON_STACK
	REG_S x1,  (sp)
	addi	sp, sp, -PT_SIZE_ON_STACK
	REG_L x1,  PT_EPC(sp)
	REG_L x2,  PT_SP(sp)
	REG_L x3,  PT_GP(sp)
	REG_L x4,  PT_TP(sp)
	REG_L x5,  PT_T0(sp)
	REG_L x6,  PT_T1(sp)
	REG_L x7,  PT_T2(sp)
	REG_L x8,  PT_S0(sp)
	REG_L x9,  PT_S1(sp)
	REG_L x10, PT_A0(sp)
	REG_L x11, PT_A1(sp)
	REG_L x12, PT_A2(sp)
	REG_L x13, PT_A3(sp)
	REG_L x14, PT_A4(sp)
	REG_L x15, PT_A5(sp)
	REG_L x16, PT_A6(sp)
	REG_L x17, PT_A7(sp)
	REG_L x18, PT_S2(sp)
	REG_L x19, PT_S3(sp)
	REG_L x20, PT_S4(sp)
	REG_L x21, PT_S5(sp)
	REG_L x22, PT_S6(sp)
	REG_L x23, PT_S7(sp)
	REG_L x24, PT_S8(sp)
	REG_L x25, PT_S9(sp)
	REG_L x26, PT_S10(sp)
	REG_L x27, PT_S11(sp)
	REG_L x28, PT_T3(sp)
	REG_L x29, PT_T4(sp)
	REG_L x30, PT_T5(sp)
	REG_L x31, PT_T6(sp)

	addi	sp, sp, PT_SIZE_ON_STACK
	addi	sp, sp, SZREG
	.endm
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */

ENTRY(ftrace_caller)
	SAVE_ABI

	addi	a0, ra, -FENTRY_RA_OFFSET
	la	a1, function_trace_op
	REG_L	a2, 0(a1)
	REG_L	a1, ABI_SIZE_ON_STACK(sp)
	mv	a3, sp

ftrace_call:
	.global ftrace_call
	call	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	addi	a0, sp, ABI_SIZE_ON_STACK
	REG_L	a1, ABI_RA(sp)
	addi	a1, a1, -FENTRY_RA_OFFSET
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	mv	a2, s0
#endif
ftrace_graph_call:
	.global ftrace_graph_call
	call	ftrace_stub
#endif
	RESTORE_ABI
	ret
ENDPROC(ftrace_caller)

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
ENTRY(ftrace_regs_caller)
	SAVE_ALL

	addi	a0, ra, -FENTRY_RA_OFFSET
	la	a1, function_trace_op
	REG_L	a2, 0(a1)
	REG_L	a1, PT_SIZE_ON_STACK(sp)
	mv	a3, sp

ftrace_regs_call:
	.global ftrace_regs_call
	call	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	addi	a0, sp, PT_RA
	REG_L	a1, PT_EPC(sp)
	addi	a1, a1, -FENTRY_RA_OFFSET
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	mv	a2, s0
#endif
ftrace_graph_regs_call:
	.global ftrace_graph_regs_call
	call	ftrace_stub
#endif

	RESTORE_ALL
	ret
ENDPROC(ftrace_regs_caller)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */
