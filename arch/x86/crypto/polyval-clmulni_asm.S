/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 */
/*
 * This is an efficient implementation of POLYVAL using intel PCLMULQDQ-NI
 * instructions. It works on 8 blocks at a time, by precomputing the first 8
 * keys powers h^8, ..., h^1 in the POLYVAL finite field. This precomputation
 * allows us to split finite field multiplication into two steps.
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
#include <asm/frame.h>

#define STRIDE_BLOCKS 8

#define GSTAR %xmm7
#define PL %xmm8
#define PH %xmm9
#define TMP_XMM %xmm11
#define LO %xmm12
#define HI %xmm13
#define MI %xmm14
#define SUM %xmm15

#define KEY_POWERS %rdi
#define MSG %rsi
#define BLOCKS_LEFT %rdx
#define ACCUMULATOR %rcx
#define TMP %rax

.section    .rodata.cst16.gstar, "aM", @progbits, 16
.align 16

.Lgstar:
	.quad 0xc200000000000000, 0xc200000000000000

.text

/*
 * Performs schoolbook1_iteration on two lists of 128-bit polynomials of length
 * count pointed to by MSG and KEY_POWERS.
 */
.macro schoolbook1 count
	.set i, 0
	.rept (\count)
		schoolbook1_iteration i 0
		.set i, (i +1)
	.endr
.endm

/*
 * Computes the product of two 128-bit polynomials at the memory locations
 * specified by (MSG + 16*i) and (KEY_POWERS + 16*i) and XORs the components of
 * the 256-bit product into LO, MI, HI.
 *
 * Given:
 *   X = [X_1 : X_0]
 *   Y = [Y_1 : Y_0]
 *
 * We compute:
 *   LO += X_0 * Y_0
 *   MI += X_0 * Y_1 + X_1 * Y_0
 *   HI += X_1 * Y_1
 *
 * Later, the 256-bit result can be extracted as:
 *   [HI_1 : HI_0 + MI_1 : LO_1 + MI_0 : LO_0]
 * This step is done when computing the polynomial reduction for efficiency
 * reasons.
 *
 * If xor_sum == 1, then also XOR the value of SUM into m_0.  This avoids an
 * extra multiplication of SUM and h^8.
 */
.macro schoolbook1_iteration i xor_sum
	movups (16*\i)(MSG), %xmm0
	.if (\i == 0 && \xor_sum == 1)
		pxor SUM, %xmm0
	.endif
	vpclmulqdq $0x01, (16*\i)(KEY_POWERS), %xmm0, %xmm2
	vpclmulqdq $0x00, (16*\i)(KEY_POWERS), %xmm0, %xmm1
	vpclmulqdq $0x10, (16*\i)(KEY_POWERS), %xmm0, %xmm3
	vpclmulqdq $0x11, (16*\i)(KEY_POWERS), %xmm0, %xmm4
	vpxor %xmm2, MI, MI
	vpxor %xmm1, LO, LO
	vpxor %xmm4, HI, HI
	vpxor %xmm3, MI, MI
.endm

/*
 * Performs the same computation as schoolbook1_iteration, except we expect the
 * arguments to already be loaded into xmm0 and xmm1 and we set the result
 * registers LO, MI, and HI directly rather than XOR'ing into them.
 */
.macro schoolbook1_noload
	vpclmulqdq $0x01, %xmm0, %xmm1, MI
	vpclmulqdq $0x10, %xmm0, %xmm1, %xmm2
	vpclmulqdq $0x00, %xmm0, %xmm1, LO
	vpclmulqdq $0x11, %xmm0, %xmm1, HI
	vpxor %xmm2, MI, MI
.endm

/*
 * Computes the 256-bit polynomial represented by LO, HI, MI. Stores
 * the result in PL, PH.
 *   [PH : PL] = [HI_1 : HI_0 + MI_1 : LO_1 + MI_0 : LO_0]
 */
.macro schoolbook2
	vpslldq $8, MI, PL
	vpsrldq $8, MI, PH
	pxor LO, PL
	pxor HI, PH
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
 * bits are zero, the polynomial division by x^128 can be done by right shifting.
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
	vpclmulqdq $0x00, PL, GSTAR, TMP_XMM	# TMP_XMM = T_1 : T_0 = P_0 * g*(x)
	pshufd $0b01001110, TMP_XMM, TMP_XMM	# TMP_XMM = T_0 : T_1
	pxor PL, TMP_XMM			# TMP_XMM = P_1 + T_0 : P_0 + T_1
	pxor TMP_XMM, PH			# PH = P_3 + P_1 + T_0 : P_2 + P_0 + T_1
	pclmulqdq $0x11, GSTAR, TMP_XMM		# TMP_XMM = V_1 : V_0 = V = [(P_1 + T_0) * g*(x)]
	vpxor TMP_XMM, PH, \dest
.endm

