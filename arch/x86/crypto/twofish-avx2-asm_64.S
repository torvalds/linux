/*
 * x86_64/AVX2 assembler optimized version of Twofish
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/linkage.h>
#include "glue_helper-asm-avx2.S"

.file "twofish-avx2-asm_64.S"

.data
.align 16

.Lvpshufb_mask0:
.long 0x80808000
.long 0x80808004
.long 0x80808008
.long 0x8080800c

.Lbswap128_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
.Lxts_gf128mul_and_shl1_mask_0:
	.byte 0x87, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
.Lxts_gf128mul_and_shl1_mask_1:
	.byte 0x0e, 1, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0

.text

/* structure of crypto context */
#define s0	0
#define s1	1024
#define s2	2048
#define s3	3072
#define w	4096
#define	k	4128

/* register macros */
#define CTX	%rdi

#define RS0	CTX
#define RS1	%r8
#define RS2	%r9
#define RS3	%r10
#define RK	%r11
#define RW	%rax
#define RROUND  %r12
#define RROUNDd %r12d

#define RA0	%ymm8
#define RB0	%ymm9
#define RC0	%ymm10
#define RD0	%ymm11
#define RA1	%ymm12
#define RB1	%ymm13
#define RC1	%ymm14
#define RD1	%ymm15

/* temp regs */
#define RX0	%ymm0
#define RY0	%ymm1
#define RX1	%ymm2
#define RY1	%ymm3
#define RT0	%ymm4
#define RIDX	%ymm5

#define RX0x	%xmm0
#define RY0x	%xmm1
#define RX1x	%xmm2
#define RY1x	%xmm3
#define RT0x	%xmm4

/* vpgatherdd mask and '-1' */
#define RNOT	%ymm6

/* byte mask, (-1 >> 24) */
#define RBYTE	%ymm7

/**********************************************************************
  16-way AVX2 twofish
 **********************************************************************/
#define init_round_constants() \
	vpcmpeqd RNOT, RNOT, RNOT; \
	vpsrld $24, RNOT, RBYTE; \
	leaq k(CTX), RK; \
	leaq w(CTX), RW; \
	leaq s1(CTX), RS1; \
	leaq s2(CTX), RS2; \
	leaq s3(CTX), RS3; \

#define g16(ab, rs0, rs1, rs2, rs3, xy) \
	vpand RBYTE, ab ## 0, RIDX; \
	vpgatherdd RNOT, (rs0, RIDX, 4), xy ## 0; \
	vpcmpeqd RNOT, RNOT, RNOT; \
		\
		vpand RBYTE, ab ## 1, RIDX; \
		vpgatherdd RNOT, (rs0, RIDX, 4), xy ## 1; \
		vpcmpeqd RNOT, RNOT, RNOT; \
	\
	vpsrld $8, ab ## 0, RIDX; \
	vpand RBYTE, RIDX, RIDX; \
	vpgatherdd RNOT, (rs1, RIDX, 4), RT0; \
	vpcmpeqd RNOT, RNOT, RNOT; \
	vpxor RT0, xy ## 0, xy ## 0; \
		\
		vpsrld $8, ab ## 1, RIDX; \
		vpand RBYTE, RIDX, RIDX; \
		vpgatherdd RNOT, (rs1, RIDX, 4), RT0; \
		vpcmpeqd RNOT, RNOT, RNOT; \
		vpxor RT0, xy ## 1, xy ## 1; \
	\
	vpsrld $16, ab ## 0, RIDX; \
	vpand RBYTE, RIDX, RIDX; \
	vpgatherdd RNOT, (rs2, RIDX, 4), RT0; \
	vpcmpeqd RNOT, RNOT, RNOT; \
	vpxor RT0, xy ## 0, xy ## 0; \
		\
		vpsrld $16, ab ## 1, RIDX; \
		vpand RBYTE, RIDX, RIDX; \
		vpgatherdd RNOT, (rs2, RIDX, 4), RT0; \
		vpcmpeqd RNOT, RNOT, RNOT; \
		vpxor RT0, xy ## 1, xy ## 1; \
	\
	vpsrld $24, ab ## 0, RIDX; \
	vpgatherdd RNOT, (rs3, RIDX, 4), RT0; \
	vpcmpeqd RNOT, RNOT, RNOT; \
	vpxor RT0, xy ## 0, xy ## 0; \
		\
		vpsrld $24, ab ## 1, RIDX; \
		vpgatherdd RNOT, (rs3, RIDX, 4), RT0; \
		vpcmpeqd RNOT, RNOT, RNOT; \
		vpxor RT0, xy ## 1, xy ## 1;

