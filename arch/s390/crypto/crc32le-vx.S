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

#include <linux/linkage.h>
#include <asm/nospec-insn.h>
#include <asm/vx-insn.h>

/* Vector register range containing CRC-32 constants */
#define CONST_PERM_LE2BE	%v9
#define CONST_R2R1		%v10
#define CONST_R4R3		%v11
#define CONST_R5		%v12
#define CONST_RU_POLY		%v13
#define CONST_CRC_POLY		%v14

.data
.align 8

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

.Lconstants_CRC_32_LE:
	.octa		0x0F0E0D0C0B0A09080706050403020100	# BE->LE mask
	.quad		0x1c6e41596, 0x154442bd4		# R2, R1
	.quad		0x0ccaa009e, 0x1751997d0		# R4, R3
	.octa		0x163cd6124				# R5
	.octa		0x1F7011641				# u'
	.octa		0x1DB710641				# P'(x) << 1

.Lconstants_CRC_32C_LE:
	.octa		0x0F0E0D0C0B0A09080706050403020100	# BE->LE mask
	.quad		0x09e4addf8, 0x740eef02			# R2, R1
	.quad		0x14cd00bd6, 0xf20c0dfe			# R4, R3
	.octa		0x0dd45aab8				# R5
	.octa		0x0dea713f1				# u'
	.octa		0x105ec76f0				# P'(x) << 1

.previous

	GEN_BR_THUNK %r14

.text

/*
 * The CRC-32 functions use these calling conventions:
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
 *	V9:	Constant for BE->LE conversion and shift operations
 *
 *	V10..V14: CRC-32 constants.
 */

ENTRY(crc32_le_vgfm_16)
	larl	%r5,.Lconstants_CRC_32_LE
	j	crc32_le_vgfm_generic
ENDPROC(crc32_le_vgfm_16)

ENTRY(crc32c_le_vgfm_16)
	larl	%r5,.Lconstants_CRC_32C_LE
	j	crc32_le_vgfm_generic
ENDPROC(crc32c_le_vgfm_16)

ENTRY(crc32_le_vgfm_generic)
	/* Load CRC-32 constants */
	VLM	CONST_PERM_LE2BE,CONST_CRC_POLY,0,%r5

	/*
	 * Load the initial CRC value.
	 *
	 * The CRC value is loaded into the rightmost word of the
	 * vector register and is later XORed with the LSB portion
	 * of the loaded input data.
	 */
	VZERO	%v0			/* Clear V0 */
	VLVGF	%v0,%r2,3		/* Load CRC into rightmost word */

	/* Load a 64-byte data chunk and XOR with CRC */
	VLM	%v1,%v4,0,%r3		/* 64-bytes into V1..V4 */
	VPERM	%v1,%v1,%v1,CONST_PERM_LE2BE
	VPERM	%v2,%v2,%v2,CONST_PERM_LE2BE
	VPERM	%v3,%v3,%v3,CONST_PERM_LE2BE
	VPERM	%v4,%v4,%v4,CONST_PERM_LE2BE

	VX	%v1,%v0,%v1		/* V1 ^= CRC */
	aghi	%r3,64			/* BUF = BUF + 64 */
	aghi	%r4,-64			/* LEN = LEN - 64 */

	cghi	%r4,64
	jl	.Lless_than_64bytes

.Lfold_64bytes_loop:
	/* Load the next 64-byte data chunk into V5 to V8 */
	VLM	%v5,%v8,0,%r3
	VPERM	%v5,%v5,%v5,CONST_PERM_LE2BE
	VPERM	%v6,%v6,%v6,CONST_PERM_LE2BE
	VPERM	%v7,%v7,%v7,CONST_PERM_LE2BE
	VPERM	%v8,%v8,%v8,CONST_PERM_LE2BE

	/*
	 * Perform a GF(2) multiplication of the doublewords in V1 with
	 * the R1 and R2 reduction constants in V0.  The intermediate result
	 * is then folded (accumulated) with the next data chunk in V5 and
	 * stored in V1. Repeat this step for the register contents
	 * in V2, V3, and V4 respectively.
	 */
	VGFMAG	%v1,CONST_R2R1,%v1,%v5
	VGFMAG	%v2,CONST_R2R1,%v2,%v6
	VGFMAG	%v3,CONST_R2R1,%v3,%v7
	VGFMAG	%v4,CONST_R2R1,%v4,%v8

	aghi	%r3,64			/* BUF = BUF + 64 */
	aghi	%r4,-64			/* LEN = LEN - 64 */

	cghi	%r4,64
	jnl	.Lfold_64bytes_loop

.Lless_than_64bytes:
	/*
	 * Fold V1 to V4 into a single 128-bit value in V1.  Multiply V1 with R3
	 * and R4 and accumulating the next 128-bit chunk until a single 128-bit
	 * value remains.
	 */
	VGFMAG	%v1,CONST_R4R3,%v1,%v2
	VGFMAG	%v1,CONST_R4R3,%v1,%v3
	VGFMAG	%v1,CONST_R4R3,%v1,%v4

	cghi	%r4,16
	jl	.Lfinal_fold

.Lfold_16bytes_loop:

	VL	%v2,0,,%r3		/* Load next data chunk */
	VPERM	%v2,%v2,%v2,CONST_PERM_LE2BE
	VGFMAG	%v1,CONST_R4R3,%v1,%v2	/* Fold next data chunk */

	aghi	%r3,16
	aghi	%r4,-16

	cghi	%r4,16
	jnl	.Lfold_16bytes_loop

.Lfinal_fold:
	/*
	 * Set up a vector register for byte shifts.  The shift value must
	 * be loaded in bits 1-4 in byte element 7 of a vector register.
	 * Shift by 8 bytes: 0x40
	 * Shift by 4 bytes: 0x20
	 */
	VLEIB	%v9,0x40,7

	/*
	 * Prepare V0 for the next GF(2) multiplication: shift V0 by 8 bytes
	 * to move R4 into the rightmost doubleword and set the leftmost
	 * doubleword to 0x1.
	 */
	VSRLB	%v0,CONST_R4R3,%v9
	VLEIG	%v0,1,0

	/*
	 * Compute GF(2) product of V1 and V0.	The rightmost doubleword
	 * of V1 is multiplied with R4.  The leftmost doubleword of V1 is
	 * multiplied by 0x1 and is then XORed with rightmost product.
	 * Implicitly, the intermediate leftmost product becomes padded
	 */
	VGFMG	%v1,%v0,%v1

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
	VLEIB	%v9,0x20,7		  /* Shift by words */
	VSRLB	%v2,%v1,%v9		  /* Store remaining bits in V2 */
	VUPLLF	%v1,%v1			  /* Split rightmost doubleword */
	VGFMAG	%v1,CONST_R5,%v1,%v2	  /* V1 = (V1 * R5) XOR V2 */

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
	VUPLLF	%v2,%v1
	VGFMG	%v2,CONST_RU_POLY,%v2

	/*
	 * Compute the GF(2) product of the CRC polynomial with T1(x) in
	 * V2 and XOR the intermediate result, T2(x), with the value in V1.
	 * The final result is stored in word element 2 of V2.
	 */
	VUPLLF	%v2,%v2
	VGFMAG	%v2,CONST_CRC_POLY,%v2,%v1

.Ldone:
	VLGVF	%r2,%v2,2
	BR_EX	%r14
ENDPROC(crc32_le_vgfm_generic)

.previous
