/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <linux/export.h>
#include <asm/asm.h>
#include <asm/csr.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>

#define ABI_SIZE_ON_STACK	80

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
	addi	sp, sp, -ABI_SIZE_ON_STACK
	REG_S	ra, 1*SZREG(sp)
	REG_S	s0, 8*SZREG(sp)
	REG_S	a0, 10*SZREG(sp)
	REG_S	a1, 11*SZREG(sp)
	addi	s0, sp, ABI_SIZE_ON_STACK
	.endm

	.macro RESTORE_ABI_STATE
	REG_L	ra, 1*SZREG(sp)
	REG_L	s0, 0*SZREG(sp)
	addi	sp, sp, 16
	.endm

	.macro RESTORE_RET_ABI_STATE
	REG_L	ra, 1*SZREG(sp)
	REG_L	s0, 8*SZREG(sp)
	REG_L	a0, 10*SZREG(sp)
	REG_L	a1, 11*SZREG(sp)
	addi	sp, sp, ABI_SIZE_ON_STACK
	.endm

SYM_TYPED_FUNC_START(ftrace_stub)
#ifdef CONFIG_DYNAMIC_FTRACE
       .global _mcount
       .set    _mcount, ftrace_stub
#endif
	ret
SYM_FUNC_END(ftrace_stub)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
SYM_TYPED_FUNC_START(ftrace_stub_graph)
	ret
SYM_FUNC_END(ftrace_stub_graph)

SYM_FUNC_START(return_to_handler)
/*
 * On implementing the frame point test, the ideal way is to compare the
 * s0 (frame pointer, if enabled) on entry and the sp (stack pointer) on return.
 * However, the psABI of variable-length-argument functions does not allow this.
 *
 * So alternatively we check the *old* frame pointer position, that is, the
 * value stored in -16(s0) on entry, and the s0 on return.
 */
	SAVE_RET_ABI_STATE
	mv	a0, sp
	call	ftrace_return_to_handler
	mv	a2, a0
	RESTORE_RET_ABI_STATE
	jalr	a2
SYM_FUNC_END(return_to_handler)
#endif

#ifndef CONFIG_DYNAMIC_FTRACE
SYM_FUNC_START(_mcount)
	la	t4, ftrace_stub
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	la	t0, ftrace_graph_return
	REG_L	t1, 0(t0)
	bne	t1, t4, .Ldo_ftrace_graph_caller

	la	t3, ftrace_graph_entry
	REG_L	t2, 0(t3)
	la	t6, ftrace_graph_entry_stub
	bne	t2, t6, .Ldo_ftrace_graph_caller
#endif
	la	t3, ftrace_trace_function
	REG_L	t5, 0(t3)
	bne	t5, t4, .Ldo_trace
	ret

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * A pseudo representation for the function graph tracer:
 * prepare_to_return(&ra_to_caller_of_caller, ra_to_caller)
 */
.Ldo_ftrace_graph_caller:
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
.Ldo_trace:
	REG_L	a1, -SZREG(s0)
	mv	a0, ra

	SAVE_ABI_STATE
	jalr	t5
	RESTORE_ABI_STATE
	ret
SYM_FUNC_END(_mcount)
#endif
EXPORT_SYMBOL(_mcount)
