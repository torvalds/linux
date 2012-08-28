/*
 * Cast6 Cipher 8-way parallel algorithm (AVX/x86_64)
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Copyright © 2012 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
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

#define RX  %xmm8

#define RKM  %xmm9
#define RKR  %xmm10
#define RKRF %xmm11
#define RKRR %xmm12
#define R32  %xmm13
#define R1ST %xmm14

#define RTMP %xmm15

#define RID1  %rbp
#define RID1d %ebp
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

#define qop(in, out, f) \
	F ## f ## _2(out ## 1, in ## 1, out ## 2, in ## 2);

#define get_round_keys(nn) \
	vbroadcastss	(km+(4*(nn)))(CTX), RKM;        \
	vpand		R1ST,               RKR,  RKRF; \
	vpsubq		RKRF,               R32,  RKRR; \
	vpsrldq $1,	RKR,                RKR;

#define Q(n) \
	get_round_keys(4*n+0); \
	qop(RD, RC, 1);        \
	\
	get_round_keys(4*n+1); \
	qop(RC, RB, 2);        \
	\
	get_round_keys(4*n+2); \
	qop(RB, RA, 3);        \
	\
	get_round_keys(4*n+3); \
	qop(RA, RD, 1);

#define QBAR(n) \
	get_round_keys(4*n+3); \
	qop(RA, RD, 1);        \
	\
	get_round_keys(4*n+2); \
	qop(RB, RA, 3);        \
	\
	get_round_keys(4*n+1); \
	qop(RC, RB, 2);        \
	\
	get_round_keys(4*n+0); \
	qop(RD, RC, 1);

#define shuffle(mask) \
	vpshufb		mask,            RKR, RKR;

#define preload_rkr(n, do_mask, mask) \
	vbroadcastss	.L16_mask,                RKR;      \
	/* add 16-bit rotation to key rotations (mod 32) */ \
	vpxor		(kr+n*16)(CTX),           RKR, RKR; \
	do_mask(mask);

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

#define inpack_blocks(in, x0, x1, x2, x3, t0, t1, t2, rmask) \
	vmovdqu (0*4*4)(in),	x0; \
	vmovdqu (1*4*4)(in),	x1; \
	vmovdqu (2*4*4)(in),	x2; \
	vmovdqu (3*4*4)(in),	x3; \
	vpshufb rmask, x0,	x0; \
	vpshufb rmask, x1,	x1; \
	vpshufb rmask, x2,	x2; \
	vpshufb rmask, x3,	x3; \
	\
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2)

#define outunpack_blocks(out, x0, x1, x2, x3, t0, t1, t2, rmask) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	vpshufb rmask,		x0, x0;       \
	vpshufb rmask,		x1, x1;       \
	vpshufb rmask,		x2, x2;       \
	vpshufb rmask,		x3, x3;       \
	vmovdqu x0,		(0*4*4)(out); \
	vmovdqu	x1,		(1*4*4)(out); \
	vmovdqu	x2,		(2*4*4)(out); \
	vmovdqu	x3,		(3*4*4)(out);

#define outunpack_xor_blocks(out, x0, x1, x2, x3, t0, t1, t2, rmask) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	vpshufb rmask,		x0, x0;       \
	vpshufb rmask,		x1, x1;       \
	vpshufb rmask,		x2, x2;       \
	vpshufb rmask,		x3, x3;       \
	vpxor (0*4*4)(out),	x0, x0;       \
	vmovdqu	x0,		(0*4*4)(out); \
	vpxor (1*4*4)(out),	x1, x1;       \
	vmovdqu	x1,		(1*4*4)(out); \
	vpxor (2*4*4)(out),	x2, x2;       \
	vmovdqu x2,		(2*4*4)(out); \
	vpxor (3*4*4)(out),	x3, x3;       \
	vmovdqu x3,		(3*4*4)(out);

.data

.align 16
.Lbswap_mask:
	.byte 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12
.Lrkr_enc_Q_Q_QBAR_QBAR:
	.byte 0, 1, 2, 3, 4, 5, 6, 7, 11, 10, 9, 8, 15, 14, 13, 12
.Lrkr_enc_QBAR_QBAR_QBAR_QBAR:
	.byte 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12
.Lrkr_dec_Q_Q_Q_Q:
	.byte 12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3
.Lrkr_dec_Q_Q_QBAR_QBAR:
	.byte 12, 13, 14, 15, 8, 9, 10, 11, 7, 6, 5, 4, 3, 2, 1, 0
.Lrkr_dec_QBAR_QBAR_QBAR_QBAR:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
.L16_mask:
	.byte 16, 16, 16, 16
.L32_mask:
	.byte 32, 0, 0, 0
.Lfirst_mask:
	.byte 0x1f, 0, 0, 0

.text

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

	pushq %rbp;
	pushq %rbx;
	pushq %rcx;

	vmovdqa .Lbswap_mask, RKM;
	vmovd .Lfirst_mask, R1ST;
	vmovd .L32_mask, R32;

	leaq (4*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	inpack_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

	movq %rsi, %r11;

	preload_rkr(0, dummy, none);
	Q(0);
	Q(1);
	Q(2);
	Q(3);
	preload_rkr(1, shuffle, .Lrkr_enc_Q_Q_QBAR_QBAR);
	Q(4);
	Q(5);
	QBAR(6);
	QBAR(7);
	preload_rkr(2, shuffle, .Lrkr_enc_QBAR_QBAR_QBAR_QBAR);
	QBAR(8);
	QBAR(9);
	QBAR(10);
	QBAR(11);

	popq %rcx;
	popq %rbx;
	popq %rbp;

	vmovdqa .Lbswap_mask, RKM;
	leaq (4*4*4)(%r11), %rax;

	testb %cl, %cl;
	jnz __enc_xor8;

	outunpack_blocks(%r11, RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	outunpack_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

	ret;

__enc_xor8:
	outunpack_xor_blocks(%r11, RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	outunpack_xor_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

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

	pushq %rbp;
	pushq %rbx;

	vmovdqa .Lbswap_mask, RKM;
	vmovd .Lfirst_mask, R1ST;
	vmovd .L32_mask, R32;

	leaq (4*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	inpack_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

	movq %rsi, %r11;

	preload_rkr(2, shuffle, .Lrkr_dec_Q_Q_Q_Q);
	Q(11);
	Q(10);
	Q(9);
	Q(8);
	preload_rkr(1, shuffle, .Lrkr_dec_Q_Q_QBAR_QBAR);
	Q(7);
	Q(6);
	QBAR(5);
	QBAR(4);
	preload_rkr(0, shuffle, .Lrkr_dec_QBAR_QBAR_QBAR_QBAR);
	QBAR(3);
	QBAR(2);
	QBAR(1);
	QBAR(0);

	popq %rbx;
	popq %rbp;

	vmovdqa .Lbswap_mask, RKM;
	leaq (4*4*4)(%r11), %rax;
	outunpack_blocks(%r11, RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	outunpack_blocks(%rax, RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

	ret;
