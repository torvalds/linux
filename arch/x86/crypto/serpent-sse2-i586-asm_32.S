/*
 * Serpent Cipher 4-way parallel algorithm (i586/SSE2)
 *
 * Copyright (C) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * Based on crypto/serpent.c by
 *  Copyright (C) 2002 Dag Arne Osvik <osvik@ii.uib.no>
 *                2003 Herbert Valerio Riedel <hvr@gnu.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

.file "serpent-sse2-i586-asm_32.S"
.text

#define arg_ctx 4
#define arg_dst 8
#define arg_src 12
#define arg_xor 16

/**********************************************************************
  4-way SSE2 serpent
 **********************************************************************/
#define CTX %edx

#define RA %xmm0
#define RB %xmm1
#define RC %xmm2
#define RD %xmm3
#define RE %xmm4

#define RT0 %xmm5
#define RT1 %xmm6

#define RNOT %xmm7

#define get_key(i, j, t) \
	movd (4*(i)+(j))*4(CTX), t; \
	pshufd $0, t, t;

#define K(x0, x1, x2, x3, x4, i) \
	get_key(i, 0, x4); \
	get_key(i, 1, RT0); \
	get_key(i, 2, RT1); \
	pxor x4,		x0; \
	pxor RT0,		x1; \
	pxor RT1,		x2; \
	get_key(i, 3, x4); \
	pxor x4,		x3;

#define LK(x0, x1, x2, x3, x4, i) \
	movdqa x0,		x4; \
	pslld $13,		x0; \
	psrld $(32 - 13),	x4; \
	por x4,			x0; \
	pxor x0,		x1; \
	movdqa x2,		x4; \
	pslld $3,		x2; \
	psrld $(32 - 3),	x4; \
	por x4,			x2; \
	pxor x2,		x1; \
	movdqa x1,		x4; \
	pslld $1,		x1; \
	psrld $(32 - 1),	x4; \
	por x4,			x1; \
	movdqa x0,		x4; \
	pslld $3,		x4; \
	pxor x2,		x3; \
	pxor x4,		x3; \
	movdqa x3,		x4; \
	pslld $7,		x3; \
	psrld $(32 - 7),	x4; \
	por x4,			x3; \
	movdqa x1,		x4; \
	pslld $7,		x4; \
	pxor x1,		x0; \
	pxor x3,		x0; \
	pxor x3,		x2; \
	pxor x4,		x2; \
	movdqa x0,		x4; \
	get_key(i, 1, RT0); \
	pxor RT0,		x1; \
	get_key(i, 3, RT0); \
	pxor RT0,		x3; \
	pslld $5,		x0; \
	psrld $(32 - 5),	x4; \
	por x4,			x0; \
	movdqa x2,		x4; \
	pslld $22,		x2; \
	psrld $(32 - 22),	x4; \
	por x4,			x2; \
	get_key(i, 0, RT0); \
	pxor RT0,		x0; \
	get_key(i, 2, RT0); \
	pxor RT0,		x2;

#define KL(x0, x1, x2, x3, x4, i) \
	K(x0, x1, x2, x3, x4, i); \
	movdqa x0,		x4; \
	psrld $5,		x0; \
	pslld $(32 - 5),	x4; \
	por x4,			x0; \
	movdqa x2,		x4; \
	psrld $22,		x2; \
	pslld $(32 - 22),	x4; \
	por x4,			x2; \
	pxor x3,		x2; \
	pxor x3,		x0; \
	movdqa x1,		x4; \
	pslld $7,		x4; \
	pxor x1,		x0; \
	pxor x4,		x2; \
	movdqa x1,		x4; \
	psrld $1,		x1; \
	pslld $(32 - 1),	x4; \
	por x4,			x1; \
	movdqa x3,		x4; \
	psrld $7,		x3; \
	pslld $(32 - 7),	x4; \
	por x4,			x3; \
	pxor x0,		x1; \
	movdqa x0,		x4; \
	pslld $3,		x4; \
	pxor x4,		x3; \
	movdqa x0,		x4; \
	psrld $13,		x0; \
	pslld $(32 - 13),	x4; \
	por x4,			x0; \
	pxor x2,		x1; \
	pxor x2,		x3; \
	movdqa x2,		x4; \
	psrld $3,		x2; \
	pslld $(32 - 3),	x4; \
	por x4,			x2;

