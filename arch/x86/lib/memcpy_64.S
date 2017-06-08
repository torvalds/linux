/* Copyright 2002 Andi Kleen */

#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/cpufeatures.h>
#include <asm/alternative-asm.h>
#include <asm/export.h>

/*
 * We build a jump to memcpy_orig by default which gets NOPped out on
 * the majority of x86 CPUs which set REP_GOOD. In addition, CPUs which
 * have the enhanced REP MOVSB/STOSB feature (ERMS), change those NOPs
 * to a jmp to memcpy_erms which does the REP; MOVSB mem copy.
 */

.weak memcpy

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
ENTRY(__memcpy)
ENTRY(memcpy)
	ALTERNATIVE_2 "jmp memcpy_orig", "", X86_FEATURE_REP_GOOD, \
		      "jmp memcpy_erms", X86_FEATURE_ERMS

	movq %rdi, %rax
	movq %rdx, %rcx
	shrq $3, %rcx
	andl $7, %edx
	rep movsq
	movl %edx, %ecx
	rep movsb
	ret
ENDPROC(memcpy)
ENDPROC(__memcpy)
EXPORT_SYMBOL(memcpy)
EXPORT_SYMBOL(__memcpy)

/*
 * memcpy_erms() - enhanced fast string memcpy. This is faster and
 * simpler than memcpy. Use memcpy_erms when possible.
 */
ENTRY(memcpy_erms)
	movq %rdi, %rax
	movq %rdx, %rcx
	rep movsb
	ret
ENDPROC(memcpy_erms)

ENTRY(memcpy_orig)
	movq %rdi, %rax

	cmpq $0x20, %rdx
	jb .Lhandle_tail

	/*
	 * We check whether memory false dependence could occur,
	 * then jump to corresponding copy mode.
	 */
	cmp  %dil, %sil
	jl .Lcopy_backward
	subq $0x20, %rdx
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
	addl $0x20,	%edx
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
	 * so append NOPS in the same 16 bytes trunk.
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
	addl $0x20,	%edx
	subq %rdx,	%rsi
	subq %rdx,	%rdi
.Lhandle_tail:
	cmpl $16,	%edx
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
	cmpl $8,	%edx
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
	cmpl $4,	%edx
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
	subl $1, %edx
	jb .Lend
	/*
	 * Move data from 1 bytes to 3 bytes.
	 */
	movzbl (%rsi), %ecx
	jz .Lstore_1byte
	movzbq 1(%rsi), %r8
	movzbq (%rsi, %rdx), %r9
	movb %r8b, 1(%rdi)
	movb %r9b, (%rdi, %rdx)
.Lstore_1byte:
	movb %cl, (%rdi)

.Lend:
	retq
ENDPROC(memcpy_orig)

#ifndef CONFIG_UML
/*
 * memcpy_mcsafe_unrolled - memory copy with machine check exception handling
 * Note that we only catch machine checks when reading the source addresses.
 * Writes to target are posted and don't generate machine checks.
 */
ENTRY(memcpy_mcsafe_unrolled)
	cmpl $8, %edx
	/* Less than 8 bytes? Go to byte copy loop */
	jb .L_no_whole_words

	/* Check for bad alignment of source */
	testl $7, %esi
	/* Already aligned */
	jz .L_8byte_aligned

	/* Copy one byte at a time until source is 8-byte aligned */
	movl %esi, %ecx
	andl $7, %ecx
	subl $8, %ecx
	negl %ecx
	subl %ecx, %edx
.L_copy_leading_bytes:
	movb (%rsi), %al
	movb %al, (%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz .L_copy_leading_bytes

.L_8byte_aligned:
	/* Figure out how many whole cache lines (64-bytes) to copy */
	movl %edx, %ecx
	andl $63, %edx
	shrl $6, %ecx
	jz .L_no_whole_cache_lines

	/* Loop copying whole cache lines */
.L_cache_w0: movq (%rsi), %r8
.L_cache_w1: movq 1*8(%rsi), %r9
.L_cache_w2: movq 2*8(%rsi), %r10
.L_cache_w3: movq 3*8(%rsi), %r11
	movq %r8, (%rdi)
	movq %r9, 1*8(%rdi)
	movq %r10, 2*8(%rdi)
	movq %r11, 3*8(%rdi)
.L_cache_w4: movq 4*8(%rsi), %r8
.L_cache_w5: movq 5*8(%rsi), %r9
.L_cache_w6: movq 6*8(%rsi), %r10
.L_cache_w7: movq 7*8(%rsi), %r11
	movq %r8, 4*8(%rdi)
	movq %r9, 5*8(%rdi)
	movq %r10, 6*8(%rdi)
	movq %r11, 7*8(%rdi)
	leaq 64(%rsi), %rsi
	leaq 64(%rdi), %rdi
	decl %ecx
	jnz .L_cache_w0

	/* Are there any trailing 8-byte words? */
.L_no_whole_cache_lines:
	movl %edx, %ecx
	andl $7, %edx
	shrl $3, %ecx
	jz .L_no_whole_words

	/* Copy trailing words */
.L_copy_trailing_words:
	movq (%rsi), %r8
	mov %r8, (%rdi)
	leaq 8(%rsi), %rsi
	leaq 8(%rdi), %rdi
	decl %ecx
	jnz .L_copy_trailing_words

	/* Any trailing bytes? */
.L_no_whole_words:
	andl %edx, %edx
	jz .L_done_memcpy_trap

	/* Copy trailing bytes */
	movl %edx, %ecx
.L_copy_trailing_bytes:
	movb (%rsi), %al
	movb %al, (%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz .L_copy_trailing_bytes

	/* Copy successful. Return zero */
.L_done_memcpy_trap:
	xorq %rax, %rax
	ret
ENDPROC(memcpy_mcsafe_unrolled)
EXPORT_SYMBOL_GPL(memcpy_mcsafe_unrolled)

	.section .fixup, "ax"
	/* Return -EFAULT for any failure */
.L_memcpy_mcsafe_fail:
	mov	$-EFAULT, %rax
	ret

	.previous

	_ASM_EXTABLE_FAULT(.L_copy_leading_bytes, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_cache_w0, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_cache_w1, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_cache_w2, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_cache_w3, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_cache_w4, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_cache_w5, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_cache_w6, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_cache_w7, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_copy_trailing_words, .L_memcpy_mcsafe_fail)
	_ASM_EXTABLE_FAULT(.L_copy_trailing_bytes, .L_memcpy_mcsafe_fail)
#endif
