/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2016-2020 Intel Corporation. All rights reserved. */

#include <linux/linkage.h>
#include <asm/asm.h>

#ifndef CONFIG_UML

#ifdef CONFIG_X86_MCE

/*
 * copy_mc_fragile - copy memory with indication if an exception / fault happened
 *
 * The 'fragile' version is opted into by platform quirks and takes
 * pains to avoid unrecoverable corner cases like 'fast-string'
 * instruction sequences, and consuming poison across a cacheline
 * boundary. The non-fragile version is equivalent to memcpy()
 * regardless of CPU machine-check-recovery capability.
 */
SYM_FUNC_START(copy_mc_fragile)
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
SYM_FUNC_END(copy_mc_fragile)

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
	jmp copy_mc_fragile_handle_tail

	.previous

	_ASM_EXTABLE_FAULT(.L_read_leading_bytes, .E_leading_bytes)
	_ASM_EXTABLE_FAULT(.L_read_words, .E_read_words)
	_ASM_EXTABLE_FAULT(.L_read_trailing_bytes, .E_trailing_bytes)
	_ASM_EXTABLE(.L_write_leading_bytes, .E_leading_bytes)
	_ASM_EXTABLE(.L_write_words, .E_write_words)
	_ASM_EXTABLE(.L_write_trailing_bytes, .E_trailing_bytes)
#endif /* CONFIG_X86_MCE */

/*
 * copy_mc_enhanced_fast_string - memory copy with exception handling
 *
 * Fast string copy + fault / exception handling. If the CPU does
 * support machine check exception recovery, but does not support
 * recovering from fast-string exceptions then this CPU needs to be
 * added to the copy_mc_fragile_key set of quirks. Otherwise, absent any
 * machine check recovery support this version should be no slower than
 * standard memcpy.
 */
SYM_FUNC_START(copy_mc_enhanced_fast_string)
	movq %rdi, %rax
	movq %rdx, %rcx
.L_copy:
	rep movsb
	/* Copy successful. Return zero */
	xorl %eax, %eax
	ret
SYM_FUNC_END(copy_mc_enhanced_fast_string)

	.section .fixup, "ax"
.E_copy:
	/*
	 * On fault %rcx is updated such that the copy instruction could
	 * optionally be restarted at the fault position, i.e. it
	 * contains 'bytes remaining'. A non-zero return indicates error
	 * to copy_mc_generic() users, or indicate short transfers to
	 * user-copy routines.
	 */
	movq %rcx, %rax
	ret

	.previous

	_ASM_EXTABLE_FAULT(.L_copy, .E_copy)
#endif /* !CONFIG_UML */
