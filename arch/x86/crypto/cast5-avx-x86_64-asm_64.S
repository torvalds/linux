/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cast5 Cipher 16-way parallel algorithm (AVX/x86_64)
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Copyright © 2012 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 */

#include <linux/linkage.h>
#include <asm/frame.h>

.file "cast5-avx-x86_64-asm_64.S"

.extern cast_s1
.extern cast_s2
.extern cast_s3
.extern cast_s4

/* structure of crypto context */
#define km	0
#define kr	(16*4)
#define rr	((16*4)+16)

/* s-boxes */
#define s1	cast_s1
#define s2	cast_s2
#define s3	cast_s3
#define s4	cast_s4

/**********************************************************************
  16-way AVX cast5
 **********************************************************************/
#define CTX %r15

#define RL1 %xmm0
#define RR1 %xmm1
#define RL2 %xmm2
#define RR2 %xmm3
#define RL3 %xmm4
#define RR3 %xmm5
#define RL4 %xmm6
#define RR4 %xmm7

#define RX %xmm8

#define RKM  %xmm9
#define RKR  %xmm10
#define RKRF %xmm11
#define RKRR %xmm12

#define R32  %xmm13
#define R1ST %xmm14

#define RTMP %xmm15

#define RID1  %rdi
#define RID1d %edi
#define RID2  %rsi
#define RID2d %esi

#define RGI1   %rdx
#define RGI1bl %dl
#define RGI1bh %dh
#define RGI2   %rcx
#define RGI2bl %cl
#define RGI2bh %ch

#define RGI3   %rax
#define RGI3bl %al
#define RGI3bh %ah
#define RGI4   %rbx
#define RGI4bl %bl
#define RGI4bh %bh

#define RFS1  %r8
#define RFS1d %r8d
#define RFS2  %r9
#define RFS2d %r9d
#define RFS3  %r10
#define RFS3d %r10d


#define lookup_32bit(src, dst, op1, op2, op3, interleave_op, il_reg) \
	movzbl		src ## bh,     RID1d;    \
	movzbl		src ## bl,     RID2d;    \
	shrq $16,	src;                     \
	movl		s1(, RID1, 4), dst ## d; \
	op1		s2(, RID2, 4), dst ## d; \
	movzbl		src ## bh,     RID1d;    \
	movzbl		src ## bl,     RID2d;    \
	interleave_op(il_reg);			 \
	op2		s3(, RID1, 4), dst ## d; \
	op3		s4(, RID2, 4), dst ## d;

#define dummy(d) /* do nothing */

#define shr_next(reg) \
	shrq $16,	reg;

#define F_head(a, x, gi1, gi2, op0) \
	op0	a,	RKM,  x;                 \
	vpslld	RKRF,	x,    RTMP;              \
	vpsrld	RKRR,	x,    x;                 \
	vpor	RTMP,	x,    x;                 \
	\
	vmovq		x,    gi1;               \
	vpextrq $1,	x,    gi2;

#define F_tail(a, x, gi1, gi2, op1, op2, op3) \
	lookup_32bit(##gi1, RFS1, op1, op2, op3, shr_next, ##gi1); \
	lookup_32bit(##gi2, RFS3, op1, op2, op3, shr_next, ##gi2); \
	\
	lookup_32bit(##gi1, RFS2, op1, op2, op3, dummy, none);     \
	shlq $32,	RFS2;                                      \
	orq		RFS1, RFS2;                                \
	lookup_32bit(##gi2, RFS1, op1, op2, op3, dummy, none);     \
	shlq $32,	RFS1;                                      \
	orq		RFS1, RFS3;                                \
	\
	vmovq		RFS2, x;                                   \
	vpinsrq $1,	RFS3, x, x;

#define F_2(a1, b1, a2, b2, op0, op1, op2, op3) \
	F_head(b1, RX, RGI1, RGI2, op0);              \
	F_head(b2, RX, RGI3, RGI4, op0);              \
	\
	F_tail(b1, RX, RGI1, RGI2, op1, op2, op3);    \
	F_tail(b2, RTMP, RGI3, RGI4, op1, op2, op3);  \
	\
	vpxor		a1, RX,   a1;                 \
	vpxor		a2, RTMP, a2;

#define F1_2(a1, b1, a2, b2) \
	F_2(a1, b1, a2, b2, vpaddd, xorl, subl, addl)
#define F2_2(a1, b1, a2, b2) \
	F_2(a1, b1, a2, b2, vpxor, subl, addl, xorl)
#define F3_2(a1, b1, a2, b2) \
	F_2(a1, b1, a2, b2, vpsubd, addl, xorl, subl)

#define subround(a1, b1, a2, b2, f) \
	F ## f ## _2(a1, b1, a2, b2);

#define round(l, r, n, f) \
	vbroadcastss 	(km+(4*n))(CTX), RKM;        \
	vpand		R1ST,            RKR,  RKRF; \
	vpsubq		RKRF,            R32,  RKRR; \
	vpsrldq $1,	RKR,             RKR;        \
	subround(l ## 1, r ## 1, l ## 2, r ## 2, f); \
	subround(l ## 3, r ## 3, l ## 4, r ## 4, f);

#define enc_preload_rkr() \
	vbroadcastss	.L16_mask,                RKR;      \
	/* add 16-bit rotation to key rotations (mod 32) */ \
	vpxor		kr(CTX),                  RKR, RKR;

#define dec_preload_rkr() \
	vbroadcastss	.L16_mask,                RKR;      \
	/* add 16-bit rotation to key rotations (mod 32) */ \
	vpxor		kr(CTX),                  RKR, RKR; \
	vpshufb		.Lbswap128_mask,          RKR, RKR;

#define transpose_2x4(x0, x1, t0, t1) \
	vpunpckldq		x1, x0, t0; \
	vpunpckhdq		x1, x0, t1; \
	\
	vpunpcklqdq		t1, t0, x0; \
	vpunpckhqdq		t1, t0, x1;

#define inpack_blocks(x0, x1, t0, t1, rmask) \
	vpshufb rmask, 	x0,	x0; \
	vpshufb rmask, 	x1,	x1; \
	\
	transpose_2x4(x0, x1, t0, t1)

#define outunpack_blocks(x0, x1, t0, t1, rmask) \
	transpose_2x4(x0, x1, t0, t1) \
	\
	vpshufb rmask,	x0, x0;           \
	vpshufb rmask,	x1, x1;

.section	.rodata.cst16.bswap_mask, "aM", @progbits, 16
.align 16
.Lbswap_mask:
	.byte 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12
.section	.rodata.cst16.bswap128_mask, "aM", @progbits, 16
.align 16
.Lbswap128_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
.section	.rodata.cst16.bswap_iv_mask, "aM", @progbits, 16
.align 16
.Lbswap_iv_mask:
	.byte 7, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0

.section	.rodata.cst4.16_mask, "aM", @progbits, 4
.align 4
.L16_mask:
	.byte 16, 16, 16, 16
.section	.rodata.cst4.32_mask, "aM", @progbits, 4
.align 4
.L32_mask:
	.byte 32, 0, 0, 0
.section	.rodata.cst4.first_mask, "aM", @progbits, 4
.align 4
.Lfirst_mask:
	.byte 0x1f, 0, 0, 0

.text

.align 16
__cast5_enc_blk16:
	/* input:
	 *	%rdi: ctx
	 *	RL1: blocks 1 and 2
	 *	RR1: blocks 3 and 4
	 *	RL2: blocks 5 and 6
	 *	RR2: blocks 7 and 8
	 *	RL3: blocks 9 and 10
	 *	RR3: blocks 11 and 12
	 *	RL4: blocks 13 and 14
	 *	RR4: blocks 15 and 16
	 * output:
	 *	RL1: encrypted blocks 1 and 2
	 *	RR1: encrypted blocks 3 and 4
	 *	RL2: encrypted blocks 5 and 6
	 *	RR2: encrypted blocks 7 and 8
	 *	RL3: encrypted blocks 9 and 10
	 *	RR3: encrypted blocks 11 and 12
	 *	RL4: encrypted blocks 13 and 14
	 *	RR4: encrypted blocks 15 and 16
	 */

	pushq %r15;
	pushq %rbx;

	movq %rdi, CTX;

	vmovdqa .Lbswap_mask, RKM;
	vmovd .Lfirst_mask, R1ST;
	vmovd .L32_mask, R32;
	enc_preload_rkr();

	inpack_blocks(RL1, RR1, RTMP, RX, RKM);
	inpack_blocks(RL2, RR2, RTMP, RX, RKM);
	inpack_blocks(RL3, RR3, RTMP, RX, RKM);
	inpack_blocks(RL4, RR4, RTMP, RX, RKM);

	round(RL, RR, 0, 1);
	round(RR, RL, 1, 2);
	round(RL, RR, 2, 3);
	round(RR, RL, 3, 1);
	round(RL, RR, 4, 2);
	round(RR, RL, 5, 3);
	round(RL, RR, 6, 1);
	round(RR, RL, 7, 2);
	round(RL, RR, 8, 3);
	round(RR, RL, 9, 1);
	round(RL, RR, 10, 2);
	round(RR, RL, 11, 3);

	movzbl rr(CTX), %eax;
	testl %eax, %eax;
	jnz .L__skip_enc;

	round(RL, RR, 12, 1);
	round(RR, RL, 13, 2);
	round(RL, RR, 14, 3);
	round(RR, RL, 15, 1);

.L__skip_enc:
	popq %rbx;
	popq %r15;

	vmovdqa .Lbswap_mask, RKM;

	outunpack_blocks(RR1, RL1, RTMP, RX, RKM);
	outunpack_blocks(RR2, RL2, RTMP, RX, RKM);
	outunpack_blocks(RR3, RL3, RTMP, RX, RKM);
	outunpack_blocks(RR4, RL4, RTMP, RX, RKM);

	ret;
ENDPROC(__cast5_enc_blk16)

.align 16
__cast5_dec_blk16:
	/* input:
	 *	%rdi: ctx
	 *	RL1: encrypted blocks 1 and 2
	 *	RR1: encrypted blocks 3 and 4
	 *	RL2: encrypted blocks 5 and 6
	 *	RR2: encrypted blocks 7 and 8
	 *	RL3: encrypted blocks 9 and 10
	 *	RR3: encrypted blocks 11 and 12
	 *	RL4: encrypted blocks 13 and 14
	 *	RR4: encrypted blocks 15 and 16
	 * output:
	 *	RL1: decrypted blocks 1 and 2
	 *	RR1: decrypted blocks 3 and 4
	 *	RL2: decrypted blocks 5 and 6
	 *	RR2: decrypted blocks 7 and 8
	 *	RL3: decrypted blocks 9 and 10
	 *	RR3: decrypted blocks 11 and 12
	 *	RL4: decrypted blocks 13 and 14
	 *	RR4: decrypted blocks 15 and 16
	 */

	pushq %r15;
	pushq %rbx;

	movq %rdi, CTX;

	vmovdqa .Lbswap_mask, RKM;
	vmovd .Lfirst_mask, R1ST;
	vmovd .L32_mask, R32;
	dec_preload_rkr();

	inpack_blocks(RL1, RR1, RTMP, RX, RKM);
	inpack_blocks(RL2, RR2, RTMP, RX, RKM);
	inpack_blocks(RL3, RR3, RTMP, RX, RKM);
	inpack_blocks(RL4, RR4, RTMP, RX, RKM);

	movzbl rr(CTX), %eax;
	testl %eax, %eax;
	jnz .L__skip_dec;

	round(RL, RR, 15, 1);
	round(RR, RL, 14, 3);
	round(RL, RR, 13, 2);
	round(RR, RL, 12, 1);

.L__dec_tail:
	round(RL, RR, 11, 3);
	round(RR, RL, 10, 2);
	round(RL, RR, 9, 1);
	round(RR, RL, 8, 3);
	round(RL, RR, 7, 2);
	round(RR, RL, 6, 1);
	round(RL, RR, 5, 3);
	round(RR, RL, 4, 2);
	round(RL, RR, 3, 1);
	round(RR, RL, 2, 3);
	round(RL, RR, 1, 2);
	round(RR, RL, 0, 1);

	vmovdqa .Lbswap_mask, RKM;
	popq %rbx;
	popq %r15;

	outunpack_blocks(RR1, RL1, RTMP, RX, RKM);
	outunpack_blocks(RR2, RL2, RTMP, RX, RKM);
	outunpack_blocks(RR3, RL3, RTMP, RX, RKM);
	outunpack_blocks(RR4, RL4, RTMP, RX, RKM);

	ret;

.L__skip_dec:
	vpsrldq $4, RKR, RKR;
	jmp .L__dec_tail;
ENDPROC(__cast5_dec_blk16)

ENTRY(cast5_ecb_enc_16way)
	/* input:
	 *	%rdi: ctx
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN
	pushq %r15;

	movq %rdi, CTX;
	movq %rsi, %r11;

	vmovdqu (0*4*4)(%rdx), RL1;
	vmovdqu (1*4*4)(%rdx), RR1;
	vmovdqu (2*4*4)(%rdx), RL2;
	vmovdqu (3*4*4)(%rdx), RR2;
	vmovdqu (4*4*4)(%rdx), RL3;
	vmovdqu (5*4*4)(%rdx), RR3;
	vmovdqu (6*4*4)(%rdx), RL4;
	vmovdqu (7*4*4)(%rdx), RR4;

	call __cast5_enc_blk16;

	vmovdqu RR1, (0*4*4)(%r11);
	vmovdqu RL1, (1*4*4)(%r11);
	vmovdqu RR2, (2*4*4)(%r11);
	vmovdqu RL2, (3*4*4)(%r11);
	vmovdqu RR3, (4*4*4)(%r11);
	vmovdqu RL3, (5*4*4)(%r11);
	vmovdqu RR4, (6*4*4)(%r11);
	vmovdqu RL4, (7*4*4)(%r11);

	popq %r15;
	FRAME_END
	ret;
ENDPROC(cast5_ecb_enc_16way)

ENTRY(cast5_ecb_dec_16way)
	/* input:
	 *	%rdi: ctx
	 *	%rsi: dst
	 *	%rdx: src
	 */

	FRAME_BEGIN
	pushq %r15;

	movq %rdi, CTX;
	movq %rsi, %r11;

	vmovdqu (0*4*4)(%rdx), RL1;
	vmovdqu (1*4*4)(%rdx), RR1;
	vmovdqu (2*4*4)(%rdx), RL2;
	vmovdqu (3*4*4)(%rdx), RR2;
	vmovdqu (4*4*4)(%rdx), RL3;
	vmovdqu (5*4*4)(%rdx), RR3;
	vmovdqu (6*4*4)(%rdx), RL4;
	vmovdqu (7*4*4)(%rdx), RR4;

	call __cast5_dec_blk16;

	vmovdqu RR1, (0*4*4)(%r11);
	vmovdqu RL1, (1*4*4)(%r11);
	vmovdqu RR2, (2*4*4)(%r11);
	vmovdqu RL2, (3*4*4)(%r11);
	vmovdqu RR3, (4*4*4)(%r11);
	vmovdqu RL3, (5*4*4)(%r11);
	vmovdqu RR4, (6*4*4)(%r11);
	vmovdqu RL4, (7*4*4)(%r11);

	popq %r15;
	FRAME_END
	ret;
ENDPROC(cast5_ecb_dec_16way)

ENTRY(cast5_cbc_dec_16way)
	/* input:
	 *	%rdi: ctx
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN
	pushq %r12;
	pushq %r15;

	movq %rdi, CTX;
	movq %rsi, %r11;
	movq %rdx, %r12;

	vmovdqu (0*16)(%rdx), RL1;
	vmovdqu (1*16)(%rdx), RR1;
	vmovdqu (2*16)(%rdx), RL2;
	vmovdqu (3*16)(%rdx), RR2;
	vmovdqu (4*16)(%rdx), RL3;
	vmovdqu (5*16)(%rdx), RR3;
	vmovdqu (6*16)(%rdx), RL4;
	vmovdqu (7*16)(%rdx), RR4;

	call __cast5_dec_blk16;

	/* xor with src */
	vmovq (%r12), RX;
	vpshufd $0x4f, RX, RX;
	vpxor RX, RR1, RR1;
	vpxor 0*16+8(%r12), RL1, RL1;
	vpxor 1*16+8(%r12), RR2, RR2;
	vpxor 2*16+8(%r12), RL2, RL2;
	vpxor 3*16+8(%r12), RR3, RR3;
	vpxor 4*16+8(%r12), RL3, RL3;
	vpxor 5*16+8(%r12), RR4, RR4;
	vpxor 6*16+8(%r12), RL4, RL4;

	vmovdqu RR1, (0*16)(%r11);
	vmovdqu RL1, (1*16)(%r11);
	vmovdqu RR2, (2*16)(%r11);
	vmovdqu RL2, (3*16)(%r11);
	vmovdqu RR3, (4*16)(%r11);
	vmovdqu RL3, (5*16)(%r11);
	vmovdqu RR4, (6*16)(%r11);
	vmovdqu RL4, (7*16)(%r11);

	popq %r15;
	popq %r12;
	FRAME_END
	ret;
ENDPROC(cast5_cbc_dec_16way)

ENTRY(cast5_ctr_16way)
	/* input:
	 *	%rdi: ctx
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: iv (big endian, 64bit)
	 */
	FRAME_BEGIN
	pushq %r12;
	pushq %r15;

	movq %rdi, CTX;
	movq %rsi, %r11;
	movq %rdx, %r12;

	vpcmpeqd RTMP, RTMP, RTMP;
	vpsrldq $8, RTMP, RTMP; /* low: -1, high: 0 */

	vpcmpeqd RKR, RKR, RKR;
	vpaddq RKR, RKR, RKR; /* low: -2, high: -2 */
	vmovdqa .Lbswap_iv_mask, R1ST;
	vmovdqa .Lbswap128_mask, RKM;

	/* load IV and byteswap */
	vmovq (%rcx), RX;
	vpshufb R1ST, RX, RX;

	/* construct IVs */
	vpsubq RTMP, RX, RX;  /* le: IV1, IV0 */
	vpshufb RKM, RX, RL1; /* be: IV0, IV1 */
	vpsubq RKR, RX, RX;
	vpshufb RKM, RX, RR1; /* be: IV2, IV3 */
	vpsubq RKR, RX, RX;
	vpshufb RKM, RX, RL2; /* be: IV4, IV5 */
	vpsubq RKR, RX, RX;
	vpshufb RKM, RX, RR2; /* be: IV6, IV7 */
	vpsubq RKR, RX, RX;
	vpshufb RKM, RX, RL3; /* be: IV8, IV9 */
	vpsubq RKR, RX, RX;
	vpshufb RKM, RX, RR3; /* be: IV10, IV11 */
	vpsubq RKR, RX, RX;
	vpshufb RKM, RX, RL4; /* be: IV12, IV13 */
	vpsubq RKR, RX, RX;
	vpshufb RKM, RX, RR4; /* be: IV14, IV15 */

	/* store last IV */
	vpsubq RTMP, RX, RX; /* le: IV16, IV14 */
	vpshufb R1ST, RX, RX; /* be: IV16, IV16 */
	vmovq RX, (%rcx);

	call __cast5_enc_blk16;

	/* dst = src ^ iv */
	vpxor (0*16)(%r12), RR1, RR1;
	vpxor (1*16)(%r12), RL1, RL1;
	vpxor (2*16)(%r12), RR2, RR2;
	vpxor (3*16)(%r12), RL2, RL2;
	vpxor (4*16)(%r12), RR3, RR3;
	vpxor (5*16)(%r12), RL3, RL3;
	vpxor (6*16)(%r12), RR4, RR4;
	vpxor (7*16)(%r12), RL4, RL4;
	vmovdqu RR1, (0*16)(%r11);
	vmovdqu RL1, (1*16)(%r11);
	vmovdqu RR2, (2*16)(%r11);
	vmovdqu RL2, (3*16)(%r11);
	vmovdqu RR3, (4*16)(%r11);
	vmovdqu RL3, (5*16)(%r11);
	vmovdqu RR4, (6*16)(%r11);
	vmovdqu RL4, (7*16)(%r11);

	popq %r15;
	popq %r12;
	FRAME_END
	ret;
ENDPROC(cast5_ctr_16way)
