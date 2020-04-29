/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Twofish Cipher 8-way parallel algorithm (AVX/x86_64)
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 */

#include <linux/linkage.h>
#include <asm/frame.h>
#include "glue_helper-asm-avx.S"

.file "twofish-avx-x86_64-asm_64.S"

.section	.rodata.cst16.bswap128_mask, "aM", @progbits, 16
.align 16
.Lbswap128_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

.section	.rodata.cst16.xts_gf128mul_and_shl1_mask, "aM", @progbits, 16
.align 16
.Lxts_gf128mul_and_shl1_mask:
	.byte 0x87, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0

.text

/* structure of crypto context */
#define s0	0
#define s1	1024
#define s2	2048
#define s3	3072
#define w	4096
#define k	4128

/**********************************************************************
  8-way AVX twofish
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

#define RX0 %xmm8
#define RY0 %xmm9

#define RX1 %xmm10
#define RY1 %xmm11

#define RK1 %xmm12
#define RK2 %xmm13

#define RT %xmm14
#define RR %xmm15

#define RID1  %r13
#define RID1d %r13d
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

#define RGS1  %r8
#define RGS1d %r8d
#define RGS2  %r9
#define RGS2d %r9d
#define RGS3  %r10
#define RGS3d %r10d


#define lookup_32bit(t0, t1, t2, t3, src, dst, interleave_op, il_reg) \
	movzbl		src ## bl,        RID1d;     \
	movzbl		src ## bh,        RID2d;     \
	shrq $16,	src;                         \
	movl		t0(CTX, RID1, 4), dst ## d;  \
	movl		t1(CTX, RID2, 4), RID2d;     \
	movzbl		src ## bl,        RID1d;     \
	xorl		RID2d,            dst ## d;  \
	movzbl		src ## bh,        RID2d;     \
	interleave_op(il_reg);			     \
	xorl		t2(CTX, RID1, 4), dst ## d;  \
	xorl		t3(CTX, RID2, 4), dst ## d;

#define dummy(d) /* do nothing */

#define shr_next(reg) \
	shrq $16,	reg;

