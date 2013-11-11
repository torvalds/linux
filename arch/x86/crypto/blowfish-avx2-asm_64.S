/*
 * x86_64/AVX2 assembler optimized version of Blowfish
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

.file "blowfish-avx2-asm_64.S"

.data
.align 32

.Lprefetch_mask:
.long 0*64
.long 1*64
.long 2*64
.long 3*64
.long 4*64
.long 5*64
.long 6*64
.long 7*64

.Lbswap32_mask:
.long 0x00010203
.long 0x04050607
.long 0x08090a0b
.long 0x0c0d0e0f

.Lbswap128_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
.Lbswap_iv_mask:
	.byte 7, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0

.text
/* structure of crypto context */
#define p	0
#define s0	((16 + 2) * 4)
#define s1	((16 + 2 + (1 * 256)) * 4)
#define s2	((16 + 2 + (2 * 256)) * 4)
#define s3	((16 + 2 + (3 * 256)) * 4)

/* register macros */
#define CTX	%rdi
#define RIO	 %rdx

#define RS0	%rax
#define RS1	%r8
#define RS2	%r9
#define RS3	%r10

#define RLOOP	%r11
#define RLOOPd	%r11d

#define RXr0	%ymm8
#define RXr1	%ymm9
#define RXr2	%ymm10
#define RXr3	%ymm11
#define RXl0	%ymm12
#define RXl1	%ymm13
#define RXl2	%ymm14
#define RXl3	%ymm15

/* temp regs */
#define RT0	%ymm0
#define RT0x	%xmm0
#define RT1	%ymm1
#define RT1x	%xmm1
#define RIDX0	%ymm2
#define RIDX1	%ymm3
#define RIDX1x	%xmm3
#define RIDX2	%ymm4
#define RIDX3	%ymm5

/* vpgatherdd mask and '-1' */
#define RNOT	%ymm6

/* byte mask, (-1 >> 24) */
#define RBYTE	%ymm7

/***********************************************************************
 * 32-way AVX2 blowfish
 ***********************************************************************/
#define F(xl, xr) \
	vpsrld $24, xl, RIDX0; \
	vpsrld $16, xl, RIDX1; \
	vpsrld $8, xl, RIDX2; \
	vpand RBYTE, RIDX1, RIDX1; \
	vpand RBYTE, RIDX2, RIDX2; \
	vpand RBYTE, xl, RIDX3; \
	\
	vpgatherdd RNOT, (RS0, RIDX0, 4), RT0; \
	vpcmpeqd RNOT, RNOT, RNOT; \
	vpcmpeqd RIDX0, RIDX0, RIDX0; \
	\
	vpgatherdd RNOT, (RS1, RIDX1, 4), RT1; \
	vpcmpeqd RIDX1, RIDX1, RIDX1; \
	vpaddd RT0, RT1, RT0; \
	\
	vpgatherdd RIDX0, (RS2, RIDX2, 4), RT1; \
	vpxor RT0, RT1, RT0; \
	\
	vpgatherdd RIDX1, (RS3, RIDX3, 4), RT1; \
	vpcmpeqd RNOT, RNOT, RNOT; \
	vpaddd RT0, RT1, RT0; \
	\
	vpxor RT0, xr, xr;

#define add_roundkey(xl, nmem) \
	vpbroadcastd nmem, RT0; \
	vpxor RT0, xl ## 0, xl ## 0; \
	vpxor RT0, xl ## 1, xl ## 1; \
	vpxor RT0, xl ## 2, xl ## 2; \
	vpxor RT0, xl ## 3, xl ## 3;

#define round_enc() \
	add_roundkey(RXr, p(CTX,RLOOP,4)); \
	F(RXl0, RXr0); \
	F(RXl1, RXr1); \
	F(RXl2, RXr2); \
	F(RXl3, RXr3); \
	\
	add_roundkey(RXl, p+4(CTX,RLOOP,4)); \
	F(RXr0, RXl0); \
	F(RXr1, RXl1); \
	F(RXr2, RXl2); \
	F(RXr3, RXl3);

