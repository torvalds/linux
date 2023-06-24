/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Implementation of POLYVAL using ARMv8 Crypto Extensions.
 *
 * Copyright 2021 Google LLC
 */
/*
 * This is an efficient implementation of POLYVAL using ARMv8 Crypto Extensions
 * It works on 8 blocks at a time, by precomputing the first 8 keys powers h^8,
 * ..., h^1 in the POLYVAL finite field. This precomputation allows us to split
 * finite field multiplication into two steps.
 *
 * In the first step, we consider h^i, m_i as normal polynomials of degree less
 * than 128. We then compute p(x) = h^8m_0 + ... + h^1m_7 where multiplication
 * is simply polynomial multiplication.
 *
 * In the second step, we compute the reduction of p(x) modulo the finite field
 * modulus g(x) = x^128 + x^127 + x^126 + x^121 + 1.
 *
 * This two step process is equivalent to computing h^8m_0 + ... + h^1m_7 where
 * multiplication is finite field multiplication. The advantage is that the
 * two-step process  only requires 1 finite field reduction for every 8
 * polynomial multiplications. Further parallelism is gained by interleaving the
 * multiplications and polynomial reductions.
 */

#include <linux/linkage.h>
#define STRIDE_BLOCKS 8

KEY_POWERS	.req	x0
MSG		.req	x1
BLOCKS_LEFT	.req	x2
ACCUMULATOR	.req	x3
KEY_START	.req	x10
EXTRA_BYTES	.req	x11
TMP	.req	x13

M0	.req	v0
M1	.req	v1
M2	.req	v2
M3	.req	v3
M4	.req	v4
M5	.req	v5
M6	.req	v6
M7	.req	v7
KEY8	.req	v8
KEY7	.req	v9
KEY6	.req	v10
KEY5	.req	v11
KEY4	.req	v12
KEY3	.req	v13
KEY2	.req	v14
KEY1	.req	v15
PL	.req	v16
PH	.req	v17
TMP_V	.req	v18
LO	.req	v20
MI	.req	v21
HI	.req	v22
SUM	.req	v23
GSTAR	.req	v24

	.text

	.arch	armv8-a+crypto
	.align	4

.Lgstar:
	.quad	0xc200000000000000, 0xc200000000000000

/*
 * Computes the product of two 128-bit polynomials in X and Y and XORs the
 * components of the 256-bit product into LO, MI, HI.
 *
 * Given:
 *  X = [X_1 : X_0]
 *  Y = [Y_1 : Y_0]
 *
 * We compute:
 *  LO += X_0 * Y_0
 *  MI += (X_0 + X_1) * (Y_0 + Y_1)
 *  HI += X_1 * Y_1
 *
 * Later, the 256-bit result can be extracted as:
 *   [HI_1 : HI_0 + HI_1 + MI_1 + LO_1 : LO_1 + HI_0 + MI_0 + LO_0 : LO_0]
 * This step is done when computing the polynomial reduction for efficiency
 * reasons.
 *
 * Karatsuba multiplication is used instead of Schoolbook multiplication because
 * it was found to be slightly faster on ARM64 CPUs.
 *
 */
.macro karatsuba1 X Y
	X .req \X
	Y .req \Y
	ext	v25.16b, X.16b, X.16b, #8
	ext	v26.16b, Y.16b, Y.16b, #8
	eor	v25.16b, v25.16b, X.16b
	eor	v26.16b, v26.16b, Y.16b
	pmull2	v28.1q, X.2d, Y.2d
	pmull	v29.1q, X.1d, Y.1d
	pmull	v27.1q, v25.1d, v26.1d
	eor	HI.16b, HI.16b, v28.16b
	eor	LO.16b, LO.16b, v29.16b
	eor	MI.16b, MI.16b, v27.16b
	.unreq X
	.unreq Y
.endm

/*
 * Same as karatsuba1, except overwrites HI, LO, MI rather than XORing into
 * them.
 */