#define S0(x0, x1, x2, x3, x4) \
	movdqa x3,		x4; \
	por x0,			x3; \
	pxor x4,		x0; \
	pxor x2,		x4; \
	pxor RNOT,		x4; \
	pxor x1,		x3; \
	pand x0,		x1; \
	pxor x4,		x1; \
	pxor x0,		x2; \
	pxor x3,		x0; \
	por x0,			x4; \
	pxor x2,		x0; \
	pand x1,		x2; \
	pxor x2,		x3; \
	pxor RNOT,		x1; \
	pxor x4,		x2; \
	pxor x2,		x1;

#define S1(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	pxor x0,		x1; \
	pxor x3,		x0; \
	pxor RNOT,		x3; \
	pand x1,		x4; \
	por x1,			x0; \
	pxor x2,		x3; \
	pxor x3,		x0; \
	pxor x3,		x1; \
	pxor x4,		x3; \
	por x4,			x1; \
	pxor x2,		x4; \
	pand x0,		x2; \
	pxor x1,		x2; \
	por x0,			x1; \
	pxor RNOT,		x0; \
	pxor x2,		x0; \
	pxor x1,		x4;

#define S2(x0, x1, x2, x3, x4) \
	pxor RNOT,		x3; \
	pxor x0,		x1; \
	movdqa x0,		x4; \
	pand x2,		x0; \
	pxor x3,		x0; \
	por x4,			x3; \
	pxor x1,		x2; \
	pxor x1,		x3; \
	pand x0,		x1; \
	pxor x2,		x0; \
	pand x3,		x2; \
	por x1,			x3; \
	pxor RNOT,		x0; \
	pxor x0,		x3; \
	pxor x0,		x4; \
	pxor x2,		x0; \
	por x2,			x1;

#define S3(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	pxor x3,		x1; \
	por x0,			x3; \
	pand x0,		x4; \
	pxor x2,		x0; \
	pxor x1,		x2; \
	pand x3,		x1; \
	pxor x3,		x2; \
	por x4,			x0; \
	pxor x3,		x4; \
	pxor x0,		x1; \
	pand x3,		x0; \
	pand x4,		x3; \
	pxor x2,		x3; \
	por x1,			x4; \
	pand x1,		x2; \
	pxor x3,		x4; \
	pxor x3,		x0; \
	pxor x2,		x3;

#define S4(x0, x1, x2, x3, x4) \
	movdqa x3,		x4; \
	pand x0,		x3; \
	pxor x4,		x0; \
	pxor x2,		x3; \
	por x4,			x2; \
	pxor x1,		x0; \
	pxor x3,		x4; \
	por x0,			x2; \
	pxor x1,		x2; \
	pand x0,		x1; \
	pxor x4,		x1; \
	pand x2,		x4; \
	pxor x3,		x2; \
	pxor x0,		x4; \
	por x1,			x3; \
	pxor RNOT,		x1; \
	pxor x0,		x3;

#define S5(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	por x0,			x1; \
	pxor x1,		x2; \
	pxor RNOT,		x3; \
	pxor x0,		x4; \
	pxor x2,		x0; \
	pand x4,		x1; \
	por x3,			x4; \
	pxor x0,		x4; \
	pand x3,		x0; \
	pxor x3,		x1; \
	pxor x2,		x3; \
	pxor x1,		x0; \
	pand x4,		x2; \
	pxor x2,		x1; \
	pand x0,		x2; \
	pxor x2,		x3;

#define S6(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	pxor x0,		x3; \
	pxor x2,		x1; \
	pxor x0,		x2; \
	pand x3,		x0; \
	por x3,			x1; \
	pxor RNOT,		x4; \
	pxor x1,		x0; \
	pxor x2,		x1; \
	pxor x4,		x3; \
	pxor x0,		x4; \
	pand x0,		x2; \
	pxor x1,		x4; \
	pxor x3,		x2; \
	pand x1,		x3; \
	pxor x0,		x3; \
	pxor x2,		x1;

#define S7(x0, x1, x2, x3, x4) \
	pxor RNOT,		x1; \
	movdqa x1,		x4; \
	pxor RNOT,		x0; \
	pand x2,		x1; \
	pxor x3,		x1; \
	por x4,			x3; \
	pxor x2,		x4; \
	pxor x3,		x2; \
	pxor x0,		x3; \
	por x1,			x0; \
	pand x0,		x2; \
	pxor x4,		x0; \
	pxor x3,		x4; \
	pand x0,		x3; \
	pxor x1,		x4; \
	pxor x4,		x2; \
	pxor x1,		x3; \
	por x0,			x4; \
	pxor x1,		x4;

