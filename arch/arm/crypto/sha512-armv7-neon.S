/* sha512-armv7-neon.S  -  ARM/NEON assembly implementation of SHA-512 transform
 *
 * Copyright © 2013-2014 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/linkage.h>


.syntax unified
.code   32
.fpu neon

.text

/* structure of SHA512_CONTEXT */
#define hd_a 0
#define hd_b ((hd_a) + 8)
#define hd_c ((hd_b) + 8)
#define hd_d ((hd_c) + 8)
#define hd_e ((hd_d) + 8)
#define hd_f ((hd_e) + 8)
#define hd_g ((hd_f) + 8)

/* register macros */
#define RK %r2

#define RA d0
#define RB d1
#define RC d2
#define RD d3
#define RE d4
#define RF d5
#define RG d6
#define RH d7

#define RT0 d8
#define RT1 d9
#define RT2 d10
#define RT3 d11
#define RT4 d12
#define RT5 d13
#define RT6 d14
#define RT7 d15

#define RT01q q4
#define RT23q q5
#define RT45q q6
#define RT67q q7

#define RW0 d16
#define RW1 d17
#define RW2 d18
#define RW3 d19
#define RW4 d20
#define RW5 d21
#define RW6 d22
#define RW7 d23
#define RW8 d24
#define RW9 d25
#define RW10 d26
#define RW11 d27
#define RW12 d28
#define RW13 d29
#define RW14 d30
#define RW15 d31

#define RW01q q8
#define RW23q q9
#define RW45q q10
#define RW67q q11
#define RW89q q12
#define RW1011q q13
#define RW1213q q14
#define RW1415q q15

/***********************************************************************
 * ARM assembly implementation of sha512 transform
 ***********************************************************************/
#define rounds2_0_63(ra, rb, rc, rd, re, rf, rg, rh, rw0, rw1, rw01q, rw2, \
                     rw23q, rw1415q, rw9, rw10, interleave_op, arg1) \
	/* t1 = h + Sum1 (e) + Ch (e, f, g) + k[t] + w[t]; */ \
	vshr.u64 RT2, re, #14; \
	vshl.u64 RT3, re, #64 - 14; \
	interleave_op(arg1); \
	vshr.u64 RT4, re, #18; \
	vshl.u64 RT5, re, #64 - 18; \
	vld1.64 {RT0}, [RK]!; \
	veor.64 RT23q, RT23q, RT45q; \
	vshr.u64 RT4, re, #41; \
	vshl.u64 RT5, re, #64 - 41; \
	vadd.u64 RT0, RT0, rw0; \
	veor.64 RT23q, RT23q, RT45q; \
	vmov.64 RT7, re; \
	veor.64 RT1, RT2, RT3; \
	vbsl.64 RT7, rf, rg; \
	\
	vadd.u64 RT1, RT1, rh; \
	vshr.u64 RT2, ra, #28; \
	vshl.u64 RT3, ra, #64 - 28; \
	vadd.u64 RT1, RT1, RT0; \
	vshr.u64 RT4, ra, #34; \
	vshl.u64 RT5, ra, #64 - 34; \
	vadd.u64 RT1, RT1, RT7; \
	\
	/* h = Sum0 (a) + Maj (a, b, c); */ \
	veor.64 RT23q, RT23q, RT45q; \
	vshr.u64 RT4, ra, #39; \
	vshl.u64 RT5, ra, #64 - 39; \
	veor.64 RT0, ra, rb; \
	veor.64 RT23q, RT23q, RT45q; \
	vbsl.64 RT0, rc, rb; \
	vadd.u64 rd, rd, RT1; /* d+=t1; */ \
	veor.64 rh, RT2, RT3; \
	\
	/* t1 = g + Sum1 (d) + Ch (d, e, f) + k[t] + w[t]; */ \
	vshr.u64 RT2, rd, #14; \
	vshl.u64 RT3, rd, #64 - 14; \
	vadd.u64 rh, rh, RT0; \
	vshr.u64 RT4, rd, #18; \
	vshl.u64 RT5, rd, #64 - 18; \
	vadd.u64 rh, rh, RT1; /* h+=t1; */ \
	vld1.64 {RT0}, [RK]!; \
	veor.64 RT23q, RT23q, RT45q; \
	vshr.u64 RT4, rd, #41; \
	vshl.u64 RT5, rd, #64 - 41; \
	vadd.u64 RT0, RT0, rw1; \
	veor.64 RT23q, RT23q, RT45q; \
	vmov.64 RT7, rd; \
	veor.64 RT1, RT2, RT3; \
	vbsl.64 RT7, re, rf; \
	\
	vadd.u64 RT1, RT1, rg; \
	vshr.u64 RT2, rh, #28; \
	vshl.u64 RT3, rh, #64 - 28; \
	vadd.u64 RT1, RT1, RT0; \
	vshr.u64 RT4, rh, #34; \
	vshl.u64 RT5, rh, #64 - 34; \
	vadd.u64 RT1, RT1, RT7; \
	\
	/* g = Sum0 (h) + Maj (h, a, b); */ \
	veor.64 RT23q, RT23q, RT45q; \
	vshr.u64 RT4, rh, #39; \
	vshl.u64 RT5, rh, #64 - 39; \
	veor.64 RT0, rh, ra; \
	veor.64 RT23q, RT23q, RT45q; \
	vbsl.64 RT0, rb, ra; \
	vadd.u64 rc, rc, RT1; /* c+=t1; */ \
	veor.64 rg, RT2, RT3; \
	\
	/* w[0] += S1 (w[14]) + w[9] + S0 (w[1]); */ \
	/* w[1] += S1 (w[15]) + w[10] + S0 (w[2]); */ \
	\
	/**** S0(w[1:2]) */ \
	\
	/* w[0:1] += w[9:10] */ \
	/* RT23q = rw1:rw2 */ \
	vext.u64 RT23q, rw01q, rw23q, #1; \
	vadd.u64 rw0, rw9; \
	vadd.u64 rg, rg, RT0; \
	vadd.u64 rw1, rw10;\
	vadd.u64 rg, rg, RT1; /* g+=t1; */ \
	\
	vshr.u64 RT45q, RT23q, #1; \
	vshl.u64 RT67q, RT23q, #64 - 1; \
	vshr.u64 RT01q, RT23q, #8; \
	veor.u64 RT45q, RT45q, RT67q; \
	vshl.u64 RT67q, RT23q, #64 - 8; \
	veor.u64 RT45q, RT45q, RT01q; \
	vshr.u64 RT01q, RT23q, #7; \
	veor.u64 RT45q, RT45q, RT67q; \
	\
	/**** S1(w[14:15]) */ \
	vshr.u64 RT23q, rw1415q, #6; \
	veor.u64 RT01q, RT01q, RT45q; \
	vshr.u64 RT45q, rw1415q, #19; \
	vshl.u64 RT67q, rw1415q, #64 - 19; \
	veor.u64 RT23q, RT23q, RT45q; \
	vshr.u64 RT45q, rw1415q, #61; \
	veor.u64 RT23q, RT23q, RT67q; \
	vshl.u64 RT67q, rw1415q, #64 - 61; \
	veor.u64 RT23q, RT23q, RT45q; \
	vadd.u64 rw01q, RT01q; /* w[0:1] += S(w[1:2]) */ \
	veor.u64 RT01q, RT23q, RT67q;
#define vadd_RT01q(rw01q) \
	/* w[0:1] += S(w[14:15]) */ \
	vadd.u64 rw01q, RT01q;

#define dummy(_) /*_*/

