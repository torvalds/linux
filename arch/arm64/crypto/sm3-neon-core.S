// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sm3-neon-core.S - SM3 secure hash using NEON instructions
 *
 * Linux/arm64 port of the libgcrypt SM3 implementation for AArch64
 *
 * Copyright (C) 2021 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 * Copyright (c) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>

/* Context structure */

#define state_h0 0
#define state_h1 4
#define state_h2 8
#define state_h3 12
#define state_h4 16
#define state_h5 20
#define state_h6 24
#define state_h7 28

/* Stack structure */

#define STACK_W_SIZE        (32 * 2 * 3)

#define STACK_W             (0)
#define STACK_SIZE          (STACK_W + STACK_W_SIZE)

/* Register macros */

#define RSTATE x0
#define RDATA  x1
#define RNBLKS x2
#define RKPTR  x28
#define RFRAME x29

#define ra w3
#define rb w4
#define rc w5
#define rd w6
#define re w7
#define rf w8
#define rg w9
#define rh w10

#define t0 w11
#define t1 w12
#define t2 w13
#define t3 w14
#define t4 w15
#define t5 w16
#define t6 w17

#define k_even w19
#define k_odd w20

#define addr0 x21
#define addr1 x22

#define s0 w23
#define s1 w24
#define s2 w25
#define s3 w26

#define W0 v0
#define W1 v1
#define W2 v2
#define W3 v3
#define W4 v4
#define W5 v5

#define XTMP0 v6
#define XTMP1 v7
#define XTMP2 v16
#define XTMP3 v17
#define XTMP4 v18
#define XTMP5 v19
#define XTMP6 v20

/* Helper macros. */

#define _(...) /*_*/

#define clear_vec(x) \
	movi	x.8h, #0;

#define rolw(o, a, n) \
	ror	o, a, #(32 - n);

/* Round function macros. */

#define GG1_1(x, y, z, o, t) \
	eor	o, x, y;
#define GG1_2(x, y, z, o, t) \
	eor	o, o, z;
#define GG1_3(x, y, z, o, t)

#define FF1_1(x, y, z, o, t) GG1_1(x, y, z, o, t)
#define FF1_2(x, y, z, o, t)
#define FF1_3(x, y, z, o, t) GG1_2(x, y, z, o, t)

#define GG2_1(x, y, z, o, t) \
	bic	o, z, x;
#define GG2_2(x, y, z, o, t) \
	and	t, y, x;
#define GG2_3(x, y, z, o, t) \
	eor	o, o, t;

#define FF2_1(x, y, z, o, t) \
	eor	o, x, y;
#define FF2_2(x, y, z, o, t) \
	and	t, x, y; \
	and	o, o, z;
#define FF2_3(x, y, z, o, t) \
	eor	o, o, t;