#define G(gi1, gi2, x, t0, t1, t2, t3) \
	lookup_32bit(t0, t1, t2, t3, ##gi1, RGS1, shr_next, ##gi1);  \
	lookup_32bit(t0, t1, t2, t3, ##gi2, RGS3, shr_next, ##gi2);  \
	\
	lookup_32bit(t0, t1, t2, t3, ##gi1, RGS2, dummy, none);      \
	shlq $32,	RGS2;                                        \
	orq		RGS1, RGS2;                                  \
	lookup_32bit(t0, t1, t2, t3, ##gi2, RGS1, dummy, none);      \
	shlq $32,	RGS1;                                        \
	orq		RGS1, RGS3;

#define round_head_2(a, b, x1, y1, x2, y2) \
	vmovq		b ## 1, RGI3;           \
	vpextrq $1,	b ## 1, RGI4;           \
	\
	G(RGI1, RGI2, x1, s0, s1, s2, s3);      \
	vmovq		a ## 2, RGI1;           \
	vpextrq $1,	a ## 2, RGI2;           \
	vmovq		RGS2, x1;               \
	vpinsrq $1,	RGS3, x1, x1;           \
	\
	G(RGI3, RGI4, y1, s1, s2, s3, s0);      \
	vmovq		b ## 2, RGI3;           \
	vpextrq $1,	b ## 2, RGI4;           \
	vmovq		RGS2, y1;               \
	vpinsrq $1,	RGS3, y1, y1;           \
	\
	G(RGI1, RGI2, x2, s0, s1, s2, s3);      \
	vmovq		RGS2, x2;               \
	vpinsrq $1,	RGS3, x2, x2;           \
	\
	G(RGI3, RGI4, y2, s1, s2, s3, s0);      \
	vmovq		RGS2, y2;               \
	vpinsrq $1,	RGS3, y2, y2;

#define encround_tail(a, b, c, d, x, y, prerotate) \
	vpaddd			x, y,   x; \
	vpaddd			x, RK1, RT;\
	prerotate(b);			   \
	vpxor			RT, c,  c; \
	vpaddd			y, x,   y; \
	vpaddd			y, RK2, y; \
	vpsrld $1,		c, RT;     \
	vpslld $(32 - 1),	c, c;      \
	vpor			c, RT,  c; \
	vpxor			d, y,   d; \

#define decround_tail(a, b, c, d, x, y, prerotate) \
	vpaddd			x, y,   x; \
	vpaddd			x, RK1, RT;\
	prerotate(a);			   \
	vpxor			RT, c,  c; \
	vpaddd			y, x,   y; \
	vpaddd			y, RK2, y; \
	vpxor			d, y,   d; \
	vpsrld $1,		d, y;      \
	vpslld $(32 - 1),	d, d;      \
	vpor			d, y,   d; \

#define rotate_1l(x) \
	vpslld $1,		x, RR;     \
	vpsrld $(32 - 1),	x, x;      \
	vpor			x, RR,  x;

#define preload_rgi(c) \
	vmovq			c, RGI1; \
	vpextrq $1,		c, RGI2;

#define encrypt_round(n, a, b, c, d, preload, prerotate) \
	vbroadcastss (k+4*(2*(n)))(CTX),   RK1;                  \
	vbroadcastss (k+4*(2*(n)+1))(CTX), RK2;                  \
	round_head_2(a, b, RX0, RY0, RX1, RY1);                  \
	encround_tail(a ## 1, b ## 1, c ## 1, d ## 1, RX0, RY0, prerotate); \
	preload(c ## 1);                                         \
	encround_tail(a ## 2, b ## 2, c ## 2, d ## 2, RX1, RY1, prerotate);

#define decrypt_round(n, a, b, c, d, preload, prerotate) \
	vbroadcastss (k+4*(2*(n)))(CTX),   RK1;                  \
	vbroadcastss (k+4*(2*(n)+1))(CTX), RK2;                  \
	round_head_2(a, b, RX0, RY0, RX1, RY1);                  \
	decround_tail(a ## 1, b ## 1, c ## 1, d ## 1, RX0, RY0, prerotate); \
	preload(c ## 1);                                         \
	decround_tail(a ## 2, b ## 2, c ## 2, d ## 2, RX1, RY1, prerotate);

#define encrypt_cycle(n) \
	encrypt_round((2*n), RA, RB, RC, RD, preload_rgi, rotate_1l); \
	encrypt_round(((2*n) + 1), RC, RD, RA, RB, preload_rgi, rotate_1l);

#define encrypt_cycle_last(n) \
	encrypt_round((2*n), RA, RB, RC, RD, preload_rgi, rotate_1l); \
	encrypt_round(((2*n) + 1), RC, RD, RA, RB, dummy, dummy);

#define decrypt_cycle(n) \
	decrypt_round(((2*n) + 1), RC, RD, RA, RB, preload_rgi, rotate_1l); \
	decrypt_round((2*n), RA, RB, RC, RD, preload_rgi, rotate_1l);

#define decrypt_cycle_last(n) \
	decrypt_round(((2*n) + 1), RC, RD, RA, RB, preload_rgi, rotate_1l); \
	decrypt_round((2*n), RA, RB, RC, RD, dummy, dummy);

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

#define inpack_blocks(x0, x1, x2, x3, wkey, t0, t1, t2) \
	vpxor		x0, wkey, x0; \
	vpxor		x1, wkey, x1; \
	vpxor		x2, wkey, x2; \
	vpxor		x3, wkey, x3; \
	\
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2)

#define outunpack_blocks(x0, x1, x2, x3, wkey, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	vpxor		x0, wkey, x0; \
	vpxor		x1, wkey, x1; \
	vpxor		x2, wkey, x2; \
	vpxor		x3, wkey, x3;

.align 8
SYM_FUNC_START_LOCAL(__twofish_enc_blk8)
	/* input:
	 *	%rdi: ctx, CTX
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: blocks
	 * output:
	 *	RC1, RD1, RA1, RB1, RC2, RD2, RA2, RB2: encrypted blocks
	 */

	vmovdqu w(CTX), RK1;

	pushq %r13;
	pushq %rbx;
	pushq %rcx;

	inpack_blocks(RA1, RB1, RC1, RD1, RK1, RX0, RY0, RK2);
	preload_rgi(RA1);
	rotate_1l(RD1);
	inpack_blocks(RA2, RB2, RC2, RD2, RK1, RX0, RY0, RK2);
	rotate_1l(RD2);

	encrypt_cycle(0);
	encrypt_cycle(1);
	encrypt_cycle(2);
	encrypt_cycle(3);
	encrypt_cycle(4);
	encrypt_cycle(5);
	encrypt_cycle(6);
	encrypt_cycle_last(7);

	vmovdqu (w+4*4)(CTX), RK1;

	popq %rcx;
	popq %rbx;
	popq %r13;

	outunpack_blocks(RC1, RD1, RA1, RB1, RK1, RX0, RY0, RK2);
	outunpack_blocks(RC2, RD2, RA2, RB2, RK1, RX0, RY0, RK2);

	ret;
SYM_FUNC_END(__twofish_enc_blk8)

.align 8
SYM_FUNC_START_LOCAL(__twofish_dec_blk8)
	/* input:
	 *	%rdi: ctx, CTX
	 *	RC1, RD1, RA1, RB1, RC2, RD2, RA2, RB2: encrypted blocks
	 * output:
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: decrypted blocks
	 */

	vmovdqu (w+4*4)(CTX), RK1;

	pushq %r13;
	pushq %rbx;

	inpack_blocks(RC1, RD1, RA1, RB1, RK1, RX0, RY0, RK2);
	preload_rgi(RC1);
	rotate_1l(RA1);
	inpack_blocks(RC2, RD2, RA2, RB2, RK1, RX0, RY0, RK2);
	rotate_1l(RA2);

	decrypt_cycle(7);
	decrypt_cycle(6);
	decrypt_cycle(5);
	decrypt_cycle(4);
	decrypt_cycle(3);
	decrypt_cycle(2);
	decrypt_cycle(1);
	decrypt_cycle_last(0);

	vmovdqu (w)(CTX), RK1;

	popq %rbx;
	popq %r13;

	outunpack_blocks(RA1, RB1, RC1, RD1, RK1, RX0, RY0, RK2);
	outunpack_blocks(RA2, RB2, RC2, RD2, RK1, RX0, RY0, RK2);

	ret;
SYM_FUNC_END(__twofish_dec_blk8)

SYM_FUNC_START(twofish_ecb_enc_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN

	movq %rsi, %r11;

	load_8way(%rdx, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	call __twofish_enc_blk8;

	store_8way(%r11, RC1, RD1, RA1, RB1, RC2, RD2, RA2, RB2);

	FRAME_END
	ret;
SYM_FUNC_END(twofish_ecb_enc_8way)

SYM_FUNC_START(twofish_ecb_dec_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN

	movq %rsi, %r11;

	load_8way(%rdx, RC1, RD1, RA1, RB1, RC2, RD2, RA2, RB2);

	call __twofish_dec_blk8;

	store_8way(%r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	FRAME_END
	ret;
SYM_FUNC_END(twofish_ecb_dec_8way)

SYM_FUNC_START(twofish_cbc_dec_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN

	pushq %r12;

	movq %rsi, %r11;
	movq %rdx, %r12;

	load_8way(%rdx, RC1, RD1, RA1, RB1, RC2, RD2, RA2, RB2);

	call __twofish_dec_blk8;

	store_cbc_8way(%r12, %r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	popq %r12;

	FRAME_END
	ret;
SYM_FUNC_END(twofish_cbc_dec_8way)

SYM_FUNC_START(twofish_ctr_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: iv (little endian, 128bit)
	 */
	FRAME_BEGIN

	pushq %r12;

	movq %rsi, %r11;
	movq %rdx, %r12;

	load_ctr_8way(%rcx, .Lbswap128_mask, RA1, RB1, RC1, RD1, RA2, RB2, RC2,
		      RD2, RX0, RX1, RY0);

	call __twofish_enc_blk8;

	store_ctr_8way(%r12, %r11, RC1, RD1, RA1, RB1, RC2, RD2, RA2, RB2);

	popq %r12;

	FRAME_END
	ret;
SYM_FUNC_END(twofish_ctr_8way)

SYM_FUNC_START(twofish_xts_enc_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 */
	FRAME_BEGIN

	movq %rsi, %r11;

	/* regs <= src, dst <= IVs, regs <= regs xor IVs */
	load_xts_8way(%rcx, %rdx, %rsi, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2,
		      RX0, RX1, RY0, .Lxts_gf128mul_and_shl1_mask);

	call __twofish_enc_blk8;

	/* dst <= regs xor IVs(in dst) */
	store_xts_8way(%r11, RC1, RD1, RA1, RB1, RC2, RD2, RA2, RB2);

	FRAME_END
	ret;
SYM_FUNC_END(twofish_xts_enc_8way)

SYM_FUNC_START(twofish_xts_dec_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 */
	FRAME_BEGIN

	movq %rsi, %r11;

	/* regs <= src, dst <= IVs, regs <= regs xor IVs */
	load_xts_8way(%rcx, %rdx, %rsi, RC1, RD1, RA1, RB1, RC2, RD2, RA2, RB2,
		      RX0, RX1, RY0, .Lxts_gf128mul_and_shl1_mask);

	call __twofish_dec_blk8;

	/* dst <= regs xor IVs(in dst) */
	store_xts_8way(%r11, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	FRAME_END
	ret;
SYM_FUNC_END(twofish_xts_dec_8way)