#define round_dec() \
	add_roundkey(RXr, p+4*2(CTX,RLOOP,4)); \
	F(RXl0, RXr0); \
	F(RXl1, RXr1); \
	F(RXl2, RXr2); \
	F(RXl3, RXr3); \
	\
	add_roundkey(RXl, p+4(CTX,RLOOP,4)); \
	F(RXr0, RXl0); \
	F(RXr1, RXl1); \
	F(RXr2, RXl2); \
	F(RXr3, RXl3);

#define init_round_constants() \
	vpcmpeqd RNOT, RNOT, RNOT; \
	leaq s0(CTX), RS0; \
	leaq s1(CTX), RS1; \
	leaq s2(CTX), RS2; \
	leaq s3(CTX), RS3; \
	vpsrld $24, RNOT, RBYTE;

#define transpose_2x2(x0, x1, t0) \
	vpunpckldq x0, x1, t0; \
	vpunpckhdq x0, x1, x1; \
	\
	vpunpcklqdq t0, x1, x0; \
	vpunpckhqdq t0, x1, x1;

#define read_block(xl, xr) \
	vbroadcasti128 .Lbswap32_mask, RT1; \
	\
	vpshufb RT1, xl ## 0, xl ## 0; \
	vpshufb RT1, xr ## 0, xr ## 0; \
	vpshufb RT1, xl ## 1, xl ## 1; \
	vpshufb RT1, xr ## 1, xr ## 1; \
	vpshufb RT1, xl ## 2, xl ## 2; \
	vpshufb RT1, xr ## 2, xr ## 2; \
	vpshufb RT1, xl ## 3, xl ## 3; \
	vpshufb RT1, xr ## 3, xr ## 3; \
	\
	transpose_2x2(xl ## 0, xr ## 0, RT0); \
	transpose_2x2(xl ## 1, xr ## 1, RT0); \
	transpose_2x2(xl ## 2, xr ## 2, RT0); \
	transpose_2x2(xl ## 3, xr ## 3, RT0);

#define write_block(xl, xr) \
	vbroadcasti128 .Lbswap32_mask, RT1; \
	\
	transpose_2x2(xl ## 0, xr ## 0, RT0); \
	transpose_2x2(xl ## 1, xr ## 1, RT0); \
	transpose_2x2(xl ## 2, xr ## 2, RT0); \
	transpose_2x2(xl ## 3, xr ## 3, RT0); \
	\
	vpshufb RT1, xl ## 0, xl ## 0; \
	vpshufb RT1, xr ## 0, xr ## 0; \
	vpshufb RT1, xl ## 1, xl ## 1; \
	vpshufb RT1, xr ## 1, xr ## 1; \
	vpshufb RT1, xl ## 2, xl ## 2; \
	vpshufb RT1, xr ## 2, xr ## 2; \
	vpshufb RT1, xl ## 3, xl ## 3; \
	vpshufb RT1, xr ## 3, xr ## 3;

.align 8
__blowfish_enc_blk32:
	/* input:
	 *	%rdi: ctx, CTX
	 *	RXl0..4, RXr0..4: plaintext
	 * output:
	 *	RXl0..4, RXr0..4: ciphertext (RXl <=> RXr swapped)
	 */
	init_round_constants();

	read_block(RXl, RXr);

	movl $1, RLOOPd;
	add_roundkey(RXl, p+4*(0)(CTX));

.align 4
.L__enc_loop:
	round_enc();

	leal 2(RLOOPd), RLOOPd;
	cmpl $17, RLOOPd;
	jne .L__enc_loop;

	add_roundkey(RXr, p+4*(17)(CTX));

	write_block(RXl, RXr);

	ret;
ENDPROC(__blowfish_enc_blk32)

.align 8
__blowfish_dec_blk32:
	/* input:
	 *	%rdi: ctx, CTX
	 *	RXl0..4, RXr0..4: ciphertext
	 * output:
	 *	RXl0..4, RXr0..4: plaintext (RXl <=> RXr swapped)
	 */
	init_round_constants();

	read_block(RXl, RXr);

	movl $14, RLOOPd;
	add_roundkey(RXl, p+4*(17)(CTX));

.align 4
.L__dec_loop:
	round_dec();

	addl $-2, RLOOPd;
	jns .L__dec_loop;

	add_roundkey(RXr, p+4*(0)(CTX));

	write_block(RXl, RXr);

	ret;
ENDPROC(__blowfish_dec_blk32)

ENTRY(blowfish_ecb_enc_32way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	vzeroupper;

	vmovdqu 0*32(%rdx), RXl0;
	vmovdqu 1*32(%rdx), RXr0;
	vmovdqu 2*32(%rdx), RXl1;
	vmovdqu 3*32(%rdx), RXr1;
	vmovdqu 4*32(%rdx), RXl2;
	vmovdqu 5*32(%rdx), RXr2;
	vmovdqu 6*32(%rdx), RXl3;
	vmovdqu 7*32(%rdx), RXr3;

	call __blowfish_enc_blk32;

	vmovdqu RXr0, 0*32(%rsi);
	vmovdqu RXl0, 1*32(%rsi);
	vmovdqu RXr1, 2*32(%rsi);
	vmovdqu RXl1, 3*32(%rsi);
	vmovdqu RXr2, 4*32(%rsi);
	vmovdqu RXl2, 5*32(%rsi);
	vmovdqu RXr3, 6*32(%rsi);
	vmovdqu RXl3, 7*32(%rsi);

	vzeroupper;

	ret;
ENDPROC(blowfish_ecb_enc_32way)

ENTRY(blowfish_ecb_dec_32way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	vzeroupper;

	vmovdqu 0*32(%rdx), RXl0;
	vmovdqu 1*32(%rdx), RXr0;
	vmovdqu 2*32(%rdx), RXl1;
	vmovdqu 3*32(%rdx), RXr1;
	vmovdqu 4*32(%rdx), RXl2;
	vmovdqu 5*32(%rdx), RXr2;
	vmovdqu 6*32(%rdx), RXl3;
	vmovdqu 7*32(%rdx), RXr3;

	call __blowfish_dec_blk32;

	vmovdqu RXr0, 0*32(%rsi);
	vmovdqu RXl0, 1*32(%rsi);
	vmovdqu RXr1, 2*32(%rsi);
	vmovdqu RXl1, 3*32(%rsi);
	vmovdqu RXr2, 4*32(%rsi);
	vmovdqu RXl2, 5*32(%rsi);
	vmovdqu RXr3, 6*32(%rsi);
	vmovdqu RXl3, 7*32(%rsi);

	vzeroupper;

	ret;
ENDPROC(blowfish_ecb_dec_32way)

ENTRY(blowfish_cbc_dec_32way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	vzeroupper;

	vmovdqu 0*32(%rdx), RXl0;
	vmovdqu 1*32(%rdx), RXr0;
	vmovdqu 2*32(%rdx), RXl1;
	vmovdqu 3*32(%rdx), RXr1;
	vmovdqu 4*32(%rdx), RXl2;
	vmovdqu 5*32(%rdx), RXr2;
	vmovdqu 6*32(%rdx), RXl3;
	vmovdqu 7*32(%rdx), RXr3;

	call __blowfish_dec_blk32;

	/* xor with src */
	vmovq (%rdx), RT0x;
	vpshufd $0x4f, RT0x, RT0x;
	vinserti128 $1, 8(%rdx), RT0, RT0;
	vpxor RT0, RXr0, RXr0;
	vpxor 0*32+24(%rdx), RXl0, RXl0;
	vpxor 1*32+24(%rdx), RXr1, RXr1;
	vpxor 2*32+24(%rdx), RXl1, RXl1;
	vpxor 3*32+24(%rdx), RXr2, RXr2;
	vpxor 4*32+24(%rdx), RXl2, RXl2;
	vpxor 5*32+24(%rdx), RXr3, RXr3;
	vpxor 6*32+24(%rdx), RXl3, RXl3;

	vmovdqu RXr0, (0*32)(%rsi);
	vmovdqu RXl0, (1*32)(%rsi);
	vmovdqu RXr1, (2*32)(%rsi);
	vmovdqu RXl1, (3*32)(%rsi);
	vmovdqu RXr2, (4*32)(%rsi);
	vmovdqu RXl2, (5*32)(%rsi);
	vmovdqu RXr3, (6*32)(%rsi);
	vmovdqu RXl3, (7*32)(%rsi);

	vzeroupper;

	ret;
ENDPROC(blowfish_cbc_dec_32way)

ENTRY(blowfish_ctr_32way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: iv (big endian, 64bit)
	 */

	vzeroupper;

	vpcmpeqd RT0, RT0, RT0;
	vpsrldq $8, RT0, RT0; /* a: -1, b: 0, c: -1, d: 0 */

	vpcmpeqd RT1x, RT1x, RT1x;
	vpaddq RT1x, RT1x, RT1x; /* a: -2, b: -2 */
	vpxor RIDX0, RIDX0, RIDX0;
	vinserti128 $1, RT1x, RIDX0, RIDX0; /* a: 0, b: 0, c: -2, d: -2 */

	vpaddq RIDX0, RT0, RT0; /* a: -1, b: 0, c: -3, d: -2 */

	vpcmpeqd RT1, RT1, RT1;
	vpaddq RT1, RT1, RT1; /* a: -2, b: -2, c: -2, d: -2 */
	vpaddq RT1, RT1, RIDX2; /* a: -4, b: -4, c: -4, d: -4 */

	vbroadcasti128 .Lbswap_iv_mask, RIDX0;
	vbroadcasti128 .Lbswap128_mask, RIDX1;

	/* load IV and byteswap */
	vmovq (%rcx), RT1x;
	vinserti128 $1, RT1x, RT1, RT1; /* a: BE, b: 0, c: BE, d: 0 */
	vpshufb RIDX0, RT1, RT1; /* a: LE, b: LE, c: LE, d: LE */

	/* construct IVs */
	vpsubq RT0, RT1, RT1;		/* a: le1, b: le0, c: le3, d: le2 */
	vpshufb RIDX1, RT1, RXl0;	/* a: be0, b: be1, c: be2, d: be3 */
	vpsubq RIDX2, RT1, RT1;		/* le5, le4, le7, le6 */
	vpshufb RIDX1, RT1, RXr0;	/* be4, be5, be6, be7 */
	vpsubq RIDX2, RT1, RT1;
	vpshufb RIDX1, RT1, RXl1;
	vpsubq RIDX2, RT1, RT1;
	vpshufb RIDX1, RT1, RXr1;
	vpsubq RIDX2, RT1, RT1;
	vpshufb RIDX1, RT1, RXl2;
	vpsubq RIDX2, RT1, RT1;
	vpshufb RIDX1, RT1, RXr2;
	vpsubq RIDX2, RT1, RT1;
	vpshufb RIDX1, RT1, RXl3;
	vpsubq RIDX2, RT1, RT1;
	vpshufb RIDX1, RT1, RXr3;

	/* store last IV */
	vpsubq RIDX2, RT1, RT1; /* a: le33, b: le32, ... */
	vpshufb RIDX1x, RT1x, RT1x; /* a: be32, ... */
	vmovq RT1x, (%rcx);

	call __blowfish_enc_blk32;

	/* dst = src ^ iv */
	vpxor 0*32(%rdx), RXr0, RXr0;
	vpxor 1*32(%rdx), RXl0, RXl0;
	vpxor 2*32(%rdx), RXr1, RXr1;
	vpxor 3*32(%rdx), RXl1, RXl1;
	vpxor 4*32(%rdx), RXr2, RXr2;
	vpxor 5*32(%rdx), RXl2, RXl2;
	vpxor 6*32(%rdx), RXr3, RXr3;
	vpxor 7*32(%rdx), RXl3, RXl3;
	vmovdqu RXr0, (0*32)(%rsi);
	vmovdqu RXl0, (1*32)(%rsi);
	vmovdqu RXr1, (2*32)(%rsi);
	vmovdqu RXl1, (3*32)(%rsi);
	vmovdqu RXr2, (4*32)(%rsi);
	vmovdqu RXl2, (5*32)(%rsi);
	vmovdqu RXr3, (6*32)(%rsi);
	vmovdqu RXl3, (7*32)(%rsi);

	vzeroupper;

	ret;
ENDPROC(blowfish_ctr_32way)
