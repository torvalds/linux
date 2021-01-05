/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Shared glue code for 128bit block ciphers, AVX2 assembler macros
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 */

#define load_16way(src, x0, x1, x2, x3, x4, x5, x6, x7) \
	vmovdqu (0*32)(src), x0; \
	vmovdqu (1*32)(src), x1; \
	vmovdqu (2*32)(src), x2; \
	vmovdqu (3*32)(src), x3; \
	vmovdqu (4*32)(src), x4; \
	vmovdqu (5*32)(src), x5; \
	vmovdqu (6*32)(src), x6; \
	vmovdqu (7*32)(src), x7;

#define store_16way(dst, x0, x1, x2, x3, x4, x5, x6, x7) \
	vmovdqu x0, (0*32)(dst); \
	vmovdqu x1, (1*32)(dst); \
	vmovdqu x2, (2*32)(dst); \
	vmovdqu x3, (3*32)(dst); \
	vmovdqu x4, (4*32)(dst); \
	vmovdqu x5, (5*32)(dst); \
	vmovdqu x6, (6*32)(dst); \
	vmovdqu x7, (7*32)(dst);

#define store_cbc_16way(src, dst, x0, x1, x2, x3, x4, x5, x6, x7, t0) \
	vpxor t0, t0, t0; \
	vinserti128 $1, (src), t0, t0; \
	vpxor t0, x0, x0; \
	vpxor (0*32+16)(src), x1, x1; \
	vpxor (1*32+16)(src), x2, x2; \
	vpxor (2*32+16)(src), x3, x3; \
	vpxor (3*32+16)(src), x4, x4; \
	vpxor (4*32+16)(src), x5, x5; \
	vpxor (5*32+16)(src), x6, x6; \
	vpxor (6*32+16)(src), x7, x7; \
	store_16way(dst, x0, x1, x2, x3, x4, x5, x6, x7);

#define inc_le128(x, minus_one, tmp) \
	vpcmpeqq minus_one, x, tmp; \
	vpsubq minus_one, x, x; \
	vpslldq $8, tmp, tmp; \
	vpsubq tmp, x, x;

#define add2_le128(x, minus_one, minus_two, tmp1, tmp2) \
	vpcmpeqq minus_one, x, tmp1; \
	vpcmpeqq minus_two, x, tmp2; \
	vpsubq minus_two, x, x; \
	vpor tmp2, tmp1, tmp1; \
	vpslldq $8, tmp1, tmp1; \
	vpsubq tmp1, x, x;

#define load_ctr_16way(iv, bswap, x0, x1, x2, x3, x4, x5, x6, x7, t0, t0x, t1, \
		       t1x, t2, t2x, t3, t3x, t4, t5) \
	vpcmpeqd t0, t0, t0; \
	vpsrldq $8, t0, t0; /* ab: -1:0 ; cd: -1:0 */ \
	vpaddq t0, t0, t4; /* ab: -2:0 ; cd: -2:0 */\
	\
	/* load IV and byteswap */ \
	vmovdqu (iv), t2x; \
	vmovdqa t2x, t3x; \
	inc_le128(t2x, t0x, t1x); \
	vbroadcasti128 bswap, t1; \
	vinserti128 $1, t2x, t3, t2; /* ab: le0 ; cd: le1 */ \
	vpshufb t1, t2, x0; \
	\
	/* construct IVs */ \
	add2_le128(t2, t0, t4, t3, t5); /* ab: le2 ; cd: le3 */ \
	vpshufb t1, t2, x1; \
	add2_le128(t2, t0, t4, t3, t5); \
	vpshufb t1, t2, x2; \
	add2_le128(t2, t0, t4, t3, t5); \
	vpshufb t1, t2, x3; \
	add2_le128(t2, t0, t4, t3, t5); \
	vpshufb t1, t2, x4; \
	add2_le128(t2, t0, t4, t3, t5); \
	vpshufb t1, t2, x5; \
	add2_le128(t2, t0, t4, t3, t5); \
	vpshufb t1, t2, x6; \
	add2_le128(t2, t0, t4, t3, t5); \
	vpshufb t1, t2, x7; \
	vextracti128 $1, t2, t2x; \
	inc_le128(t2x, t0x, t3x); \
	vmovdqu t2x, (iv);

#define store_ctr_16way(src, dst, x0, x1, x2, x3, x4, x5, x6, x7) \
	vpxor (0*32)(src), x0, x0; \
	vpxor (1*32)(src), x1, x1; \
	vpxor (2*32)(src), x2, x2; \
	vpxor (3*32)(src), x3, x3; \
	vpxor (4*32)(src), x4, x4; \
	vpxor (5*32)(src), x5, x5; \
	vpxor (6*32)(src), x6, x6; \
	vpxor (7*32)(src), x7, x7; \
	store_16way(dst, x0, x1, x2, x3, x4, x5, x6, x7);
