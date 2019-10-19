/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Shared glue code for 128bit block ciphers, AVX assembler macros
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 */

#define load_8way(src, x0, x1, x2, x3, x4, x5, x6, x7) \
	vmovdqu (0*16)(src), x0; \
	vmovdqu (1*16)(src), x1; \
	vmovdqu (2*16)(src), x2; \
	vmovdqu (3*16)(src), x3; \
	vmovdqu (4*16)(src), x4; \
	vmovdqu (5*16)(src), x5; \
	vmovdqu (6*16)(src), x6; \
	vmovdqu (7*16)(src), x7;

#define store_8way(dst, x0, x1, x2, x3, x4, x5, x6, x7) \
	vmovdqu x0, (0*16)(dst); \
	vmovdqu x1, (1*16)(dst); \
	vmovdqu x2, (2*16)(dst); \
	vmovdqu x3, (3*16)(dst); \
	vmovdqu x4, (4*16)(dst); \
	vmovdqu x5, (5*16)(dst); \
	vmovdqu x6, (6*16)(dst); \
	vmovdqu x7, (7*16)(dst);

#define store_cbc_8way(src, dst, x0, x1, x2, x3, x4, x5, x6, x7) \
	vpxor (0*16)(src), x1, x1; \
	vpxor (1*16)(src), x2, x2; \
	vpxor (2*16)(src), x3, x3; \
	vpxor (3*16)(src), x4, x4; \
	vpxor (4*16)(src), x5, x5; \
	vpxor (5*16)(src), x6, x6; \
	vpxor (6*16)(src), x7, x7; \
	store_8way(dst, x0, x1, x2, x3, x4, x5, x6, x7);

#define inc_le128(x, minus_one, tmp) \
	vpcmpeqq minus_one, x, tmp; \
	vpsubq minus_one, x, x; \
	vpslldq $8, tmp, tmp; \
	vpsubq tmp, x, x;

#define load_ctr_8way(iv, bswap, x0, x1, x2, x3, x4, x5, x6, x7, t0, t1, t2) \
	vpcmpeqd t0, t0, t0; \
	vpsrldq $8, t0, t0; /* low: -1, high: 0 */ \
	vmovdqa bswap, t1; \
	\
	/* load IV and byteswap */ \
	vmovdqu (iv), x7; \
	vpshufb t1, x7, x0; \
	\
	/* construct IVs */ \
	inc_le128(x7, t0, t2); \
	vpshufb t1, x7, x1; \
	inc_le128(x7, t0, t2); \
	vpshufb t1, x7, x2; \
	inc_le128(x7, t0, t2); \
	vpshufb t1, x7, x3; \
	inc_le128(x7, t0, t2); \
	vpshufb t1, x7, x4; \
	inc_le128(x7, t0, t2); \
	vpshufb t1, x7, x5; \
	inc_le128(x7, t0, t2); \
	vpshufb t1, x7, x6; \
	inc_le128(x7, t0, t2); \
	vmovdqa x7, t2; \
	vpshufb t1, x7, x7; \
	inc_le128(t2, t0, t1); \
	vmovdqu t2, (iv);

#define store_ctr_8way(src, dst, x0, x1, x2, x3, x4, x5, x6, x7) \
	vpxor (0*16)(src), x0, x0; \
	vpxor (1*16)(src), x1, x1; \
	vpxor (2*16)(src), x2, x2; \
	vpxor (3*16)(src), x3, x3; \
	vpxor (4*16)(src), x4, x4; \
	vpxor (5*16)(src), x5, x5; \
	vpxor (6*16)(src), x6, x6; \
	vpxor (7*16)(src), x7, x7; \
	store_8way(dst, x0, x1, x2, x3, x4, x5, x6, x7);

#define gf128mul_x_ble(iv, mask, tmp) \
	vpsrad $31, iv, tmp; \
	vpaddq iv, iv, iv; \
	vpshufd $0x13, tmp, tmp; \
	vpand mask, tmp, tmp; \
	vpxor tmp, iv, iv;

#define load_xts_8way(iv, src, dst, x0, x1, x2, x3, x4, x5, x6, x7, tiv, t0, \
		      t1, xts_gf128mul_and_shl1_mask) \
	vmovdqa xts_gf128mul_and_shl1_mask, t0; \
	\
	/* load IV */ \
	vmovdqu (iv), tiv; \
	vpxor (0*16)(src), tiv, x0; \
	vmovdqu tiv, (0*16)(dst); \
	\
	/* construct and store IVs, also xor with source */ \
	gf128mul_x_ble(tiv, t0, t1); \
	vpxor (1*16)(src), tiv, x1; \
	vmovdqu tiv, (1*16)(dst); \
	\
	gf128mul_x_ble(tiv, t0, t1); \
	vpxor (2*16)(src), tiv, x2; \
	vmovdqu tiv, (2*16)(dst); \
	\
	gf128mul_x_ble(tiv, t0, t1); \
	vpxor (3*16)(src), tiv, x3; \
	vmovdqu tiv, (3*16)(dst); \
	\
	gf128mul_x_ble(tiv, t0, t1); \
	vpxor (4*16)(src), tiv, x4; \
	vmovdqu tiv, (4*16)(dst); \
	\
	gf128mul_x_ble(tiv, t0, t1); \
	vpxor (5*16)(src), tiv, x5; \
	vmovdqu tiv, (5*16)(dst); \
	\
	gf128mul_x_ble(tiv, t0, t1); \
	vpxor (6*16)(src), tiv, x6; \
	vmovdqu tiv, (6*16)(dst); \
	\
	gf128mul_x_ble(tiv, t0, t1); \
	vpxor (7*16)(src), tiv, x7; \
	vmovdqu tiv, (7*16)(dst); \
	\
	gf128mul_x_ble(tiv, t0, t1); \
	vmovdqu tiv, (iv);

#define store_xts_8way(dst, x0, x1, x2, x3, x4, x5, x6, x7) \
	vpxor (0*16)(dst), x0, x0; \
	vpxor (1*16)(dst), x1, x1; \
	vpxor (2*16)(dst), x2, x2; \
	vpxor (3*16)(dst), x3, x3; \
	vpxor (4*16)(dst), x4, x4; \
	vpxor (5*16)(dst), x5, x5; \
	vpxor (6*16)(dst), x6, x6; \
	vpxor (7*16)(dst), x7, x7; \
	store_8way(dst, x0, x1, x2, x3, x4, x5, x6, x7);
