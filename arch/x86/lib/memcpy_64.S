/* Copyright 2002 Andi Kleen */

#include <linux/linkage.h>

#include <asm/cpufeature.h>
#include <asm/dwarf2.h>
#include <asm/alternative-asm.h>

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
	movq %rdx, %rcx
	shrq $3, %rcx
	andl $7, %edx
	rep movsq
	movl %edx, %ecx
	rep movsb
	ret
.Lmemcpy_e:
	.previous

/*
 * memcpy_c_e() - enhanced fast string memcpy. This is faster and simpler than
 * memcpy_c. Use memcpy_c_e when possible.
 *
 * This gets patched over the unrolled variant (below) via the
 * alternative instructions framework:
 */
	.section .altinstr_replacement, "ax", @progbits
.Lmemcpy_c_e:
	movq %rdi, %rax
	movq %rdx, %rcx
	rep movsb
	ret
.Lmemcpy_e_e:
	.previous

.weak memcpy

ENTRY(__memcpy)
ENTRY(memcpy)
	CFI_STARTPROC
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
	CFI_ENDPROC
ENDPROC(memcpy)
ENDPROC(__memcpy)

	/*
	 * Some CPUs are adding enhanced REP MOVSB/STOSB feature
	 * If the feature is supported, memcpy_c_e() is the first choice.
	 * If enhanced rep movsb copy is not available, use fast string copy
	 * memcpy_c() when possible. This is faster and code is simpler than
	 * original memcpy().
	 * Otherwise, original memcpy() is used.
	 * In .altinstructions section, ERMS feature is placed after REG_GOOD
         * feature to implement the right patch order.
	 *
	 * Replace only beginning, memcpy is used to apply alternatives,
	 * so it is silly to overwrite itself with nops - reboot is the
	 * only outcome...
	 */
	.section .altinstructions, "a"
	altinstruction_entry __memcpy,.Lmemcpy_c,X86_FEATURE_REP_GOOD,\
			     .Lmemcpy_e-.Lmemcpy_c,.Lmemcpy_e-.Lmemcpy_c
	altinstruction_entry __memcpy,.Lmemcpy_c_e,X86_FEATURE_ERMS, \
			     .Lmemcpy_e_e-.Lmemcpy_c_e,.Lmemcpy_e_e-.Lmemcpy_c_e
	.previous