#define g1_16(a, x) \
	g16(a, RS0, RS1, RS2, RS3, x);

#define g2_16(b, y) \
	g16(b, RS1, RS2, RS3, RS0, y);

#define encrypt_round_end16(a, b, c, d, nk) \
	vpaddd RY0, RX0, RX0; \
	vpaddd RX0, RY0, RY0; \
	vpbroadcastd nk(RK,RROUND,8), RT0; \
	vpaddd RT0, RX0, RX0; \
	vpbroadcastd 4+nk(RK,RROUND,8), RT0; \
	vpaddd RT0, RY0, RY0; \
	\
	vpxor RY0, d ## 0, d ## 0; \
	\
	vpxor RX0, c ## 0, c ## 0; \
	vpsrld $1, c ## 0, RT0; \
	vpslld $31, c ## 0, c ## 0; \
	vpor RT0, c ## 0, c ## 0; \
	\
		vpaddd RY1, RX1, RX1; \
		vpaddd RX1, RY1, RY1; \
		vpbroadcastd nk(RK,RROUND,8), RT0; \
		vpaddd RT0, RX1, RX1; \
		vpbroadcastd 4+nk(RK,RROUND,8), RT0; \
		vpaddd RT0, RY1, RY1; \
		\
		vpxor RY1, d ## 1, d ## 1; \
		\
		vpxor RX1, c ## 1, c ## 1; \
		vpsrld $1, c ## 1, RT0; \
		vpslld $31, c ## 1, c ## 1; \
		vpor RT0, c ## 1, c ## 1; \

#define encrypt_round16(a, b, c, d, nk) \
	g2_16(b, RY); \
	\
	vpslld $1, b ## 0, RT0; \
	vpsrld $31, b ## 0, b ## 0; \
	vpor RT0, b ## 0, b ## 0; \
	\
		vpslld $1, b ## 1, RT0; \
		vpsrld $31, b ## 1, b ## 1; \
		vpor RT0, b ## 1, b ## 1; \
	\
	g1_16(a, RX); \
	\
	encrypt_round_end16(a, b, c, d, nk);

#define encrypt_round_first16(a, b, c, d, nk) \
	vpslld $1, d ## 0, RT0; \
	vpsrld $31, d ## 0, d ## 0; \
	vpor RT0, d ## 0, d ## 0; \
	\
		vpslld $1, d ## 1, RT0; \
		vpsrld $31, d ## 1, d ## 1; \
		vpor RT0, d ## 1, d ## 1; \
	\
	encrypt_round16(a, b, c, d, nk);

#define encrypt_round_last16(a, b, c, d, nk) \
	g2_16(b, RY); \
	\
	g1_16(a, RX); \
	\
	encrypt_round_end16(a, b, c, d, nk);

