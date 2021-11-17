/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Serpent Cipher 8-way parallel algorithm (x86_64/SSE2)
 *
 * Copyright (C) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * Based on crypto/serpent.c by
 *  Copyright (C) 2002 Dag Arne Osvik <osvik@ii.uib.no>
 *                2003 Herbert Valerio Riedel <hvr@gnu.org>
 */

#include <linux/linkage.h>

.file "serpent-sse2-x86_64-asm_64.S"
.text

#define CTX %rdi

/**********************************************************************
  8-way SSE2 serpent
 **********************************************************************/
#define RA1 %xmm0
#define RB1 %xmm1
#define RC1 %xmm2
#define RD1 %xmm3
#define RE1 %xmm4

#define RA2 %xmm5
#define RB2 %xmm6
#define RC2 %xmm7
#define RD2 %xmm8
#define RE2 %xmm9

#define RNOT %xmm10

#define RK0 %xmm11
#define RK1 %xmm12
#define RK2 %xmm13
#define RK3 %xmm14

#define S0_1(x0, x1, x2, x3, x4) \
	movdqa x3,		x4; \
	por x0,			x3; \
	pxor x4,		x0; \
	pxor x2,		x4; \
	pxor RNOT,		x4; \
	pxor x1,		x3; \
	pand x0,		x1; \
	pxor x4,		x1; \
	pxor x0,		x2;
#define S0_2(x0, x1, x2, x3, x4) \
	pxor x3,		x0; \
	por x0,			x4; \
	pxor x2,		x0; \
	pand x1,		x2; \
	pxor x2,		x3; \
	pxor RNOT,		x1; \
	pxor x4,		x2; \
	pxor x2,		x1;

#define S1_1(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	pxor x0,		x1; \
	pxor x3,		x0; \
	pxor RNOT,		x3; \
	pand x1,		x4; \
	por x1,			x0; \
	pxor x2,		x3; \
	pxor x3,		x0; \
	pxor x3,		x1;
#define S1_2(x0, x1, x2, x3, x4) \
	pxor x4,		x3; \
	por x4,			x1; \
	pxor x2,		x4; \
	pand x0,		x2; \
	pxor x1,		x2; \
	por x0,			x1; \
	pxor RNOT,		x0; \
	pxor x2,		x0; \
	pxor x1,		x4;

#define S2_1(x0, x1, x2, x3, x4) \
	pxor RNOT,		x3; \
	pxor x0,		x1; \
	movdqa x0,		x4; \
	pand x2,		x0; \
	pxor x3,		x0; \
	por x4,			x3; \
	pxor x1,		x2; \
	pxor x1,		x3; \
	pand x0,		x1;
#define S2_2(x0, x1, x2, x3, x4) \
	pxor x2,		x0; \
	pand x3,		x2; \
	por x1,			x3; \
	pxor RNOT,		x0; \
	pxor x0,		x3; \
	pxor x0,		x4; \
	pxor x2,		x0; \
	por x2,			x1;

#define S3_1(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	pxor x3,		x1; \
	por x0,			x3; \
	pand x0,		x4; \
	pxor x2,		x0; \
	pxor x1,		x2; \
	pand x3,		x1; \
	pxor x3,		x2; \
	por x4,			x0; \
	pxor x3,		x4;
#define S3_2(x0, x1, x2, x3, x4) \
	pxor x0,		x1; \
	pand x3,		x0; \
	pand x4,		x3; \
	pxor x2,		x3; \
	por x1,			x4; \
	pand x1,		x2; \
	pxor x3,		x4; \
	pxor x3,		x0; \
	pxor x2,		x3;

#define S4_1(x0, x1, x2, x3, x4) \
	movdqa x3,		x4; \
	pand x0,		x3; \
	pxor x4,		x0; \
	pxor x2,		x3; \
	por x4,			x2; \
	pxor x1,		x0; \
	pxor x3,		x4; \
	por x0,			x2; \
	pxor x1,		x2;
#define S4_2(x0, x1, x2, x3, x4) \
	pand x0,		x1; \
	pxor x4,		x1; \
	pand x2,		x4; \
	pxor x3,		x2; \
	pxor x0,		x4; \
	por x1,			x3; \
	pxor RNOT,		x1; \
	pxor x0,		x3;

#define S5_1(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	por x0,			x1; \
	pxor x1,		x2; \
	pxor RNOT,		x3; \
	pxor x0,		x4; \
	pxor x2,		x0; \
	pand x4,		x1; \
	por x3,			x4; \
	pxor x0,		x4;
#define S5_2(x0, x1, x2, x3, x4) \
	pand x3,		x0; \
	pxor x3,		x1; \
	pxor x2,		x3; \
	pxor x1,		x0; \
	pand x4,		x2; \
	pxor x2,		x1; \
	pand x0,		x2; \
	pxor x2,		x3;

#define S6_1(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	pxor x0,		x3; \
	pxor x2,		x1; \
	pxor x0,		x2; \
	pand x3,		x0; \
	por x3,			x1; \
	pxor RNOT,		x4; \
	pxor x1,		x0; \
	pxor x2,		x1;
#define S6_2(x0, x1, x2, x3, x4) \
	pxor x4,		x3; \
	pxor x0,		x4; \
	pand x0,		x2; \
	pxor x1,		x4; \
	pxor x3,		x2; \
	pand x1,		x3; \
	pxor x0,		x3; \
	pxor x2,		x1;

#define S7_1(x0, x1, x2, x3, x4) \
	pxor RNOT,		x1; \
	movdqa x1,		x4; \
	pxor RNOT,		x0; \
	pand x2,		x1; \
	pxor x3,		x1; \
	por x4,			x3; \
	pxor x2,		x4; \
	pxor x3,		x2; \
	pxor x0,		x3; \
	por x1,			x0;
#define S7_2(x0, x1, x2, x3, x4) \
	pand x0,		x2; \
	pxor x4,		x0; \
	pxor x3,		x4; \
	pand x0,		x3; \
	pxor x1,		x4; \
	pxor x4,		x2; \
	pxor x1,		x3; \
	por x0,			x4; \
	pxor x1,		x4;

#define SI0_1(x0, x1, x2, x3, x4) \
	movdqa x3,		x4; \
	pxor x0,		x1; \
	por x1,			x3; \
	pxor x1,		x4; \
	pxor RNOT,		x0; \
	pxor x3,		x2; \
	pxor x0,		x3; \
	pand x1,		x0; \
	pxor x2,		x0;
#define SI0_2(x0, x1, x2, x3, x4) \
	pand x3,		x2; \
	pxor x4,		x3; \
	pxor x3,		x2; \
	pxor x3,		x1; \
	pand x0,		x3; \
	pxor x0,		x1; \
	pxor x2,		x0; \
	pxor x3,		x4;

#define SI1_1(x0, x1, x2, x3, x4) \
	pxor x3,		x1; \
	movdqa x0,		x4; \
	pxor x2,		x0; \
	pxor RNOT,		x2; \
	por x1,			x4; \
	pxor x3,		x4; \
	pand x1,		x3; \
	pxor x2,		x1; \
	pand x4,		x2;
#define SI1_2(x0, x1, x2, x3, x4) \
	pxor x1,		x4; \
	por x3,			x1; \
	pxor x0,		x3; \
	pxor x0,		x2; \
	por x4,			x0; \
	pxor x4,		x2; \
	pxor x0,		x1; \
	pxor x1,		x4;

#define SI2_1(x0, x1, x2, x3, x4) \
	pxor x1,		x2; \
	movdqa x3,		x4; \
	pxor RNOT,		x3; \
	por x2,			x3; \
	pxor x4,		x2; \
	pxor x0,		x4; \
	pxor x1,		x3; \
	por x2,			x1; \
	pxor x0,		x2;
#define SI2_2(x0, x1, x2, x3, x4) \
	pxor x4,		x1; \
	por x3,			x4; \
	pxor x3,		x2; \
	pxor x2,		x4; \
	pand x1,		x2; \
	pxor x3,		x2; \
	pxor x4,		x3; \
	pxor x0,		x4;

#define SI3_1(x0, x1, x2, x3, x4) \
	pxor x1,		x2; \
	movdqa x1,		x4; \
	pand x2,		x1; \
	pxor x0,		x1; \
	por x4,			x0; \
	pxor x3,		x4; \
	pxor x3,		x0; \
	por x1,			x3; \
	pxor x2,		x1;
#define SI3_2(x0, x1, x2, x3, x4) \
	pxor x3,		x1; \
	pxor x2,		x0; \
	pxor x3,		x2; \
	pand x1,		x3; \
	pxor x0,		x1; \
	pand x2,		x0; \
	pxor x3,		x4; \
	pxor x0,		x3; \
	pxor x1,		x0;

