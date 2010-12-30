/* Copyright 2002 Andi Kleen */

#include <linux/linkage.h>

#include <asm/cpufeature.h>
#include <asm/dwarf2.h>

/*
 * memcpy - Copy a memory block.
 *
 * Input:
 *  rdi destination
 *  rsi source
 *  rdx count
 *
 * Output:
 * rax original destination
 */

/*
 * memcpy_c() - fast string ops (REP MOVSQ) based variant.
 *
 * This gets patched over the unrolled variant (below) via the
 * alternative instructions framework:
 */
	.section .altinstr_replacement, "ax", @progbits
.Lmemcpy_c:
	movq %rdi, %rax

	movl %edx, %ecx
	shrl $3, %ecx
	andl $7, %edx
	rep movsq
	movl %edx, %ecx
	rep movsb
	ret
.Lmemcpy_e:
	.previous

ENTRY(__memcpy)
ENTRY(memcpy)
	CFI_STARTPROC
	movq %rdi, %rax

	/*
	 * Use 32bit CMP here to avoid long NOP padding.
	 */
	cmp  $0x20, %edx
	jb .Lhandle_tail

	/*
	 * We check whether memory false dependece could occur,
	 * then jump to corresponding copy mode.
	 */
	cmp  %dil, %sil
	jl .Lcopy_backward
	subl $0x20, %edx
.Lcopy_forward_loop:
	subq $0x20,	%rdx

	/*
	 * Move in blocks of 4x8 bytes:
	 */
	movq 0*8(%rsi),	%r8
	movq 1*8(%rsi),	%r9
	movq 2*8(%rsi),	%r10
	movq 3*8(%rsi),	%r11
	leaq 4*8(%rsi),	%rsi

	movq %r8,	0*8(%rdi)
	movq %r9,	1*8(%rdi)
	movq %r10,	2*8(%rdi)
	movq %r11,	3*8(%rdi)
	leaq 4*8(%rdi),	%rdi
	jae  .Lcopy_forward_loop
	addq $0x20,	%rdx
	jmp  .Lhandle_tail

.Lcopy_backward:
	/*
	 * Calculate copy position to tail.
	 */
	addq %rdx,	%rsi
	addq %rdx,	%rdi
	subq $0x20,	%rdx
	/*
	 * At most 3 ALU operations in one cycle,
	 * so append NOPS in the same 16bytes trunk.
	 */
	.p2align 4
.Lcopy_backward_loop:
	subq $0x20,	%rdx
	movq -1*8(%rsi),	%r8
	movq -2*8(%rsi),	%r9
	movq -3*8(%rsi),	%r10
	movq -4*8(%rsi),	%r11
	leaq -4*8(%rsi),	%rsi
	movq %r8,		-1*8(%rdi)
	movq %r9,		-2*8(%rdi)
	movq %r10,		-3*8(%rdi)
	movq %r11,		-4*8(%rdi)
	leaq -4*8(%rdi),	%rdi
	jae  .Lcopy_backward_loop

	/*
	 * Calculate copy position to head.
	 */
	addq $0x20,	%rdx
	subq %rdx,	%rsi
	subq %rdx,	%rdi
.Lhandle_tail:
	cmpq $16,	%rdx
	jb   .Lless_16bytes

	/*
	 * Move data from 16 bytes to 31 bytes.
	 */
	movq 0*8(%rsi), %r8
	movq 1*8(%rsi),	%r9
	movq -2*8(%rsi, %rdx),	%r10
	movq -1*8(%rsi, %rdx),	%r11
	movq %r8,	0*8(%rdi)
	movq %r9,	1*8(%rdi)
	movq %r10,	-2*8(%rdi, %rdx)
	movq %r11,	-1*8(%rdi, %rdx)
	retq
	.p2align 4
.Lless_16bytes:
	cmpq $8,	%rdx
	jb   .Lless_8bytes
	/*
	 * Move data from 8 bytes to 15 bytes.
	 */
	movq 0*8(%rsi),	%r8
	movq -1*8(%rsi, %rdx),	%r9
	movq %r8,	0*8(%rdi)
	movq %r9,	-1*8(%rdi, %rdx)
	retq
	.p2align 4
.Lless_8bytes:
	cmpq $4,	%rdx
	jb   .Lless_3bytes

	/*
	 * Move data from 4 bytes to 7 bytes.
	 */
	movl (%rsi), %ecx
	movl -4(%rsi, %rdx), %r8d
	movl %ecx, (%rdi)
	movl %r8d, -4(%rdi, %rdx)
	retq
	.p2align 4
.Lless_3bytes:
	cmpl $0, %edx
	je .Lend
	/*
	 * Move data from 1 bytes to 3 bytes.
	 */
.Lloop_1:
	movb (%rsi), %r8b
	movb %r8b, (%rdi)
	incq %rdi
	incq %rsi
	decl %edx
	jnz .Lloop_1

.Lend:
	retq
	CFI_ENDPROC
ENDPROC(memcpy)
ENDPROC(__memcpy)

	/*
	 * Some CPUs run faster using the string copy instructions.
	 * It is also a lot simpler. Use this when possible:
	 */

	.section .altinstructions, "a"
	.align 8
	.quad memcpy
	.quad .Lmemcpy_c
	.word X86_FEATURE_REP_GOOD

	/*
	 * Replace only beginning, memcpy is used to apply alternatives,
	 * so it is silly to overwrite itself with nops - reboot is the
	 * only outcome...
	 */
	.byte .Lmemcpy_e - .Lmemcpy_c
	.byte .Lmemcpy_e - .Lmemcpy_c
	.previous
