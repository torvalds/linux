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
	.macro SAVE_ALL
	addi	sp, sp, -PT_SIZE_ON_STACK

	REG_S t0,  PT_EPC(sp)
	REG_S x1,  PT_RA(sp)
	REG_S x2,  PT_SP(sp)
	REG_S x3,  PT_GP(sp)
	REG_S x4,  PT_TP(sp)
	REG_S x5,  PT_T0(sp)
	save_from_x6_to_x31
	.endm

	.macro RESTORE_ALL
	REG_L x1,  PT_RA(sp)
	REG_L x2,  PT_SP(sp)
	REG_L x3,  PT_GP(sp)
	REG_L x4,  PT_TP(sp)
	/* Restore t0 with PT_EPC */
	REG_L x5,  PT_EPC(sp)
	restore_from_x6_to_x31

	addi	sp, sp, PT_SIZE_ON_STACK
	.endm
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */

ENTRY(ftrace_caller)
	SAVE_ABI

	addi	a0, t0, -FENTRY_RA_OFFSET
	la	a1, function_trace_op
	REG_L	a2, 0(a1)
	mv	a1, ra
	mv	a3, sp

ftrace_call:
	.global ftrace_call
	call	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	addi	a0, sp, ABI_RA
	REG_L	a1, ABI_T0(sp)
	addi	a1, a1, -FENTRY_RA_OFFSET
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	mv	a2, s0
#endif
ftrace_graph_call:
	.global ftrace_graph_call
	call	ftrace_stub
#endif
	RESTORE_ABI
	jr t0
ENDPROC(ftrace_caller)

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
ENTRY(ftrace_regs_caller)
	SAVE_ALL

	addi	a0, t0, -FENTRY_RA_OFFSET
	la	a1, function_trace_op
	REG_L	a2, 0(a1)
	mv	a1, ra
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
	jr t0
ENDPROC(ftrace_regs_caller)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */
