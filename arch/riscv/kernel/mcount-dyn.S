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
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	addi    sp, sp, -48
	sd      s0, 32(sp)
	sd      ra, 40(sp)
	addi    s0, sp, 48
	sd      t0, 24(sp)
	sd      t1, 16(sp)
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	sd      t2, 8(sp)
#endif
#else
	addi	sp, sp, -16
	sd	s0, 0(sp)
	sd	ra, 8(sp)
	addi	s0, sp, 16
#endif
	.endm

	.macro RESTORE_ABI_STATE
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	ld	s0, 32(sp)
	ld	ra, 40(sp)
	addi	sp, sp, 48
#else
	ld	ra, 8(sp)
	ld	s0, 0(sp)
	addi	sp, sp, 16
#endif
	.endm

	.macro RESTORE_GRAPH_ARGS
	ld	a0, 24(sp)
	ld	a1, 16(sp)
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	ld	a2, 8(sp)
#endif
	.endm

ENTRY(ftrace_graph_caller)
	addi	sp, sp, -16
	sd	s0, 0(sp)
	sd	ra, 8(sp)
	addi	s0, sp, 16
ftrace_graph_call:
	.global ftrace_graph_call
	/*
	 * Calling ftrace_enable/disable_ftrace_graph_caller would overwrite the
	 * call below.  Check ftrace_modify_all_code for details.
	 */
	call	ftrace_stub
	ld	ra, 8(sp)
	ld	s0, 0(sp)
	addi	sp, sp, 16
	ret
ENDPROC(ftrace_graph_caller)

ENTRY(ftrace_caller)
	/*
	 * a0: the address in the caller when calling ftrace_caller
	 * a1: the caller's return address
	 */
	ld	a1, -8(s0)
	addi	a0, ra, -MCOUNT_INSN_SIZE

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/*
	 * the graph tracer (specifically, prepare_ftrace_return) needs these
	 * arguments but for now the function tracer occupies the regs, so we
	 * save them in temporary regs to recover later.
	 */
	addi	t0, s0, -8
	mv	t1, a0
#ifdef HAVE_FUNCTION_GRAPH_FP_TEST
	ld	t2, -16(s0)
#endif
#endif

	SAVE_ABI_STATE
ftrace_call:
	.global ftrace_call
	/*
	 * For the dynamic ftrace to work, here we should reserve at least
	 * 8 bytes for a functional auipc-jalr pair.  The following call
	 * serves this purpose.
	 *
	 * Calling ftrace_update_ftrace_func would overwrite the nops below.
	 * Check ftrace_modify_all_code for details.
	 */
	call	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	RESTORE_GRAPH_ARGS
	call	ftrace_graph_caller
#endif

	RESTORE_ABI_STATE
	ret
ENDPROC(ftrace_caller)
