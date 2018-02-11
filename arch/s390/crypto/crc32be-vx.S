/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hardware-accelerated CRC-32 variants for Linux on z Systems
 *
 * Use the z/Architecture Vector Extension Facility to accelerate the
 * computing of CRC-32 checksums.
 *
 * This CRC-32 implementation algorithm processes the most-significant
 * bit first (BE).
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#include <linux/linkage.h>
#include <asm/vx-insn.h>

/* Vector register range containing CRC-32 constants */
#define CONST_R1R2		%v9
#define CONST_R3R4		%v10
#define CONST_R5		%v11
#define CONST_R6		%v12
#define CONST_RU_POLY		%v13
#define CONST_CRC_POLY		%v14

.data
.align 8

/*
 * The CRC-32 constant block contains reduction constants to fold and
 * process particular chunks of the input data stream in parallel.
 *
 * For the CRC-32 variants, the constants are precomputed according to
 * these defintions:
 *
 *	R1 = x4*128+64 mod P(x)
 *	R2 = x4*128    mod P(x)
 *	R3 = x128+64   mod P(x)
 *	R4 = x128      mod P(x)
 *	R5 = x96       mod P(x)
 *	R6 = x64       mod P(x)
 *
 *	Barret reduction constant, u, is defined as floor(x**64 / P(x)).
 *
 *	where P(x) is the polynomial in the normal domain and the P'(x) is the
 *	polynomial in the reversed (bitreflected) domain.
 *
 * Note that the constant definitions below are extended in order to compute
 * intermediate results with a single VECTOR GALOIS FIELD MULTIPLY instruction.
 * The righmost doubleword can be 0 to prevent contribution to the result or
 * can be multiplied by 1 to perform an XOR without the need for a separate
 * VECTOR EXCLUSIVE OR instruction.
 *
 * CRC-32 (IEEE 802.3 Ethernet, ...) polynomials:
 *
 *	P(x)  = 0x04C11DB7
 *	P'(x) = 0xEDB88320
 */

.Lconstants_CRC_32_BE:
	.quad		0x08833794c, 0x0e6228b11	# R1, R2
	.quad		0x0c5b9cd4c, 0x0e8a45605	# R3, R4
	.quad		0x0f200aa66, 1 << 32		# R5, x32
	.quad		0x0490d678d, 1			# R6, 1
	.quad		0x104d101df, 0			# u
	.quad		0x104C11DB7, 0			# P(x)

.previous

.text
/*
 * The CRC-32 function(s) use these calling conventions:
 *
 * Parameters:
 *
 *	%r2:	Initial CRC value, typically ~0; and final CRC (return) value.
 *	%r3:	Input buffer pointer, performance might be improved if the
 *		buffer is on a doubleword boundary.
 *	%r4:	Length of the buffer, must be 64 bytes or greater.
 *
 * Register usage:
 *
 *	%r5:	CRC-32 constant pool base pointer.
 *	V0:	Initial CRC value and intermediate constants and results.
 *	V1..V4:	Data for CRC computation.
 *	V5..V8:	Next data chunks that are fetched from the input buffer.
 *
 *	V9..V14: CRC-32 constants.
 */
ENTRY(crc32_be_vgfm_16)
	/* Load CRC-32 constants */
	larl	%r5,.Lconstants_CRC_32_BE
	VLM	CONST_R1R2,CONST_CRC_POLY,0,%r5

	/* Load the initial CRC value into the leftmost word of V0. */
	VZERO	%v0
	VLVGF	%v0,%r2,0

	/* Load a 64-byte data chunk and XOR with CRC */
	VLM	%v1,%v4,0,%r3		/* 64-bytes into V1..V4 */
	VX	%v1,%v0,%v1		/* V1 ^= CRC */
	aghi	%r3,64			/* BUF = BUF + 64 */
	aghi	%r4,-64			/* LEN = LEN - 64 */

	/* Check remaining buffer size and jump to proper folding method */
	cghi	%r4,64
	jl	.Lless_than_64bytes

