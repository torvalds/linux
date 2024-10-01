/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * x86_64/AVX2 assembler optimized version of Serpent
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * Based on AVX assembler implementation of Serpent by:
 *  Copyright © 2012 Johannes Goetzfried
 *      <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 */

#include <linux/linkage.h>
#include <asm/frame.h>
#include "glue_helper-asm-avx2.S"

.file "serpent-avx2-asm_64.S"

.section	.rodata.cst16.bswap128_mask, "aM", @progbits, 16
.align 16
.Lbswap128_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

.text

#define CTX %rdi

#define RNOT %ymm0
#define tp  %ymm1

#define RA1 %ymm2
#define RA2 %ymm3
#define RB1 %ymm4
#define RB2 %ymm5
#define RC1 %ymm6
#define RC2 %ymm7
#define RD1 %ymm8
#define RD2 %ymm9
#define RE1 %ymm10
#define RE2 %ymm11

#define RK0 %ymm12
#define RK1 %ymm13
#define RK2 %ymm14
#define RK3 %ymm15

#define RK0x %xmm12
#define RK1x %xmm13
#define RK2x %xmm14
#define RK3x %xmm15

#define S0_1(x0, x1, x2, x3, x4)      \
	vpor		x0,   x3, tp; \
	vpxor		x3,   x0, x0; \
	vpxor		x2,   x3, x4; \
	vpxor		RNOT, x4, x4; \
	vpxor		x1,   tp, x3; \
	vpand		x0,   x1, x1; \
	vpxor		x4,   x1, x1; \
	vpxor		x0,   x2, x2;
#define S0_2(x0, x1, x2, x3, x4)      \
	vpxor		x3,   x0, x0; \
	vpor		x0,   x4, x4; \
	vpxor		x2,   x0, x0; \
	vpand		x1,   x2, x2; \
	vpxor		x2,   x3, x3; \
	vpxor		RNOT, x1, x1; \
	vpxor		x4,   x2, x2; \
	vpxor		x2,   x1, x1;

#define S1_1(x0, x1, x2, x3, x4)      \
	vpxor		x0,   x1, tp; \
	vpxor		x3,   x0, x0; \
	vpxor		RNOT, x3, x3; \
	vpand		tp,   x1, x4; \
	vpor		tp,   x0, x0; \
	vpxor		x2,   x3, x3; \
	vpxor		x3,   x0, x0; \
	vpxor		x3,   tp, x1;
#define S1_2(x0, x1, x2, x3, x4)      \
	vpxor		x4,   x3, x3; \
	vpor		x4,   x1, x1; \
	vpxor		x2,   x4, x4; \
	vpand		x0,   x2, x2; \
	vpxor		x1,   x2, x2; \
	vpor		x0,   x1, x1; \
	vpxor		RNOT, x0, x0; \
	vpxor		x2,   x0, x0; \
	vpxor		x1,   x4, x4;

#define S2_1(x0, x1, x2, x3, x4)      \
	vpxor		RNOT, x3, x3; \
	vpxor		x0,   x1, x1; \
	vpand		x2,   x0, tp; \
	vpxor		x3,   tp, tp; \
	vpor		x0,   x3, x3; \
	vpxor		x1,   x2, x2; \
	vpxor		x1,   x3, x3; \
	vpand		tp,   x1, x1;
#define S2_2(x0, x1, x2, x3, x4)      \
	vpxor		x2,   tp, tp; \
	vpand		x3,   x2, x2; \
	vpor		x1,   x3, x3; \
	vpxor		RNOT, tp, tp; \
	vpxor		tp,   x3, x3; \
	vpxor		tp,   x0, x4; \
	vpxor		x2,   tp, x0; \
	vpor		x2,   x1, x1;

#define S3_1(x0, x1, x2, x3, x4)      \
	vpxor		x3,   x1, tp; \
	vpor		x0,   x3, x3; \
	vpand		x0,   x1, x4; \
	vpxor		x2,   x0, x0; \
	vpxor		tp,   x2, x2; \
	vpand		x3,   tp, x1; \
	vpxor		x3,   x2, x2; \
	vpor		x4,   x0, x0; \
	vpxor		x3,   x4, x4;
#define S3_2(x0, x1, x2, x3, x4)      \
	vpxor		x0,   x1, x1; \
	vpand		x3,   x0, x0; \
	vpand		x4,   x3, x3; \
	vpxor		x2,   x3, x3; \
	vpor		x1,   x4, x4; \
	vpand		x1,   x2, x2; \
	vpxor		x3,   x4, x4; \
	vpxor		x3,   x0, x0; \
	vpxor		x2,   x3, x3;

