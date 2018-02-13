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
	sd	s0, 0(sp)
	sd	ra, 8(sp)
	addi	s0, sp, 16
	.endm

	.macro RESTORE_ABI_STATE
	ld	ra, 8(sp)
	ld	s0, 0(sp)
	addi	sp, sp, 16
	.endm

ENTRY(ftrace_caller)
	/*
	 * a0: the address in the caller when calling ftrace_caller
	 * a1: the caller's return address
	 */
	ld	a1, -8(s0)
	addi	a0, ra, -MCOUNT_INSN_SIZE
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
	RESTORE_ABI_STATE
	ret
ENDPROC(ftrace_caller)