#define rounds2_64_79(ra, rb, rc, rd, re, rf, rg, rh, rw0, rw1, \
	              interleave_op1, arg1, interleave_op2, arg2) \
	/* t1 = h + Sum1 (e) + Ch (e, f, g) + k[t] + w[t]; */ \
	vshr.u64 RT2, re, #14; \
	vshl.u64 RT3, re, #64 - 14; \
	interleave_op1(arg1); \
	vshr.u64 RT4, re, #18; \
	vshl.u64 RT5, re, #64 - 18; \
	interleave_op2(arg2); \
	vld1.64 {RT0}, [RK]!; \
	veor.64 RT23q, RT23q, RT45q; \
	vshr.u64 RT4, re, #41; \
	vshl.u64 RT5, re, #64 - 41; \
	vadd.u64 RT0, RT0, rw0; \
	veor.64 RT23q, RT23q, RT45q; \
	vmov.64 RT7, re; \
	veor.64 RT1, RT2, RT3; \
	vbsl.64 RT7, rf, rg; \
	\
	vadd.u64 RT1, RT1, rh; \
	vshr.u64 RT2, ra, #28; \
	vshl.u64 RT3, ra, #64 - 28; \
	vadd.u64 RT1, RT1, RT0; \
	vshr.u64 RT4, ra, #34; \
	vshl.u64 RT5, ra, #64 - 34; \
	vadd.u64 RT1, RT1, RT7; \
	\
	/* h = Sum0 (a) + Maj (a, b, c); */ \
	veor.64 RT23q, RT23q, RT45q; \
	vshr.u64 RT4, ra, #39; \
	vshl.u64 RT5, ra, #64 - 39; \
	veor.64 RT0, ra, rb; \
	veor.64 RT23q, RT23q, RT45q; \
	vbsl.64 RT0, rc, rb; \
	vadd.u64 rd, rd, RT1; /* d+=t1; */ \
	veor.64 rh, RT2, RT3; \
	\
	/* t1 = g + Sum1 (d) + Ch (d, e, f) + k[t] + w[t]; */ \
	vshr.u64 RT2, rd, #14; \
	vshl.u64 RT3, rd, #64 - 14; \
	vadd.u64 rh, rh, RT0; \
	vshr.u64 RT4, rd, #18; \
	vshl.u64 RT5, rd, #64 - 18; \
	vadd.u64 rh, rh, RT1; /* h+=t1; */ \
	vld1.64 {RT0}, [RK]!; \
	veor.64 RT23q, RT23q, RT45q; \
	vshr.u64 RT4, rd, #41; \
	vshl.u64 RT5, rd, #64 - 41; \
	vadd.u64 RT0, RT0, rw1; \
	veor.64 RT23q, RT23q, RT45q; \
	vmov.64 RT7, rd; \
	veor.64 RT1, RT2, RT3; \
	vbsl.64 RT7, re, rf; \
	\
	vadd.u64 RT1, RT1, rg; \
	vshr.u64 RT2, rh, #28; \
	vshl.u64 RT3, rh, #64 - 28; \
	vadd.u64 RT1, RT1, RT0; \
	vshr.u64 RT4, rh, #34; \
	vshl.u64 RT5, rh, #64 - 34; \
	vadd.u64 RT1, RT1, RT7; \
	\
	/* g = Sum0 (h) + Maj (h, a, b); */ \
	veor.64 RT23q, RT23q, RT45q; \
	vshr.u64 RT4, rh, #39; \
	vshl.u64 RT5, rh, #64 - 39; \
	veor.64 RT0, rh, ra; \
	veor.64 RT23q, RT23q, RT45q; \
	vbsl.64 RT0, rb, ra; \
	vadd.u64 rc, rc, RT1; /* c+=t1; */ \
	veor.64 rg, RT2, RT3;
#define vadd_rg_RT0(rg) \
	vadd.u64 rg, rg, RT0;
#define vadd_rg_RT1(rg) \
	vadd.u64 rg, rg, RT1; /* g+=t1; */

.align 3
ENTRY(sha512_transform_neon)
	/* Input:
	 *	%r0: SHA512_CONTEXT
	 *	%r1: data
	 *	%r2: u64 k[] constants
	 *	%r3: nblks
	 */
	push {%lr};

	mov %lr, #0;

	/* Load context to d0-d7 */
	vld1.64 {RA-RD}, [%r0]!;
	vld1.64 {RE-RH}, [%r0];
	sub %r0, #(4*8);

	/* Load input to w[16], d16-d31 */
	/* NOTE: Assumes that on ARMv7 unaligned accesses are always allowed. */
	vld1.64 {RW0-RW3}, [%r1]!;
	vld1.64 {RW4-RW7}, [%r1]!;
	vld1.64 {RW8-RW11}, [%r1]!;
	vld1.64 {RW12-RW15}, [%r1]!;