#define SI0(x0, x1, x2, x3, x4) \
	movdqa x3,		x4; \
	pxor x0,		x1; \
	por x1,			x3; \
	pxor x1,		x4; \
	pxor RNOT,		x0; \
	pxor x3,		x2; \
	pxor x0,		x3; \
	pand x1,		x0; \
	pxor x2,		x0; \
	pand x3,		x2; \
	pxor x4,		x3; \
	pxor x3,		x2; \
	pxor x3,		x1; \
	pand x0,		x3; \
	pxor x0,		x1; \
	pxor x2,		x0; \
	pxor x3,		x4;

#define SI1(x0, x1, x2, x3, x4) \
	pxor x3,		x1; \
	movdqa x0,		x4; \
	pxor x2,		x0; \
	pxor RNOT,		x2; \
	por x1,			x4; \
	pxor x3,		x4; \
	pand x1,		x3; \
	pxor x2,		x1; \
	pand x4,		x2; \
	pxor x1,		x4; \
	por x3,			x1; \
	pxor x0,		x3; \
	pxor x0,		x2; \
	por x4,			x0; \
	pxor x4,		x2; \
	pxor x0,		x1; \
	pxor x1,		x4;

#define SI2(x0, x1, x2, x3, x4) \
	pxor x1,		x2; \
	movdqa x3,		x4; \
	pxor RNOT,		x3; \
	por x2,			x3; \
	pxor x4,		x2; \
	pxor x0,		x4; \
	pxor x1,		x3; \
	por x2,			x1; \
	pxor x0,		x2; \
	pxor x4,		x1; \
	por x3,			x4; \
	pxor x3,		x2; \
	pxor x2,		x4; \
	pand x1,		x2; \
	pxor x3,		x2; \
	pxor x4,		x3; \
	pxor x0,		x4;

#define SI3(x0, x1, x2, x3, x4) \
	pxor x1,		x2; \
	movdqa x1,		x4; \
	pand x2,		x1; \
	pxor x0,		x1; \
	por x4,			x0; \
	pxor x3,		x4; \
	pxor x3,		x0; \
	por x1,			x3; \
	pxor x2,		x1; \
	pxor x3,		x1; \
	pxor x2,		x0; \
	pxor x3,		x2; \
	pand x1,		x3; \
	pxor x0,		x1; \
	pand x2,		x0; \
	pxor x3,		x4; \
	pxor x0,		x3; \
	pxor x1,		x0;

#define SI4(x0, x1, x2, x3, x4) \
	pxor x3,		x2; \
	movdqa x0,		x4; \
	pand x1,		x0; \
	pxor x2,		x0; \
	por x3,			x2; \
	pxor RNOT,		x4; \
	pxor x0,		x1; \
	pxor x2,		x0; \
	pand x4,		x2; \
	pxor x0,		x2; \
	por x4,			x0; \
	pxor x3,		x0; \
	pand x2,		x3; \
	pxor x3,		x4; \
	pxor x1,		x3; \
	pand x0,		x1; \
	pxor x1,		x4; \
	pxor x3,		x0;

#define SI5(x0, x1, x2, x3, x4) \
	movdqa x1,		x4; \
	por x2,			x1; \
	pxor x4,		x2; \
	pxor x3,		x1; \
	pand x4,		x3; \
	pxor x3,		x2; \
	por x0,			x3; \
	pxor RNOT,		x0; \
	pxor x2,		x3; \
	por x0,			x2; \
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

#define SI6(x0, x1, x2, x3, x4) \
	pxor x2,		x0; \
	movdqa x0,		x4; \
	pand x3,		x0; \
	pxor x3,		x2; \
	pxor x2,		x0; \
	pxor x1,		x3; \
	por x4,			x2; \
	pxor x3,		x2; \
	pand x0,		x3; \
	pxor RNOT,		x0; \
	pxor x1,		x3; \
	pand x2,		x1; \
	pxor x0,		x4; \
	pxor x4,		x3; \
	pxor x2,		x4; \
	pxor x1,		x0; \
	pxor x0,		x2;

#define SI7(x0, x1, x2, x3, x4) \
	movdqa x3,		x4; \
	pand x0,		x3; \
	pxor x2,		x0; \
	por x4,			x2; \
	pxor x1,		x4; \
	pxor RNOT,		x0; \
	por x3,			x1; \
	pxor x0,		x4; \
	pand x2,		x0; \
	pxor x1,		x0; \
	pand x2,		x1; \
	pxor x2,		x3; \
	pxor x3,		x4; \
	pand x3,		x2; \
	por x0,			x3; \
	pxor x4,		x1; \
	pxor x4,		x3; \
	pand x0,		x4; \
	pxor x2,		x4;

