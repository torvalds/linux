/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM3 AVX accelerated transform.
 * specified in: https://datatracker.ietf.org/doc/html/draft-sca-cfrg-sm3-02
 *
 * Copyright (C) 2021 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 * Copyright (C) 2021 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

/* Based on SM3 AES/BMI2 accelerated work by libgcrypt at:
 *  https://gnupg.org/software/libgcrypt/index.html
 */

#include <linux/linkage.h>
#include <asm/frame.h>

/* Context structure */

#define state_h0 0
#define state_h1 4
#define state_h2 8
#define state_h3 12
#define state_h4 16
#define state_h5 20
#define state_h6 24
#define state_h7 28

/* Constants */

/* Round constant macros */

#define K0   2043430169  /* 0x79cc4519 */
#define K1   -208106958  /* 0xf3988a32 */
#define K2   -416213915  /* 0xe7311465 */
#define K3   -832427829  /* 0xce6228cb */
#define K4  -1664855657  /* 0x9cc45197 */
#define K5    965255983  /* 0x3988a32f */
#define K6   1930511966  /* 0x7311465e */
#define K7   -433943364  /* 0xe6228cbc */
#define K8   -867886727  /* 0xcc451979 */
#define K9  -1735773453  /* 0x988a32f3 */
#define K10   823420391  /* 0x311465e7 */
#define K11  1646840782  /* 0x6228cbce */
#define K12 -1001285732  /* 0xc451979c */
#define K13 -2002571463  /* 0x88a32f39 */
#define K14   289824371  /* 0x11465e73 */
#define K15   579648742  /* 0x228cbce6 */
#define K16 -1651869049  /* 0x9d8a7a87 */
#define K17   991229199  /* 0x3b14f50f */
#define K18  1982458398  /* 0x7629ea1e */
#define K19  -330050500  /* 0xec53d43c */
#define K20  -660100999  /* 0xd8a7a879 */
#define K21 -1320201997  /* 0xb14f50f3 */
#define K22  1654563303  /* 0x629ea1e7 */
#define K23  -985840690  /* 0xc53d43ce */
#define K24 -1971681379  /* 0x8a7a879d */
#define K25   351604539  /* 0x14f50f3b */
#define K26   703209078  /* 0x29ea1e76 */
#define K27  1406418156  /* 0x53d43cec */
#define K28 -1482130984  /* 0xa7a879d8 */
#define K29  1330705329  /* 0x4f50f3b1 */
#define K30 -1633556638  /* 0x9ea1e762 */
#define K31  1027854021  /* 0x3d43cec5 */
#define K32  2055708042  /* 0x7a879d8a */
#define K33  -183551212  /* 0xf50f3b14 */
#define K34  -367102423  /* 0xea1e7629 */
#define K35  -734204845  /* 0xd43cec53 */
#define K36 -1468409689  /* 0xa879d8a7 */
#define K37  1358147919  /* 0x50f3b14f */
#define K38 -1578671458  /* 0xa1e7629e */
#define K39  1137624381  /* 0x43cec53d */
#define K40 -2019718534  /* 0x879d8a7a */
#define K41   255530229  /* 0x0f3b14f5 */
#define K42   511060458  /* 0x1e7629ea */
#define K43  1022120916  /* 0x3cec53d4 */
#define K44  2044241832  /* 0x79d8a7a8 */
#define K45  -206483632  /* 0xf3b14f50 */
#define K46  -412967263  /* 0xe7629ea1 */
#define K47  -825934525  /* 0xcec53d43 */
#define K48 -1651869049  /* 0x9d8a7a87 */
#define K49   991229199  /* 0x3b14f50f */
#define K50  1982458398  /* 0x7629ea1e */
#define K51  -330050500  /* 0xec53d43c */
#define K52  -660100999  /* 0xd8a7a879 */
#define K53 -1320201997  /* 0xb14f50f3 */
#define K54  1654563303  /* 0x629ea1e7 */
#define K55  -985840690  /* 0xc53d43ce */
#define K56 -1971681379  /* 0x8a7a879d */
#define K57   351604539  /* 0x14f50f3b */
#define K58   703209078  /* 0x29ea1e76 */
#define K59  1406418156  /* 0x53d43cec */
#define K60 -1482130984  /* 0xa7a879d8 */
#define K61  1330705329  /* 0x4f50f3b1 */
#define K62 -1633556638  /* 0x9ea1e762 */
#define K63  1027854021  /* 0x3d43cec5 */

/* Register macros */

#define RSTATE %rdi
#define RDATA  %rsi
#define RNBLKS %rdx

#define t0 %eax
#define t1 %ebx
#define t2 %ecx

#define a %r8d
#define b %r9d
#define c %r10d
#define d %r11d
#define e %r12d
#define f %r13d
#define g %r14d
#define h %r15d

#define W0 %xmm0
#define W1 %xmm1
#define W2 %xmm2
#define W3 %xmm3
#define W4 %xmm4
#define W5 %xmm5

#define XTMP0 %xmm6
#define XTMP1 %xmm7
#define XTMP2 %xmm8
#define XTMP3 %xmm9
#define XTMP4 %xmm10
#define XTMP5 %xmm11
#define XTMP6 %xmm12

#define BSWAP_REG %xmm15

/* Stack structure */

#define STACK_W_SIZE        (32 * 2 * 3)
#define STACK_REG_SAVE_SIZE (64)

#define STACK_W             (0)
#define STACK_REG_SAVE      (STACK_W + STACK_W_SIZE)
#define STACK_SIZE          (STACK_REG_SAVE + STACK_REG_SAVE_SIZE)

/* Instruction helpers. */

#define roll2(v, reg)		\
	roll $(v), reg;

#define roll3mov(v, src, dst)	\
	movl src, dst;		\
	roll $(v), dst;

#define roll3(v, src, dst)	\
	rorxl $(32-(v)), src, dst;

#define addl2(a, out)		\
	leal (a, out), out;

/* Round function macros. */

#define GG1(x, y, z, o, t)	\
	movl x, o;		\
	xorl y, o;		\
	xorl z, o;

#define FF1(x, y, z, o, t) GG1(x, y, z, o, t)

#define GG2(x, y, z, o, t)	\
	andnl z, x, o;		\
	movl y, t;		\
	andl x, t;		\
	addl2(t, o);

#define FF2(x, y, z, o, t)	\
	movl y, o;		\
	xorl x, o;		\
	movl y, t;		\
	andl x, t;		\
	andl z, o;		\
	xorl t, o;

#define R(i, a, b, c, d, e, f, g, h, round, widx, wtype)		\
	/* rol(a, 12) => t0 */						\
	roll3mov(12, a, t0); /* rorxl here would reduce perf by 6% on zen3 */ \
	/* rol (t0 + e + t), 7) => t1 */				\
	leal K##round(t0, e, 1), t1;					\
	roll2(7, t1);							\
	/* h + w1 => h */						\
	addl wtype##_W1_ADDR(round, widx), h;				\
	/* h + t1 => h */						\
	addl2(t1, h);							\
	/* t1 ^ t0 => t0 */						\
	xorl t1, t0;							\
	/* w1w2 + d => d */						\
	addl wtype##_W1W2_ADDR(round, widx), d;				\
	/* FF##i(a,b,c) => t1 */					\
	FF##i(a, b, c, t1, t2);						\
	/* d + t1 => d */						\
	addl2(t1, d);							\
	/* GG#i(e,f,g) => t2 */						\
	GG##i(e, f, g, t2, t1);						\
	/* h + t2 => h */						\
	addl2(t2, h);							\
	/* rol (f, 19) => f */						\
	roll2(19, f);							\
	/* d + t0 => d */						\
	addl2(t0, d);							\
	/* rol (b, 9) => b */						\
	roll2(9, b);							\
	/* P0(h) => h */						\
	roll3(9, h, t2);						\
	roll3(17, h, t1);						\
	xorl t2, h;							\
	xorl t1, h;

