/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Puranjay Mohan <puranjay@kernel.org> */

#include <linux/linkage.h>

SYM_FUNC_START(arch_bpf_timed_may_goto)
	/* Allocate stack space and emit frame record */
	stp     x29, x30, [sp, #-96]!
	mov     x29, sp

	/* Save BPF registers R0 - R5 (x8, x0-x4)*/
	stp	x8, x0, [sp, #16]
	stp	x1, x2, [sp, #32]
	stp	x3, x4, [sp, #48]
	/* Save stack arguments 6 - 8 (x5-x7) */
	stp	x5, x6, [sp, #64]
	str	x7, [sp, #80]

	/*
	 * The pointer to count and timestamp was passed in BPF_REG_AX (x9),
	 * pass it as the first argument in x0.
	 */
	mov	x0, x9
	bl	bpf_check_timed_may_goto
	/* BPF_REG_AX(x9) will be stored into count, so move return value to it. */
	mov	x9, x0

	/* Restore BPF registers R0 - R5 (x8, x0-x4) */
	ldp	x8, x0, [sp, #16]
	ldp	x1, x2, [sp, #32]
	ldp	x3, x4, [sp, #48]
	/* Restore stack arguments 6 - 8 (x5-x7) */
	ldp	x5, x6, [sp, #64]
	ldr	x7, [sp, #80]

	/* Restore FP and LR */
	ldp     x29, x30, [sp], #96

	ret
SYM_FUNC_END(arch_bpf_timed_may_goto)
