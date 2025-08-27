/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Puranjay Mohan <puranjay@kernel.org> */

#include <linux/linkage.h>

SYM_FUNC_START(arch_bpf_timed_may_goto)
	/* Allocate stack space and emit frame record */
	stp     x29, x30, [sp, #-64]!
	mov     x29, sp

	/* Save BPF registers R0 - R5 (x7, x0-x4)*/
	stp	x7, x0, [sp, #16]
	stp	x1, x2, [sp, #32]
	stp	x3, x4, [sp, #48]

	/*
	 * Stack depth was passed in BPF_REG_AX (x9), add it to the BPF_FP
	 * (x25) to get the pointer to count and timestamp and pass it as the
	 * first argument in x0.
	 *
	 * Before generating the call to arch_bpf_timed_may_goto, the verifier
	 * generates a load instruction using FP, i.e. REG_AX = *(u64 *)(FP -
	 * stack_off_cnt), so BPF_REG_FP (x25) is always set up by the arm64
	 * jit in this case.
	 */
	add	x0, x9, x25
	bl	bpf_check_timed_may_goto
	/* BPF_REG_AX(x9) will be stored into count, so move return value to it. */
	mov	x9, x0

	/* Restore BPF registers R0 - R5 (x7, x0-x4) */
	ldp	x7, x0, [sp, #16]
	ldp	x1, x2, [sp, #32]
	ldp	x3, x4, [sp, #48]

	/* Restore FP and LR */
	ldp     x29, x30, [sp], #64

	ret
SYM_FUNC_END(arch_bpf_timed_may_goto)
