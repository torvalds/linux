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

	.macro SAVE_ABI_STATE
	addi	sp, sp, -16
	REG_S	s0, 0*SZREG(sp)
	REG_S	ra, 1*SZREG(sp)
	addi	s0, sp, 16
	.endm

	/*
	 * The call to ftrace_return_to_handler would overwrite the return
	 * register if a0 was not saved.
	 */
	.macro SAVE_RET_ABI_STATE
	addi	sp, sp, -4*SZREG
	REG_S	s0, 2*SZREG(sp)
	REG_S	ra, 3*SZREG(sp)
	REG_S	a0, 1*SZREG(sp)
	REG_S	a1, 0*SZREG(sp)
	addi	s0, sp, 4*SZREG
	.endm

	.macro RESTORE_ABI_STATE
	REG_L	ra, 1*SZREG(sp)
	REG_L	s0, 0*SZREG(sp)
	addi	sp, sp, 16
	.endm

	.macro RESTORE_RET_ABI_STATE
	REG_L	ra, 3*SZREG(sp)
	REG_L	s0, 2*SZREG(sp)
	REG_L	a0, 1*SZREG(sp)
	REG_L	a1, 0*SZREG(sp)
	addi	sp, sp, 4*SZREG
	.endm

ENTRY(ftrace_stub)
#ifdef CONFIG_DYNAMIC_FTRACE
       .global MCOUNT_NAME
       .set    MCOUNT_NAME, ftrace_stub
#endif
	ret
ENDPROC(ftrace_stub)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(return_to_handler)
/*
 * On implementing the frame point test, the ideal way is to compare the
 * s0 (frame pointer, if enabled) on entry and the sp (stack pointer) on return.
 * However, the psABI of variable-length-argument functions does not allow this.
 *
 * So alternatively we check the *old* frame pointer position, that is, the
 * value stored in -16(s0) on entry, and the s0 on return.
 */
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	mv	t6, s0
#endif
	SAVE_RET_ABI_STATE
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	mv	a0, t6
#endif
	call	ftrace_return_to_handler
	mv	a2, a0
	RESTORE_RET_ABI_STATE
	jalr	a2
ENDPROC(return_to_handler)
#endif

#ifndef CONFIG_DYNAMIC_FTRACE
ENTRY(MCOUNT_NAME)
	la	t4, ftrace_stub
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	la	t0, ftrace_graph_return
	REG_L	t1, 0(t0)
	bne	t1, t4, do_ftrace_graph_caller

	la	t3, ftrace_graph_entry
	REG_L	t2, 0(t3)
	la	t6, ftrace_graph_entry_stub
	bne	t2, t6, do_ftrace_graph_caller
#endif
	la	t3, ftrace_trace_function
	REG_L	t5, 0(t3)
	bne	t5, t4, do_trace
	ret

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * A pseudo representation for the function graph tracer:
 * prepare_to_return(&ra_to_caller_of_caller, ra_to_caller)
 */
do_ftrace_graph_caller:
	addi	a0, s0, -SZREG
	mv	a1, ra
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	REG_L	a2, -2*SZREG(s0)
#endif
	SAVE_ABI_STATE
	call	prepare_ftrace_return
	RESTORE_ABI_STATE
	ret
#endif

/*
 * A pseudo representation for the function tracer:
 * (*ftrace_trace_function)(ra_to_caller, ra_to_caller_of_caller)
 */
do_trace:
	REG_L	a1, -SZREG(s0)
	mv	a0, ra

	SAVE_ABI_STATE
	jalr	t5
	RESTORE_ABI_STATE
	ret
ENDPROC(MCOUNT_NAME)
#endif
EXPORT_SYMBOL(MCOUNT_NAME)