#define S4_1(x0, x1, x2, x3, x4)      \
	vpand		x0,   x3, tp; \
	vpxor		x3,   x0, x0; \
	vpxor		x2,   tp, tp; \
	vpor		x3,   x2, x2; \
	vpxor		x1,   x0, x0; \
	vpxor		tp,   x3, x4; \
	vpor		x0,   x2, x2; \
	vpxor		x1,   x2, x2;
#define S4_2(x0, x1, x2, x3, x4)      \
	vpand		x0,   x1, x1; \
	vpxor		x4,   x1, x1; \
	vpand		x2,   x4, x4; \
	vpxor		tp,   x2, x2; \
	vpxor		x0,   x4, x4; \
	vpor		x1,   tp, x3; \
	vpxor		RNOT, x1, x1; \
	vpxor		x0,   x3, x3;

#define S5_1(x0, x1, x2, x3, x4)      \
	vpor		x0,   x1, tp; \
	vpxor		tp,   x2, x2; \
	vpxor		RNOT, x3, x3; \
	vpxor		x0,   x1, x4; \
	vpxor		x2,   x0, x0; \
	vpand		x4,   tp, x1; \
	vpor		x3,   x4, x4; \
	vpxor		x0,   x4, x4;
#define S5_2(x0, x1, x2, x3, x4)      \
	vpand		x3,   x0, x0; \
	vpxor		x3,   x1, x1; \
	vpxor		x2,   x3, x3; \
	vpxor		x1,   x0, x0; \
	vpand		x4,   x2, x2; \
	vpxor		x2,   x1, x1; \
	vpand		x0,   x2, x2; \
	vpxor		x2,   x3, x3;

#define S6_1(x0, x1, x2, x3, x4)      \
	vpxor		x0,   x3, x3; \
	vpxor		x2,   x1, tp; \
	vpxor		x0,   x2, x2; \
	vpand		x3,   x0, x0; \
	vpor		x3,   tp, tp; \
	vpxor		RNOT, x1, x4; \
	vpxor		tp,   x0, x0; \
	vpxor		x2,   tp, x1;
#define S6_2(x0, x1, x2, x3, x4)      \
	vpxor		x4,   x3, x3; \
	vpxor		x0,   x4, x4; \
	vpand		x0,   x2, x2; \
	vpxor		x1,   x4, x4; \
	vpxor		x3,   x2, x2; \
	vpand		x1,   x3, x3; \
	vpxor		x0,   x3, x3; \
	vpxor		x2,   x1, x1;

#define S7_1(x0, x1, x2, x3, x4)      \
	vpxor		RNOT, x1, tp; \
	vpxor		RNOT, x0, x0; \
	vpand		x2,   tp, x1; \
	vpxor		x3,   x1, x1; \
	vpor		tp,   x3, x3; \
	vpxor		x2,   tp, x4; \
	vpxor		x3,   x2, x2; \
	vpxor		x0,   x3, x3; \
	vpor		x1,   x0, x0;
#define S7_2(x0, x1, x2, x3, x4)      \
	vpand		x0,   x2, x2; \
	vpxor		x4,   x0, x0; \
	vpxor		x3,   x4, x4; \
	vpand		x0,   x3, x3; \
	vpxor		x1,   x4, x4; \
	vpxor		x4,   x2, x2; \
	vpxor		x1,   x3, x3; \
	vpor		x0,   x4, x4; \
	vpxor		x1,   x4, x4;

#define SI0_1(x0, x1, x2, x3, x4)     \
	vpxor		x0,   x1, x1; \
	vpor		x1,   x3, tp; \
	vpxor		x1,   x3, x4; \
	vpxor		RNOT, x0, x0; \
	vpxor		tp,   x2, x2; \
	vpxor		x0,   tp, x3; \
	vpand		x1,   x0, x0; \
	vpxor		x2,   x0, x0;
#define SI0_2(x0, x1, x2, x3, x4)     \
	vpand		x3,   x2, x2; \
	vpxor		x4,   x3, x3; \
	vpxor		x3,   x2, x2; \
	vpxor		x3,   x1, x1; \
	vpand		x0,   x3, x3; \
	vpxor		x0,   x1, x1; \
	vpxor		x2,   x0, x0; \
	vpxor		x3,   x4, x4;

#define SI1_1(x0, x1, x2, x3, x4)     \
	vpxor		x3,   x1, x1; \
	vpxor		x2,   x0, tp; \
	vpxor		RNOT, x2, x2; \
	vpor		x1,   x0, x4; \
	vpxor		x3,   x4, x4; \
	vpand		x1,   x3, x3; \
	vpxor		x2,   x1, x1; \
	vpand		x4,   x2, x2;
#define SI1_2(x0, x1, x2, x3, x4)     \
	vpxor		x1,   x4, x4; \
	vpor		x3,   x1, x1; \
	vpxor		tp,   x3, x3; \
	vpxor		tp,   x2, x2; \
	vpor		x4,   tp, x0; \
	vpxor		x4,   x2, x2; \
	vpxor		x0,   x1, x1; \
	vpxor		x1,   x4, x4;

