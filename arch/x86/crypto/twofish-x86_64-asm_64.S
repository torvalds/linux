/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
*   Copyright (C) 2006 by Joachim Fritschi, <jfritschi@freenet.de>        *
*                                                                         *
***************************************************************************/

.file "twofish-x86_64-asm.S"
.text

#include <linux/linkage.h>
#include <asm/asm-offsets.h>

#define a_offset	0
#define b_offset	4
#define c_offset	8
#define d_offset	12

/* Structure of the crypto context struct*/

#define s0	0	/* S0 Array 256 Words each */
#define s1	1024	/* S1 Array */
#define s2	2048	/* S2 Array */
#define s3	3072	/* S3 Array */
#define w	4096	/* 8 whitening keys (word) */
#define k	4128	/* key 1-32 ( word ) */

/* define a few register aliases to allow macro substitution */

#define R0     %rax
#define R0D    %eax
#define R0B    %al
#define R0H    %ah

#define R1     %rbx
#define R1D    %ebx
#define R1B    %bl
#define R1H    %bh

#define R2     %rcx
#define R2D    %ecx
#define R2B    %cl
#define R2H    %ch

#define R3     %rdx
#define R3D    %edx
#define R3B    %dl
#define R3H    %dh


/* performs input whitening */
#define input_whitening(src,context,offset)\
	xor	w+offset(context),	src;

/* performs input whitening */
#define output_whitening(src,context,offset)\
	xor	w+16+offset(context),	src;


/*
 * a input register containing a (rotated 16)
 * b input register containing b
 * c input register containing c
 * d input register containing d (already rol $1)
 * operations on a and b are interleaved to increase performance
 */
#define encrypt_round(a,b,c,d,round)\
	movzx	b ## B,		%edi;\
	mov	s1(%r11,%rdi,4),%r8d;\
	movzx	a ## B,		%edi;\
	mov	s2(%r11,%rdi,4),%r9d;\
	movzx	b ## H,		%edi;\
	ror	$16,		b ## D;\
	xor	s2(%r11,%rdi,4),%r8d;\
	movzx	a ## H,		%edi;\
	ror	$16,		a ## D;\
	xor	s3(%r11,%rdi,4),%r9d;\
	movzx	b ## B,		%edi;\
	xor	s3(%r11,%rdi,4),%r8d;\
	movzx	a ## B,		%edi;\
	xor	(%r11,%rdi,4),	%r9d;\
	movzx	b ## H,		%edi;\
	ror	$15,		b ## D;\
	xor	(%r11,%rdi,4),	%r8d;\
	movzx	a ## H,		%edi;\
	xor	s1(%r11,%rdi,4),%r9d;\
	add	%r8d,		%r9d;\
	add	%r9d,		%r8d;\
	add	k+round(%r11),	%r9d;\
	xor	%r9d,		c ## D;\
	rol	$15,		c ## D;\
	add	k+4+round(%r11),%r8d;\
	xor	%r8d,		d ## D;

/*
 * a input register containing a(rotated 16)
 * b input register containing b
 * c input register containing c
 * d input register containing d (already rol $1)
 * operations on a and b are interleaved to increase performance
 * during the round a and b are prepared for the output whitening
 */
#define encrypt_last_round(a,b,c,d,round)\
	mov	b ## D,		%r10d;\
	shl	$32,		%r10;\
	movzx	b ## B,		%edi;\
	mov	s1(%r11,%rdi,4),%r8d;\
	movzx	a ## B,		%edi;\
	mov	s2(%r11,%rdi,4),%r9d;\
	movzx	b ## H,		%edi;\
	ror	$16,		b ## D;\
	xor	s2(%r11,%rdi,4),%r8d;\
	movzx	a ## H,		%edi;\
	ror	$16,		a ## D;\
	xor	s3(%r11,%rdi,4),%r9d;\
	movzx	b ## B,		%edi;\
	xor	s3(%r11,%rdi,4),%r8d;\
	movzx	a ## B,		%edi;\
	xor	(%r11,%rdi,4),	%r9d;\
	xor	a,		%r10;\
	movzx	b ## H,		%edi;\
	xor	(%r11,%rdi,4),	%r8d;\
	movzx	a ## H,		%edi;\
	xor	s1(%r11,%rdi,4),%r9d;\
	add	%r8d,		%r9d;\
	add	%r9d,		%r8d;\
	add	k+round(%r11),	%r9d;\
	xor	%r9d,		c ## D;\
	ror	$1,		c ## D;\
	add	k+4+round(%r11),%r8d;\
	xor	%r8d,		d ## D

