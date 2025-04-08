/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hardware-accelerated CRC-32 variants for Linux on z Systems
 *
 * Use the z/Architecture Vector Extension Facility to accelerate the
 * computing of bitreflected CRC-32 checksums for IEEE 802.3 Ethernet
 * and Castagnoli.
 *
 * This CRC-32 implementation algorithm is bitreflected and processes
 * the least-significant bit first (Little-Endian).
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#include <linux/types.h>
#include <asm/fpu.h>
#include "crc32-vx.h"

/* Vector register range containing CRC-32 constants */
#define CONST_PERM_LE2BE	9
#define CONST_R2R1		10
#define CONST_R4R3		11
#define CONST_R5		12
#define CONST_RU_POLY		13
#define CONST_CRC_POLY		14

/*
 * The CRC-32 constant block contains reduction constants to fold and
 * process particular chunks of the input data stream in parallel.
 *
 * For the CRC-32 variants, the constants are precomputed according to
 * these definitions:
 *
 *	R1 = [(x4*128+32 mod P'(x) << 32)]' << 1
 *	R2 = [(x4*128-32 mod P'(x) << 32)]' << 1
 *	R3 = [(x128+32 mod P'(x) << 32)]'   << 1
 *	R4 = [(x128-32 mod P'(x) << 32)]'   << 1
 *	R5 = [(x64 mod P'(x) << 32)]'	    << 1
 *	R6 = [(x32 mod P'(x) << 32)]'	    << 1
 *
 *	The bitreflected Barret reduction constant, u', is defined as
 *	the bit reversal of floor(x**64 / P(x)).
 *
 *	where P(x) is the polynomial in the normal domain and the P'(x) is the
 *	polynomial in the reversed (bitreflected) domain.
 *
 * CRC-32 (IEEE 802.3 Ethernet, ...) polynomials:
 *
 *	P(x)  = 0x04C11DB7
 *	P'(x) = 0xEDB88320
 *
 * CRC-32C (Castagnoli) polynomials:
 *
 *	P(x)  = 0x1EDC6F41
 *	P'(x) = 0x82F63B78
 */

static unsigned long constants_CRC_32_LE[] = {
	0x0f0e0d0c0b0a0908, 0x0706050403020100,	/* BE->LE mask */
	0x1c6e41596, 0x154442bd4,		/* R2, R1 */
	0x0ccaa009e, 0x1751997d0,		/* R4, R3 */
	0x0, 0x163cd6124,			/* R5 */
	0x0, 0x1f7011641,			/* u' */
	0x0, 0x1db710641			/* P'(x) << 1 */
};

static unsigned long constants_CRC_32C_LE[] = {
	0x0f0e0d0c0b0a0908, 0x0706050403020100,	/* BE->LE mask */
	0x09e4addf8, 0x740eef02,		/* R2, R1 */
	0x14cd00bd6, 0xf20c0dfe,		/* R4, R3 */
	0x0, 0x0dd45aab8,			/* R5 */
	0x0, 0x0dea713f1,			/* u' */
	0x0, 0x105ec76f0			/* P'(x) << 1 */
};

/**
 * crc32_le_vgfm_generic - Compute CRC-32 (LE variant) with vector registers
 * @crc: Initial CRC value, typically ~0.
 * @buf: Input buffer pointer, performance might be improved if the
 *	 buffer is on a doubleword boundary.
 * @size: Size of the buffer, must be 64 bytes or greater.
 * @constants: CRC-32 constant pool base pointer.
 *
 * Register usage:
 *	V0:	  Initial CRC value and intermediate constants and results.
 *	V1..V4:	  Data for CRC computation.
 *	V5..V8:	  Next data chunks that are fetched from the input buffer.
 *	V9:	  Constant for BE->LE conversion and shift operations
 *	V10..V14: CRC-32 constants.
 */