#define decrypt_round_end16(a, b, c, d, nk) \
	vpaddd RY0, RX0, RX0; \
	vpaddd RX0, RY0, RY0; \
	vpbroadcastd nk(RK,RROUND,8), RT0; \
	vpaddd RT0, RX0, RX0; \
	vpbroadcastd 4+nk(RK,RROUND,8), RT0; \
	vpaddd RT0, RY0, RY0; \
	\
	vpxor RX0, c ## 0, c ## 0; \
	\
	vpxor RY0, d ## 0, d ## 0; \
	vpsrld $1, d ## 0, RT0; \
	vpslld $31, d ## 0, d ## 0; \
	vpor RT0, d ## 0, d ## 0; \
	\
		vpaddd RY1, RX1, RX1; \
		vpaddd RX1, RY1, RY1; \
		vpbroadcastd nk(RK,RROUND,8), RT0; \
		vpaddd RT0, RX1, RX1; \
		vpbroadcastd 4+nk(RK,RROUND,8), RT0; \
		vpaddd RT0, RY1, RY1; \
		\
		vpxor RX1, c ## 1, c ## 1; \
		\
		vpxor RY1, d ## 1, d ## 1; \
		vpsrld $1, d ## 1, RT0; \
		vpslld $31, d ## 1, d ## 1; \
		vpor RT0, d ## 1, d ## 1;

#define decrypt_round16(a, b, c, d, nk) \
	g1_16(a, RX); \
	\
	vpslld $1, a ## 0, RT0; \
	vpsrld $31, a ## 0, a ## 0; \
	vpor RT0, a ## 0, a ## 0; \
	\
		vpslld $1, a ## 1, RT0; \
		vpsrld $31, a ## 1, a ## 1; \
		vpor RT0, a ## 1, a ## 1; \
	\
	g2_16(b, RY); \
	\
	decrypt_round_end16(a, b, c, d, nk);

#define decrypt_round_first16(a, b, c, d, nk) \
	vpslld $1, c ## 0, RT0; \
	vpsrld $31, c ## 0, c ## 0; \
	vpor RT0, c ## 0, c ## 0; \
	\
		vpslld $1, c ## 1, RT0; \
		vpsrld $31, c ## 1, c ## 1; \
		vpor RT0, c ## 1, c ## 1; \
	\
	decrypt_round16(a, b, c, d, nk)

#define decrypt_round_last16(a, b, c, d, nk) \
	g1_16(a, RX); \
	\
	g2_16(b, RY); \
	\
	decrypt_round_end16(a, b, c, d, nk);

#define encrypt_cycle16() \
	encrypt_round16(RA, RB, RC, RD, 0); \
	encrypt_round16(RC, RD, RA, RB, 8);

#define encrypt_cycle_first16() \
	encrypt_round_first16(RA, RB, RC, RD, 0); \
	encrypt_round16(RC, RD, RA, RB, 8);

#define encrypt_cycle_last16() \
	encrypt_round16(RA, RB, RC, RD, 0); \
	encrypt_round_last16(RC, RD, RA, RB, 8);

#define decrypt_cycle16(n) \
	decrypt_round16(RC, RD, RA, RB, 8); \
	decrypt_round16(RA, RB, RC, RD, 0);

#define decrypt_cycle_first16(n) \
	decrypt_round_first16(RC, RD, RA, RB, 8); \
	decrypt_round16(RA, RB, RC, RD, 0);

#define decrypt_cycle_last16(n) \
	decrypt_round16(RC, RD, RA, RB, 8); \
	decrypt_round_last16(RA, RB, RC, RD, 0);

#define transpose_4x4(x0,x1,x2,x3,t1,t2) \
	vpunpckhdq x1, x0, t2; \
	vpunpckldq x1, x0, x0; \
	\
	vpunpckldq x3, x2, t1; \
	vpunpckhdq x3, x2, x2; \
	\
	vpunpckhqdq t1,	x0, x1; \
	vpunpcklqdq t1,	x0, x0; \
	\
	vpunpckhqdq x2, t2, x3; \
	vpunpcklqdq x2,	t2, x2;

#define read_blocks8(offs,a,b,c,d) \
	transpose_4x4(a, b, c, d, RX0, RY0);

#define write_blocks8(offs,a,b,c,d) \
	transpose_4x4(a, b, c, d, RX0, RY0);

#define inpack_enc8(a,b,c,d) \
	vpbroadcastd 4*0(RW), RT0; \
	vpxor RT0, a, a; \
	\
	vpbroadcastd 4*1(RW), RT0; \
	vpxor RT0, b, b; \
	\
	vpbroadcastd 4*2(RW), RT0; \
	vpxor RT0, c, c; \
	\
	vpbroadcastd 4*3(RW), RT0; \
	vpxor RT0, d, d;

