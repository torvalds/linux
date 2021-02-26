/* Copyright 2002 Andi Kleen */

#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/cpufeatures.h>
#include <asm/mcsafe_test.h>
#include <asm/alternative-asm.h>
#include <asm/export.h>

/*
 * We build a jump to memcpy_orig by default which gets NOPped out on
 * the majority of x86 CPUs which set REP_GOOD. In addition, CPUs which
 * have the enhanced REP MOVSB/STOSB feature (ERMS), change those NOPs
 * to a jmp to memcpy_erms which does the REP; MOVSB mem copy.
 */

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
.weak memcpy
.p2align 4, 0x90
memcpy:
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

MCSAFE_TEST_CTL

/*
 * __memcpy_mcsafe - memory copy with machine check exception handling
 * Note that we only catch machine checks when reading the source addresses.
 * Writes to target are posted and don't generate machine checks.
 */
ENTRY(__memcpy_mcsafe)
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
.L_read_leading_bytes:
	movb (%rsi), %al
	MCSAFE_TEST_SRC %rsi 1 .E_leading_bytes
	MCSAFE_TEST_DST %rdi 1 .E_leading_bytes
.L_write_leading_bytes:
	movb %al, (%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz .L_read_leading_bytes

.L_8byte_aligned:
	movl %edx, %ecx
	andl $7, %edx
	shrl $3, %ecx
	jz .L_no_whole_words

.L_read_words:
	movq (%rsi), %r8
	MCSAFE_TEST_SRC %rsi 8 .E_read_words
	MCSAFE_TEST_DST %rdi 8 .E_write_words
.L_write_words:
	movq %r8, (%rdi)
	addq $8, %rsi
	addq $8, %rdi
	decl %ecx
	jnz .L_read_words

	/* Any trailing bytes? */
.L_no_whole_words:
	andl %edx, %edx
	jz .L_done_memcpy_trap

	/* Copy trailing bytes */
	movl %edx, %ecx
.L_read_trailing_bytes:
	movb (%rsi), %al
	MCSAFE_TEST_SRC %rsi 1 .E_trailing_bytes
	MCSAFE_TEST_DST %rdi 1 .E_trailing_bytes
.L_write_trailing_bytes:
	movb %al, (%rdi)
	incq %rsi
	incq %rdi
	decl %ecx
	jnz .L_read_trailing_bytes

	/* Copy successful. Return zero */
.L_done_memcpy_trap:
	xorl %eax, %eax
.L_done:
	ret
ENDPROC(__memcpy_mcsafe)
EXPORT_SYMBOL_GPL(__memcpy_mcsafe)

	.section .fixup, "ax"
	/*
	 * Return number of bytes not copied for any failure. Note that
	 * there is no "tail" handling since the source buffer is 8-byte
	 * aligned and poison is cacheline aligned.
	 */
.E_read_words:
	shll	$3, %ecx
.E_leading_bytes:
	addl	%edx, %ecx
.E_trailing_bytes:
	mov	%ecx, %eax
	jmp	.L_done

	/*
	 * For write fault handling, given the destination is unaligned,
	 * we handle faults on multi-byte writes with a byte-by-byte
	 * copy up to the write-protected page.
	 */
.E_write_words:
	shll	$3, %ecx
	addl	%edx, %ecx
	movl	%ecx, %edx
	jmp mcsafe_handle_tail

	.previous

	_ASM_EXTABLE_FAULT(.L_read_leading_bytes, .E_leading_bytes)
	_ASM_EXTABLE_FAULT(.L_read_words, .E_read_words)
	_ASM_EXTABLE_FAULT(.L_read_trailing_bytes, .E_trailing_bytes)
	_ASM_EXTABLE(.L_write_leading_bytes, .E_leading_bytes)
	_ASM_EXTABLE(.L_write_words, .E_write_words)
	_ASM_EXTABLE(.L_write_trailing_bytes, .E_trailing_bytes)
#endif
