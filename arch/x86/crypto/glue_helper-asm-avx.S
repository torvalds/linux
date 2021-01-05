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