#define outunpack_enc8(a,b,c,d) \
	vpbroadcastd 4*4(RW), RX0; \
	vpbroadcastd 4*5(RW), RY0; \
	vpxor RX0, c, RX0; \
	vpxor RY0, d, RY0; \
	\
	vpbroadcastd 4*6(RW), RT0; \
	vpxor RT0, a, c; \
	vpbroadcastd 4*7(RW), RT0; \
	vpxor RT0, b, d; \
	\
	vmovdqa RX0, a; \
	vmovdqa RY0, b;

#define inpack_dec8(a,b,c,d) \
	vpbroadcastd 4*4(RW), RX0; \
	vpbroadcastd 4*5(RW), RY0; \
	vpxor RX0, a, RX0; \
	vpxor RY0, b, RY0; \
	\
	vpbroadcastd 4*6(RW), RT0; \
	vpxor RT0, c, a; \
	vpbroadcastd 4*7(RW), RT0; \
	vpxor RT0, d, b; \
	\
	vmovdqa RX0, c; \
	vmovdqa RY0, d;

#define outunpack_dec8(a,b,c,d) \
	vpbroadcastd 4*0(RW), RT0; \
	vpxor RT0, a, a; \
	\
	vpbroadcastd 4*1(RW), RT0; \
	vpxor RT0, b, b; \
	\
	vpbroadcastd 4*2(RW), RT0; \
	vpxor RT0, c, c; \
	\
	vpbroadcastd 4*3(RW), RT0; \
	vpxor RT0, d, d;