.Lfold_64bytes_loop:
	/* Load the next 64-byte data chunk into V5 to V8 */
	VLM	%v5,%v8,0,%r3

	/*
	 * Perform a GF(2) multiplication of the doublewords in V1 with
	 * the reduction constants in V0.  The intermediate result is
	 * then folded (accumulated) with the next data chunk in V5 and
	 * stored in V1.  Repeat this step for the register contents
	 * in V2, V3, and V4 respectively.
	 */
	VGFMAG	%v1,CONST_R1R2,%v1,%v5
	VGFMAG	%v2,CONST_R1R2,%v2,%v6
	VGFMAG	%v3,CONST_R1R2,%v3,%v7
	VGFMAG	%v4,CONST_R1R2,%v4,%v8

	/* Adjust buffer pointer and length for next loop */
	aghi	%r3,64			/* BUF = BUF + 64 */
	aghi	%r4,-64			/* LEN = LEN - 64 */

	cghi	%r4,64
	jnl	.Lfold_64bytes_loop

.Lless_than_64bytes:
	/* Fold V1 to V4 into a single 128-bit value in V1 */
	VGFMAG	%v1,CONST_R3R4,%v1,%v2
	VGFMAG	%v1,CONST_R3R4,%v1,%v3
	VGFMAG	%v1,CONST_R3R4,%v1,%v4

	/* Check whether to continue with 64-bit folding */
	cghi	%r4,16
	jl	.Lfinal_fold

.Lfold_16bytes_loop:

	VL	%v2,0,,%r3		/* Load next data chunk */
	VGFMAG	%v1,CONST_R3R4,%v1,%v2	/* Fold next data chunk */

	/* Adjust buffer pointer and size for folding next data chunk */
	aghi	%r3,16
	aghi	%r4,-16

	/* Process remaining data chunks */
	cghi	%r4,16
	jnl	.Lfold_16bytes_loop

.Lfinal_fold:
	/*
	 * The R5 constant is used to fold a 128-bit value into an 96-bit value
	 * that is XORed with the next 96-bit input data chunk.  To use a single
	 * VGFMG instruction, multiply the rightmost 64-bit with x^32 (1<<32) to
	 * form an intermediate 96-bit value (with appended zeros) which is then
	 * XORed with the intermediate reduction result.
	 */
	VGFMG	%v1,CONST_R5,%v1

	/*
	 * Further reduce the remaining 96-bit value to a 64-bit value using a
	 * single VGFMG, the rightmost doubleword is multiplied with 0x1. The
	 * intermediate result is then XORed with the product of the leftmost
	 * doubleword with R6.	The result is a 64-bit value and is subject to
	 * the Barret reduction.
	 */
	VGFMG	%v1,CONST_R6,%v1

	/*
	 * The input values to the Barret reduction are the degree-63 polynomial
	 * in V1 (R(x)), degree-32 generator polynomial, and the reduction
	 * constant u.	The Barret reduction result is the CRC value of R(x) mod
	 * P(x).
	 *
	 * The Barret reduction algorithm is defined as:
	 *
	 *    1. T1(x) = floor( R(x) / x^32 ) GF2MUL u
	 *    2. T2(x) = floor( T1(x) / x^32 ) GF2MUL P(x)
	 *    3. C(x)  = R(x) XOR T2(x) mod x^32
	 *
	 * Note: To compensate the division by x^32, use the vector unpack
	 * instruction to move the leftmost word into the leftmost doubleword
	 * of the vector register.  The rightmost doubleword is multiplied
	 * with zero to not contribute to the intermedate results.
	 */

	/* T1(x) = floor( R(x) / x^32 ) GF2MUL u */
	VUPLLF	%v2,%v1
	VGFMG	%v2,CONST_RU_POLY,%v2

	/*
	 * Compute the GF(2) product of the CRC polynomial in VO with T1(x) in
	 * V2 and XOR the intermediate result, T2(x),  with the value in V1.
	 * The final result is in the rightmost word of V2.
	 */
	VUPLLF	%v2,%v2
	VGFMAG	%v2,CONST_CRC_POLY,%v2,%v1

.Ldone:
	VLGVF	%r2,%v2,3
	br	%r14

.previous
