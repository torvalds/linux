/* SPDX-License-Identifier: GPL-2.0 */
/* Written 2003 by Andi Kleen, based on a kernel by Evandro Menezes */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>

/*
 * Some CPUs run faster using the string copy instructions (sane microcode).
 * It is also a lot simpler. Use this when possible. But, don't use streaming
 * copy unless the CPU indicates X86_FEATURE_REP_GOOD. Could vary the
 * prefetch distance based on SMP/UP.
 */
	ALIGN
SYM_FUNC_START(copy_page)
	ALTERNATIVE "jmp copy_page_regs", "", X86_FEATURE_REP_GOOD
	movl	$4096/8, %ecx
	rep	movsq
	RET
SYM_FUNC_END(copy_page)
EXPORT_SYMBOL(copy_page)

SYM_FUNC_START_LOCAL(copy_page_regs)
	subq	$2*8,	%rsp
	movq	%rbx,	(%rsp)
	movq	%r12,	1*8(%rsp)

	movl	$(4096/64)-5,	%ecx
	.p2align 4
.Loop64:
	dec	%rcx
	movq	0x8*0(%rsi), %rax
	movq	0x8*1(%rsi), %rbx
	movq	0x8*2(%rsi), %rdx
	movq	0x8*3(%rsi), %r8
	movq	0x8*4(%rsi), %r9
	movq	0x8*5(%rsi), %r10
	movq	0x8*6(%rsi), %r11
	movq	0x8*7(%rsi), %r12

	prefetcht0 5*64(%rsi)

	movq	%rax, 0x8*0(%rdi)
	movq	%rbx, 0x8*1(%rdi)
	movq	%rdx, 0x8*2(%rdi)
	movq	%r8,  0x8*3(%rdi)
	movq	%r9,  0x8*4(%rdi)
	movq	%r10, 0x8*5(%rdi)
	movq	%r11, 0x8*6(%rdi)
	movq	%r12, 0x8*7(%rdi)

	leaq	64 (%rsi), %rsi
	leaq	64 (%rdi), %rdi

	jnz	.Loop64

	movl	$5, %ecx
	.p2align 4
.Loop2:
	decl	%ecx

	movq	0x8*0(%rsi), %rax
	movq	0x8*1(%rsi), %rbx
	movq	0x8*2(%rsi), %rdx
	movq	0x8*3(%rsi), %r8
	movq	0x8*4(%rsi), %r9
	movq	0x8*5(%rsi), %r10
	movq	0x8*6(%rsi), %r11
	movq	0x8*7(%rsi), %r12

	movq	%rax, 0x8*0(%rdi)
	movq	%rbx, 0x8*1(%rdi)
	movq	%rdx, 0x8*2(%rdi)
	movq	%r8,  0x8*3(%rdi)
	movq	%r9,  0x8*4(%rdi)
	movq	%r10, 0x8*5(%rdi)
	movq	%r11, 0x8*6(%rdi)
	movq	%r12, 0x8*7(%rdi)

	leaq	64(%rdi), %rdi
	leaq	64(%rsi), %rsi
	jnz	.Loop2

	movq	(%rsp), %rbx
	movq	1*8(%rsp), %r12
	addq	$2*8, %rsp
	RET
SYM_FUNC_END(copy_page_regs)
