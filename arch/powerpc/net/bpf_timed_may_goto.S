/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 IBM Corporation, Saket Kumar Bhaskar <skb99@linux.ibm.com> */

#include <linux/linkage.h>
#include <asm/ppc_asm.h>

/*
 * arch_bpf_timed_may_goto() trampoline for powerpc64
 *
 * Custom BPF convention (verifier/JIT):
 *	- input:  stack offset in BPF_REG_AX (r12)
 *	- output: updated count in BPF_REG_AX (r12)
 *
 * Call bpf_check_timed_may_goto(ptr) with normal powerpc64 ABI:
 * 	- r3 = ptr, return in r3
 *
 * Preserve BPF regs R0-R5 (mapping: r8, r3-r7).
 */

SYM_FUNC_START(arch_bpf_timed_may_goto)
	/* Prologue: save LR, allocate frame */
	mflr    r0
	std     r0, 16(r1)
	stdu    r1, -112(r1)

	/* Save BPF registers R0 - R5 (r8, r3-r7) */
	std     r3, 32(r1)
	std     r4, 40(r1)
	std     r5, 48(r1)
	std     r6, 56(r1)
	std     r7, 64(r1)
	std     r8, 72(r1)

	/*
	 * r3 = BPF_REG_FP + BPF_REG_AX
	 * BPF_REG_FP is r31; BPF_REG_AX is r12 (stack offset in bytes).
	 */
	add     r3, r31, r12
	bl      bpf_check_timed_may_goto

	/* Put return value back into AX */
	mr      r12, r3

	/* Restore BPF registers R0 - R5 (r8, r3-r7) */
	ld      r3, 32(r1)
	ld      r4, 40(r1)
	ld      r5, 48(r1)
	ld      r6, 56(r1)
	ld      r7, 64(r1)
	ld      r8, 72(r1)

	/* Epilogue: pop frame, restore LR, return */
	addi    r1, r1, 112
	ld      r0, 16(r1)
	mtlr    r0
	blr
SYM_FUNC_END(arch_bpf_timed_may_goto)