#ifdef __ARMEL__
	/* byteswap */
	vrev64.8 RW01q, RW01q;
	vrev64.8 RW23q, RW23q;
	vrev64.8 RW45q, RW45q;
	vrev64.8 RW67q, RW67q;
	vrev64.8 RW89q, RW89q;
	vrev64.8 RW1011q, RW1011q;
	vrev64.8 RW1213q, RW1213q;
	vrev64.8 RW1415q, RW1415q;
#endif

	/* EABI says that d8-d15 must be preserved by callee. */
	/*vpush {RT0-RT7};*/

.Loop:
	rounds2_0_63(RA, RB, RC, RD, RE, RF, RG, RH, RW0, RW1, RW01q, RW2,
		     RW23q, RW1415q, RW9, RW10, dummy, _);
	b .Lenter_rounds;

.Loop_rounds:
	rounds2_0_63(RA, RB, RC, RD, RE, RF, RG, RH, RW0, RW1, RW01q, RW2,
		     RW23q, RW1415q, RW9, RW10, vadd_RT01q, RW1415q);
.Lenter_rounds:
	rounds2_0_63(RG, RH, RA, RB, RC, RD, RE, RF, RW2, RW3, RW23q, RW4,
		     RW45q, RW01q, RW11, RW12, vadd_RT01q, RW01q);
	rounds2_0_63(RE, RF, RG, RH, RA, RB, RC, RD, RW4, RW5, RW45q, RW6,
		     RW67q, RW23q, RW13, RW14, vadd_RT01q, RW23q);
	rounds2_0_63(RC, RD, RE, RF, RG, RH, RA, RB, RW6, RW7, RW67q, RW8,
		     RW89q, RW45q, RW15, RW0, vadd_RT01q, RW45q);
	rounds2_0_63(RA, RB, RC, RD, RE, RF, RG, RH, RW8, RW9, RW89q, RW10,
		     RW1011q, RW67q, RW1, RW2, vadd_RT01q, RW67q);
	rounds2_0_63(RG, RH, RA, RB, RC, RD, RE, RF, RW10, RW11, RW1011q, RW12,
		     RW1213q, RW89q, RW3, RW4, vadd_RT01q, RW89q);
	add %lr, #16;
	rounds2_0_63(RE, RF, RG, RH, RA, RB, RC, RD, RW12, RW13, RW1213q, RW14,
		     RW1415q, RW1011q, RW5, RW6, vadd_RT01q, RW1011q);
	cmp %lr, #64;
	rounds2_0_63(RC, RD, RE, RF, RG, RH, RA, RB, RW14, RW15, RW1415q, RW0,
		     RW01q, RW1213q, RW7, RW8, vadd_RT01q, RW1213q);
	bne .Loop_rounds;

	subs %r3, #1;

	rounds2_64_79(RA, RB, RC, RD, RE, RF, RG, RH, RW0, RW1,
		      vadd_RT01q, RW1415q, dummy, _);
	rounds2_64_79(RG, RH, RA, RB, RC, RD, RE, RF, RW2, RW3,
		      vadd_rg_RT0, RG, vadd_rg_RT1, RG);
	beq .Lhandle_tail;
	vld1.64 {RW0-RW3}, [%r1]!;
	rounds2_64_79(RE, RF, RG, RH, RA, RB, RC, RD, RW4, RW5,
		      vadd_rg_RT0, RE, vadd_rg_RT1, RE);
	rounds2_64_79(RC, RD, RE, RF, RG, RH, RA, RB, RW6, RW7,
		      vadd_rg_RT0, RC, vadd_rg_RT1, RC);
#ifdef __ARMEL__
	vrev64.8 RW01q, RW01q;
	vrev64.8 RW23q, RW23q;
#endif
	vld1.64 {RW4-RW7}, [%r1]!;
	rounds2_64_79(RA, RB, RC, RD, RE, RF, RG, RH, RW8, RW9,
		      vadd_rg_RT0, RA, vadd_rg_RT1, RA);
	rounds2_64_79(RG, RH, RA, RB, RC, RD, RE, RF, RW10, RW11,
		      vadd_rg_RT0, RG, vadd_rg_RT1, RG);
#ifdef __ARMEL__
	vrev64.8 RW45q, RW45q;
	vrev64.8 RW67q, RW67q;