/*
 * a input register containing a
 * b input register containing b (rotated 16)
 * c input register containing c (already rol $1)
 * d input register containing d
 * operations on a and b are interleaved to increase performance
 */
#define decrypt_round(a,b,c,d,round)\
	movzx	a ## B,		%edi;\
	mov	(%r11,%rdi,4),	%r9d;\
	movzx	b ## B,		%edi;\
	mov	s3(%r11,%rdi,4),%r8d;\
	movzx	a ## H,		%edi;\
	ror	$16,		a ## D;\
	xor	s1(%r11,%rdi,4),%r9d;\
	movzx	b ## H,		%edi;\
	ror	$16,		b ## D;\
	xor	(%r11,%rdi,4),	%r8d;\
	movzx	a ## B,		%edi;\
	xor	s2(%r11,%rdi,4),%r9d;\
	movzx	b ## B,		%edi;\
	xor	s1(%r11,%rdi,4),%r8d;\
	movzx	a ## H,		%edi;\
	ror	$15,		a ## D;\
	xor	s3(%r11,%rdi,4),%r9d;\
	movzx	b ## H,		%edi;\
	xor	s2(%r11,%rdi,4),%r8d;\
	add	%r8d,		%r9d;\
	add	%r9d,		%r8d;\
	add	k+round(%r11),	%r9d;\
	xor	%r9d,		c ## D;\
	add	k+4+round(%r11),%r8d;\
	xor	%r8d,		d ## D;\
	rol	$15,		d ## D;

/*
 * a input register containing a
 * b input register containing b
 * c input register containing c (already rol $1)
 * d input register containing d
 * operations on a and b are interleaved to increase performance
 * during the round a and b are prepared for the output whitening
 */
#define decrypt_last_round(a,b,c,d,round)\
	movzx	a ## B,		%edi;\
	mov	(%r11,%rdi,4),	%r9d;\
	movzx	b ## B,		%edi;\
	mov	s3(%r11,%rdi,4),%r8d;\
	movzx	b ## H,		%edi;\
	ror	$16,		b ## D;\
	xor	(%r11,%rdi,4),	%r8d;\
	movzx	a ## H,		%edi;\
	mov	b ## D,		%r10d;\
	shl	$32,		%r10;\
	xor	a,		%r10;\
	ror	$16,		a ## D;\
	xor	s1(%r11,%rdi,4),%r9d;\
	movzx	b ## B,		%edi;\
	xor	s1(%r11,%rdi,4),%r8d;\
	movzx	a ## B,		%edi;\
	xor	s2(%r11,%rdi,4),%r9d;\
	movzx	b ## H,		%edi;\
	xor	s2(%r11,%rdi,4),%r8d;\
	movzx	a ## H,		%edi;\
	xor	s3(%r11,%rdi,4),%r9d;\
	add	%r8d,		%r9d;\
	add	%r9d,		%r8d;\
	add	k+round(%r11),	%r9d;\
	xor	%r9d,		c ## D;\
	add	k+4+round(%r11),%r8d;\
	xor	%r8d,		d ## D;\
	ror	$1,		d ## D;