#define transpose_4x4(x0, x1, x2, x3, t1, t2, t3) \
	movdqa x2,		t3; \
	movdqa x0,		t1; \
	unpcklps x3,		t3; \
	movdqa x0,		t2; \
	unpcklps x1,		t1; \
	unpckhps x1,		t2; \
	movdqa t3,		x1; \
	unpckhps x3,		x2; \
	movdqa t1,		x0; \
	movhlps t1,		x1; \
	movdqa t2,		t1; \
	movlhps t3,		x0; \
	movlhps x2,		t1; \
	movhlps t2,		x2; \
	movdqa x2,		x3; \
	movdqa t1,		x2;

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
	movdqu x0, (0*4*4)(out); \
	movdqu x1, (1*4*4)(out); \
	movdqu x2, (2*4*4)(out); \
	movdqu x3, (3*4*4)(out);

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

.align 8
.global __serpent_enc_blk_4way
.type   __serpent_enc_blk_4way,@function;

__serpent_enc_blk_4way:
	/* input:
	 *	arg_ctx(%esp): ctx, CTX
	 *	arg_dst(%esp): dst
	 *	arg_src(%esp): src
	 *	arg_xor(%esp): bool, if true: xor output
	 */

	pcmpeqd RNOT, RNOT;

	movl arg_ctx(%esp), CTX;

	movl arg_src(%esp), %eax;
	read_blocks(%eax, RA, RB, RC, RD, RT0, RT1, RE);

					 K(RA, RB, RC, RD, RE, 0);
	S0(RA, RB, RC, RD, RE);		LK(RC, RB, RD, RA, RE, 1);
	S1(RC, RB, RD, RA, RE);		LK(RE, RD, RA, RC, RB, 2);
	S2(RE, RD, RA, RC, RB);		LK(RB, RD, RE, RC, RA, 3);
	S3(RB, RD, RE, RC, RA);		LK(RC, RA, RD, RB, RE, 4);
	S4(RC, RA, RD, RB, RE);		LK(RA, RD, RB, RE, RC, 5);
	S5(RA, RD, RB, RE, RC);		LK(RC, RA, RD, RE, RB, 6);
	S6(RC, RA, RD, RE, RB);		LK(RD, RB, RA, RE, RC, 7);
	S7(RD, RB, RA, RE, RC);		LK(RC, RA, RE, RD, RB, 8);
	S0(RC, RA, RE, RD, RB);		LK(RE, RA, RD, RC, RB, 9);
	S1(RE, RA, RD, RC, RB);		LK(RB, RD, RC, RE, RA, 10);
	S2(RB, RD, RC, RE, RA);		LK(RA, RD, RB, RE, RC, 11);
	S3(RA, RD, RB, RE, RC);		LK(RE, RC, RD, RA, RB, 12);
	S4(RE, RC, RD, RA, RB);		LK(RC, RD, RA, RB, RE, 13);
	S5(RC, RD, RA, RB, RE);		LK(RE, RC, RD, RB, RA, 14);
	S6(RE, RC, RD, RB, RA);		LK(RD, RA, RC, RB, RE, 15);
	S7(RD, RA, RC, RB, RE);		LK(RE, RC, RB, RD, RA, 16);
	S0(RE, RC, RB, RD, RA);		LK(RB, RC, RD, RE, RA, 17);
	S1(RB, RC, RD, RE, RA);		LK(RA, RD, RE, RB, RC, 18);
	S2(RA, RD, RE, RB, RC);		LK(RC, RD, RA, RB, RE, 19);
	S3(RC, RD, RA, RB, RE);		LK(RB, RE, RD, RC, RA, 20);
	S4(RB, RE, RD, RC, RA);		LK(RE, RD, RC, RA, RB, 21);
	S5(RE, RD, RC, RA, RB);		LK(RB, RE, RD, RA, RC, 22);
	S6(RB, RE, RD, RA, RC);		LK(RD, RC, RE, RA, RB, 23);
	S7(RD, RC, RE, RA, RB);		LK(RB, RE, RA, RD, RC, 24);
	S0(RB, RE, RA, RD, RC);		LK(RA, RE, RD, RB, RC, 25);
	S1(RA, RE, RD, RB, RC);		LK(RC, RD, RB, RA, RE, 26);
	S2(RC, RD, RB, RA, RE);		LK(RE, RD, RC, RA, RB, 27);
	S3(RE, RD, RC, RA, RB);		LK(RA, RB, RD, RE, RC, 28);
	S4(RA, RB, RD, RE, RC);		LK(RB, RD, RE, RC, RA, 29);
	S5(RB, RD, RE, RC, RA);		LK(RA, RB, RD, RC, RE, 30);
	S6(RA, RB, RD, RC, RE);		LK(RD, RE, RB, RC, RA, 31);
	S7(RD, RE, RB, RC, RA);		 K(RA, RB, RC, RD, RE, 32);

	movl arg_dst(%esp), %eax;

	cmpb $0, arg_xor(%esp);
	jnz __enc_xor4;

	write_blocks(%eax, RA, RB, RC, RD, RT0, RT1, RE);

	ret;

