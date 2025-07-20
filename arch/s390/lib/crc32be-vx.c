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

#include <linux/types.h>
#include <asm/fpu.h>
#include "crc32-vx.h"

/* Vector register range containing CRC-32 constants */
#define CONST_R1R2		9
#define CONST_R3R4		10
#define CONST_R5		11
#define CONST_R6		12
#define CONST_RU_POLY		13
#define CONST_CRC_POLY		14

/*
 * The CRC-32 constant block contains reduction constants to fold and
 * process particular chunks of the input data stream in parallel.
 *
 * For the CRC-32 variants, the constants are precomputed according to
 * these definitions:
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
 * The rightmost doubleword can be 0 to prevent contribution to the result or
 * can be multiplied by 1 to perform an XOR without the need for a separate
 * VECTOR EXCLUSIVE OR instruction.
 *
 * CRC-32 (IEEE 802.3 Ethernet, ...) polynomials:
 *
 *	P(x)  = 0x04C11DB7
 *	P'(x) = 0xEDB88320
 */

static unsigned long constants_CRC_32_BE[] = {
	0x08833794c, 0x0e6228b11,	/* R1, R2 */
	0x0c5b9cd4c, 0x0e8a45605,	/* R3, R4 */
	0x0f200aa66, 1UL << 32,		/* R5, x32 */
	0x0490d678d, 1,			/* R6, 1 */
	0x104d101df, 0,			/* u */
	0x104C11DB7, 0,			/* P(x) */
};

/**
 * crc32_be_vgfm_16 - Compute CRC-32 (BE variant) with vector registers
 * @crc: Initial CRC value, typically ~0.
 * @buf: Input buffer pointer, performance might be improved if the
 *	  buffer is on a doubleword boundary.
 * @size: Size of the buffer, must be 64 bytes or greater.
 *
 * Register usage:
 *	V0:	Initial CRC value and intermediate constants and results.
 *	V1..V4:	Data for CRC computation.
 *	V5..V8:	Next data chunks that are fetched from the input buffer.
 *	V9..V14: CRC-32 constants.
 */
u32 crc32_be_vgfm_16(u32 crc, unsigned char const *buf, size_t size)
{
	/* Load CRC-32 constants */
	fpu_vlm(CONST_R1R2, CONST_CRC_POLY, &constants_CRC_32_BE);
	fpu_vzero(0);

	/* Load the initial CRC value into the leftmost word of V0. */
	fpu_vlvgf(0, crc, 0);

	/* Load a 64-byte data chunk and XOR with CRC */
	fpu_vlm(1, 4, buf);
	fpu_vx(1, 0, 1);
	buf += 64;
	size -= 64;

	while (size >= 64) {
		/* Load the next 64-byte data chunk into V5 to V8 */
		fpu_vlm(5, 8, buf);

		/*
		 * Perform a GF(2) multiplication of the doublewords in V1 with
		 * the reduction constants in V0.  The intermediate result is
		 * then folded (accumulated) with the next data chunk in V5 and
		 * stored in V1.  Repeat this step for the register contents
		 * in V2, V3, and V4 respectively.
		 */
		fpu_vgfmag(1, CONST_R1R2, 1, 5);
		fpu_vgfmag(2, CONST_R1R2, 2, 6);
		fpu_vgfmag(3, CONST_R1R2, 3, 7);
		fpu_vgfmag(4, CONST_R1R2, 4, 8);
		buf += 64;
		size -= 64;
	}

	/* Fold V1 to V4 into a single 128-bit value in V1 */
	fpu_vgfmag(1, CONST_R3R4, 1, 2);
	fpu_vgfmag(1, CONST_R3R4, 1, 3);
	fpu_vgfmag(1, CONST_R3R4, 1, 4);

	while (size >= 16) {
		fpu_vl(2, buf);
		fpu_vgfmag(1, CONST_R3R4, 1, 2);
		buf += 16;
		size -= 16;
	}

	/*
	 * The R5 constant is used to fold a 128-bit value into an 96-bit value
	 * that is XORed with the next 96-bit input data chunk.  To use a single
	 * VGFMG instruction, multiply the rightmost 64-bit with x^32 (1<<32) to
	 * form an intermediate 96-bit value (with appended zeros) which is then
	 * XORed with the intermediate reduction result.
	 */
	fpu_vgfmg(1, CONST_R5, 1);

	/*
	 * Further reduce the remaining 96-bit value to a 64-bit value using a
	 * single VGFMG, the rightmost doubleword is multiplied with 0x1. The
	 * intermediate result is then XORed with the product of the leftmost
	 * doubleword with R6.	The result is a 64-bit value and is subject to
	 * the Barret reduction.
	 */
	fpu_vgfmg(1, CONST_R6, 1);

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
	 * with zero to not contribute to the intermediate results.
	 */

	/* T1(x) = floor( R(x) / x^32 ) GF2MUL u */
	fpu_vupllf(2, 1);
	fpu_vgfmg(2, CONST_RU_POLY, 2);

	/*
	 * Compute the GF(2) product of the CRC polynomial in VO with T1(x) in
	 * V2 and XOR the intermediate result, T2(x),  with the value in V1.
	 * The final result is in the rightmost word of V2.
	 */
	fpu_vupllf(2, 2);
	fpu_vgfmag(2, CONST_CRC_POLY, 2, 1);
	return fpu_vlgvf(2, 3);
}