#define SI2_1(x0, x1, x2, x3, x4)     \
	vpxor		x1,   x2, x2; \
	vpxor		RNOT, x3, tp; \
	vpor		x2,   tp, tp; \
	vpxor		x3,   x2, x2; \
	vpxor		x0,   x3, x4; \
	vpxor		x1,   tp, x3; \
	vpor		x2,   x1, x1; \
	vpxor		x0,   x2, x2;
#define SI2_2(x0, x1, x2, x3, x4)     \
	vpxor		x4,   x1, x1; \
	vpor		x3,   x4, x4; \
	vpxor		x3,   x2, x2; \
	vpxor		x2,   x4, x4; \
	vpand		x1,   x2, x2; \
	vpxor		x3,   x2, x2; \
	vpxor		x4,   x3, x3; \
	vpxor		x0,   x4, x4;

#define SI3_1(x0, x1, x2, x3, x4)     \
	vpxor		x1,   x2, x2; \
	vpand		x2,   x1, tp; \
	vpxor		x0,   tp, tp; \
	vpor		x1,   x0, x0; \
	vpxor		x3,   x1, x4; \
	vpxor		x3,   x0, x0; \
	vpor		tp,   x3, x3; \
	vpxor		x2,   tp, x1;
#define SI3_2(x0, x1, x2, x3, x4)     \
	vpxor		x3,   x1, x1; \
	vpxor		x2,   x0, x0; \
	vpxor		x3,   x2, x2; \
	vpand		x1,   x3, x3; \
	vpxor		x0,   x1, x1; \
	vpand		x2,   x0, x0; \
	vpxor		x3,   x4, x4; \
	vpxor		x0,   x3, x3; \
	vpxor		x1,   x0, x0;

#define SI4_1(x0, x1, x2, x3, x4)     \
	vpxor		x3,   x2, x2; \
	vpand		x1,   x0, tp; \
	vpxor		x2,   tp, tp; \
	vpor		x3,   x2, x2; \
	vpxor		RNOT, x0, x4; \
	vpxor		tp,   x1, x1; \
	vpxor		x2,   tp, x0; \
	vpand		x4,   x2, x2;
#define SI4_2(x0, x1, x2, x3, x4)     \
	vpxor		x0,   x2, x2; \
	vpor		x4,   x0, x0; \
	vpxor		x3,   x0, x0; \
	vpand		x2,   x3, x3; \
	vpxor		x3,   x4, x4; \
	vpxor		x1,   x3, x3; \
	vpand		x0,   x1, x1; \
	vpxor		x1,   x4, x4; \
	vpxor		x3,   x0, x0;

#define SI5_1(x0, x1, x2, x3, x4)     \
	vpor		x2,   x1, tp; \
	vpxor		x1,   x2, x2; \
	vpxor		x3,   tp, tp; \
	vpand		x1,   x3, x3; \
	vpxor		x3,   x2, x2; \
	vpor		x0,   x3, x3; \
	vpxor		RNOT, x0, x0; \
	vpxor		x2,   x3, x3; \
	vpor		x0,   x2, x2;
#define SI5_2(x0, x1, x2, x3, x4)     \
	vpxor		tp,   x1, x4; \
	vpxor		x4,   x2, x2; \
	vpand		x0,   x4, x4; \
	vpxor		tp,   x0, x0; \
	vpxor		x3,   tp, x1; \
	vpand		x2,   x0, x0; \
	vpxor		x3,   x2, x2; \
	vpxor		x2,   x0, x0; \
	vpxor		x4,   x2, x2; \
	vpxor		x3,   x4, x4;

#define SI6_1(x0, x1, x2, x3, x4)     \
	vpxor		x2,   x0, x0; \
	vpand		x3,   x0, tp; \
	vpxor		x3,   x2, x2; \
	vpxor		x2,   tp, tp; \
	vpxor		x1,   x3, x3; \
	vpor		x0,   x2, x2; \
	vpxor		x3,   x2, x2; \
	vpand		tp,   x3, x3;
#define SI6_2(x0, x1, x2, x3, x4)     \
	vpxor		RNOT, tp, tp; \
	vpxor		x1,   x3, x3; \
	vpand		x2,   x1, x1; \
	vpxor		tp,   x0, x4; \
	vpxor		x4,   x3, x3; \
	vpxor		x2,   x4, x4; \
	vpxor		x1,   tp, x0; \
	vpxor		x0,   x2, x2;

