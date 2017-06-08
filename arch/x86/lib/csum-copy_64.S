/*
 * Copyright 2002, 2003 Andi Kleen, SuSE Labs.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details. No warranty for anything given at all.
 */
#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/asm.h>

/*
 * Checksum copy with exception handling.
 * On exceptions src_err_ptr or dst_err_ptr is set to -EFAULT and the
 * destination is zeroed.
 *
 * Input
 * rdi  source
 * rsi  destination
 * edx  len (32bit)
 * ecx  sum (32bit)
 * r8   src_err_ptr (int)
 * r9   dst_err_ptr (int)
 *
 * Output
 * eax  64bit sum. undefined in case of exception.
 *
 * Wrappers need to take care of valid exception sum and zeroing.
 * They also should align source or destination to 8 bytes.
 */

	.macro source
10:
	_ASM_EXTABLE(10b, .Lbad_source)
	.endm

	.macro dest
20:
	_ASM_EXTABLE(20b, .Lbad_dest)
	.endm

	.macro ignore L=.Lignore
30:
	_ASM_EXTABLE(30b, \L)
	.endm


ENTRY(csum_partial_copy_generic)
	cmpl	$3*64, %edx
	jle	.Lignore

.Lignore:
	subq  $7*8, %rsp
	movq  %rbx, 2*8(%rsp)
	movq  %r12, 3*8(%rsp)
	movq  %r14, 4*8(%rsp)
	movq  %r13, 5*8(%rsp)
	movq  %r15, 6*8(%rsp)

	movq  %r8, (%rsp)
	movq  %r9, 1*8(%rsp)

	movl  %ecx, %eax
	movl  %edx, %ecx

	xorl  %r9d, %r9d
	movq  %rcx, %r12

	shrq  $6, %r12
	jz	.Lhandle_tail       /* < 64 */

	clc

	/* main loop. clear in 64 byte blocks */
	/* r9: zero, r8: temp2, rbx: temp1, rax: sum, rcx: saved length */
	/* r11:	temp3, rdx: temp4, r12 loopcnt */
	/* r10:	temp5, r15: temp6, r14 temp7, r13 temp8 */
	.p2align 4
.Lloop:
	source
	movq  (%rdi), %rbx
	source
	movq  8(%rdi), %r8
	source
	movq  16(%rdi), %r11
	source
	movq  24(%rdi), %rdx

	source
	movq  32(%rdi), %r10
	source
	movq  40(%rdi), %r15
	source
	movq  48(%rdi), %r14
	source
	movq  56(%rdi), %r13

	ignore 2f
	prefetcht0 5*64(%rdi)
2:
	adcq  %rbx, %rax
	adcq  %r8, %rax
	adcq  %r11, %rax
	adcq  %rdx, %rax
	adcq  %r10, %rax
	adcq  %r15, %rax
	adcq  %r14, %rax
	adcq  %r13, %rax

	decl %r12d

	dest
	movq %rbx, (%rsi)
	dest
	movq %r8, 8(%rsi)
	dest
	movq %r11, 16(%rsi)
	dest
	movq %rdx, 24(%rsi)

	dest
	movq %r10, 32(%rsi)
	dest
	movq %r15, 40(%rsi)
	dest
	movq %r14, 48(%rsi)
	dest
	movq %r13, 56(%rsi)

3:

	leaq 64(%rdi), %rdi
	leaq 64(%rsi), %rsi

	jnz	.Lloop

	adcq  %r9, %rax

	/* do last up to 56 bytes */
.Lhandle_tail:
	/* ecx:	count */
	movl %ecx, %r10d
	andl $63, %ecx
	shrl $3, %ecx
	jz	.Lfold
	clc
	.p2align 4
.Lloop_8:
	source
	movq (%rdi), %rbx
	adcq %rbx, %rax
	decl %ecx
	dest
	movq %rbx, (%rsi)
	leaq 8(%rsi), %rsi /* preserve carry */
	leaq 8(%rdi), %rdi
	jnz	.Lloop_8
	adcq %r9, %rax	/* add in carry */

.Lfold:
	/* reduce checksum to 32bits */
	movl %eax, %ebx
	shrq $32, %rax
	addl %ebx, %eax
	adcl %r9d, %eax

	/* do last up to 6 bytes */
.Lhandle_7:
	movl %r10d, %ecx
	andl $7, %ecx
	shrl $1, %ecx
	jz   .Lhandle_1
	movl $2, %edx
	xorl %ebx, %ebx
	clc
	.p2align 4
.Lloop_1:
	source
	movw (%rdi), %bx
	adcl %ebx, %eax
	decl %ecx
	dest
	movw %bx, (%rsi)
	leaq 2(%rdi), %rdi
	leaq 2(%rsi), %rsi
	jnz .Lloop_1
	adcl %r9d, %eax	/* add in carry */

	/* handle last odd byte */
.Lhandle_1:
	testb $1, %r10b
	jz    .Lende
	xorl  %ebx, %ebx
	source
	movb (%rdi), %bl
	dest
	movb %bl, (%rsi)
	addl %ebx, %eax
	adcl %r9d, %eax		/* carry */

.Lende:
	movq 2*8(%rsp), %rbx
	movq 3*8(%rsp), %r12
	movq 4*8(%rsp), %r14
	movq 5*8(%rsp), %r13
	movq 6*8(%rsp), %r15
	addq $7*8, %rsp
	ret

	/* Exception handlers. Very simple, zeroing is done in the wrappers */
.Lbad_source:
	movq (%rsp), %rax
	testq %rax, %rax
	jz   .Lende
	movl $-EFAULT, (%rax)
	jmp  .Lende

.Lbad_dest:
	movq 8(%rsp), %rax
	testq %rax, %rax
	jz   .Lende
	movl $-EFAULT, (%rax)
	jmp .Lende
ENDPROC(csum_partial_copy_generic)
