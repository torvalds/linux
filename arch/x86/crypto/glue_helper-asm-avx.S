/*
 * Shared glue code for 128bit block ciphers, AVX assembler macros
 *
 * Copyright (c) 2012 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
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