#define SI7_1(x0, x1, x2, x3, x4)     \
	vpand		x0,   x3, tp; \
	vpxor		x2,   x0, x0; \
	vpor		x3,   x2, x2; \
	vpxor		x1,   x3, x4; \
	vpxor		RNOT, x0, x0; \
	vpor		tp,   x1, x1; \
	vpxor		x0,   x4, x4; \
	vpand		x2,   x0, x0; \
	vpxor		x1,   x0, x0;
#define SI7_2(x0, x1, x2, x3, x4)     \
	vpand		x2,   x1, x1; \
	vpxor		x2,   tp, x3; \
	vpxor		x3,   x4, x4; \
	vpand		x3,   x2, x2; \
	vpor		x0,   x3, x3; \
	vpxor		x4,   x1, x1; \
	vpxor		x4,   x3, x3; \
	vpand		x0,   x4, x4; \
	vpxor		x2,   x4, x4;

#define get_key(i,j,t) \
	vpbroadcastd (4*(i)+(j))*4(CTX), t;

#define K2(x0, x1, x2, x3, x4, i) \
	get_key(i, 0, RK0); \
	get_key(i, 1, RK1); \
	get_key(i, 2, RK2); \
	get_key(i, 3, RK3); \
	vpxor RK0,	x0 ## 1, x0 ## 1; \
	vpxor RK1,	x1 ## 1, x1 ## 1; \
	vpxor RK2,	x2 ## 1, x2 ## 1; \
	vpxor RK3,	x3 ## 1, x3 ## 1; \
		vpxor RK0,	x0 ## 2, x0 ## 2; \
		vpxor RK1,	x1 ## 2, x1 ## 2; \
		vpxor RK2,	x2 ## 2, x2 ## 2; \
		vpxor RK3,	x3 ## 2, x3 ## 2;

#define LK2(x0, x1, x2, x3, x4, i) \
	vpslld $13,		x0 ## 1, x4 ## 1;          \
	vpsrld $(32 - 13),	x0 ## 1, x0 ## 1;          \
	vpor			x4 ## 1, x0 ## 1, x0 ## 1; \
	vpxor			x0 ## 1, x1 ## 1, x1 ## 1; \
	vpslld $3,		x2 ## 1, x4 ## 1;          \
	vpsrld $(32 - 3),	x2 ## 1, x2 ## 1;          \
	vpor			x4 ## 1, x2 ## 1, x2 ## 1; \
	vpxor			x2 ## 1, x1 ## 1, x1 ## 1; \
		vpslld $13,		x0 ## 2, x4 ## 2;          \
		vpsrld $(32 - 13),	x0 ## 2, x0 ## 2;          \
		vpor			x4 ## 2, x0 ## 2, x0 ## 2; \
		vpxor			x0 ## 2, x1 ## 2, x1 ## 2; \
		vpslld $3,		x2 ## 2, x4 ## 2;          \
		vpsrld $(32 - 3),	x2 ## 2, x2 ## 2;          \
		vpor			x4 ## 2, x2 ## 2, x2 ## 2; \
		vpxor			x2 ## 2, x1 ## 2, x1 ## 2; \
	vpslld $1,		x1 ## 1, x4 ## 1;          \
	vpsrld $(32 - 1),	x1 ## 1, x1 ## 1;          \
	vpor			x4 ## 1, x1 ## 1, x1 ## 1; \
	vpslld $3,		x0 ## 1, x4 ## 1;          \
	vpxor			x2 ## 1, x3 ## 1, x3 ## 1; \
	vpxor			x4 ## 1, x3 ## 1, x3 ## 1; \
	get_key(i, 1, RK1); \
		vpslld $1,		x1 ## 2, x4 ## 2;          \
		vpsrld $(32 - 1),	x1 ## 2, x1 ## 2;          \
		vpor			x4 ## 2, x1 ## 2, x1 ## 2; \
		vpslld $3,		x0 ## 2, x4 ## 2;          \
		vpxor			x2 ## 2, x3 ## 2, x3 ## 2; \
		vpxor			x4 ## 2, x3 ## 2, x3 ## 2; \
		get_key(i, 3, RK3); \
	vpslld $7,		x3 ## 1, x4 ## 1;          \
	vpsrld $(32 - 7),	x3 ## 1, x3 ## 1;          \
	vpor			x4 ## 1, x3 ## 1, x3 ## 1; \
	vpslld $7,		x1 ## 1, x4 ## 1;          \
	vpxor			x1 ## 1, x0 ## 1, x0 ## 1; \
	vpxor			x3 ## 1, x0 ## 1, x0 ## 1; \
	vpxor			x3 ## 1, x2 ## 1, x2 ## 1; \
	vpxor			x4 ## 1, x2 ## 1, x2 ## 1; \
	get_key(i, 0, RK0); \
		vpslld $7,		x3 ## 2, x4 ## 2;          \
		vpsrld $(32 - 7),	x3 ## 2, x3 ## 2;          \
		vpor			x4 ## 2, x3 ## 2, x3 ## 2; \
		vpslld $7,		x1 ## 2, x4 ## 2;          \
		vpxor			x1 ## 2, x0 ## 2, x0 ## 2; \
		vpxor			x3 ## 2, x0 ## 2, x0 ## 2; \
		vpxor			x3 ## 2, x2 ## 2, x2 ## 2; \
		vpxor			x4 ## 2, x2 ## 2, x2 ## 2; \
		get_key(i, 2, RK2); \
	vpxor			RK1, x1 ## 1, x1 ## 1;     \
	vpxor			RK3, x3 ## 1, x3 ## 1;     \
	vpslld $5,		x0 ## 1, x4 ## 1;          \
	vpsrld $(32 - 5),	x0 ## 1, x0 ## 1;          \
	vpor			x4 ## 1, x0 ## 1, x0 ## 1; \
	vpslld $22,		x2 ## 1, x4 ## 1;          \
	vpsrld $(32 - 22),	x2 ## 1, x2 ## 1;          \
	vpor			x4 ## 1, x2 ## 1, x2 ## 1; \
	vpxor			RK0, x0 ## 1, x0 ## 1;     \
	vpxor			RK2, x2 ## 1, x2 ## 1;     \
		vpxor			RK1, x1 ## 2, x1 ## 2;     \
		vpxor			RK3, x3 ## 2, x3 ## 2;     \
		vpslld $5,		x0 ## 2, x4 ## 2;          \
		vpsrld $(32 - 5),	x0 ## 2, x0 ## 2;          \
		vpor			x4 ## 2, x0 ## 2, x0 ## 2; \
		vpslld $22,		x2 ## 2, x4 ## 2;          \
		vpsrld $(32 - 22),	x2 ## 2, x2 ## 2;          \
		vpor			x4 ## 2, x2 ## 2, x2 ## 2; \
		vpxor			RK0, x0 ## 2, x0 ## 2;     \
		vpxor			RK2, x2 ## 2, x2 ## 2;

