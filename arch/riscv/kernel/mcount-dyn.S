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

/**
* SAVE_ABI_REGS - save regs against the ftrace_regs struct
*
* After the stack is established,
*
* 0(sp) stores the PC of the traced function which can be accessed
* by &(fregs)->epc in tracing function.
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
	addi	sp, sp, -FREGS_SIZE_ON_STACK
	REG_S	t0,  FREGS_EPC(sp)
	REG_S	x1,  FREGS_RA(sp)
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	REG_S	x8,  FREGS_S0(sp)
#endif
	REG_S	x6,  FREGS_T1(sp)
#ifdef CONFIG_CC_IS_CLANG
	REG_S	x7,  FREGS_T2(sp)
	REG_S	x28, FREGS_T3(sp)
	REG_S	x29, FREGS_T4(sp)
	REG_S	x30, FREGS_T5(sp)
	REG_S	x31, FREGS_T6(sp)
#endif
	// save the arguments
	REG_S	x10, FREGS_A0(sp)
	REG_S	x11, FREGS_A1(sp)
	REG_S	x12, FREGS_A2(sp)
	REG_S	x13, FREGS_A3(sp)
	REG_S	x14, FREGS_A4(sp)
	REG_S	x15, FREGS_A5(sp)
	REG_S	x16, FREGS_A6(sp)
	REG_S	x17, FREGS_A7(sp)
	mv	a0, sp
	addi	a0, a0, FREGS_SIZE_ON_STACK
	REG_S	a0, FREGS_SP(sp)	// Put original SP on stack
	.endm

	.macro RESTORE_ABI_REGS
	REG_L	t0, FREGS_EPC(sp)
	REG_L	x1, FREGS_RA(sp)
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	REG_L	x8, FREGS_S0(sp)
#endif
	REG_L	x6,  FREGS_T1(sp)
#ifdef CONFIG_CC_IS_CLANG
	REG_L	x7,  FREGS_T2(sp)
	REG_L	x28, FREGS_T3(sp)
	REG_L	x29, FREGS_T4(sp)
	REG_L	x30, FREGS_T5(sp)
	REG_L	x31, FREGS_T6(sp)
#endif
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
	addi	a0, t0, -MCOUNT_JALR_SIZE	// ip (callsite's jalr insn)
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
	mv 	a1, ra				// parent_ip
	REG_L   a2, -16(t0)			// op
	REG_L   ra, FTRACE_OPS_FUNC(a2)		// op->func
#else
	la	a1, function_trace_op
	REG_L	a2, 0(a1)			// op
	mv	a1, ra				// parent_ip
#endif
	mv	a3, sp				// regs
	.endm

SYM_FUNC_START(ftrace_caller)
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
	/*
	 * When CALL_OPS is enabled (2 or 4) nops [8B] are placed before the
	 * function entry, these are later overwritten with the pointer to the
	 * associated struct ftrace_ops.
	 *
	 * -8: &ftrace_ops of the associated tracer function.
	 *<ftrace enable>:
	 *  0: auipc  t0/ra, 0x?
	 *  4: jalr   t0/ra, ?(t0/ra)
	 *
	 * -8: &ftrace_nop_ops
	 *<ftrace disable>:
	 *  0: nop
	 *  4: nop
	 *
	 * t0 is set to ip+8 after the jalr is executed at the callsite,
	 * so we find the associated op at t0-16.
	 */
	REG_L	t1, -16(t0) // op Should be SZ_REG instead of 16

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	/*
	 * If the op has a direct call, handle it immediately without
	 * saving/restoring registers.
	 */
	REG_L	t1, FTRACE_OPS_DIRECT_CALL(t1)
	bnez	t1, ftrace_caller_direct
#endif
#endif
	SAVE_ABI_REGS
	PREPARE_ARGS

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_CALL_OPS
	jalr	ra
#else
SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)
	REG_L	ra, ftrace_call_dest
	jalr	ra, 0(ra)
#endif
	RESTORE_ABI_REGS
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	bnez	t1, ftrace_caller_direct
#endif
	jr	t0
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
SYM_INNER_LABEL(ftrace_caller_direct, SYM_L_LOCAL)
	jr	t1
#endif
SYM_FUNC_END(ftrace_caller)

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
SYM_CODE_START(ftrace_stub_direct_tramp)
	jr	t0
SYM_CODE_END(ftrace_stub_direct_tramp)
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */
