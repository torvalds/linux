/*
 * Cast6 Cipher 8-way parallel algorithm (AVX/x86_64)
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

.file "cast6-avx-x86_64-asm_64.S"
.text

.extern cast6_s1
.extern cast6_s2
.extern cast6_s3
.extern cast6_s4

/* structure of crypto context */
#define km	0
#define kr	(12*4*4)

/* s-boxes */
#define s1	cast6_s1
#define s2	cast6_s2
#define s3	cast6_s3
#define s4	cast6_s4

/**********************************************************************
  8-way AVX cast6
 **********************************************************************/
#define CTX %rdi

#define RA1 %xmm0
#define RB1 %xmm1
#define RC1 %xmm2
#define RD1 %xmm3

#define RA2 %xmm4
#define RB2 %xmm5
#define RC2 %xmm6
#define RD2 %xmm7

#define RX %xmm8

#define RKM  %xmm9
#define RKRF %xmm10
#define RKRR %xmm11

#define RTMP  %xmm12
#define RMASK %xmm13
#define R32   %xmm14

#define RID1  %rax
#define RID1b %al
#define RID2  %rbx
#define RID2b %bl

#define RGI1   %rdx
#define RGI1bl %dl
#define RGI1bh %dh
#define RGI2   %rcx
#define RGI2bl %cl
#define RGI2bh %ch

#define RFS1  %r8
#define RFS1d %r8d
#define RFS2  %r9
#define RFS2d %r9d
#define RFS3  %r10
#define RFS3d %r10d


#define lookup_32bit(src, dst, op1, op2, op3) \
	movb		src ## bl,     RID1b;    \
	movb		src ## bh,     RID2b;    \
	movl		s1(, RID1, 4), dst ## d; \
	op1		s2(, RID2, 4), dst ## d; \
	shrq $16,	src;                     \
	movb		src ## bl,     RID1b;    \
	movb		src ## bh,     RID2b;    \
	op2		s3(, RID1, 4), dst ## d; \
	op3		s4(, RID2, 4), dst ## d;

#define F(a, x, op0, op1, op2, op3) \
	op0	a,	RKM,  x;                 \
	vpslld  RKRF,	x,    RTMP;              \
	vpsrld  RKRR,	x,    x;                 \
	vpor	RTMP,	x,    x;                 \
	\
	vpshufb	RMASK,	x,    x;                 \
	vmovq		x,    RGI1;              \
	vpsrldq $8,	x,    x;                 \
	vmovq		x,    RGI2;              \
	\
	lookup_32bit(RGI1, RFS1, op1, op2, op3); \
	shrq $16,	RGI1;                    \
	lookup_32bit(RGI1, RFS2, op1, op2, op3); \
	shlq $32,	RFS2;                    \
	orq		RFS1, RFS2;              \
	\
	lookup_32bit(RGI2, RFS1, op1, op2, op3); \
	shrq $16,	RGI2;                    \
	lookup_32bit(RGI2, RFS3, op1, op2, op3); \
	shlq $32,	RFS3;                    \
	orq		RFS1, RFS3;              \
	\
	vmovq		RFS2, x;                 \
	vpinsrq $1,	RFS3, x, x;

#define F1(b, x) F(b, x, vpaddd, xorl, subl, addl)
#define F2(b, x) F(b, x, vpxor,  subl, addl, xorl)
#define F3(b, x) F(b, x, vpsubd, addl, xorl, subl)

#define qop(in, out, x, f) \
	F ## f(in ## 1, x);          \
	vpxor out ## 1, x, out ## 1; \
	F ## f(in ## 2, x);          \
	vpxor out ## 2, x, out ## 2; \

#define Q(n) \
	vbroadcastss	(km+(4*(4*n+0)))(CTX), RKM;        \
	vpinsrb $0,	(kr+(4*n+0))(CTX),     RKRF, RKRF; \
	vpsubq		RKRF,                  R32,  RKRR; \
	qop(RD, RC, RX, 1);                                \
	\
	vbroadcastss	(km+(4*(4*n+1)))(CTX), RKM;        \
	vpinsrb $0,	(kr+(4*n+1))(CTX),     RKRF, RKRF; \
	vpsubq		RKRF,                  R32,  RKRR; \
	qop(RC, RB, RX, 2);                                \
	\
	vbroadcastss	(km+(4*(4*n+2)))(CTX), RKM;        \
	vpinsrb $0,	(kr+(4*n+2))(CTX),     RKRF, RKRF; \
	vpsubq		RKRF,                  R32,  RKRR; \
	qop(RB, RA, RX, 3);                                \
	\
	vbroadcastss	(km+(4*(4*n+3)))(CTX), RKM;        \
	vpinsrb $0,	(kr+(4*n+3))(CTX),     RKRF, RKRF; \
	vpsubq		RKRF,                  R32,  RKRR; \
	qop(RA, RD, RX, 1);

#define QBAR(n) \
	vbroadcastss	(km+(4*(4*n+3)))(CTX), RKM;        \
	vpinsrb $0,	(kr+(4*n+3))(CTX),     RKRF, RKRF; \
	vpsubq		RKRF,                  R32,  RKRR; \
	qop(RA, RD, RX, 1);                                \
	\
	vbroadcastss	(km+(4*(4*n+2)))(CTX), RKM;        \
	vpinsrb $0,	(kr+(4*n+2))(CTX),     RKRF, RKRF; \
	vpsubq		RKRF,                  R32,  RKRR; \
	qop(RB, RA, RX, 3);                                \
	\
	vbroadcastss	(km+(4*(4*n+1)))(CTX), RKM;        \
	vpinsrb $0,	(kr+(4*n+1))(CTX),     RKRF, RKRF; \
	vpsubq		RKRF,                  R32,  RKRR; \
	qop(RC, RB, RX, 2);                                \
	\
	vbroadcastss	(km+(4*(4*n+0)))(CTX), RKM;        \
	vpinsrb $0,	(kr+(4*n+0))(CTX),     RKRF, RKRF; \
	vpsubq		RKRF,                  R32,  RKRR; \
	qop(RD, RC, RX, 1);


#define transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	vpunpckldq		x1, x0, t0; \
	vpunpckhdq		x1, x0, t2; \
	vpunpckldq		x3, x2, t1; \
	vpunpckhdq		x3, x2, x3; \
	\
	vpunpcklqdq		t1, t0, x0; \
	vpunpckhqdq		t1, t0, x1; \
	vpunpcklqdq		x3, t2, x2; \
	vpunpckhqdq		x3, t2, x3;

#define inpack_blocks(in, x0, x1, x2, x3, t0, t1, t2) \
	vmovdqu (0*4*4)(in),	x0; \
	vmovdqu (1*4*4)(in),	x1; \
	vmovdqu (2*4*4)(in),	x2; \
	vmovdqu (3*4*4)(in),	x3; \
	vpshufb RMASK, x0,	x0; \
	vpshufb RMASK, x1,	x1; \
	vpshufb RMASK, x2,	x2; \
	vpshufb RMASK, x3,	x3; \
	\
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2)

#define outunpack_blocks(out, x0, x1, x2, x3, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	vpshufb RMASK,		x0, x0;       \
	vpshufb RMASK,		x1, x1;       \
	vpshufb RMASK,		x2, x2;       \
	vpshufb RMASK,		x3, x3;       \
	vmovdqu x0,		(0*4*4)(out); \
	vmovdqu	x1,		(1*4*4)(out); \
	vmovdqu	x2,		(2*4*4)(out); \
	vmovdqu	x3,		(3*4*4)(out);

#define outunpack_xor_blocks(out, x0, x1, x2, x3, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	vpshufb RMASK,		x0, x0;       \
	vpshufb RMASK,		x1, x1;       \
	vpshufb RMASK,		x2, x2;       \
	vpshufb RMASK,		x3, x3;       \
	vpxor (0*4*4)(out),	x0, x0;       \
	vmovdqu	x0,		(0*4*4)(out); \
	vpxor (1*4*4)(out),	x1, x1;       \
	vmovdqu	x1,		(1*4*4)(out); \
	vpxor (2*4*4)(out),	x2, x2;       \
	vmovdqu x2,		(2*4*4)(out); \
	vpxor (3*4*4)(out),	x3, x3;       \
	vmovdqu x3,		(3*4*4)(out);

.align 16
.Lbswap_mask:
	.byte 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12
.L32_mask:
	.byte 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 ,0, 0, 0, 0, 0

.align 16
.global __cast6_enc_blk_8way
.type   __cast6_enc_blk_8way,@function;

__cast6_enc_blk_8way:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: bool, if true: xor output
	 */

	pushq %rbx;
	pushq %rcx;

	vmovdqu .Lbswap_mask, RMASK;
	vmovdqu .L32_mask, R32;
	vpxor RKRF, RKRF, RKRF;

	leaq (4*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RA1, RB1, RC1, RD1, RTMP, RX, RKM);
	inpack_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKM);

	xorq RID1, RID1;
	xorq RID2, RID2;

	Q(0);
	Q(1);
	Q(2);
	Q(3);
	Q(4);
	Q(5);
	QBAR(6);
	QBAR(7);
	QBAR(8);
	QBAR(9);
	QBAR(10);
	QBAR(11);

	popq %rcx;
	popq %rbx;

	leaq (4*4*4)(%rsi), %rax;

	testb %cl, %cl;
	jnz __enc_xor8;

	outunpack_blocks(%rsi, RA1, RB1, RC1, RD1, RTMP, RX, RKM);
	outunpack_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKM);

	ret;

__enc_xor8:
	outunpack_xor_blocks(%rsi, RA1, RB1, RC1, RD1, RTMP, RX, RKM);
	outunpack_xor_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKM);

	ret;

.align 16
.global cast6_dec_blk_8way
.type   cast6_dec_blk_8way,@function;

cast6_dec_blk_8way:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	pushq %rbx;

	vmovdqu .Lbswap_mask, RMASK;
	vmovdqu .L32_mask, R32;
	vpxor RKRF, RKRF, RKRF;

	leaq (4*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RA1, RB1, RC1, RD1, RTMP, RX, RKM);
	inpack_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKM);

	xorq RID1, RID1;
	xorq RID2, RID2;

	Q(11);
	Q(10);
	Q(9);
	Q(8);
	Q(7);
	Q(6);
	QBAR(5);
	QBAR(4);
	QBAR(3);
	QBAR(2);
	QBAR(1);
	QBAR(0);

	popq %rbx;

	leaq (4*4*4)(%rsi), %rax;
	outunpack_blocks(%rsi, RA1, RB1, RC1, RD1, RTMP, RX, RKM);
	outunpack_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKM);

	ret;