#define KL2(x0, x1, x2, x3, x4, i) \
	vpxor			RK0, x0 ## 1, x0 ## 1;     \
	vpxor			RK2, x2 ## 1, x2 ## 1;     \
	vpsrld $5,		x0 ## 1, x4 ## 1;          \
	vpslld $(32 - 5),	x0 ## 1, x0 ## 1;          \
	vpor			x4 ## 1, x0 ## 1, x0 ## 1; \
	vpxor			RK3, x3 ## 1, x3 ## 1;     \
	vpxor			RK1, x1 ## 1, x1 ## 1;     \
	vpsrld $22,		x2 ## 1, x4 ## 1;          \
	vpslld $(32 - 22),	x2 ## 1, x2 ## 1;          \
	vpor			x4 ## 1, x2 ## 1, x2 ## 1; \
	vpxor			x3 ## 1, x2 ## 1, x2 ## 1; \
		vpxor			RK0, x0 ## 2, x0 ## 2;     \
		vpxor			RK2, x2 ## 2, x2 ## 2;     \
		vpsrld $5,		x0 ## 2, x4 ## 2;          \
		vpslld $(32 - 5),	x0 ## 2, x0 ## 2;          \
		vpor			x4 ## 2, x0 ## 2, x0 ## 2; \
		vpxor			RK3, x3 ## 2, x3 ## 2;     \
		vpxor			RK1, x1 ## 2, x1 ## 2;     \
		vpsrld $22,		x2 ## 2, x4 ## 2;          \
		vpslld $(32 - 22),	x2 ## 2, x2 ## 2;          \
		vpor			x4 ## 2, x2 ## 2, x2 ## 2; \
		vpxor			x3 ## 2, x2 ## 2, x2 ## 2; \
	vpxor			x3 ## 1, x0 ## 1, x0 ## 1; \
	vpslld $7,		x1 ## 1, x4 ## 1;          \
	vpxor			x1 ## 1, x0 ## 1, x0 ## 1; \
	vpxor			x4 ## 1, x2 ## 1, x2 ## 1; \
	vpsrld $1,		x1 ## 1, x4 ## 1;          \
	vpslld $(32 - 1),	x1 ## 1, x1 ## 1;          \
	vpor			x4 ## 1, x1 ## 1, x1 ## 1; \
		vpxor			x3 ## 2, x0 ## 2, x0 ## 2; \
		vpslld $7,		x1 ## 2, x4 ## 2;          \
		vpxor			x1 ## 2, x0 ## 2, x0 ## 2; \
		vpxor			x4 ## 2, x2 ## 2, x2 ## 2; \
		vpsrld $1,		x1 ## 2, x4 ## 2;          \
		vpslld $(32 - 1),	x1 ## 2, x1 ## 2;          \
		vpor			x4 ## 2, x1 ## 2, x1 ## 2; \
	vpsrld $7,		x3 ## 1, x4 ## 1;          \
	vpslld $(32 - 7),	x3 ## 1, x3 ## 1;          \
	vpor			x4 ## 1, x3 ## 1, x3 ## 1; \
	vpxor			x0 ## 1, x1 ## 1, x1 ## 1; \
	vpslld $3,		x0 ## 1, x4 ## 1;          \
	vpxor			x4 ## 1, x3 ## 1, x3 ## 1; \
		vpsrld $7,		x3 ## 2, x4 ## 2;          \
		vpslld $(32 - 7),	x3 ## 2, x3 ## 2;          \
		vpor			x4 ## 2, x3 ## 2, x3 ## 2; \
		vpxor			x0 ## 2, x1 ## 2, x1 ## 2; \
		vpslld $3,		x0 ## 2, x4 ## 2;          \
		vpxor			x4 ## 2, x3 ## 2, x3 ## 2; \
	vpsrld $13,		x0 ## 1, x4 ## 1;          \
	vpslld $(32 - 13),	x0 ## 1, x0 ## 1;          \
	vpor			x4 ## 1, x0 ## 1, x0 ## 1; \
	vpxor			x2 ## 1, x1 ## 1, x1 ## 1; \
	vpxor			x2 ## 1, x3 ## 1, x3 ## 1; \
	vpsrld $3,		x2 ## 1, x4 ## 1;          \
	vpslld $(32 - 3),	x2 ## 1, x2 ## 1;          \
	vpor			x4 ## 1, x2 ## 1, x2 ## 1; \
		vpsrld $13,		x0 ## 2, x4 ## 2;          \
		vpslld $(32 - 13),	x0 ## 2, x0 ## 2;          \
		vpor			x4 ## 2, x0 ## 2, x0 ## 2; \
		vpxor			x2 ## 2, x1 ## 2, x1 ## 2; \
		vpxor			x2 ## 2, x3 ## 2, x3 ## 2; \
		vpsrld $3,		x2 ## 2, x4 ## 2;          \
		vpslld $(32 - 3),	x2 ## 2, x2 ## 2;          \
		vpor			x4 ## 2, x2 ## 2, x2 ## 2;