.macro karatsuba1_store X Y
	X .req \X
	Y .req \Y
	ext	v25.16b, X.16b, X.16b, #8
	ext	v26.16b, Y.16b, Y.16b, #8
	eor	v25.16b, v25.16b, X.16b
	eor	v26.16b, v26.16b, Y.16b
	pmull2	HI.1q, X.2d, Y.2d
	pmull	LO.1q, X.1d, Y.1d
	pmull	MI.1q, v25.1d, v26.1d
	.unreq X
	.unreq Y
.endm

/*
 * Computes the 256-bit polynomial represented by LO, HI, MI. Stores
 * the result in PL, PH.
 * [PH : PL] =
 *   [HI_1 : HI_1 + HI_0 + MI_1 + LO_1 : HI_0 + MI_0 + LO_1 + LO_0 : LO_0]
 */
.macro karatsuba2
	// v4 = [HI_1 + MI_1 : HI_0 + MI_0]
	eor	v4.16b, HI.16b, MI.16b
	// v4 = [HI_1 + MI_1 + LO_1 : HI_0 + MI_0 + LO_0]
	eor	v4.16b, v4.16b, LO.16b
	// v5 = [HI_0 : LO_1]
	ext	v5.16b, LO.16b, HI.16b, #8
	// v4 = [HI_1 + HI_0 + MI_1 + LO_1 : HI_0 + MI_0 + LO_1 + LO_0]
	eor	v4.16b, v4.16b, v5.16b
	// HI = [HI_0 : HI_1]
	ext	HI.16b, HI.16b, HI.16b, #8
	// LO = [LO_0 : LO_1]
	ext	LO.16b, LO.16b, LO.16b, #8
	// PH = [HI_1 : HI_1 + HI_0 + MI_1 + LO_1]
	ext	PH.16b, v4.16b, HI.16b, #8
	// PL = [HI_0 + MI_0 + LO_1 + LO_0 : LO_0]
	ext	PL.16b, LO.16b, v4.16b, #8
.endm

/*
 * Computes the 128-bit reduction of PH : PL. Stores the result in dest.
 *
 * This macro computes p(x) mod g(x) where p(x) is in montgomery form and g(x) =
 * x^128 + x^127 + x^126 + x^121 + 1.
 *
 * We have a 256-bit polynomial PH : PL = P_3 : P_2 : P_1 : P_0 that is the
 * product of two 128-bit polynomials in Montgomery form.  We need to reduce it
 * mod g(x).  Also, since polynomials in Montgomery form have an "extra" factor
 * of x^128, this product has two extra factors of x^128.  To get it back into
 * Montgomery form, we need to remove one of these factors by dividing by x^128.
 *
 * To accomplish both of these goals, we add multiples of g(x) that cancel out
 * the low 128 bits P_1 : P_0, leaving just the high 128 bits. Since the low
 * bits are zero, the polynomial division by x^128 can be done by right
 * shifting.
 *
 * Since the only nonzero term in the low 64 bits of g(x) is the constant term,
 * the multiple of g(x) needed to cancel out P_0 is P_0 * g(x).  The CPU can
 * only do 64x64 bit multiplications, so split P_0 * g(x) into x^128 * P_0 +
 * x^64 * g*(x) * P_0 + P_0, where g*(x) is bits 64-127 of g(x).  Adding this to
 * the original polynomial gives P_3 : P_2 + P_0 + T_1 : P_1 + T_0 : 0, where T
 * = T_1 : T_0 = g*(x) * P_0.  Thus, bits 0-63 got "folded" into bits 64-191.
 *
 * Repeating this same process on the next 64 bits "folds" bits 64-127 into bits
 * 128-255, giving the answer in bits 128-255. This time, we need to cancel P_1
 * + T_0 in bits 64-127. The multiple of g(x) required is (P_1 + T_0) * g(x) *
 * x^64. Adding this to our previous computation gives P_3 + P_1 + T_0 + V_1 :
 * P_2 + P_0 + T_1 + V_0 : 0 : 0, where V = V_1 : V_0 = g*(x) * (P_1 + T_0).
 *
 * So our final computation is:
 *   T = T_1 : T_0 = g*(x) * P_0
 *   V = V_1 : V_0 = g*(x) * (P_1 + T_0)
 *   p(x) / x^{128} mod g(x) = P_3 + P_1 + T_0 + V_1 : P_2 + P_0 + T_1 + V_0
 *
 * The implementation below saves a XOR instruction by computing P_1 + T_0 : P_0
 * + T_1 and XORing into dest, rather than separately XORing P_1 : P_0 and T_0 :
 * T_1 into dest.  This allows us to reuse P_1 + T_0 when computing V.
 */