#define SI4_1(x0, x1, x2, x3, x4) \
	pxor x3,		x2; \
	movdqa x0,		x4; \
	pand x1,		x0; \
	pxor x2,		x0; \
	por x3,			x2; \
	pxor RNOT,		x4; \
	pxor x0,		x1; \
	pxor x2,		x0; \
	pand x4,		x2;
#define SI4_2(x0, x1, x2, x3, x4) \
	pxor x0,		x2; \
	por x4,			x0; \
	pxor x3,		x0; \
	pand x2,		x3; \
	pxor x3,		x4; \
	pxor x1,		x3; \
	pand x0,		x1; \
	pxor x1,		x4; \
	pxor x3,		x0;

#define SI5_1(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	por x2,			x1; \
	pxor x4,		x2; \
	pxor x3,		x1; \
	pand x4,		x3; \
	pxor x3,		x2; \
	por x0,			x3; \
	pxor RNOT,		x0; \
	pxor x2,		x3; \
	por x0,			x2;
#define SI5_2(x0, x1, x2, x3, x4) \
	pxor x1,		x4; \
	pxor x4,		x2; \
	pand x0,		x4; \
	pxor x1,		x0; \
	pxor x3,		x1; \
	pand x2,		x0; \
	pxor x3,		x2; \
	pxor x2,		x0; \
	pxor x4,		x2; \
	pxor x3,		x4;

#define SI6_1(x0, x1, x2, x3, x4) \
	pxor x2,		x0; \
	movdqa x0,		x4; \
	pand x3,		x0; \
	pxor x3,		x2; \
	pxor x2,		x0; \
	pxor x1,		x3; \
	por x4,			x2; \
	pxor x3,		x2; \
	pand x0,		x3;
#define SI6_2(x0, x1, x2, x3, x4) \
	pxor RNOT,		x0; \
	pxor x1,		x3; \
	pand x2,		x1; \
	pxor x0,		x4; \
	pxor x4,		x3; \
	pxor x2,		x4; \
	pxor x1,		x0; \
	pxor x0,		x2;

#define SI7_1(x0, x1, x2, x3, x4) \
	movdqa x3,		x4; \
	pand x0,		x3; \
	pxor x2,		x0; \
	por x4,			x2; \
	pxor x1,		x4; \
	pxor RNOT,		x0; \
	por x3,			x1; \
	pxor x0,		x4; \
	pand x2,		x0; \
	pxor x1,		x0;
#define SI7_2(x0, x1, x2, x3, x4) \
	pand x2,		x1; \
	pxor x2,		x3; \
	pxor x3,		x4; \
	pand x3,		x2; \
	por x0,			x3; \
	pxor x4,		x1; \
	pxor x4,		x3; \
	pand x0,		x4; \
	pxor x2,		x4;

#define get_key(i, j, t) \
	movd (4*(i)+(j))*4(CTX), t; \
	pshufd $0, t, t;

#define K2(x0, x1, x2, x3, x4, i) \
	get_key(i, 0, RK0); \
	get_key(i, 1, RK1); \
	get_key(i, 2, RK2); \
	get_key(i, 3, RK3); \
	pxor RK0,		x0 ## 1; \
	pxor RK1,		x1 ## 1; \
	pxor RK2,		x2 ## 1; \
	pxor RK3,		x3 ## 1; \
		pxor RK0,		x0 ## 2; \
		pxor RK1,		x1 ## 2; \
		pxor RK2,		x2 ## 2; \
		pxor RK3,		x3 ## 2;

#define LK2(x0, x1, x2, x3, x4, i) \
	movdqa x0 ## 1,		x4 ## 1; \
	pslld $13,		x0 ## 1; \
	psrld $(32 - 13),	x4 ## 1; \
	por x4 ## 1,		x0 ## 1; \
	pxor x0 ## 1,		x1 ## 1; \
	movdqa x2 ## 1,		x4 ## 1; \
	pslld $3,		x2 ## 1; \
	psrld $(32 - 3),	x4 ## 1; \
	por x4 ## 1,		x2 ## 1; \
	pxor x2 ## 1,		x1 ## 1; \
		movdqa x0 ## 2,		x4 ## 2; \
		pslld $13,		x0 ## 2; \
		psrld $(32 - 13),	x4 ## 2; \
		por x4 ## 2,		x0 ## 2; \
		pxor x0 ## 2,		x1 ## 2; \
		movdqa x2 ## 2,		x4 ## 2; \
		pslld $3,		x2 ## 2; \
		psrld $(32 - 3),	x4 ## 2; \
		por x4 ## 2,		x2 ## 2; \
		pxor x2 ## 2,		x1 ## 2; \
	movdqa x1 ## 1,		x4 ## 1; \
	pslld $1,		x1 ## 1; \
	psrld $(32 - 1),	x4 ## 1; \
	por x4 ## 1,		x1 ## 1; \
	movdqa x0 ## 1,		x4 ## 1; \
	pslld $3,		x4 ## 1; \
	pxor x2 ## 1,		x3 ## 1; \
	pxor x4 ## 1,		x3 ## 1; \
	movdqa x3 ## 1,		x4 ## 1; \
	get_key(i, 1, RK1); \
		movdqa x1 ## 2,		x4 ## 2; \
		pslld $1,		x1 ## 2; \
		psrld $(32 - 1),	x4 ## 2; \
		por x4 ## 2,		x1 ## 2; \
		movdqa x0 ## 2,		x4 ## 2; \
		pslld $3,		x4 ## 2; \
		pxor x2 ## 2,		x3 ## 2; \
		pxor x4 ## 2,		x3 ## 2; \
		movdqa x3 ## 2,		x4 ## 2; \
		get_key(i, 3, RK3); \
	pslld $7,		x3 ## 1; \
	psrld $(32 - 7),	x4 ## 1; \
	por x4 ## 1,		x3 ## 1; \
	movdqa x1 ## 1,		x4 ## 1; \
	pslld $7,		x4 ## 1; \
	pxor x1 ## 1,		x0 ## 1; \
	pxor x3 ## 1,		x0 ## 1; \
	pxor x3 ## 1,		x2 ## 1; \
	pxor x4 ## 1,		x2 ## 1; \
	get_key(i, 0, RK0); \
		pslld $7,		x3 ## 2; \
		psrld $(32 - 7),	x4 ## 2; \
		por x4 ## 2,		x3 ## 2; \
		movdqa x1 ## 2,		x4 ## 2; \
		pslld $7,		x4 ## 2; \
		pxor x1 ## 2,		x0 ## 2; \
		pxor x3 ## 2,		x0 ## 2; \
		pxor x3 ## 2,		x2 ## 2; \
		pxor x4 ## 2,		x2 ## 2; \
		get_key(i, 2, RK2); \
	pxor RK1,		x1 ## 1; \
	pxor RK3,		x3 ## 1; \
	movdqa x0 ## 1,		x4 ## 1; \
	pslld $5,		x0 ## 1; \
	psrld $(32 - 5),	x4 ## 1; \
	por x4 ## 1,		x0 ## 1; \
	movdqa x2 ## 1,		x4 ## 1; \
	pslld $22,		x2 ## 1; \
	psrld $(32 - 22),	x4 ## 1; \
	por x4 ## 1,		x2 ## 1; \
	pxor RK0,		x0 ## 1; \
	pxor RK2,		x2 ## 1; \
		pxor RK1,		x1 ## 2; \
		pxor RK3,		x3 ## 2; \
		movdqa x0 ## 2,		x4 ## 2; \
		pslld $5,		x0 ## 2; \
		psrld $(32 - 5),	x4 ## 2; \
		por x4 ## 2,		x0 ## 2; \
		movdqa x2 ## 2,		x4 ## 2; \
		pslld $22,		x2 ## 2; \
		psrld $(32 - 22),	x4 ## 2; \
		por x4 ## 2,		x2 ## 2; \
		pxor RK0,		x0 ## 2; \
		pxor RK2,		x2 ## 2;