static u32 crc32_le_vgfm_generic(u32 crc, unsigned char const *buf, size_t size, unsigned long *constants)
{
	/* Load CRC-32 constants */
	fpu_vlm(CONST_PERM_LE2BE, CONST_CRC_POLY, constants);

	/*
	 * Load the initial CRC value.
	 *
	 * The CRC value is loaded into the rightmost word of the
	 * vector register and is later XORed with the LSB portion
	 * of the loaded input data.
	 */
	fpu_vzero(0);			/* Clear V0 */
	fpu_vlvgf(0, crc, 3);		/* Load CRC into rightmost word */

	/* Load a 64-byte data chunk and XOR with CRC */
	fpu_vlm(1, 4, buf);
	fpu_vperm(1, 1, 1, CONST_PERM_LE2BE);
	fpu_vperm(2, 2, 2, CONST_PERM_LE2BE);
	fpu_vperm(3, 3, 3, CONST_PERM_LE2BE);
	fpu_vperm(4, 4, 4, CONST_PERM_LE2BE);

	fpu_vx(1, 0, 1);		/* V1 ^= CRC */
	buf += 64;
	size -= 64;

	while (size >= 64) {
		fpu_vlm(5, 8, buf);
		fpu_vperm(5, 5, 5, CONST_PERM_LE2BE);
		fpu_vperm(6, 6, 6, CONST_PERM_LE2BE);
		fpu_vperm(7, 7, 7, CONST_PERM_LE2BE);
		fpu_vperm(8, 8, 8, CONST_PERM_LE2BE);
		/*
		 * Perform a GF(2) multiplication of the doublewords in V1 with
		 * the R1 and R2 reduction constants in V0.  The intermediate
		 * result is then folded (accumulated) with the next data chunk
		 * in V5 and stored in V1. Repeat this step for the register
		 * contents in V2, V3, and V4 respectively.
		 */
		fpu_vgfmag(1, CONST_R2R1, 1, 5);
		fpu_vgfmag(2, CONST_R2R1, 2, 6);
		fpu_vgfmag(3, CONST_R2R1, 3, 7);
		fpu_vgfmag(4, CONST_R2R1, 4, 8);
		buf += 64;
		size -= 64;
	}

	/*
	 * Fold V1 to V4 into a single 128-bit value in V1.  Multiply V1 with R3
	 * and R4 and accumulating the next 128-bit chunk until a single 128-bit
	 * value remains.
	 */
	fpu_vgfmag(1, CONST_R4R3, 1, 2);
	fpu_vgfmag(1, CONST_R4R3, 1, 3);
	fpu_vgfmag(1, CONST_R4R3, 1, 4);

	while (size >= 16) {
		fpu_vl(2, buf);
		fpu_vperm(2, 2, 2, CONST_PERM_LE2BE);
		fpu_vgfmag(1, CONST_R4R3, 1, 2);
		buf += 16;
		size -= 16;
	}

	/*
	 * Set up a vector register for byte shifts.  The shift value must
	 * be loaded in bits 1-4 in byte element 7 of a vector register.
	 * Shift by 8 bytes: 0x40
	 * Shift by 4 bytes: 0x20
	 */
	fpu_vleib(9, 0x40, 7);

	/*
	 * Prepare V0 for the next GF(2) multiplication: shift V0 by 8 bytes
	 * to move R4 into the rightmost doubleword and set the leftmost
	 * doubleword to 0x1.
	 */
	fpu_vsrlb(0, CONST_R4R3, 9);
	fpu_vleig(0, 1, 0);

	/*
	 * Compute GF(2) product of V1 and V0.	The rightmost doubleword
	 * of V1 is multiplied with R4.  The leftmost doubleword of V1 is
	 * multiplied by 0x1 and is then XORed with rightmost product.
	 * Implicitly, the intermediate leftmost product becomes padded
	 */
	fpu_vgfmg(1, 0, 1);

	/*
	 * Now do the final 32-bit fold by multiplying the rightmost word
	 * in V1 with R5 and XOR the result with the remaining bits in V1.
	 *
	 * To achieve this by a single VGFMAG, right shift V1 by a word
	 * and store the result in V2 which is then accumulated.  Use the
	 * vector unpack instruction to load the rightmost half of the
	 * doubleword into the rightmost doubleword element of V1; the other
	 * half is loaded in the leftmost doubleword.
	 * The vector register with CONST_R5 contains the R5 constant in the
	 * rightmost doubleword and the leftmost doubleword is zero to ignore
	 * the leftmost product of V1.
	 */
	fpu_vleib(9, 0x20, 7);		  /* Shift by words */
	fpu_vsrlb(2, 1, 9);		  /* Store remaining bits in V2 */
	fpu_vupllf(1, 1);		  /* Split rightmost doubleword */
	fpu_vgfmag(1, CONST_R5, 1, 2);	  /* V1 = (V1 * R5) XOR V2 */

	/*
	 * Apply a Barret reduction to compute the final 32-bit CRC value.
	 *
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
	 *  Note: The leftmost doubleword of vector register containing
	 *  CONST_RU_POLY is zero and, thus, the intermediate GF(2) product
	 *  is zero and does not contribute to the final result.
	 */

	/* T1(x) = floor( R(x) / x^32 ) GF2MUL u */
	fpu_vupllf(2, 1);
	fpu_vgfmg(2, CONST_RU_POLY, 2);

	/*
	 * Compute the GF(2) product of the CRC polynomial with T1(x) in
	 * V2 and XOR the intermediate result, T2(x), with the value in V1.
	 * The final result is stored in word element 2 of V2.
	 */
	fpu_vupllf(2, 2);
	fpu_vgfmag(2, CONST_CRC_POLY, 2, 1);

	return fpu_vlgvf(2, 2);
}

u32 crc32_le_vgfm_16(u32 crc, unsigned char const *buf, size_t size)
{
	return crc32_le_vgfm_generic(crc, buf, size, &constants_CRC_32_LE[0]);
}

u32 crc32c_le_vgfm_16(u32 crc, unsigned char const *buf, size_t size)
{
	return crc32_le_vgfm_generic(crc, buf, size, &constants_CRC_32C_LE[0]);
}
