/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated GHASH implementation with Intel PCLMULQDQ-NI
 * instructions. This file contains accelerated part of ghash
 * implementation. More information about PCLMULQDQ can be found at:
 *
 * https://www.intel.com/content/dam/develop/external/us/en/documents/clmul-wp-rev-2-02-2014-04-20.pdf
 *
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *	     Vinodh Gopal
 *	     Erdinc Ozturk
 *	     Deniz Karakoyunlu
 */

#include <linux/linkage.h>
#include <asm/frame.h>

.section	.rodata.cst16.bswap_mask, "aM", @progbits, 16
.align 16
.Lbswap_mask:
	.octa 0x000102030405060708090a0b0c0d0e0f

#define DATA	%xmm0
#define SHASH	%xmm1
#define T1	%xmm2
#define T2	%xmm3
#define T3	%xmm4
#define BSWAP	%xmm5
#define IN1	%xmm6

.text

/*
 * __clmul_gf128mul_ble:	internal ABI
 * input:
 *	DATA:			operand1
 *	SHASH:			operand2, hash_key << 1 mod poly
 * output:
 *	DATA:			operand1 * operand2 mod poly
 * changed:
 *	T1
 *	T2
 *	T3
 */
SYM_FUNC_START_LOCAL(__clmul_gf128mul_ble)
	movaps DATA, T1
	pshufd $0b01001110, DATA, T2
	pshufd $0b01001110, SHASH, T3
	pxor DATA, T2
	pxor SHASH, T3

	pclmulqdq $0x00, SHASH, DATA	# DATA = a0 * b0
	pclmulqdq $0x11, SHASH, T1	# T1 = a1 * b1
	pclmulqdq $0x00, T3, T2		# T2 = (a1 + a0) * (b1 + b0)
	pxor DATA, T2
	pxor T1, T2			# T2 = a0 * b1 + a1 * b0

	movaps T2, T3
	pslldq $8, T3
	psrldq $8, T2
	pxor T3, DATA
	pxor T2, T1			# <T1:DATA> is result of
					# carry-less multiplication

	# first phase of the reduction
	movaps DATA, T3
	psllq $1, T3
	pxor DATA, T3
	psllq $5, T3
	pxor DATA, T3
	psllq $57, T3
	movaps T3, T2
	pslldq $8, T2
	psrldq $8, T3
	pxor T2, DATA
	pxor T3, T1

	# second phase of the reduction
	movaps DATA, T2
	psrlq $5, T2
	pxor DATA, T2
	psrlq $1, T2
	pxor DATA, T2
	psrlq $1, T2
	pxor T2, T1
	pxor T1, DATA
	RET
SYM_FUNC_END(__clmul_gf128mul_ble)

/* void clmul_ghash_mul(char *dst, const le128 *shash) */
SYM_FUNC_START(clmul_ghash_mul)
	FRAME_BEGIN
	movups (%rdi), DATA
	movups (%rsi), SHASH
	movaps .Lbswap_mask, BSWAP
	pshufb BSWAP, DATA
	call __clmul_gf128mul_ble
	pshufb BSWAP, DATA
	movups DATA, (%rdi)
	FRAME_END
	RET
SYM_FUNC_END(clmul_ghash_mul)

/*
 * void clmul_ghash_update(char *dst, const char *src, unsigned int srclen,
 *			   const le128 *shash);
 */
SYM_FUNC_START(clmul_ghash_update)
	FRAME_BEGIN
	cmp $16, %rdx
	jb .Lupdate_just_ret	# check length
	movaps .Lbswap_mask, BSWAP
	movups (%rdi), DATA
	movups (%rcx), SHASH
	pshufb BSWAP, DATA
.align 4
.Lupdate_loop:
	movups (%rsi), IN1
	pshufb BSWAP, IN1
	pxor IN1, DATA
	call __clmul_gf128mul_ble
	sub $16, %rdx
	add $16, %rsi
	cmp $16, %rdx
	jge .Lupdate_loop
	pshufb BSWAP, DATA
	movups DATA, (%rdi)
.Lupdate_just_ret:
	FRAME_END
	RET
SYM_FUNC_END(clmul_ghash_update)
