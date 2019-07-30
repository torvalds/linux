/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cast6 Cipher 8-way parallel algorithm (AVX/x86_64)
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 */

#include <linux/linkage.h>
#include <asm/frame.h>
#include "glue_helper-asm-avx.S"

.file "cast6-avx-x86_64-asm_64.S"

.extern cast_s1
.extern cast_s2
.extern cast_s3
.extern cast_s4

/* structure of crypto context */
#define km	0
#define kr	(12*4*4)

/* s-boxes */
#define s1	cast_s1
#define s2	cast_s2
#define s3	cast_s3
#define s4	cast_s4

/**********************************************************************
  8-way AVX cast6
 **********************************************************************/
#define CTX %r15

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

#define inpack_blocks(x0, x1, x2, x3, t0, t1, t2, rmask) \
	vpshufb rmask, x0,	x0; \
	vpshufb rmask, x1,	x1; \
	vpshufb rmask, x2,	x2; \
	vpshufb rmask, x3,	x3; \
	\
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2)

#define outunpack_blocks(x0, x1, x2, x3, t0, t1, t2, rmask) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	vpshufb rmask,		x0, x0;       \
	vpshufb rmask,		x1, x1;       \
	vpshufb rmask,		x2, x2;       \
	vpshufb rmask,		x3, x3;

.section	.rodata.cst16, "aM", @progbits, 16
.align 16
.Lxts_gf128mul_and_shl1_mask:
	.byte 0x87, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
.Lbswap_mask:
	.byte 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12
.Lbswap128_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
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

.section	.rodata.cst4.L16_mask, "aM", @progbits, 4
.align 4
.L16_mask:
	.byte 16, 16, 16, 16

.section	.rodata.cst4.L32_mask, "aM", @progbits, 4
.align 4
.L32_mask:
	.byte 32, 0, 0, 0

.section	.rodata.cst4.first_mask, "aM", @progbits, 4
.align 4
.Lfirst_mask:
	.byte 0x1f, 0, 0, 0

.text

.align 8
__cast6_enc_blk8:
	/* input:
	 *	%rdi: ctx
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: blocks
	 * output:
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: encrypted blocks
	 */

	pushq %r15;
	pushq %rbx;

	movq %rdi, CTX;

	vmovdqa .Lbswap_mask, RKM;
	vmovd .Lfirst_mask, R1ST;
	vmovd .L32_mask, R32;

	inpack_blocks(RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	inpack_blocks(RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

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

	popq %rbx;
	popq %r15;

	vmovdqa .Lbswap_mask, RKM;

	outunpack_blocks(RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	outunpack_blocks(RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

	ret;
ENDPROC(__cast6_enc_blk8)

.align 8
__cast6_dec_blk8:
	/* input:
	 *	%rdi: ctx
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: encrypted blocks
	 * output:
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: decrypted blocks
	 */

	pushq %r15;
	pushq %rbx;

	movq %rdi, CTX;

	vmovdqa .Lbswap_mask, RKM;
	vmovd .Lfirst_mask, R1ST;
	vmovd .L32_mask, R32;

	inpack_blocks(RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	inpack_blocks(RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

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
	popq %r15;

	vmovdqa .Lbswap_mask, RKM;
	outunpack_blocks(RA1, RB1, RC1, RD1, RTMP, RX, RKRF, RKM);
	outunpack_blocks(RA2, RB2, RC2, RD2, RTMP, RX, RKRF, RKM);

	ret;
ENDPROC(__cast6_dec_blk8)

ENTRY(cast6_ecb_enc_8way)
	/* input:
	 *	%rdi: ctx
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN
	pushq %r15;

	movq %rdi, CTX;
	movq %rsi, %r11;

	load_8way(%rdx, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	call __cast6_enc_blk8;

	store_8way(%r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	popq %r15;
	FRAME_END
	ret;
ENDPROC(cast6_ecb_enc_8way)

ENTRY(cast6_ecb_dec_8way)
	/* input:
	 *	%rdi: ctx
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN
	pushq %r15;

	movq %rdi, CTX;
	movq %rsi, %r11;

	load_8way(%rdx, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	call __cast6_dec_blk8;

	store_8way(%r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	popq %r15;
	FRAME_END
	ret;
ENDPROC(cast6_ecb_dec_8way)

ENTRY(cast6_cbc_dec_8way)
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

	load_8way(%rdx, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	call __cast6_dec_blk8;

	store_cbc_8way(%r12, %r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	popq %r15;
	popq %r12;
	FRAME_END
	ret;
ENDPROC(cast6_cbc_dec_8way)

ENTRY(cast6_ctr_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: iv (little endian, 128bit)
	 */
	FRAME_BEGIN
	pushq %r12;
	pushq %r15

	movq %rdi, CTX;
	movq %rsi, %r11;
	movq %rdx, %r12;

	load_ctr_8way(%rcx, .Lbswap128_mask, RA1, RB1, RC1, RD1, RA2, RB2, RC2,
		      RD2, RX, RKR, RKM);

	call __cast6_enc_blk8;

	store_ctr_8way(%r12, %r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	popq %r15;
	popq %r12;
	FRAME_END
	ret;
ENDPROC(cast6_ctr_8way)

ENTRY(cast6_xts_enc_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 */
	FRAME_BEGIN
	pushq %r15;

	movq %rdi, CTX
	movq %rsi, %r11;

	/* regs <= src, dst <= IVs, regs <= regs xor IVs */
	load_xts_8way(%rcx, %rdx, %rsi, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2,
		      RX, RKR, RKM, .Lxts_gf128mul_and_shl1_mask);

	call __cast6_enc_blk8;

	/* dst <= regs xor IVs(in dst) */
	store_xts_8way(%r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	popq %r15;
	FRAME_END
	ret;
ENDPROC(cast6_xts_enc_8way)

ENTRY(cast6_xts_dec_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 */
	FRAME_BEGIN
	pushq %r15;

	movq %rdi, CTX
	movq %rsi, %r11;

	/* regs <= src, dst <= IVs, regs <= regs xor IVs */
	load_xts_8way(%rcx, %rdx, %rsi, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2,
		      RX, RKR, RKM, .Lxts_gf128mul_and_shl1_mask);

	call __cast6_dec_blk8;

	/* dst <= regs xor IVs(in dst) */
	store_xts_8way(%r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	popq %r15;
	FRAME_END
	ret;
ENDPROC(cast6_xts_dec_8way)