.macro montgomery_reduction dest
	DEST .req \dest
	// TMP_V = T_1 : T_0 = P_0 * g*(x)
	pmull	TMP_V.1q, PL.1d, GSTAR.1d
	// TMP_V = T_0 : T_1
	ext	TMP_V.16b, TMP_V.16b, TMP_V.16b, #8
	// TMP_V = P_1 + T_0 : P_0 + T_1
	eor	TMP_V.16b, PL.16b, TMP_V.16b
	// PH = P_3 + P_1 + T_0 : P_2 + P_0 + T_1
	eor	PH.16b, PH.16b, TMP_V.16b
	// TMP_V = V_1 : V_0 = (P_1 + T_0) * g*(x)
	pmull2	TMP_V.1q, TMP_V.2d, GSTAR.2d
	eor	DEST.16b, PH.16b, TMP_V.16b
	.unreq DEST
.endm

/*
 * Compute Polyval on 8 blocks.
 *
 * If reduce is set, also computes the montgomery reduction of the
 * previous full_stride call and XORs with the first message block.
 * (m_0 + REDUCE(PL, PH))h^8 + ... + m_7h^1.
 * I.e., the first multiplication uses m_0 + REDUCE(PL, PH) instead of m_0.
 *
 * Sets PL, PH.
 */
.macro full_stride reduce
	eor		LO.16b, LO.16b, LO.16b
	eor		MI.16b, MI.16b, MI.16b
	eor		HI.16b, HI.16b, HI.16b

	ld1		{M0.16b, M1.16b, M2.16b, M3.16b}, [MSG], #64
	ld1		{M4.16b, M5.16b, M6.16b, M7.16b}, [MSG], #64

	karatsuba1 M7 KEY1
	.if \reduce
	pmull	TMP_V.1q, PL.1d, GSTAR.1d
	.endif

	karatsuba1 M6 KEY2
	.if \reduce
	ext	TMP_V.16b, TMP_V.16b, TMP_V.16b, #8
	.endif

	karatsuba1 M5 KEY3
	.if \reduce
	eor	TMP_V.16b, PL.16b, TMP_V.16b
	.endif

	karatsuba1 M4 KEY4
	.if \reduce
	eor	PH.16b, PH.16b, TMP_V.16b
	.endif

	karatsuba1 M3 KEY5
	.if \reduce
	pmull2	TMP_V.1q, TMP_V.2d, GSTAR.2d
	.endif

	karatsuba1 M2 KEY6
	.if \reduce
	eor	SUM.16b, PH.16b, TMP_V.16b
	.endif

	karatsuba1 M1 KEY7
	eor	M0.16b, M0.16b, SUM.16b

	karatsuba1 M0 KEY8
	karatsuba2
.endm

/*
 * Handle any extra blocks after full_stride loop.
 */
.macro partial_stride
	add	KEY_POWERS, KEY_START, #(STRIDE_BLOCKS << 4)
	sub	KEY_POWERS, KEY_POWERS, BLOCKS_LEFT, lsl #4
	ld1	{KEY1.16b}, [KEY_POWERS], #16

	ld1	{TMP_V.16b}, [MSG], #16
	eor	SUM.16b, SUM.16b, TMP_V.16b
	karatsuba1_store KEY1 SUM
	sub	BLOCKS_LEFT, BLOCKS_LEFT, #1

	tst	BLOCKS_LEFT, #4
	beq	.Lpartial4BlocksDone
	ld1	{M0.16b, M1.16b,  M2.16b, M3.16b}, [MSG], #64
	ld1	{KEY8.16b, KEY7.16b, KEY6.16b,	KEY5.16b}, [KEY_POWERS], #64
	karatsuba1 M0 KEY8
	karatsuba1 M1 KEY7
	karatsuba1 M2 KEY6
	karatsuba1 M3 KEY5