#define S(SBOX, x0, x1, x2, x3, x4) \
	SBOX ## _1(x0 ## 1, x1 ## 1, x2 ## 1, x3 ## 1, x4 ## 1); \
	SBOX ## _2(x0 ## 1, x1 ## 1, x2 ## 1, x3 ## 1, x4 ## 1); \
	SBOX ## _1(x0 ## 2, x1 ## 2, x2 ## 2, x3 ## 2, x4 ## 2); \
	SBOX ## _2(x0 ## 2, x1 ## 2, x2 ## 2, x3 ## 2, x4 ## 2);

#define SP(SBOX, x0, x1, x2, x3, x4, i) \
	get_key(i, 0, RK0); \
	SBOX ## _1(x0 ## 1, x1 ## 1, x2 ## 1, x3 ## 1, x4 ## 1); \
	get_key(i, 2, RK2); \
	SBOX ## _2(x0 ## 1, x1 ## 1, x2 ## 1, x3 ## 1, x4 ## 1); \
	get_key(i, 3, RK3); \
	SBOX ## _1(x0 ## 2, x1 ## 2, x2 ## 2, x3 ## 2, x4 ## 2); \
	get_key(i, 1, RK1); \
	SBOX ## _2(x0 ## 2, x1 ## 2, x2 ## 2, x3 ## 2, x4 ## 2); \

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

#define read_blocks(x0, x1, x2, x3, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2)

#define write_blocks(x0, x1, x2, x3, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2)