#define R(i, a, b, c, d, e, f, g, h, k, K_LOAD, round, widx, wtype, IOP, iop_param) \
	K_LOAD(round);                                                        \
	ldr	t5, [sp, #(wtype##_W1_ADDR(round, widx))];                    \
	rolw(t0, a, 12);                              /* rol(a, 12) => t0 */  \
      IOP(1, iop_param);                                                      \
	FF##i##_1(a, b, c, t1, t2);                                           \
	ldr	t6, [sp, #(wtype##_W1W2_ADDR(round, widx))];                  \
	add	k, k, e;                                                      \
      IOP(2, iop_param);                                                      \
	GG##i##_1(e, f, g, t3, t4);                                           \
	FF##i##_2(a, b, c, t1, t2);                                           \
      IOP(3, iop_param);                                                      \
	add	k, k, t0;                                                     \
	add	h, h, t5;                                                     \
	add	d, d, t6;                     /* w1w2 + d => d */             \
      IOP(4, iop_param);                                                      \
	rolw(k, k, 7);                        /* rol (t0 + e + t), 7) => k */ \
	GG##i##_2(e, f, g, t3, t4);                                           \
	add	h, h, k;                      /* h + w1 + k => h */           \
      IOP(5, iop_param);                                                      \
	FF##i##_3(a, b, c, t1, t2);                                           \
	eor	t0, t0, k;                    /* k ^ t0 => t0 */              \
	GG##i##_3(e, f, g, t3, t4);                                           \
	add	d, d, t1;                     /* FF(a,b,c) + d => d */        \
      IOP(6, iop_param);                                                      \
	add	t3, t3, h;                    /* GG(e,f,g) + h => t3 */       \
	rolw(b, b, 9);                        /* rol(b, 9) => b */            \
	eor	h, t3, t3, ror #(32-9);                                       \
      IOP(7, iop_param);                                                      \
	add	d, d, t0;                     /* t0 + d => d */               \
	rolw(f, f, 19);                       /* rol(f, 19) => f */           \
      IOP(8, iop_param);                                                      \
	eor	h, h, t3, ror #(32-17);       /* P0(t3) => h */

#define R1(a, b, c, d, e, f, g, h, k, K_LOAD, round, widx, wtype, IOP, iop_param) \
	R(1, ##a, ##b, ##c, ##d, ##e, ##f, ##g, ##h, ##k, K_LOAD, round, widx, wtype, IOP, iop_param)

#define R2(a, b, c, d, e, f, g, h, k, K_LOAD, round, widx, wtype, IOP, iop_param) \
	R(2, ##a, ##b, ##c, ##d, ##e, ##f, ##g, ##h, ##k, K_LOAD, round, widx, wtype, IOP, iop_param)

#define KL(round) \
	ldp	k_even, k_odd, [RKPTR, #(4*(round))];

/* Input expansion macros. */

/* Byte-swapped input address. */
#define IW_W_ADDR(round, widx, offs) \
	(STACK_W + ((round) / 4) * 64 + (offs) + ((widx) * 4))

/* Expanded input address. */
#define XW_W_ADDR(round, widx, offs) \
	(STACK_W + ((((round) / 3) - 4) % 2) * 64 + (offs) + ((widx) * 4))

/* Rounds 1-12, byte-swapped input block addresses. */
#define IW_W1_ADDR(round, widx)   IW_W_ADDR(round, widx, 32)
#define IW_W1W2_ADDR(round, widx) IW_W_ADDR(round, widx, 48)

/* Rounds 1-12, expanded input block addresses. */
#define XW_W1_ADDR(round, widx)   XW_W_ADDR(round, widx, 0)
#define XW_W1W2_ADDR(round, widx) XW_W_ADDR(round, widx, 16)

/* Input block loading.
 * Interleaving within round function needed for in-order CPUs. */
#define LOAD_W_VEC_1_1() \
	add	addr0, sp, #IW_W1_ADDR(0, 0);
#define LOAD_W_VEC_1_2() \
	add	addr1, sp, #IW_W1_ADDR(4, 0);
#define LOAD_W_VEC_1_3() \
	ld1	{W0.16b}, [RDATA], #16;
#define LOAD_W_VEC_1_4() \
	ld1	{W1.16b}, [RDATA], #16;
#define LOAD_W_VEC_1_5() \
	ld1	{W2.16b}, [RDATA], #16;
#define LOAD_W_VEC_1_6() \
	ld1	{W3.16b}, [RDATA], #16;
#define LOAD_W_VEC_1_7() \
	rev32	XTMP0.16b, W0.16b;
#define LOAD_W_VEC_1_8() \
	rev32	XTMP1.16b, W1.16b;
#define LOAD_W_VEC_2_1() \
	rev32	XTMP2.16b, W2.16b;
#define LOAD_W_VEC_2_2() \
	rev32	XTMP3.16b, W3.16b;
#define LOAD_W_VEC_2_3() \
	eor	XTMP4.16b, XTMP1.16b, XTMP0.16b;
#define LOAD_W_VEC_2_4() \
	eor	XTMP5.16b, XTMP2.16b, XTMP1.16b;
#define LOAD_W_VEC_2_5() \
	st1	{XTMP0.16b}, [addr0], #16;
#define LOAD_W_VEC_2_6() \
	st1	{XTMP4.16b}, [addr0]; \
	add	addr0, sp, #IW_W1_ADDR(8, 0);
#define LOAD_W_VEC_2_7() \
	eor	XTMP6.16b, XTMP3.16b, XTMP2.16b;
#define LOAD_W_VEC_2_8() \
	ext	W0.16b, XTMP0.16b, XTMP0.16b, #8;  /* W0: xx, w0, xx, xx */
#define LOAD_W_VEC_3_1() \
	mov	W2.16b, XTMP1.16b;                 /* W2: xx, w6, w5, w4 */
#define LOAD_W_VEC_3_2() \
	st1	{XTMP1.16b}, [addr1], #16;
#define LOAD_W_VEC_3_3() \
	st1	{XTMP5.16b}, [addr1]; \
	ext	W1.16b, XTMP0.16b, XTMP0.16b, #4;  /* W1: xx, w3, w2, w1 */
#define LOAD_W_VEC_3_4() \
	ext	W3.16b, XTMP1.16b, XTMP2.16b, #12; /* W3: xx, w9, w8, w7 */
#define LOAD_W_VEC_3_5() \
	ext	W4.16b, XTMP2.16b, XTMP3.16b, #8;  /* W4: xx, w12, w11, w10 */
#define LOAD_W_VEC_3_6() \
	st1	{XTMP2.16b}, [addr0], #16;
#define LOAD_W_VEC_3_7() \
	st1	{XTMP6.16b}, [addr0];
#define LOAD_W_VEC_3_8() \
	ext	W5.16b, XTMP3.16b, XTMP3.16b, #4;  /* W5: xx, w15, w14, w13 */

#define LOAD_W_VEC_1(iop_num, ...) \
	LOAD_W_VEC_1_##iop_num()
#define LOAD_W_VEC_2(iop_num, ...) \
	LOAD_W_VEC_2_##iop_num()
#define LOAD_W_VEC_3(iop_num, ...) \
	LOAD_W_VEC_3_##iop_num()

/* Message scheduling. Note: 3 words per vector register.
 * Interleaving within round function needed for in-order CPUs. */
#define SCHED_W_1_1(round, w0, w1, w2, w3, w4, w5) \
	/* Load (w[i - 16]) => XTMP0 */            \
	/* Load (w[i - 13]) => XTMP5 */            \
	ext	XTMP0.16b, w0.16b, w0.16b, #12;    /* XTMP0: w0, xx, xx, xx */
#define SCHED_W_1_2(round, w0, w1, w2, w3, w4, w5) \
	ext	XTMP5.16b, w1.16b, w1.16b, #12;
#define SCHED_W_1_3(round, w0, w1, w2, w3, w4, w5) \
	ext	XTMP0.16b, XTMP0.16b, w1.16b, #12; /* XTMP0: xx, w2, w1, w0 */
#define SCHED_W_1_4(round, w0, w1, w2, w3, w4, w5) \
	ext	XTMP5.16b, XTMP5.16b, w2.16b, #12;
#define SCHED_W_1_5(round, w0, w1, w2, w3, w4, w5) \
	/* w[i - 9] == w3 */                       \
	/* W3 ^ XTMP0 => XTMP0 */                  \
	eor	XTMP0.16b, XTMP0.16b, w3.16b;
#define SCHED_W_1_6(round, w0, w1, w2, w3, w4, w5) \
	/* w[i - 3] == w5 */                       \
	/* rol(XMM5, 15) ^ XTMP0 => XTMP0 */       \
	/* rol(XTMP5, 7) => XTMP1 */               \
	add	addr0, sp, #XW_W1_ADDR((round), 0); \
	shl	XTMP2.4s, w5.4s, #15;
#define SCHED_W_1_7(round, w0, w1, w2, w3, w4, w5) \
	shl	XTMP1.4s, XTMP5.4s, #7;
#define SCHED_W_1_8(round, w0, w1, w2, w3, w4, w5) \
	sri	XTMP2.4s, w5.4s, #(32-15);
#define SCHED_W_2_1(round, w0, w1, w2, w3, w4, w5) \
	sri	XTMP1.4s, XTMP5.4s, #(32-7);
#define SCHED_W_2_2(round, w0, w1, w2, w3, w4, w5) \
	eor	XTMP0.16b, XTMP0.16b, XTMP2.16b;
#define SCHED_W_2_3(round, w0, w1, w2, w3, w4, w5) \
	/* w[i - 6] == W4 */                       \
	/* W4 ^ XTMP1 => XTMP1 */                  \
	eor	XTMP1.16b, XTMP1.16b, w4.16b;
#define SCHED_W_2_4(round, w0, w1, w2, w3, w4, w5) \
	/* P1(XTMP0) ^ XTMP1 => W0 */              \
	shl	XTMP3.4s, XTMP0.4s, #15;
#define SCHED_W_2_5(round, w0, w1, w2, w3, w4, w5) \
	shl	XTMP4.4s, XTMP0.4s, #23;
#define SCHED_W_2_6(round, w0, w1, w2, w3, w4, w5) \
	eor	w0.16b, XTMP1.16b, XTMP0.16b;
#define SCHED_W_2_7(round, w0, w1, w2, w3, w4, w5) \
	sri	XTMP3.4s, XTMP0.4s, #(32-15);
#define SCHED_W_2_8(round, w0, w1, w2, w3, w4, w5) \
	sri	XTMP4.4s, XTMP0.4s, #(32-23);
#define SCHED_W_3_1(round, w0, w1, w2, w3, w4, w5) \
	eor	w0.16b, w0.16b, XTMP3.16b;
#define SCHED_W_3_2(round, w0, w1, w2, w3, w4, w5) \
	/* Load (w[i - 3]) => XTMP2 */             \
	ext	XTMP2.16b, w4.16b, w4.16b, #12;
#define SCHED_W_3_3(round, w0, w1, w2, w3, w4, w5) \
	eor	w0.16b, w0.16b, XTMP4.16b;
#define SCHED_W_3_4(round, w0, w1, w2, w3, w4, w5) \
	ext	XTMP2.16b, XTMP2.16b, w5.16b, #12;
#define SCHED_W_3_5(round, w0, w1, w2, w3, w4, w5) \
	/* W1 ^ W2 => XTMP3 */                     \
	eor	XTMP3.16b, XTMP2.16b, w0.16b;
#define SCHED_W_3_6(round, w0, w1, w2, w3, w4, w5)
#define SCHED_W_3_7(round, w0, w1, w2, w3, w4, w5) \
	st1	{XTMP2.16b-XTMP3.16b}, [addr0];
#define SCHED_W_3_8(round, w0, w1, w2, w3, w4, w5)

#define SCHED_W_W0W1W2W3W4W5_1(iop_num, round) \
	SCHED_W_1_##iop_num(round, W0, W1, W2, W3, W4, W5)
#define SCHED_W_W0W1W2W3W4W5_2(iop_num, round) \
	SCHED_W_2_##iop_num(round, W0, W1, W2, W3, W4, W5)
#define SCHED_W_W0W1W2W3W4W5_3(iop_num, round) \
	SCHED_W_3_##iop_num(round, W0, W1, W2, W3, W4, W5)

#define SCHED_W_W1W2W3W4W5W0_1(iop_num, round) \
	SCHED_W_1_##iop_num(round, W1, W2, W3, W4, W5, W0)
#define SCHED_W_W1W2W3W4W5W0_2(iop_num, round) \
	SCHED_W_2_##iop_num(round, W1, W2, W3, W4, W5, W0)
#define SCHED_W_W1W2W3W4W5W0_3(iop_num, round) \
	SCHED_W_3_##iop_num(round, W1, W2, W3, W4, W5, W0)

#define SCHED_W_W2W3W4W5W0W1_1(iop_num, round) \
	SCHED_W_1_##iop_num(round, W2, W3, W4, W5, W0, W1)
#define SCHED_W_W2W3W4W5W0W1_2(iop_num, round) \
	SCHED_W_2_##iop_num(round, W2, W3, W4, W5, W0, W1)
#define SCHED_W_W2W3W4W5W0W1_3(iop_num, round) \
	SCHED_W_3_##iop_num(round, W2, W3, W4, W5, W0, W1)

#define SCHED_W_W3W4W5W0W1W2_1(iop_num, round) \
	SCHED_W_1_##iop_num(round, W3, W4, W5, W0, W1, W2)
#define SCHED_W_W3W4W5W0W1W2_2(iop_num, round) \
	SCHED_W_2_##iop_num(round, W3, W4, W5, W0, W1, W2)
#define SCHED_W_W3W4W5W0W1W2_3(iop_num, round) \
	SCHED_W_3_##iop_num(round, W3, W4, W5, W0, W1, W2)

#define SCHED_W_W4W5W0W1W2W3_1(iop_num, round) \
	SCHED_W_1_##iop_num(round, W4, W5, W0, W1, W2, W3)
#define SCHED_W_W4W5W0W1W2W3_2(iop_num, round) \
	SCHED_W_2_##iop_num(round, W4, W5, W0, W1, W2, W3)
#define SCHED_W_W4W5W0W1W2W3_3(iop_num, round) \
	SCHED_W_3_##iop_num(round, W4, W5, W0, W1, W2, W3)

#define SCHED_W_W5W0W1W2W3W4_1(iop_num, round) \
	SCHED_W_1_##iop_num(round, W5, W0, W1, W2, W3, W4)
#define SCHED_W_W5W0W1W2W3W4_2(iop_num, round) \
	SCHED_W_2_##iop_num(round, W5, W0, W1, W2, W3, W4)
#define SCHED_W_W5W0W1W2W3W4_3(iop_num, round) \
	SCHED_W_3_##iop_num(round, W5, W0, W1, W2, W3, W4)


	/*
	 * Transform blocks*64 bytes (blocks*16 32-bit words) at 'src'.
	 *
	 * void sm3_neon_transform(struct sm3_state *sst, u8 const *src,
	 *                         int blocks)
	 */
	.text
.align 3
SYM_TYPED_FUNC_START(sm3_neon_transform)
	ldp		ra, rb, [RSTATE, #0]
	ldp		rc, rd, [RSTATE, #8]
	ldp		re, rf, [RSTATE, #16]
	ldp		rg, rh, [RSTATE, #24]

	stp		x28, x29, [sp, #-16]!
	stp		x19, x20, [sp, #-16]!
	stp		x21, x22, [sp, #-16]!
	stp		x23, x24, [sp, #-16]!
	stp		x25, x26, [sp, #-16]!
	mov		RFRAME, sp

	sub		addr0, sp, #STACK_SIZE
	adr_l		RKPTR, .LKtable
	and		sp, addr0, #(~63)

	/* Preload first block. */
	LOAD_W_VEC_1(1, 0)
	LOAD_W_VEC_1(2, 0)
	LOAD_W_VEC_1(3, 0)
	LOAD_W_VEC_1(4, 0)
	LOAD_W_VEC_1(5, 0)
	LOAD_W_VEC_1(6, 0)
	LOAD_W_VEC_1(7, 0)
	LOAD_W_VEC_1(8, 0)
	LOAD_W_VEC_2(1, 0)
	LOAD_W_VEC_2(2, 0)
	LOAD_W_VEC_2(3, 0)
	LOAD_W_VEC_2(4, 0)
	LOAD_W_VEC_2(5, 0)
	LOAD_W_VEC_2(6, 0)
	LOAD_W_VEC_2(7, 0)
	LOAD_W_VEC_2(8, 0)
	LOAD_W_VEC_3(1, 0)
	LOAD_W_VEC_3(2, 0)
	LOAD_W_VEC_3(3, 0)
	LOAD_W_VEC_3(4, 0)
	LOAD_W_VEC_3(5, 0)
	LOAD_W_VEC_3(6, 0)
	LOAD_W_VEC_3(7, 0)
	LOAD_W_VEC_3(8, 0)

.balign 16
.Loop:
	/* Transform 0-3 */
	R1(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 0, 0, IW, _, 0)
	R1(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  1, 1, IW, _, 0)
	R1(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 2, 2, IW, _, 0)
	R1(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  3, 3, IW, _, 0)

	/* Transform 4-7 + Precalc 12-14 */
	R1(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 4, 0, IW, _, 0)
	R1(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  5, 1, IW, _, 0)
	R1(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 6, 2, IW, SCHED_W_W0W1W2W3W4W5_1, 12)
	R1(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  7, 3, IW, SCHED_W_W0W1W2W3W4W5_2, 12)

	/* Transform 8-11 + Precalc 12-17 */
	R1(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 8, 0, IW, SCHED_W_W0W1W2W3W4W5_3, 12)
	R1(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  9, 1, IW, SCHED_W_W1W2W3W4W5W0_1, 15)
	R1(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 10, 2, IW, SCHED_W_W1W2W3W4W5W0_2, 15)
	R1(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  11, 3, IW, SCHED_W_W1W2W3W4W5W0_3, 15)

	/* Transform 12-14 + Precalc 18-20 */
	R1(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 12, 0, XW, SCHED_W_W2W3W4W5W0W1_1, 18)
	R1(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  13, 1, XW, SCHED_W_W2W3W4W5W0W1_2, 18)
	R1(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 14, 2, XW, SCHED_W_W2W3W4W5W0W1_3, 18)

	/* Transform 15-17 + Precalc 21-23 */
	R1(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  15, 0, XW, SCHED_W_W3W4W5W0W1W2_1, 21)
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 16, 1, XW, SCHED_W_W3W4W5W0W1W2_2, 21)
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  17, 2, XW, SCHED_W_W3W4W5W0W1W2_3, 21)

	/* Transform 18-20 + Precalc 24-26 */
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 18, 0, XW, SCHED_W_W4W5W0W1W2W3_1, 24)
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  19, 1, XW, SCHED_W_W4W5W0W1W2W3_2, 24)
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 20, 2, XW, SCHED_W_W4W5W0W1W2W3_3, 24)

	/* Transform 21-23 + Precalc 27-29 */
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  21, 0, XW, SCHED_W_W5W0W1W2W3W4_1, 27)
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 22, 1, XW, SCHED_W_W5W0W1W2W3W4_2, 27)
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  23, 2, XW, SCHED_W_W5W0W1W2W3W4_3, 27)

	/* Transform 24-26 + Precalc 30-32 */
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 24, 0, XW, SCHED_W_W0W1W2W3W4W5_1, 30)
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  25, 1, XW, SCHED_W_W0W1W2W3W4W5_2, 30)
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 26, 2, XW, SCHED_W_W0W1W2W3W4W5_3, 30)

	/* Transform 27-29 + Precalc 33-35 */
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  27, 0, XW, SCHED_W_W1W2W3W4W5W0_1, 33)
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 28, 1, XW, SCHED_W_W1W2W3W4W5W0_2, 33)
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  29, 2, XW, SCHED_W_W1W2W3W4W5W0_3, 33)

	/* Transform 30-32 + Precalc 36-38 */
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 30, 0, XW, SCHED_W_W2W3W4W5W0W1_1, 36)
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  31, 1, XW, SCHED_W_W2W3W4W5W0W1_2, 36)
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 32, 2, XW, SCHED_W_W2W3W4W5W0W1_3, 36)

	/* Transform 33-35 + Precalc 39-41 */
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  33, 0, XW, SCHED_W_W3W4W5W0W1W2_1, 39)
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 34, 1, XW, SCHED_W_W3W4W5W0W1W2_2, 39)
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  35, 2, XW, SCHED_W_W3W4W5W0W1W2_3, 39)

	/* Transform 36-38 + Precalc 42-44 */
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 36, 0, XW, SCHED_W_W4W5W0W1W2W3_1, 42)
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  37, 1, XW, SCHED_W_W4W5W0W1W2W3_2, 42)
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 38, 2, XW, SCHED_W_W4W5W0W1W2W3_3, 42)

	/* Transform 39-41 + Precalc 45-47 */
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  39, 0, XW, SCHED_W_W5W0W1W2W3W4_1, 45)
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 40, 1, XW, SCHED_W_W5W0W1W2W3W4_2, 45)
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  41, 2, XW, SCHED_W_W5W0W1W2W3W4_3, 45)

	/* Transform 42-44 + Precalc 48-50 */
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 42, 0, XW, SCHED_W_W0W1W2W3W4W5_1, 48)
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  43, 1, XW, SCHED_W_W0W1W2W3W4W5_2, 48)
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 44, 2, XW, SCHED_W_W0W1W2W3W4W5_3, 48)

	/* Transform 45-47 + Precalc 51-53 */
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  45, 0, XW, SCHED_W_W1W2W3W4W5W0_1, 51)
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 46, 1, XW, SCHED_W_W1W2W3W4W5W0_2, 51)
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  47, 2, XW, SCHED_W_W1W2W3W4W5W0_3, 51)

	/* Transform 48-50 + Precalc 54-56 */
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 48, 0, XW, SCHED_W_W2W3W4W5W0W1_1, 54)
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  49, 1, XW, SCHED_W_W2W3W4W5W0W1_2, 54)
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 50, 2, XW, SCHED_W_W2W3W4W5W0W1_3, 54)

	/* Transform 51-53 + Precalc 57-59 */
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  51, 0, XW, SCHED_W_W3W4W5W0W1W2_1, 57)
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 52, 1, XW, SCHED_W_W3W4W5W0W1W2_2, 57)
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  53, 2, XW, SCHED_W_W3W4W5W0W1W2_3, 57)

	/* Transform 54-56 + Precalc 60-62 */
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 54, 0, XW, SCHED_W_W4W5W0W1W2W3_1, 60)
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  55, 1, XW, SCHED_W_W4W5W0W1W2W3_2, 60)
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 56, 2, XW, SCHED_W_W4W5W0W1W2W3_3, 60)

	/* Transform 57-59 + Precalc 63 */
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  57, 0, XW, SCHED_W_W5W0W1W2W3W4_1, 63)
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 58, 1, XW, SCHED_W_W5W0W1W2W3W4_2, 63)
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  59, 2, XW, SCHED_W_W5W0W1W2W3W4_3, 63)

	/* Transform 60 */
	R2(ra, rb, rc, rd, re, rf, rg, rh, k_even, KL, 60, 0, XW, _, _)
	subs		RNBLKS, RNBLKS, #1
	b.eq		.Lend

	/* Transform 61-63 + Preload next block */
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  61, 1, XW, LOAD_W_VEC_1, _)
	ldp		s0, s1, [RSTATE, #0]
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 62, 2, XW, LOAD_W_VEC_2, _)
	ldp		s2, s3, [RSTATE, #8]
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  63, 0, XW, LOAD_W_VEC_3, _)

	/* Update the chaining variables. */
	eor		ra, ra, s0
	eor		rb, rb, s1
	ldp		s0, s1, [RSTATE, #16]
	eor		rc, rc, s2
	ldp		k_even, k_odd, [RSTATE, #24]
	eor		rd, rd, s3
	eor		re, re, s0
	stp		ra, rb, [RSTATE, #0]
	eor		rf, rf, s1
	stp		rc, rd, [RSTATE, #8]
	eor		rg, rg, k_even
	stp		re, rf, [RSTATE, #16]
	eor		rh, rh, k_odd
	stp		rg, rh, [RSTATE, #24]
	b		.Loop

.Lend:
	/* Transform 61-63 */
	R2(rd, ra, rb, rc, rh, re, rf, rg, k_odd,  _,  61, 1, XW, _, _)
	ldp		s0, s1, [RSTATE, #0]
	R2(rc, rd, ra, rb, rg, rh, re, rf, k_even, KL, 62, 2, XW, _, _)
	ldp		s2, s3, [RSTATE, #8]
	R2(rb, rc, rd, ra, rf, rg, rh, re, k_odd,  _,  63, 0, XW, _, _)

	/* Update the chaining variables. */
	eor		ra, ra, s0
	clear_vec(W0)
	eor		rb, rb, s1
	clear_vec(W1)
	ldp		s0, s1, [RSTATE, #16]
	clear_vec(W2)
	eor		rc, rc, s2
	clear_vec(W3)
	ldp		k_even, k_odd, [RSTATE, #24]
	clear_vec(W4)
	eor		rd, rd, s3
	clear_vec(W5)
	eor		re, re, s0
	clear_vec(XTMP0)
	stp		ra, rb, [RSTATE, #0]
	clear_vec(XTMP1)
	eor		rf, rf, s1
	clear_vec(XTMP2)
	stp		rc, rd, [RSTATE, #8]
	clear_vec(XTMP3)
	eor		rg, rg, k_even
	clear_vec(XTMP4)
	stp		re, rf, [RSTATE, #16]
	clear_vec(XTMP5)
	eor		rh, rh, k_odd
	clear_vec(XTMP6)
	stp		rg, rh, [RSTATE, #24]

	/* Clear message expansion area */
	add		addr0, sp, #STACK_W
	st1		{W0.16b-W3.16b}, [addr0], #64
	st1		{W0.16b-W3.16b}, [addr0], #64
	st1		{W0.16b-W3.16b}, [addr0]

	mov		sp, RFRAME

	ldp		x25, x26, [sp], #16
	ldp		x23, x24, [sp], #16
	ldp		x21, x22, [sp], #16
	ldp		x19, x20, [sp], #16
	ldp		x28, x29, [sp], #16

	ret
SYM_FUNC_END(sm3_neon_transform)


	.section	".rodata", "a"

	.align 4
.LKtable:
	.long 0x79cc4519, 0xf3988a32, 0xe7311465, 0xce6228cb
	.long 0x9cc45197, 0x3988a32f, 0x7311465e, 0xe6228cbc
	.long 0xcc451979, 0x988a32f3, 0x311465e7, 0x6228cbce
	.long 0xc451979c, 0x88a32f39, 0x11465e73, 0x228cbce6
	.long 0x9d8a7a87, 0x3b14f50f, 0x7629ea1e, 0xec53d43c
	.long 0xd8a7a879, 0xb14f50f3, 0x629ea1e7, 0xc53d43ce
	.long 0x8a7a879d, 0x14f50f3b, 0x29ea1e76, 0x53d43cec
	.long 0xa7a879d8, 0x4f50f3b1, 0x9ea1e762, 0x3d43cec5
	.long 0x7a879d8a, 0xf50f3b14, 0xea1e7629, 0xd43cec53
	.long 0xa879d8a7, 0x50f3b14f, 0xa1e7629e, 0x43cec53d
	.long 0x879d8a7a, 0x0f3b14f5, 0x1e7629ea, 0x3cec53d4
	.long 0x79d8a7a8, 0xf3b14f50, 0xe7629ea1, 0xcec53d43
	.long 0x9d8a7a87, 0x3b14f50f, 0x7629ea1e, 0xec53d43c
	.long 0xd8a7a879, 0xb14f50f3, 0x629ea1e7, 0xc53d43ce
	.long 0x8a7a879d, 0x14f50f3b, 0x29ea1e76, 0x53d43cec
	.long 0xa7a879d8, 0x4f50f3b1, 0x9ea1e762, 0x3d43cec5