#define R1(a, b, c, d, e, f, g, h, round, widx, wtype) \
	R(1, a, b, c, d, e, f, g, h, round, widx, wtype)

#define R2(a, b, c, d, e, f, g, h, round, widx, wtype) \
	R(2, a, b, c, d, e, f, g, h, round, widx, wtype)

/* Input expansion macros. */

/* Byte-swapped input address. */
#define IW_W_ADDR(round, widx, offs) \
	(STACK_W + ((round) / 4) * 64 + (offs) + ((widx) * 4))(%rsp)

/* Expanded input address. */
#define XW_W_ADDR(round, widx, offs) \
	(STACK_W + ((((round) / 3) - 4) % 2) * 64 + (offs) + ((widx) * 4))(%rsp)

/* Rounds 1-12, byte-swapped input block addresses. */
#define IW_W1_ADDR(round, widx)   IW_W_ADDR(round, widx, 0)
#define IW_W1W2_ADDR(round, widx) IW_W_ADDR(round, widx, 32)

/* Rounds 1-12, expanded input block addresses. */
#define XW_W1_ADDR(round, widx)   XW_W_ADDR(round, widx, 0)
#define XW_W1W2_ADDR(round, widx) XW_W_ADDR(round, widx, 32)

/* Input block loading. */
#define LOAD_W_XMM_1()							\
	vmovdqu 0*16(RDATA), XTMP0; /* XTMP0: w3, w2, w1, w0 */		\
	vmovdqu 1*16(RDATA), XTMP1; /* XTMP1: w7, w6, w5, w4 */		\
	vmovdqu 2*16(RDATA), XTMP2; /* XTMP2: w11, w10, w9, w8 */	\
	vmovdqu 3*16(RDATA), XTMP3; /* XTMP3: w15, w14, w13, w12 */	\
	vpshufb BSWAP_REG, XTMP0, XTMP0;				\
	vpshufb BSWAP_REG, XTMP1, XTMP1;				\
	vpshufb BSWAP_REG, XTMP2, XTMP2;				\
	vpshufb BSWAP_REG, XTMP3, XTMP3;				\
	vpxor XTMP0, XTMP1, XTMP4;					\
	vpxor XTMP1, XTMP2, XTMP5;					\
	vpxor XTMP2, XTMP3, XTMP6;					\
	leaq 64(RDATA), RDATA;						\
	vmovdqa XTMP0, IW_W1_ADDR(0, 0);				\
	vmovdqa XTMP4, IW_W1W2_ADDR(0, 0);				\
	vmovdqa XTMP1, IW_W1_ADDR(4, 0);				\
	vmovdqa XTMP5, IW_W1W2_ADDR(4, 0);

#define LOAD_W_XMM_2()				\
	vmovdqa XTMP2, IW_W1_ADDR(8, 0);	\
	vmovdqa XTMP6, IW_W1W2_ADDR(8, 0);

#define LOAD_W_XMM_3()							\
	vpshufd $0b00000000, XTMP0, W0; /* W0: xx, w0, xx, xx */	\
	vpshufd $0b11111001, XTMP0, W1; /* W1: xx, w3, w2, w1 */	\
	vmovdqa XTMP1, W2;              /* W2: xx, w6, w5, w4 */	\
	vpalignr $12, XTMP1, XTMP2, W3; /* W3: xx, w9, w8, w7 */	\
	vpalignr $8, XTMP2, XTMP3, W4;  /* W4: xx, w12, w11, w10 */	\
	vpshufd $0b11111001, XTMP3, W5; /* W5: xx, w15, w14, w13 */

/* Message scheduling. Note: 3 words per XMM register. */
#define SCHED_W_0(round, w0, w1, w2, w3, w4, w5)			\
	/* Load (w[i - 16]) => XTMP0 */					\
	vpshufd $0b10111111, w0, XTMP0;					\
	vpalignr $12, XTMP0, w1, XTMP0; /* XTMP0: xx, w2, w1, w0 */	\
	/* Load (w[i - 13]) => XTMP1 */					\
	vpshufd $0b10111111, w1, XTMP1;					\
	vpalignr $12, XTMP1, w2, XTMP1;					\
	/* w[i - 9] == w3 */						\
	/* XMM3 ^ XTMP0 => XTMP0 */					\
	vpxor w3, XTMP0, XTMP0;