#endif
	vld1.64 {RW8-RW11}, [%r1]!;
	rounds2_64_79(RE, RF, RG, RH, RA, RB, RC, RD, RW12, RW13,
		      vadd_rg_RT0, RE, vadd_rg_RT1, RE);
	rounds2_64_79(RC, RD, RE, RF, RG, RH, RA, RB, RW14, RW15,
		      vadd_rg_RT0, RC, vadd_rg_RT1, RC);
#ifdef __ARMEL__
	vrev64.8 RW89q, RW89q;
	vrev64.8 RW1011q, RW1011q;
#endif
	vld1.64 {RW12-RW15}, [%r1]!;
	vadd_rg_RT0(RA);
	vadd_rg_RT1(RA);

	/* Load context */
	vld1.64 {RT0-RT3}, [%r0]!;
	vld1.64 {RT4-RT7}, [%r0];
	sub %r0, #(4*8);

#ifdef __ARMEL__
	vrev64.8 RW1213q, RW1213q;
	vrev64.8 RW1415q, RW1415q;
#endif

	vadd.u64 RA, RT0;
	vadd.u64 RB, RT1;
	vadd.u64 RC, RT2;
	vadd.u64 RD, RT3;
	vadd.u64 RE, RT4;
	vadd.u64 RF, RT5;
	vadd.u64 RG, RT6;
	vadd.u64 RH, RT7;

	/* Store the first half of context */
	vst1.64 {RA-RD}, [%r0]!;
	sub RK, $(8*80);
	vst1.64 {RE-RH}, [%r0]; /* Store the last half of context */
	mov %lr, #0;
	sub %r0, #(4*8);

	b .Loop;

.Lhandle_tail:
	rounds2_64_79(RE, RF, RG, RH, RA, RB, RC, RD, RW4, RW5,
		      vadd_rg_RT0, RE, vadd_rg_RT1, RE);
	rounds2_64_79(RC, RD, RE, RF, RG, RH, RA, RB, RW6, RW7,
		      vadd_rg_RT0, RC, vadd_rg_RT1, RC);
	rounds2_64_79(RA, RB, RC, RD, RE, RF, RG, RH, RW8, RW9,
		      vadd_rg_RT0, RA, vadd_rg_RT1, RA);
	rounds2_64_79(RG, RH, RA, RB, RC, RD, RE, RF, RW10, RW11,
		      vadd_rg_RT0, RG, vadd_rg_RT1, RG);
	rounds2_64_79(RE, RF, RG, RH, RA, RB, RC, RD, RW12, RW13,
		      vadd_rg_RT0, RE, vadd_rg_RT1, RE);
	rounds2_64_79(RC, RD, RE, RF, RG, RH, RA, RB, RW14, RW15,
		      vadd_rg_RT0, RC, vadd_rg_RT1, RC);

	/* Load context to d16-d23 */
	vld1.64 {RW0-RW3}, [%r0]!;
	vadd_rg_RT0(RA);
	vld1.64 {RW4-RW7}, [%r0];
	vadd_rg_RT1(RA);
	sub %r0, #(4*8);

	vadd.u64 RA, RW0;
	vadd.u64 RB, RW1;
	vadd.u64 RC, RW2;
	vadd.u64 RD, RW3;
	vadd.u64 RE, RW4;
	vadd.u64 RF, RW5;
	vadd.u64 RG, RW6;
	vadd.u64 RH, RW7;

	/* Store the first half of context */
	vst1.64 {RA-RD}, [%r0]!;

	/* Clear used registers */
	/* d16-d31 */
	veor.u64 RW01q, RW01q;
	veor.u64 RW23q, RW23q;
	veor.u64 RW45q, RW45q;
	veor.u64 RW67q, RW67q;
	vst1.64 {RE-RH}, [%r0]; /* Store the last half of context */
	veor.u64 RW89q, RW89q;
	veor.u64 RW1011q, RW1011q;
	veor.u64 RW1213q, RW1213q;
	veor.u64 RW1415q, RW1415q;
	/* d8-d15 */
	/*vpop {RT0-RT7};*/
	/* d0-d7 (q0-q3) */
	veor.u64 %q0, %q0;
	veor.u64 %q1, %q1;
	veor.u64 %q2, %q2;
	veor.u64 %q3, %q3;

	pop {%pc};
ENDPROC(sha512_transform_neon)