#define KL2(x0, x1, x2, x3, x4, i) \
	pxor RK0,		x0 ## 1; \
	pxor RK2,		x2 ## 1; \
	movdqa x0 ## 1,		x4 ## 1; \
	psrld $5,		x0 ## 1; \
	pslld $(32 - 5),	x4 ## 1; \
	por x4 ## 1,		x0 ## 1; \
	pxor RK3,		x3 ## 1; \
	pxor RK1,		x1 ## 1; \
	movdqa x2 ## 1,		x4 ## 1; \
	psrld $22,		x2 ## 1; \
	pslld $(32 - 22),	x4 ## 1; \
	por x4 ## 1,		x2 ## 1; \
	pxor x3 ## 1,		x2 ## 1; \
		pxor RK0,		x0 ## 2; \
		pxor RK2,		x2 ## 2; \
		movdqa x0 ## 2,		x4 ## 2; \
		psrld $5,		x0 ## 2; \
		pslld $(32 - 5),	x4 ## 2; \
		por x4 ## 2,		x0 ## 2; \
		pxor RK3,		x3 ## 2; \
		pxor RK1,		x1 ## 2; \
		movdqa x2 ## 2,		x4 ## 2; \
		psrld $22,		x2 ## 2; \
		pslld $(32 - 22),	x4 ## 2; \
		por x4 ## 2,		x2 ## 2; \
		pxor x3 ## 2,		x2 ## 2; \
	pxor x3 ## 1,		x0 ## 1; \
	movdqa x1 ## 1,		x4 ## 1; \
	pslld $7,		x4 ## 1; \
	pxor x1 ## 1,		x0 ## 1; \
	pxor x4 ## 1,		x2 ## 1; \
	movdqa x1 ## 1,		x4 ## 1; \
	psrld $1,		x1 ## 1; \
	pslld $(32 - 1),	x4 ## 1; \
	por x4 ## 1,		x1 ## 1; \
		pxor x3 ## 2,		x0 ## 2; \
		movdqa x1 ## 2,		x4 ## 2; \
		pslld $7,		x4 ## 2; \
		pxor x1 ## 2,		x0 ## 2; \
		pxor x4 ## 2,		x2 ## 2; \
		movdqa x1 ## 2,		x4 ## 2; \
		psrld $1,		x1 ## 2; \
		pslld $(32 - 1),	x4 ## 2; \
		por x4 ## 2,		x1 ## 2; \
	movdqa x3 ## 1,		x4 ## 1; \
	psrld $7,		x3 ## 1; \
	pslld $(32 - 7),	x4 ## 1; \
	por x4 ## 1,		x3 ## 1; \
	pxor x0 ## 1,		x1 ## 1; \
	movdqa x0 ## 1,		x4 ## 1; \
	pslld $3,		x4 ## 1; \
	pxor x4 ## 1,		x3 ## 1; \
	movdqa x0 ## 1,		x4 ## 1; \
		movdqa x3 ## 2,		x4 ## 2; \
		psrld $7,		x3 ## 2; \
		pslld $(32 - 7),	x4 ## 2; \
		por x4 ## 2,		x3 ## 2; \
		pxor x0 ## 2,		x1 ## 2; \
		movdqa x0 ## 2,		x4 ## 2; \
		pslld $3,		x4 ## 2; \
		pxor x4 ## 2,		x3 ## 2; \
		movdqa x0 ## 2,		x4 ## 2; \
	psrld $13,		x0 ## 1; \
	pslld $(32 - 13),	x4 ## 1; \
	por x4 ## 1,		x0 ## 1; \
	pxor x2 ## 1,		x1 ## 1; \
	pxor x2 ## 1,		x3 ## 1; \
	movdqa x2 ## 1,		x4 ## 1; \
	psrld $3,		x2 ## 1; \
	pslld $(32 - 3),	x4 ## 1; \
	por x4 ## 1,		x2 ## 1; \
		psrld $13,		x0 ## 2; \
		pslld $(32 - 13),	x4 ## 2; \
		por x4 ## 2,		x0 ## 2; \
		pxor x2 ## 2,		x1 ## 2; \
		pxor x2 ## 2,		x3 ## 2; \
		movdqa x2 ## 2,		x4 ## 2; \
		psrld $3,		x2 ## 2; \
		pslld $(32 - 3),	x4 ## 2; \
		por x4 ## 2,		x2 ## 2;

#define S(SBOX, x0, x1, x2, x3, x4) \
	SBOX ## _1(x0 ## 1, x1 ## 1, x2 ## 1, x3 ## 1, x4 ## 1); \
	SBOX ## _2(x0 ## 1, x1 ## 1, x2 ## 1, x3 ## 1, x4 ## 1); \
	SBOX ## _1(x0 ## 2, x1 ## 2, x2 ## 2, x3 ## 2, x4 ## 2); \
	SBOX ## _2(x0 ## 2, x1 ## 2, x2 ## 2, x3 ## 2, x4 ## 2);