.align 8
SYM_FUNC_START_LOCAL(__serpent_enc_blk16)
	/* input:
	 *	%rdi: ctx, CTX
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: plaintext
	 * output:
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: ciphertext
	 */

	vpcmpeqd RNOT, RNOT, RNOT;

	read_blocks(RA1, RB1, RC1, RD1, RK0, RK1, RK2);
	read_blocks(RA2, RB2, RC2, RD2, RK0, RK1, RK2);

						 K2(RA, RB, RC, RD, RE, 0);
	S(S0, RA, RB, RC, RD, RE);		LK2(RC, RB, RD, RA, RE, 1);
	S(S1, RC, RB, RD, RA, RE);		LK2(RE, RD, RA, RC, RB, 2);
	S(S2, RE, RD, RA, RC, RB);		LK2(RB, RD, RE, RC, RA, 3);
	S(S3, RB, RD, RE, RC, RA);		LK2(RC, RA, RD, RB, RE, 4);
	S(S4, RC, RA, RD, RB, RE);		LK2(RA, RD, RB, RE, RC, 5);
	S(S5, RA, RD, RB, RE, RC);		LK2(RC, RA, RD, RE, RB, 6);
	S(S6, RC, RA, RD, RE, RB);		LK2(RD, RB, RA, RE, RC, 7);
	S(S7, RD, RB, RA, RE, RC);		LK2(RC, RA, RE, RD, RB, 8);
	S(S0, RC, RA, RE, RD, RB);		LK2(RE, RA, RD, RC, RB, 9);
	S(S1, RE, RA, RD, RC, RB);		LK2(RB, RD, RC, RE, RA, 10);
	S(S2, RB, RD, RC, RE, RA);		LK2(RA, RD, RB, RE, RC, 11);
	S(S3, RA, RD, RB, RE, RC);		LK2(RE, RC, RD, RA, RB, 12);
	S(S4, RE, RC, RD, RA, RB);		LK2(RC, RD, RA, RB, RE, 13);
	S(S5, RC, RD, RA, RB, RE);		LK2(RE, RC, RD, RB, RA, 14);
	S(S6, RE, RC, RD, RB, RA);		LK2(RD, RA, RC, RB, RE, 15);
	S(S7, RD, RA, RC, RB, RE);		LK2(RE, RC, RB, RD, RA, 16);
	S(S0, RE, RC, RB, RD, RA);		LK2(RB, RC, RD, RE, RA, 17);
	S(S1, RB, RC, RD, RE, RA);		LK2(RA, RD, RE, RB, RC, 18);
	S(S2, RA, RD, RE, RB, RC);		LK2(RC, RD, RA, RB, RE, 19);
	S(S3, RC, RD, RA, RB, RE);		LK2(RB, RE, RD, RC, RA, 20);
	S(S4, RB, RE, RD, RC, RA);		LK2(RE, RD, RC, RA, RB, 21);
	S(S5, RE, RD, RC, RA, RB);		LK2(RB, RE, RD, RA, RC, 22);
	S(S6, RB, RE, RD, RA, RC);		LK2(RD, RC, RE, RA, RB, 23);
	S(S7, RD, RC, RE, RA, RB);		LK2(RB, RE, RA, RD, RC, 24);
	S(S0, RB, RE, RA, RD, RC);		LK2(RA, RE, RD, RB, RC, 25);
	S(S1, RA, RE, RD, RB, RC);		LK2(RC, RD, RB, RA, RE, 26);
	S(S2, RC, RD, RB, RA, RE);		LK2(RE, RD, RC, RA, RB, 27);
	S(S3, RE, RD, RC, RA, RB);		LK2(RA, RB, RD, RE, RC, 28);
	S(S4, RA, RB, RD, RE, RC);		LK2(RB, RD, RE, RC, RA, 29);
	S(S5, RB, RD, RE, RC, RA);		LK2(RA, RB, RD, RC, RE, 30);
	S(S6, RA, RB, RD, RC, RE);		LK2(RD, RE, RB, RC, RA, 31);
	S(S7, RD, RE, RB, RC, RA);		 K2(RA, RB, RC, RD, RE, 32);

	write_blocks(RA1, RB1, RC1, RD1, RK0, RK1, RK2);
	write_blocks(RA2, RB2, RC2, RD2, RK0, RK1, RK2);

	RET;
SYM_FUNC_END(__serpent_enc_blk16)