#define SCHED_W_1(round, w0, w1, w2, w3, w4, w5)	\
	/* w[i - 3] == w5 */				\
	/* rol(XMM5, 15) ^ XTMP0 => XTMP0 */		\
	vpslld $15, w5, XTMP2;				\
	vpsrld $(32-15), w5, XTMP3;			\
	vpxor XTMP2, XTMP3, XTMP3;			\
	vpxor XTMP3, XTMP0, XTMP0;			\
	/* rol(XTMP1, 7) => XTMP1 */			\
	vpslld $7, XTMP1, XTMP5;			\
	vpsrld $(32-7), XTMP1, XTMP1;			\
	vpxor XTMP5, XTMP1, XTMP1;			\
	/* XMM4 ^ XTMP1 => XTMP1 */			\
	vpxor w4, XTMP1, XTMP1;				\
	/* w[i - 6] == XMM4 */				\
	/* P1(XTMP0) ^ XTMP1 => XMM0 */			\
	vpslld $15, XTMP0, XTMP5;			\
	vpsrld $(32-15), XTMP0, XTMP6;			\
	vpslld $23, XTMP0, XTMP2;			\
	vpsrld $(32-23), XTMP0, XTMP3;			\
	vpxor XTMP0, XTMP1, XTMP1;			\
	vpxor XTMP6, XTMP5, XTMP5;			\
	vpxor XTMP3, XTMP2, XTMP2;			\
	vpxor XTMP2, XTMP5, XTMP5;			\
	vpxor XTMP5, XTMP1, w0;

#define SCHED_W_2(round, w0, w1, w2, w3, w4, w5)	\
	/* W1 in XMM12 */				\
	vpshufd $0b10111111, w4, XTMP4;			\
	vpalignr $12, XTMP4, w5, XTMP4;			\
	vmovdqa XTMP4, XW_W1_ADDR((round), 0);		\
	/* W1 ^ W2 => XTMP1 */				\
	vpxor w0, XTMP4, XTMP1;				\
	vmovdqa XTMP1, XW_W1W2_ADDR((round), 0);


.section	.rodata.cst16, "aM", @progbits, 16
.align 16

.Lbe32mask:
	.long 0x00010203, 0x04050607, 0x08090a0b, 0x0c0d0e0f

.text

/*
 * Transform nblocks*64 bytes (nblocks*16 32-bit words) at DATA.
 *
 * void sm3_transform_avx(struct sm3_state *state,
 *                        const u8 *data, int nblocks);
 */
.align 16
SYM_FUNC_START(sm3_transform_avx)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: data (64*nblks bytes)
	 *	%rdx: nblocks
	 */
	vzeroupper;

	pushq %rbp;
	movq %rsp, %rbp;

	movq %rdx, RNBLKS;

	subq $STACK_SIZE, %rsp;
	andq $(~63), %rsp;

	movq %rbx, (STACK_REG_SAVE + 0 * 8)(%rsp);
	movq %r15, (STACK_REG_SAVE + 1 * 8)(%rsp);
	movq %r14, (STACK_REG_SAVE + 2 * 8)(%rsp);
	movq %r13, (STACK_REG_SAVE + 3 * 8)(%rsp);
	movq %r12, (STACK_REG_SAVE + 4 * 8)(%rsp);

	vmovdqa .Lbe32mask (%rip), BSWAP_REG;

	/* Get the values of the chaining variables. */
	movl state_h0(RSTATE), a;
	movl state_h1(RSTATE), b;
	movl state_h2(RSTATE), c;
	movl state_h3(RSTATE), d;
	movl state_h4(RSTATE), e;
	movl state_h5(RSTATE), f;
	movl state_h6(RSTATE), g;
	movl state_h7(RSTATE), h;

