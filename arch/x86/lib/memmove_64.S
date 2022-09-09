/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Normally compiler builtins are used, but sometimes the compiler calls out
 * of line code. Based on asm-i386/string.h.
 *
 * This assembly file is re-written from memmove_64.c file.
 *	- Copyright 2011 Fenghua Yu <fenghua.yu@intel.com>
 */
#include <linux/linkage.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/export.h>

#undef memmove

/*
 * Implement memmove(). This can handle overlap between src and dst.
 *
 * Input:
 * rdi: dest
 * rsi: src
 * rdx: count
 *
 * Output:
 * rax: dest
 */
SYM_FUNC_START(__memmove)

	mov %rdi, %rax

	/* Decide forward/backward copy mode */
	cmp %rdi, %rsi
	jge .Lmemmove_begin_forward
	mov %rsi, %r8
	add %rdx, %r8
	cmp %rdi, %r8
	jg 2f

	/* FSRM implies ERMS => no length checks, do the copy directly */
.Lmemmove_begin_forward:
	ALTERNATIVE "cmp $0x20, %rdx; jb 1f", "", X86_FEATURE_FSRM
	ALTERNATIVE "", "jmp .Lmemmove_erms", X86_FEATURE_ERMS

	/*
	 * movsq instruction have many startup latency
	 * so we handle small size by general register.
	 */
	cmp  $680, %rdx
	jb	3f
	/*
	 * movsq instruction is only good for aligned case.
	 */

	cmpb %dil, %sil
	je 4f
3:
	sub $0x20, %rdx
	/*
	 * We gobble 32 bytes forward in each loop.
	 */
5:
	sub $0x20, %rdx
	movq 0*8(%rsi), %r11
	movq 1*8(%rsi), %r10
	movq 2*8(%rsi), %r9
	movq 3*8(%rsi), %r8
	leaq 4*8(%rsi), %rsi

	movq %r11, 0*8(%rdi)
	movq %r10, 1*8(%rdi)
	movq %r9, 2*8(%rdi)
	movq %r8, 3*8(%rdi)
	leaq 4*8(%rdi), %rdi
	jae 5b
	addq $0x20, %rdx
	jmp 1f
	/*
	 * Handle data forward by movsq.
	 */
	.p2align 4
4:
	movq %rdx, %rcx
	movq -8(%rsi, %rdx), %r11
	lea -8(%rdi, %rdx), %r10
	shrq $3, %rcx
	rep movsq
	movq %r11, (%r10)
	jmp 13f
.Lmemmove_end_forward:

	/*
	 * Handle data backward by movsq.
	 */
	.p2align 4
7:
	movq %rdx, %rcx
	movq (%rsi), %r11
	movq %rdi, %r10
	leaq -8(%rsi, %rdx), %rsi
	leaq -8(%rdi, %rdx), %rdi
	shrq $3, %rcx
	std
	rep movsq
	cld
	movq %r11, (%r10)
	jmp 13f

	/*
	 * Start to prepare for backward copy.
	 */
	.p2align 4
2:
	cmp $0x20, %rdx
	jb 1f
	cmp $680, %rdx
	jb 6f
	cmp %dil, %sil
	je 7b
6:
	/*
	 * Calculate copy position to tail.
	 */
	addq %rdx, %rsi
	addq %rdx, %rdi
	subq $0x20, %rdx
	/*
	 * We gobble 32 bytes backward in each loop.
	 */
8:
	subq $0x20, %rdx
	movq -1*8(%rsi), %r11
	movq -2*8(%rsi), %r10
	movq -3*8(%rsi), %r9
	movq -4*8(%rsi), %r8
	leaq -4*8(%rsi), %rsi

	movq %r11, -1*8(%rdi)
	movq %r10, -2*8(%rdi)
	movq %r9, -3*8(%rdi)
	movq %r8, -4*8(%rdi)
	leaq -4*8(%rdi), %rdi
	jae 8b
	/*
	 * Calculate copy position to head.
	 */
	addq $0x20, %rdx
	subq %rdx, %rsi
	subq %rdx, %rdi
1:
	cmpq $16, %rdx
	jb 9f
	/*
	 * Move data from 16 bytes to 31 bytes.
	 */
	movq 0*8(%rsi), %r11
	movq 1*8(%rsi), %r10
	movq -2*8(%rsi, %rdx), %r9
	movq -1*8(%rsi, %rdx), %r8
	movq %r11, 0*8(%rdi)
	movq %r10, 1*8(%rdi)
	movq %r9, -2*8(%rdi, %rdx)
	movq %r8, -1*8(%rdi, %rdx)
	jmp 13f
	.p2align 4
9:
	cmpq $8, %rdx
	jb 10f
	/*
	 * Move data from 8 bytes to 15 bytes.
	 */
	movq 0*8(%rsi), %r11
	movq -1*8(%rsi, %rdx), %r10
	movq %r11, 0*8(%rdi)
	movq %r10, -1*8(%rdi, %rdx)
	jmp 13f
10:
	cmpq $4, %rdx
	jb 11f
	/*
	 * Move data from 4 bytes to 7 bytes.
	 */
	movl (%rsi), %r11d
	movl -4(%rsi, %rdx), %r10d
	movl %r11d, (%rdi)
	movl %r10d, -4(%rdi, %rdx)
	jmp 13f
11:
	cmp $2, %rdx
	jb 12f
	/*
	 * Move data from 2 bytes to 3 bytes.
	 */
	movw (%rsi), %r11w
	movw -2(%rsi, %rdx), %r10w
	movw %r11w, (%rdi)
	movw %r10w, -2(%rdi, %rdx)
	jmp 13f
12:
	cmp $1, %rdx
	jb 13f
	/*
	 * Move data for 1 byte.
	 */
	movb (%rsi), %r11b
	movb %r11b, (%rdi)
13:
	RET

.Lmemmove_erms:
	movq %rdx, %rcx
	rep movsb
	RET
SYM_FUNC_END(__memmove)
EXPORT_SYMBOL(__memmove)

SYM_FUNC_ALIAS_WEAK(memmove, __memmove)
EXPORT_SYMBOL(memmove)