.align 8
SYM_FUNC_START_LOCAL(__serpent_dec_blk16)
	/* input:
	 *	%rdi: ctx, CTX
	 *	RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2: ciphertext
	 * output:
	 *	RC1, RD1, RB1, RE1, RC2, RD2, RB2, RE2: plaintext
	 */

	vpcmpeqd RNOT, RNOT, RNOT;

	read_blocks(RA1, RB1, RC1, RD1, RK0, RK1, RK2);
	read_blocks(RA2, RB2, RC2, RD2, RK0, RK1, RK2);

						 K2(RA, RB, RC, RD, RE, 32);
	SP(SI7, RA, RB, RC, RD, RE, 31);	KL2(RB, RD, RA, RE, RC, 31);
	SP(SI6, RB, RD, RA, RE, RC, 30);	KL2(RA, RC, RE, RB, RD, 30);
	SP(SI5, RA, RC, RE, RB, RD, 29);	KL2(RC, RD, RA, RE, RB, 29);
	SP(SI4, RC, RD, RA, RE, RB, 28);	KL2(RC, RA, RB, RE, RD, 28);
	SP(SI3, RC, RA, RB, RE, RD, 27);	KL2(RB, RC, RD, RE, RA, 27);
	SP(SI2, RB, RC, RD, RE, RA, 26);	KL2(RC, RA, RE, RD, RB, 26);
	SP(SI1, RC, RA, RE, RD, RB, 25);	KL2(RB, RA, RE, RD, RC, 25);
	SP(SI0, RB, RA, RE, RD, RC, 24);	KL2(RE, RC, RA, RB, RD, 24);
	SP(SI7, RE, RC, RA, RB, RD, 23);	KL2(RC, RB, RE, RD, RA, 23);
	SP(SI6, RC, RB, RE, RD, RA, 22);	KL2(RE, RA, RD, RC, RB, 22);
	SP(SI5, RE, RA, RD, RC, RB, 21);	KL2(RA, RB, RE, RD, RC, 21);
	SP(SI4, RA, RB, RE, RD, RC, 20);	KL2(RA, RE, RC, RD, RB, 20);
	SP(SI3, RA, RE, RC, RD, RB, 19);	KL2(RC, RA, RB, RD, RE, 19);
	SP(SI2, RC, RA, RB, RD, RE, 18);	KL2(RA, RE, RD, RB, RC, 18);
	SP(SI1, RA, RE, RD, RB, RC, 17);	KL2(RC, RE, RD, RB, RA, 17);
	SP(SI0, RC, RE, RD, RB, RA, 16);	KL2(RD, RA, RE, RC, RB, 16);
	SP(SI7, RD, RA, RE, RC, RB, 15);	KL2(RA, RC, RD, RB, RE, 15);
	SP(SI6, RA, RC, RD, RB, RE, 14);	KL2(RD, RE, RB, RA, RC, 14);
	SP(SI5, RD, RE, RB, RA, RC, 13);	KL2(RE, RC, RD, RB, RA, 13);
	SP(SI4, RE, RC, RD, RB, RA, 12);	KL2(RE, RD, RA, RB, RC, 12);
	SP(SI3, RE, RD, RA, RB, RC, 11);	KL2(RA, RE, RC, RB, RD, 11);
	SP(SI2, RA, RE, RC, RB, RD, 10);	KL2(RE, RD, RB, RC, RA, 10);
	SP(SI1, RE, RD, RB, RC, RA, 9);		KL2(RA, RD, RB, RC, RE, 9);
	SP(SI0, RA, RD, RB, RC, RE, 8);		KL2(RB, RE, RD, RA, RC, 8);
	SP(SI7, RB, RE, RD, RA, RC, 7);		KL2(RE, RA, RB, RC, RD, 7);
	SP(SI6, RE, RA, RB, RC, RD, 6);		KL2(RB, RD, RC, RE, RA, 6);
	SP(SI5, RB, RD, RC, RE, RA, 5);		KL2(RD, RA, RB, RC, RE, 5);
	SP(SI4, RD, RA, RB, RC, RE, 4);		KL2(RD, RB, RE, RC, RA, 4);
	SP(SI3, RD, RB, RE, RC, RA, 3);		KL2(RE, RD, RA, RC, RB, 3);
	SP(SI2, RE, RD, RA, RC, RB, 2);		KL2(RD, RB, RC, RA, RE, 2);
	SP(SI1, RD, RB, RC, RA, RE, 1);		KL2(RE, RB, RC, RA, RD, 1);
	S(SI0, RE, RB, RC, RA, RD);		 K2(RC, RD, RB, RE, RA, 0);

	write_blocks(RC1, RD1, RB1, RE1, RK0, RK1, RK2);
	write_blocks(RC2, RD2, RB2, RE2, RK0, RK1, RK2);

	RET;
SYM_FUNC_END(__serpent_dec_blk16)

SYM_FUNC_START(serpent_ecb_enc_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN

	vzeroupper;

	load_16way(%rdx, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	call __serpent_enc_blk16;

	store_16way(%rsi, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	vzeroupper;

	FRAME_END
	RET;
SYM_FUNC_END(serpent_ecb_enc_16way)

SYM_FUNC_START(serpent_ecb_dec_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN

	vzeroupper;

	load_16way(%rdx, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	call __serpent_dec_blk16;

	store_16way(%rsi, RC1, RD1, RB1, RE1, RC2, RD2, RB2, RE2);

	vzeroupper;

	FRAME_END
	RET;
SYM_FUNC_END(serpent_ecb_dec_16way)

SYM_FUNC_START(serpent_cbc_dec_16way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	FRAME_BEGIN

	vzeroupper;

	load_16way(%rdx, RA1, RB1, RC1, RD1, RA2, RB2, RC2, RD2);

	call __serpent_dec_blk16;

	store_cbc_16way(%rdx, %rsi, RC1, RD1, RB1, RE1, RC2, RD2, RB2, RE2,
			RK0);

	vzeroupper;

	FRAME_END
	RET;
SYM_FUNC_END(serpent_cbc_dec_16way)