.align 16
.Loop:
	/* Load data part1. */
	LOAD_W_XMM_1();

	leaq -1(RNBLKS), RNBLKS;

	/* Transform 0-3 + Load data part2. */
	R1(a, b, c, d, e, f, g, h, 0, 0, IW); LOAD_W_XMM_2();
	R1(d, a, b, c, h, e, f, g, 1, 1, IW);
	R1(c, d, a, b, g, h, e, f, 2, 2, IW);
	R1(b, c, d, a, f, g, h, e, 3, 3, IW); LOAD_W_XMM_3();

	/* Transform 4-7 + Precalc 12-14. */
	R1(a, b, c, d, e, f, g, h, 4, 0, IW);
	R1(d, a, b, c, h, e, f, g, 5, 1, IW);
	R1(c, d, a, b, g, h, e, f, 6, 2, IW); SCHED_W_0(12, W0, W1, W2, W3, W4, W5);
	R1(b, c, d, a, f, g, h, e, 7, 3, IW); SCHED_W_1(12, W0, W1, W2, W3, W4, W5);

	/* Transform 8-11 + Precalc 12-17. */
	R1(a, b, c, d, e, f, g, h, 8, 0, IW); SCHED_W_2(12, W0, W1, W2, W3, W4, W5);
	R1(d, a, b, c, h, e, f, g, 9, 1, IW); SCHED_W_0(15, W1, W2, W3, W4, W5, W0);
	R1(c, d, a, b, g, h, e, f, 10, 2, IW); SCHED_W_1(15, W1, W2, W3, W4, W5, W0);
	R1(b, c, d, a, f, g, h, e, 11, 3, IW); SCHED_W_2(15, W1, W2, W3, W4, W5, W0);

	/* Transform 12-14 + Precalc 18-20 */
	R1(a, b, c, d, e, f, g, h, 12, 0, XW); SCHED_W_0(18, W2, W3, W4, W5, W0, W1);
	R1(d, a, b, c, h, e, f, g, 13, 1, XW); SCHED_W_1(18, W2, W3, W4, W5, W0, W1);
	R1(c, d, a, b, g, h, e, f, 14, 2, XW); SCHED_W_2(18, W2, W3, W4, W5, W0, W1);

	/* Transform 15-17 + Precalc 21-23 */
	R1(b, c, d, a, f, g, h, e, 15, 0, XW); SCHED_W_0(21, W3, W4, W5, W0, W1, W2);
	R2(a, b, c, d, e, f, g, h, 16, 1, XW); SCHED_W_1(21, W3, W4, W5, W0, W1, W2);
	R2(d, a, b, c, h, e, f, g, 17, 2, XW); SCHED_W_2(21, W3, W4, W5, W0, W1, W2);

	/* Transform 18-20 + Precalc 24-26 */
	R2(c, d, a, b, g, h, e, f, 18, 0, XW); SCHED_W_0(24, W4, W5, W0, W1, W2, W3);
	R2(b, c, d, a, f, g, h, e, 19, 1, XW); SCHED_W_1(24, W4, W5, W0, W1, W2, W3);
	R2(a, b, c, d, e, f, g, h, 20, 2, XW); SCHED_W_2(24, W4, W5, W0, W1, W2, W3);

	/* Transform 21-23 + Precalc 27-29 */
	R2(d, a, b, c, h, e, f, g, 21, 0, XW); SCHED_W_0(27, W5, W0, W1, W2, W3, W4);
	R2(c, d, a, b, g, h, e, f, 22, 1, XW); SCHED_W_1(27, W5, W0, W1, W2, W3, W4);
	R2(b, c, d, a, f, g, h, e, 23, 2, XW); SCHED_W_2(27, W5, W0, W1, W2, W3, W4);

	/* Transform 24-26 + Precalc 30-32 */
	R2(a, b, c, d, e, f, g, h, 24, 0, XW); SCHED_W_0(30, W0, W1, W2, W3, W4, W5);
	R2(d, a, b, c, h, e, f, g, 25, 1, XW); SCHED_W_1(30, W0, W1, W2, W3, W4, W5);
	R2(c, d, a, b, g, h, e, f, 26, 2, XW); SCHED_W_2(30, W0, W1, W2, W3, W4, W5);

	/* Transform 27-29 + Precalc 33-35 */
	R2(b, c, d, a, f, g, h, e, 27, 0, XW); SCHED_W_0(33, W1, W2, W3, W4, W5, W0);
	R2(a, b, c, d, e, f, g, h, 28, 1, XW); SCHED_W_1(33, W1, W2, W3, W4, W5, W0);
	R2(d, a, b, c, h, e, f, g, 29, 2, XW); SCHED_W_2(33, W1, W2, W3, W4, W5, W0);

	/* Transform 30-32 + Precalc 36-38 */
	R2(c, d, a, b, g, h, e, f, 30, 0, XW); SCHED_W_0(36, W2, W3, W4, W5, W0, W1);
	R2(b, c, d, a, f, g, h, e, 31, 1, XW); SCHED_W_1(36, W2, W3, W4, W5, W0, W1);
	R2(a, b, c, d, e, f, g, h, 32, 2, XW); SCHED_W_2(36, W2, W3, W4, W5, W0, W1);

	/* Transform 33-35 + Precalc 39-41 */
	R2(d, a, b, c, h, e, f, g, 33, 0, XW); SCHED_W_0(39, W3, W4, W5, W0, W1, W2);
	R2(c, d, a, b, g, h, e, f, 34, 1, XW); SCHED_W_1(39, W3, W4, W5, W0, W1, W2);
	R2(b, c, d, a, f, g, h, e, 35, 2, XW); SCHED_W_2(39, W3, W4, W5, W0, W1, W2);

	/* Transform 36-38 + Precalc 42-44 */
	R2(a, b, c, d, e, f, g, h, 36, 0, XW); SCHED_W_0(42, W4, W5, W0, W1, W2, W3);
	R2(d, a, b, c, h, e, f, g, 37, 1, XW); SCHED_W_1(42, W4, W5, W0, W1, W2, W3);
	R2(c, d, a, b, g, h, e, f, 38, 2, XW); SCHED_W_2(42, W4, W5, W0, W1, W2, W3);

	/* Transform 39-41 + Precalc 45-47 */
	R2(b, c, d, a, f, g, h, e, 39, 0, XW); SCHED_W_0(45, W5, W0, W1, W2, W3, W4);
	R2(a, b, c, d, e, f, g, h, 40, 1, XW); SCHED_W_1(45, W5, W0, W1, W2, W3, W4);
	R2(d, a, b, c, h, e, f, g, 41, 2, XW); SCHED_W_2(45, W5, W0, W1, W2, W3, W4);

	/* Transform 42-44 + Precalc 48-50 */
	R2(c, d, a, b, g, h, e, f, 42, 0, XW); SCHED_W_0(48, W0, W1, W2, W3, W4, W5);
	R2(b, c, d, a, f, g, h, e, 43, 1, XW); SCHED_W_1(48, W0, W1, W2, W3, W4, W5);
	R2(a, b, c, d, e, f, g, h, 44, 2, XW); SCHED_W_2(48, W0, W1, W2, W3, W4, W5);

	/* Transform 45-47 + Precalc 51-53 */
	R2(d, a, b, c, h, e, f, g, 45, 0, XW); SCHED_W_0(51, W1, W2, W3, W4, W5, W0);
	R2(c, d, a, b, g, h, e, f, 46, 1, XW); SCHED_W_1(51, W1, W2, W3, W4, W5, W0);
	R2(b, c, d, a, f, g, h, e, 47, 2, XW); SCHED_W_2(51, W1, W2, W3, W4, W5, W0);

	/* Transform 48-50 + Precalc 54-56 */
	R2(a, b, c, d, e, f, g, h, 48, 0, XW); SCHED_W_0(54, W2, W3, W4, W5, W0, W1);
	R2(d, a, b, c, h, e, f, g, 49, 1, XW); SCHED_W_1(54, W2, W3, W4, W5, W0, W1);
	R2(c, d, a, b, g, h, e, f, 50, 2, XW); SCHED_W_2(54, W2, W3, W4, W5, W0, W1);

	/* Transform 51-53 + Precalc 57-59 */
	R2(b, c, d, a, f, g, h, e, 51, 0, XW); SCHED_W_0(57, W3, W4, W5, W0, W1, W2);
	R2(a, b, c, d, e, f, g, h, 52, 1, XW); SCHED_W_1(57, W3, W4, W5, W0, W1, W2);
	R2(d, a, b, c, h, e, f, g, 53, 2, XW); SCHED_W_2(57, W3, W4, W5, W0, W1, W2);

	/* Transform 54-56 + Precalc 60-62 */
	R2(c, d, a, b, g, h, e, f, 54, 0, XW); SCHED_W_0(60, W4, W5, W0, W1, W2, W3);
	R2(b, c, d, a, f, g, h, e, 55, 1, XW); SCHED_W_1(60, W4, W5, W0, W1, W2, W3);
	R2(a, b, c, d, e, f, g, h, 56, 2, XW); SCHED_W_2(60, W4, W5, W0, W1, W2, W3);

	/* Transform 57-59 + Precalc 63 */
	R2(d, a, b, c, h, e, f, g, 57, 0, XW); SCHED_W_0(63, W5, W0, W1, W2, W3, W4);
	R2(c, d, a, b, g, h, e, f, 58, 1, XW);
	R2(b, c, d, a, f, g, h, e, 59, 2, XW); SCHED_W_1(63, W5, W0, W1, W2, W3, W4);

	/* Transform 60-62 + Precalc 63 */
	R2(a, b, c, d, e, f, g, h, 60, 0, XW);
	R2(d, a, b, c, h, e, f, g, 61, 1, XW); SCHED_W_2(63, W5, W0, W1, W2, W3, W4);
	R2(c, d, a, b, g, h, e, f, 62, 2, XW);

	/* Transform 63 */
	R2(b, c, d, a, f, g, h, e, 63, 0, XW);

	/* Update the chaining variables. */
	xorl state_h0(RSTATE), a;
	xorl state_h1(RSTATE), b;
	xorl state_h2(RSTATE), c;
	xorl state_h3(RSTATE), d;
	movl a, state_h0(RSTATE);
	movl b, state_h1(RSTATE);
	movl c, state_h2(RSTATE);
	movl d, state_h3(RSTATE);
	xorl state_h4(RSTATE), e;
	xorl state_h5(RSTATE), f;
	xorl state_h6(RSTATE), g;
	xorl state_h7(RSTATE), h;
	movl e, state_h4(RSTATE);
	movl f, state_h5(RSTATE);
	movl g, state_h6(RSTATE);
	movl h, state_h7(RSTATE);

	cmpq $0, RNBLKS;
	jne .Loop;

	vzeroall;

	movq (STACK_REG_SAVE + 0 * 8)(%rsp), %rbx;
	movq (STACK_REG_SAVE + 1 * 8)(%rsp), %r15;
	movq (STACK_REG_SAVE + 2 * 8)(%rsp), %r14;
	movq (STACK_REG_SAVE + 3 * 8)(%rsp), %r13;
	movq (STACK_REG_SAVE + 4 * 8)(%rsp), %r12;

	vmovdqa %xmm0, IW_W1_ADDR(0, 0);
	vmovdqa %xmm0, IW_W1W2_ADDR(0, 0);
	vmovdqa %xmm0, IW_W1_ADDR(4, 0);
	vmovdqa %xmm0, IW_W1W2_ADDR(4, 0);
	vmovdqa %xmm0, IW_W1_ADDR(8, 0);
	vmovdqa %xmm0, IW_W1W2_ADDR(8, 0);

	movq %rbp, %rsp;
	popq %rbp;
	RET;
SYM_FUNC_END(sm3_transform_avx)