#define read_blocks16(a,b,c,d) \
	read_blocks8(0, a ## 0, b ## 0, c ## 0, d ## 0); \
	read_blocks8(8, a ## 1, b ## 1, c ## 1, d ## 1);

#define write_blocks16(a,b,c,d) \
	write_blocks8(0, a ## 0, b ## 0, c ## 0, d ## 0); \
	write_blocks8(8, a ## 1, b ## 1, c ## 1, d ## 1);

#define xor_blocks16(a,b,c,d) \
	xor_blocks8(0, a ## 0, b ## 0, c ## 0, d ## 0); \
	xor_blocks8(8, a ## 1, b ## 1, c ## 1, d ## 1);

#define inpack_enc16(a,b,c,d) \
	inpack_enc8(a ## 0, b ## 0, c ## 0, d ## 0); \
	inpack_enc8(a ## 1, b ## 1, c ## 1, d ## 1);

#define outunpack_enc16(a,b,c,d) \
	outunpack_enc8(a ## 0, b ## 0, c ## 0, d ## 0); \
	outunpack_enc8(a ## 1, b ## 1, c ## 1, d ## 1);

#define inpack_dec16(a,b,c,d) \
	inpack_dec8(a ## 0, b ## 0, c ## 0, d ## 0); \
	inpack_dec8(a ## 1, b ## 1, c ## 1, d ## 1);

#define outunpack_dec16(a,b,c,d) \
	outunpack_dec8(a ## 0, b ## 0, c ## 0, d ## 0); \
	outunpack_dec8(a ## 1, b ## 1, c ## 1, d ## 1);

.align 8
__twofish_enc_blk16:
	/* input:
	 *	%rdi: ctx, CTX
	 *	RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1: plaintext
	 * output:
	 *	RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1: ciphertext
	 */
	init_round_constants();

	read_blocks16(RA, RB, RC, RD);
	inpack_enc16(RA, RB, RC, RD);

	xorl RROUNDd, RROUNDd;
	encrypt_cycle_first16();
	movl $2, RROUNDd;

.align 4
.L__enc_loop:
	encrypt_cycle16();

	addl $2, RROUNDd;
	cmpl $14, RROUNDd;
	jne .L__enc_loop;

	encrypt_cycle_last16();

	outunpack_enc16(RA, RB, RC, RD);
	write_blocks16(RA, RB, RC, RD);

	ret;
ENDPROC(__twofish_enc_blk16)

.align 8
__twofish_dec_blk16:
	/* input:
	 *	%rdi: ctx, CTX
	 *	RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1: ciphertext
	 * output:
	 *	RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1: plaintext
	 */
	init_round_constants();

	read_blocks16(RA, RB, RC, RD);
	inpack_dec16(RA, RB, RC, RD);

	movl $14, RROUNDd;
	decrypt_cycle_first16();
	movl $12, RROUNDd;

.align 4
.L__dec_loop:
	decrypt_cycle16();

	addl $-2, RROUNDd;
	jnz .L__dec_loop;

	decrypt_cycle_last16();

	outunpack_dec16(RA, RB, RC, RD);
	write_blocks16(RA, RB, RC, RD);

	ret;
ENDPROC(__twofish_dec_blk16)

ENTRY(twofish_ecb_enc_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	vzeroupper;
	pushq %r12;

	load_16way(%rdx, RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1);

	call __twofish_enc_blk16;

	store_16way(%rsi, RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1);

	popq %r12;
	vzeroupper;

	ret;
ENDPROC(twofish_ecb_enc_16way)

ENTRY(twofish_ecb_dec_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	vzeroupper;
	pushq %r12;

	load_16way(%rdx, RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1);

	call __twofish_dec_blk16;

	store_16way(%rsi, RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1);

	popq %r12;
	vzeroupper;

	ret;
ENDPROC(twofish_ecb_dec_16way)

ENTRY(twofish_cbc_dec_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	vzeroupper;
	pushq %r12;

	load_16way(%rdx, RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1);

	call __twofish_dec_blk16;

	store_cbc_16way(%rdx, %rsi, RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1,
			RX0);

	popq %r12;
	vzeroupper;

	ret;
ENDPROC(twofish_cbc_dec_16way)

ENTRY(twofish_ctr_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 *	%rcx: iv (little endian, 128bit)
	 */

	vzeroupper;
	pushq %r12;

	load_ctr_16way(%rcx, .Lbswap128_mask, RA0, RB0, RC0, RD0, RA1, RB1, RC1,
		       RD1, RX0, RX0x, RX1, RX1x, RY0, RY0x, RY1, RY1x, RNOT,
		       RBYTE);

	call __twofish_enc_blk16;

	store_ctr_16way(%rdx, %rsi, RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1);

	popq %r12;
	vzeroupper;

	ret;
ENDPROC(twofish_ctr_16way)

.align 8
twofish_xts_crypt_16way:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 *	%r8: pointer to __twofish_enc_blk16 or __twofish_dec_blk16
	 */

	vzeroupper;
	pushq %r12;

	load_xts_16way(%rcx, %rdx, %rsi, RA0, RB0, RC0, RD0, RA1, RB1, RC1,
		       RD1, RX0, RX0x, RX1, RX1x, RY0, RY0x, RY1, RY1x, RNOT,
		       .Lxts_gf128mul_and_shl1_mask_0,
		       .Lxts_gf128mul_and_shl1_mask_1);

	call *%r8;

	store_xts_16way(%rsi, RA0, RB0, RC0, RD0, RA1, RB1, RC1, RD1);

	popq %r12;
	vzeroupper;

	ret;
ENDPROC(twofish_xts_crypt_16way)

ENTRY(twofish_xts_enc_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 */
	leaq __twofish_enc_blk16, %r8;
	jmp twofish_xts_crypt_16way;
ENDPROC(twofish_xts_enc_16way)

ENTRY(twofish_xts_dec_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst (16 blocks)
	 *	%rdx: src (16 blocks)
	 *	%rcx: iv (t ⊕ αⁿ ∈ GF(2¹²⁸))
	 */
	leaq __twofish_dec_blk16, %r8;
	jmp twofish_xts_crypt_16way;
ENDPROC(twofish_xts_dec_16way)
