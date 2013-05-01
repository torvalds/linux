/*
 * Accelerated GHASH implementation with Intel PCLMULQDQ-NI
 * instructions. This file contains accelerated part of ghash
 * implementation. More information about PCLMULQDQ can be found at:
 *
 * http://software.intel.com/en-us/articles/carry-less-multiplication-and-its-usage-for-computing-the-gcm-mode/
 *
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *	     Vinodh Gopal
 *	     Erdinc Ozturk
 *	     Deniz Karakoyunlu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/inst.h>

.data

.align 16
.Lbswap_mask:
	.octa 0x000102030405060708090a0b0c0d0e0f
.Lpoly:
	.octa 0xc2000000000000000000000000000001
.Ltwo_one:
	.octa 0x00000001000000000000000000000001

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
__clmul_gf128mul_ble:
	movaps DATA, T1
	pshufd $0b01001110, DATA, T2
	pshufd $0b01001110, SHASH, T3
	pxor DATA, T2
	pxor SHASH, T3

	PCLMULQDQ 0x00 SHASH DATA	# DATA = a0 * b0
	PCLMULQDQ 0x11 SHASH T1		# T1 = a1 * b1
	PCLMULQDQ 0x00 T3 T2		# T2 = (a1 + a0) * (b1 + b0)
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
	ret
ENDPROC(__clmul_gf128mul_ble)

/* void clmul_ghash_mul(char *dst, const be128 *shash) */
ENTRY(clmul_ghash_mul)
	movups (%rdi), DATA
	movups (%rsi), SHASH
	movaps .Lbswap_mask, BSWAP
	PSHUFB_XMM BSWAP DATA
	call __clmul_gf128mul_ble
	PSHUFB_XMM BSWAP DATA
	movups DATA, (%rdi)
	ret
ENDPROC(clmul_ghash_mul)

/*
 * void clmul_ghash_update(char *dst, const char *src, unsigned int srclen,
 *			   const be128 *shash);
 */
ENTRY(clmul_ghash_update)
	cmp $16, %rdx
	jb .Lupdate_just_ret	# check length
	movaps .Lbswap_mask, BSWAP
	movups (%rdi), DATA
	movups (%rcx), SHASH
	PSHUFB_XMM BSWAP DATA
.align 4
.Lupdate_loop:
	movups (%rsi), IN1
	PSHUFB_XMM BSWAP IN1
	pxor IN1, DATA
	call __clmul_gf128mul_ble
	sub $16, %rdx
	add $16, %rsi
	cmp $16, %rdx
	jge .Lupdate_loop
	PSHUFB_XMM BSWAP DATA
	movups DATA, (%rdi)
.Lupdate_just_ret:
	ret
ENDPROC(clmul_ghash_update)

/*
 * void clmul_ghash_setkey(be128 *shash, const u8 *key);
 *
 * Calculate hash_key << 1 mod poly
 */
ENTRY(clmul_ghash_setkey)
	movaps .Lbswap_mask, BSWAP
	movups (%rsi), %xmm0
	PSHUFB_XMM BSWAP %xmm0
	movaps %xmm0, %xmm1
	psllq $1, %xmm0
	psrlq $63, %xmm1
	movaps %xmm1, %xmm2
	pslldq $8, %xmm1
	psrldq $8, %xmm2
	por %xmm1, %xmm0
	# reduction
	pshufd $0b00100100, %xmm2, %xmm1
	pcmpeqd .Ltwo_one, %xmm1
	pand .Lpoly, %xmm1
	pxor %xmm1, %xmm0
	movups %xmm0, (%rdi)
	ret
ENDPROC(clmul_ghash_setkey)
