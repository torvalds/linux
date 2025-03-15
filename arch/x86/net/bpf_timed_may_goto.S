// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/nospec-branch.h>

	.code64
	.section .text, "ax"

SYM_FUNC_START(arch_bpf_timed_may_goto)
	ANNOTATE_NOENDBR

	/*
	 * r10 passes us stack depth, load the pointer to count and timestamp
	 * into r10 by adding it to BPF frame pointer.
	 */
	leaq (%rbp, %r10, 1), %r10

	/* Setup frame. */
	pushq %rbp
	movq %rsp, %rbp

	/* Save r0-r5. */
	pushq %rax
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %r8

	/*
	 * r10 has the pointer to count and timestamp, pass it as first
	 * argument.
	 */
	movq %r10, %rdi

	/* Emit call depth accounting for call below. */
	CALL_DEPTH_ACCOUNT
	call bpf_check_timed_may_goto

	/* BPF_REG_AX=r10 will be stored into count, so move return value to it. */
	movq %rax, %r10

	/* Restore r5-r0. */
	popq %r8
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %rax

	leave
	RET
SYM_FUNC_END(arch_bpf_timed_may_goto)
