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