__enc_xor4:
	xor_blocks(%eax, RA, RB, RC, RD, RT0, RT1, RE);

	ret;

.align 8
.global serpent_dec_blk_4way
.type   serpent_dec_blk_4way,@function;

serpent_dec_blk_4way:
	/* input:
	 *	arg_ctx(%esp): ctx, CTX
	 *	arg_dst(%esp): dst
	 *	arg_src(%esp): src
	 */

	pcmpeqd RNOT, RNOT;

	movl arg_ctx(%esp), CTX;

	movl arg_src(%esp), %eax;
	read_blocks(%eax, RA, RB, RC, RD, RT0, RT1, RE);

					 K(RA, RB, RC, RD, RE, 32);
	SI7(RA, RB, RC, RD, RE);	KL(RB, RD, RA, RE, RC, 31);
	SI6(RB, RD, RA, RE, RC);	KL(RA, RC, RE, RB, RD, 30);
	SI5(RA, RC, RE, RB, RD);	KL(RC, RD, RA, RE, RB, 29);
	SI4(RC, RD, RA, RE, RB);	KL(RC, RA, RB, RE, RD, 28);
	SI3(RC, RA, RB, RE, RD);	KL(RB, RC, RD, RE, RA, 27);
	SI2(RB, RC, RD, RE, RA);	KL(RC, RA, RE, RD, RB, 26);
	SI1(RC, RA, RE, RD, RB);	KL(RB, RA, RE, RD, RC, 25);
	SI0(RB, RA, RE, RD, RC);	KL(RE, RC, RA, RB, RD, 24);
	SI7(RE, RC, RA, RB, RD);	KL(RC, RB, RE, RD, RA, 23);
	SI6(RC, RB, RE, RD, RA);	KL(RE, RA, RD, RC, RB, 22);
	SI5(RE, RA, RD, RC, RB);	KL(RA, RB, RE, RD, RC, 21);
	SI4(RA, RB, RE, RD, RC);	KL(RA, RE, RC, RD, RB, 20);
	SI3(RA, RE, RC, RD, RB);	KL(RC, RA, RB, RD, RE, 19);
	SI2(RC, RA, RB, RD, RE);	KL(RA, RE, RD, RB, RC, 18);
	SI1(RA, RE, RD, RB, RC);	KL(RC, RE, RD, RB, RA, 17);
	SI0(RC, RE, RD, RB, RA);	KL(RD, RA, RE, RC, RB, 16);
	SI7(RD, RA, RE, RC, RB);	KL(RA, RC, RD, RB, RE, 15);
	SI6(RA, RC, RD, RB, RE);	KL(RD, RE, RB, RA, RC, 14);
	SI5(RD, RE, RB, RA, RC);	KL(RE, RC, RD, RB, RA, 13);
	SI4(RE, RC, RD, RB, RA);	KL(RE, RD, RA, RB, RC, 12);
	SI3(RE, RD, RA, RB, RC);	KL(RA, RE, RC, RB, RD, 11);
	SI2(RA, RE, RC, RB, RD);	KL(RE, RD, RB, RC, RA, 10);
	SI1(RE, RD, RB, RC, RA);	KL(RA, RD, RB, RC, RE, 9);
	SI0(RA, RD, RB, RC, RE);	KL(RB, RE, RD, RA, RC, 8);
	SI7(RB, RE, RD, RA, RC);	KL(RE, RA, RB, RC, RD, 7);
	SI6(RE, RA, RB, RC, RD);	KL(RB, RD, RC, RE, RA, 6);
	SI5(RB, RD, RC, RE, RA);	KL(RD, RA, RB, RC, RE, 5);
	SI4(RD, RA, RB, RC, RE);	KL(RD, RB, RE, RC, RA, 4);
	SI3(RD, RB, RE, RC, RA);	KL(RE, RD, RA, RC, RB, 3);
	SI2(RE, RD, RA, RC, RB);	KL(RD, RB, RC, RA, RE, 2);
	SI1(RD, RB, RC, RA, RE);	KL(RE, RB, RC, RA, RD, 1);
	SI0(RE, RB, RC, RA, RD);	 K(RC, RD, RB, RE, RA, 0);

	movl arg_dst(%esp), %eax;
	write_blocks(%eax, RC, RD, RB, RE, RT0, RT1, RA);

	ret;