.Lpartial4BlocksDone:
	tst	BLOCKS_LEFT, #2
	beq	.Lpartial2BlocksDone
	ld1	{M0.16b, M1.16b}, [MSG], #32
	ld1	{KEY8.16b, KEY7.16b}, [KEY_POWERS], #32
	karatsuba1 M0 KEY8
	karatsuba1 M1 KEY7
.Lpartial2BlocksDone:
	tst	BLOCKS_LEFT, #1
	beq	.LpartialDone
	ld1	{M0.16b}, [MSG], #16
	ld1	{KEY8.16b}, [KEY_POWERS], #16
	karatsuba1 M0 KEY8
.LpartialDone:
	karatsuba2
	montgomery_reduction SUM
.endm

/*
 * Perform montgomery multiplication in GF(2^128) and store result in op1.
 *
 * Computes op1*op2*x^{-128} mod x^128 + x^127 + x^126 + x^121 + 1
 * If op1, op2 are in montgomery form, this computes the montgomery
 * form of op1*op2.
 *
 * void pmull_polyval_mul(u8 *op1, const u8 *op2);
 */
SYM_FUNC_START(pmull_polyval_mul)
	adr	TMP, .Lgstar
	ld1	{GSTAR.2d}, [TMP]
	ld1	{v0.16b}, [x0]
	ld1	{v1.16b}, [x1]
	karatsuba1_store v0 v1
	karatsuba2
	montgomery_reduction SUM
	st1	{SUM.16b}, [x0]
	ret
SYM_FUNC_END(pmull_polyval_mul)

/*
 * Perform polynomial evaluation as specified by POLYVAL.  This computes:
 *	h^n * accumulator + h^n * m_0 + ... + h^1 * m_{n-1}
 * where n=nblocks, h is the hash key, and m_i are the message blocks.
 *
 * x0 - pointer to precomputed key powers h^8 ... h^1
 * x1 - pointer to message blocks
 * x2 - number of blocks to hash
 * x3 - pointer to accumulator
 *
 * void pmull_polyval_update(const struct polyval_ctx *ctx, const u8 *in,
 *			     size_t nblocks, u8 *accumulator);
 */
SYM_FUNC_START(pmull_polyval_update)
	adr	TMP, .Lgstar
	mov	KEY_START, KEY_POWERS
	ld1	{GSTAR.2d}, [TMP]
	ld1	{SUM.16b}, [ACCUMULATOR]
	subs	BLOCKS_LEFT, BLOCKS_LEFT, #STRIDE_BLOCKS
	blt .LstrideLoopExit
	ld1	{KEY8.16b, KEY7.16b, KEY6.16b, KEY5.16b}, [KEY_POWERS], #64
	ld1	{KEY4.16b, KEY3.16b, KEY2.16b, KEY1.16b}, [KEY_POWERS], #64
	full_stride 0
	subs	BLOCKS_LEFT, BLOCKS_LEFT, #STRIDE_BLOCKS
	blt .LstrideLoopExitReduce
.LstrideLoop:
	full_stride 1
	subs	BLOCKS_LEFT, BLOCKS_LEFT, #STRIDE_BLOCKS
	bge	.LstrideLoop
.LstrideLoopExitReduce:
	montgomery_reduction SUM
.LstrideLoopExit:
	adds	BLOCKS_LEFT, BLOCKS_LEFT, #STRIDE_BLOCKS
	beq	.LskipPartial
	partial_stride
.LskipPartial:
	st1	{SUM.16b}, [ACCUMULATOR]
	ret
SYM_FUNC_END(pmull_polyval_update)