/*
 * Compute schoolbook multiplication for 8 blocks
 * m_0h^8 + ... + m_7h^1
 *
 * If reduce is set, also computes the montgomery reduction of the
 * previous full_stride call and XORs with the first message block.
 * (m_0 + REDUCE(PL, PH))h^8 + ... + m_7h^1.
 * I.e., the first multiplication uses m_0 + REDUCE(PL, PH) instead of m_0.
 */
.macro full_stride reduce
	pxor LO, LO
	pxor HI, HI
	pxor MI, MI

	schoolbook1_iteration 7 0
	.if \reduce
		vpclmulqdq $0x00, PL, GSTAR, TMP_XMM
	.endif

	schoolbook1_iteration 6 0
	.if \reduce
		pshufd $0b01001110, TMP_XMM, TMP_XMM
	.endif

	schoolbook1_iteration 5 0
	.if \reduce
		pxor PL, TMP_XMM
	.endif

	schoolbook1_iteration 4 0
	.if \reduce
		pxor TMP_XMM, PH
	.endif

	schoolbook1_iteration 3 0
	.if \reduce
		pclmulqdq $0x11, GSTAR, TMP_XMM
	.endif

	schoolbook1_iteration 2 0
	.if \reduce
		vpxor TMP_XMM, PH, SUM
	.endif

	schoolbook1_iteration 1 0

	schoolbook1_iteration 0 1

	addq $(8*16), MSG
	schoolbook2
.endm

/*
 * Process BLOCKS_LEFT blocks, where 0 < BLOCKS_LEFT < STRIDE_BLOCKS
 */
.macro partial_stride
	mov BLOCKS_LEFT, TMP
	shlq $4, TMP
	addq $(16*STRIDE_BLOCKS), KEY_POWERS
	subq TMP, KEY_POWERS

	movups (MSG), %xmm0
	pxor SUM, %xmm0
	movaps (KEY_POWERS), %xmm1
	schoolbook1_noload
	dec BLOCKS_LEFT
	addq $16, MSG
	addq $16, KEY_POWERS

	test $4, BLOCKS_LEFT
	jz .Lpartial4BlocksDone
	schoolbook1 4
	addq $(4*16), MSG
	addq $(4*16), KEY_POWERS
.Lpartial4BlocksDone:
	test $2, BLOCKS_LEFT
	jz .Lpartial2BlocksDone
	schoolbook1 2
	addq $(2*16), MSG
	addq $(2*16), KEY_POWERS
.Lpartial2BlocksDone:
	test $1, BLOCKS_LEFT
	jz .LpartialDone
	schoolbook1 1
.LpartialDone:
	schoolbook2
	montgomery_reduction SUM
.endm

/*
 * Perform montgomery multiplication in GF(2^128) and store result in op1.
 *
 * Computes op1*op2*x^{-128} mod x^128 + x^127 + x^126 + x^121 + 1
 * If op1, op2 are in montgomery form, this computes the montgomery
 * form of op1*op2.
 *
 * void clmul_polyval_mul(u8 *op1, const u8 *op2);
 */
SYM_FUNC_START(clmul_polyval_mul)
	FRAME_BEGIN
	vmovdqa .Lgstar(%rip), GSTAR
	movups (%rdi), %xmm0
	movups (%rsi), %xmm1
	schoolbook1_noload
	schoolbook2
	montgomery_reduction SUM
	movups SUM, (%rdi)
	FRAME_END
	RET
SYM_FUNC_END(clmul_polyval_mul)

/*
 * Perform polynomial evaluation as specified by POLYVAL.  This computes:
 *	h^n * accumulator + h^n * m_0 + ... + h^1 * m_{n-1}
 * where n=nblocks, h is the hash key, and m_i are the message blocks.
 *
 * rdi - pointer to precomputed key powers h^8 ... h^1
 * rsi - pointer to message blocks
 * rdx - number of blocks to hash
 * rcx - pointer to the accumulator
 *
 * void clmul_polyval_update(const struct polyval_tfm_ctx *keys,
 *	const u8 *in, size_t nblocks, u8 *accumulator);
 */
SYM_FUNC_START(clmul_polyval_update)
	FRAME_BEGIN
	vmovdqa .Lgstar(%rip), GSTAR
	movups (ACCUMULATOR), SUM
	subq $STRIDE_BLOCKS, BLOCKS_LEFT
	js .LstrideLoopExit
	full_stride 0
	subq $STRIDE_BLOCKS, BLOCKS_LEFT
	js .LstrideLoopExitReduce
.LstrideLoop:
	full_stride 1
	subq $STRIDE_BLOCKS, BLOCKS_LEFT
	jns .LstrideLoop
.LstrideLoopExitReduce:
	montgomery_reduction SUM
.LstrideLoopExit:
	add $STRIDE_BLOCKS, BLOCKS_LEFT
	jz .LskipPartial
	partial_stride
.LskipPartial:
	movups SUM, (ACCUMULATOR)
	FRAME_END
	RET
SYM_FUNC_END(clmul_polyval_update)