SYM_FUNC_START(twofish_enc_blk)
	pushq    R1

	/* %rdi contains the ctx address */
	/* %rsi contains the output address */
	/* %rdx contains the input address */
	/* ctx address is moved to free one non-rex register
	as target for the 8bit high operations */
	mov	%rdi,		%r11

	movq	(R3),	R1
	movq	8(R3),	R3
	input_whitening(R1,%r11,a_offset)
	input_whitening(R3,%r11,c_offset)
	mov	R1D,	R0D
	rol	$16,	R0D
	shr	$32,	R1
	mov	R3D,	R2D
	shr	$32,	R3
	rol	$1,	R3D

	encrypt_round(R0,R1,R2,R3,0);
	encrypt_round(R2,R3,R0,R1,8);
	encrypt_round(R0,R1,R2,R3,2*8);
	encrypt_round(R2,R3,R0,R1,3*8);
	encrypt_round(R0,R1,R2,R3,4*8);
	encrypt_round(R2,R3,R0,R1,5*8);
	encrypt_round(R0,R1,R2,R3,6*8);
	encrypt_round(R2,R3,R0,R1,7*8);
	encrypt_round(R0,R1,R2,R3,8*8);
	encrypt_round(R2,R3,R0,R1,9*8);
	encrypt_round(R0,R1,R2,R3,10*8);
	encrypt_round(R2,R3,R0,R1,11*8);
	encrypt_round(R0,R1,R2,R3,12*8);
	encrypt_round(R2,R3,R0,R1,13*8);
	encrypt_round(R0,R1,R2,R3,14*8);
	encrypt_last_round(R2,R3,R0,R1,15*8);


	output_whitening(%r10,%r11,a_offset)
	movq	%r10,	(%rsi)

	shl	$32,	R1
	xor	R0,	R1

	output_whitening(R1,%r11,c_offset)
	movq	R1,	8(%rsi)

	popq	R1
	movl	$1,%eax
	ret
SYM_FUNC_END(twofish_enc_blk)

SYM_FUNC_START(twofish_dec_blk)
	pushq    R1

	/* %rdi contains the ctx address */
	/* %rsi contains the output address */
	/* %rdx contains the input address */
	/* ctx address is moved to free one non-rex register
	as target for the 8bit high operations */
	mov	%rdi,		%r11

	movq	(R3),	R1
	movq	8(R3),	R3
	output_whitening(R1,%r11,a_offset)
	output_whitening(R3,%r11,c_offset)
	mov	R1D,	R0D
	shr	$32,	R1
	rol	$16,	R1D
	mov	R3D,	R2D
	shr	$32,	R3
	rol	$1,	R2D

	decrypt_round(R0,R1,R2,R3,15*8);
	decrypt_round(R2,R3,R0,R1,14*8);
	decrypt_round(R0,R1,R2,R3,13*8);
	decrypt_round(R2,R3,R0,R1,12*8);
	decrypt_round(R0,R1,R2,R3,11*8);
	decrypt_round(R2,R3,R0,R1,10*8);
	decrypt_round(R0,R1,R2,R3,9*8);
	decrypt_round(R2,R3,R0,R1,8*8);
	decrypt_round(R0,R1,R2,R3,7*8);
	decrypt_round(R2,R3,R0,R1,6*8);
	decrypt_round(R0,R1,R2,R3,5*8);
	decrypt_round(R2,R3,R0,R1,4*8);
	decrypt_round(R0,R1,R2,R3,3*8);
	decrypt_round(R2,R3,R0,R1,2*8);
	decrypt_round(R0,R1,R2,R3,1*8);
	decrypt_last_round(R2,R3,R0,R1,0);

	input_whitening(%r10,%r11,a_offset)
	movq	%r10,	(%rsi)

	shl	$32,	R1
	xor	R0,	R1

	input_whitening(R1,%r11,c_offset)
	movq	R1,	8(%rsi)

	popq	R1
	movl	$1,%eax
	ret
SYM_FUNC_END(twofish_dec_blk)