#define SP(SBOX, x0, x1, x2, x3, x4, i) \
	get_key(i, 0, RK0); \
	SBOX ## _1(x0 ## 1, x1 ## 1, x2 ## 1, x3 ## 1, x4 ## 1); \
	get_key(i, 2, RK2); \
	SBOX ## _1(x0 ## 2, x1 ## 2, x2 ## 2, x3 ## 2, x4 ## 2); \
	get_key(i, 3, RK3); \
	SBOX ## _2(x0 ## 1, x1 ## 1, x2 ## 1, x3 ## 1, x4 ## 1); \
	get_key(i, 1, RK1); \
	SBOX ## _2(x0 ## 2, x1 ## 2, x2 ## 2, x3 ## 2, x4 ## 2); \

#define transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	movdqa x0,		t2; \
	punpckldq x1,		x0; \
	punpckhdq x1,		t2; \
	movdqa x2,		t1; \
	punpckhdq x3,		x2; \
	punpckldq x3,		t1; \
	movdqa x0,		x1; \
	punpcklqdq t1,		x0; \
	punpckhqdq t1,		x1; \
	movdqa t2,		x3; \
	punpcklqdq x2,		t2; \
	punpckhqdq x2,		x3; \
	movdqa t2,		x2;

#define read_blocks(in, x0, x1, x2, x3, t0, t1, t2) \
	movdqu (0*4*4)(in),	x0; \
	movdqu (1*4*4)(in),	x1; \
	movdqu (2*4*4)(in),	x2; \
	movdqu (3*4*4)(in),	x3; \
	\
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2)

#define write_blocks(out, x0, x1, x2, x3, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	movdqu x0,		(0*4*4)(out); \
	movdqu x1,		(1*4*4)(out); \
	movdqu x2,		(2*4*4)(out); \
	movdqu x3,		(3*4*4)(out);

#define xor_blocks(out, x0, x1, x2, x3, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	movdqu (0*4*4)(out),	t0; \
	pxor t0,		x0; \
	movdqu x0,		(0*4*4)(out); \
	movdqu (1*4*4)(out),	t0; \
	pxor t0,		x1; \
	movdqu x1,		(1*4*4)(out); \
	movdqu (2*4*4)(out),	t0; \
	pxor t0,		x2; \
	movdqu x2,		(2*4*4)(out); \
	movdqu (3*4*4)(out),	t0; \
	pxor t0,		x3; \
	movdqu x3,		(3*4*4)(out);

SYM_FUNC_START(__serpent_enc_blk_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: bool, if true: xor output
	 */

	pcmpeqd RNOT, RNOT;

	leaq (4*4*4)(%rdx), %rax;
	read_blocks(%rdx, RA1, RB1, RC1, RD1, RK0, RK1, RK2);
	read_blocks(%rax, RA2, RB2, RC2, RD2, RK0, RK1, RK2);

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

	leaq (4*4*4)(%rsi), %rax;

	testb %cl, %cl;
	jnz .L__enc_xor8;

	write_blocks(%rsi, RA1, RB1, RC1, RD1, RK0, RK1, RK2);
	write_blocks(%rax, RA2, RB2, RC2, RD2, RK0, RK1, RK2);

	ret;

.L__enc_xor8:
	xor_blocks(%rsi, RA1, RB1, RC1, RD1, RK0, RK1, RK2);
	xor_blocks(%rax, RA2, RB2, RC2, RD2, RK0, RK1, RK2);

	ret;
SYM_FUNC_END(__serpent_enc_blk_8way)

SYM_FUNC_START(serpent_dec_blk_8way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	pcmpeqd RNOT, RNOT;

	leaq (4*4*4)(%rdx), %rax;
	read_blocks(%rdx, RA1, RB1, RC1, RD1, RK0, RK1, RK2);
	read_blocks(%rax, RA2, RB2, RC2, RD2, RK0, RK1, RK2);

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

	leaq (4*4*4)(%rsi), %rax;
	write_blocks(%rsi, RC1, RD1, RB1, RE1, RK0, RK1, RK2);
	write_blocks(%rax, RC2, RD2, RB2, RE2, RK0, RK1, RK2);

	ret;
SYM_FUNC_END(serpent_dec_blk_8way)
