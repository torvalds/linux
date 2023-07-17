/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2012 Xyratex Technology Limited
 *
 * Using hardware provided PCLMULQDQ instruction to accelerate the CRC32
 * calculation.
 * CRC32 polynomial:0x04c11db7(BE)/0xEDB88320(LE)
 * PCLMULQDQ is a new instruction in Intel SSE4.2, the reference can be found
 * at:
 * http://www.intel.com/products/processor/manuals/
 * Intel(R) 64 and IA-32 Architectures Software Developer's Manual
 * Volume 2B: Instruction Set Reference, N-Z
 *
 * Authors:   Gregory Prestas <Gregory_Prestas@us.xyratex.com>
 *	      Alexander Boyko <Alexander_Boyko@xyratex.com>
 */

#include <linux/linkage.h>


.section .rodata
.align 16
/*
 * [x4*128+32 mod P(x) << 32)]'  << 1   = 0x154442bd4
 * #define CONSTANT_R1  0x154442bd4LL
 *
 * [(x4*128-32 mod P(x) << 32)]' << 1   = 0x1c6e41596
 * #define CONSTANT_R2  0x1c6e41596LL
 */
.Lconstant_R2R1:
	.octa 0x00000001c6e415960000000154442bd4
/*
 * [(x128+32 mod P(x) << 32)]'   << 1   = 0x1751997d0
 * #define CONSTANT_R3  0x1751997d0LL
 *
 * [(x128-32 mod P(x) << 32)]'   << 1   = 0x0ccaa009e
 * #define CONSTANT_R4  0x0ccaa009eLL
 */
.Lconstant_R4R3:
	.octa 0x00000000ccaa009e00000001751997d0
/*
 * [(x64 mod P(x) << 32)]'       << 1   = 0x163cd6124
 * #define CONSTANT_R5  0x163cd6124LL
 */
.Lconstant_R5:
	.octa 0x00000000000000000000000163cd6124
.Lconstant_mask32:
	.octa 0x000000000000000000000000FFFFFFFF
/*
 * #define CRCPOLY_TRUE_LE_FULL 0x1DB710641LL
 *
 * Barrett Reduction constant (u64`) = u` = (x**64 / P(x))` = 0x1F7011641LL
 * #define CONSTANT_RU  0x1F7011641LL
 */
.Lconstant_RUpoly:
	.octa 0x00000001F701164100000001DB710641

#define CONSTANT %xmm0

#ifdef __x86_64__
#define BUF     %rdi
#define LEN     %rsi
#define CRC     %edx
#else
#define BUF     %eax
#define LEN     %edx
#define CRC     %ecx
#endif



.text
/**
 *      Calculate crc32
 *      BUF - buffer (16 bytes aligned)
 *      LEN - sizeof buffer (16 bytes aligned), LEN should be grater than 63
 *      CRC - initial crc32
 *      return %eax crc32
 *      uint crc32_pclmul_le_16(unsigned char const *buffer,
 *	                     size_t len, uint crc32)
 */

SYM_FUNC_START(crc32_pclmul_le_16) /* buffer and buffer size are 16 bytes aligned */
	movdqa  (BUF), %xmm1
	movdqa  0x10(BUF), %xmm2
	movdqa  0x20(BUF), %xmm3
	movdqa  0x30(BUF), %xmm4
	movd    CRC, CONSTANT
	pxor    CONSTANT, %xmm1
	sub     $0x40, LEN
	add     $0x40, BUF
	cmp     $0x40, LEN
	jb      .Lless_64

#ifdef __x86_64__
	movdqa .Lconstant_R2R1(%rip), CONSTANT
#else
	movdqa .Lconstant_R2R1, CONSTANT
#endif

.Lloop_64:/*  64 bytes Full cache line folding */
	prefetchnta    0x40(BUF)
	movdqa  %xmm1, %xmm5
	movdqa  %xmm2, %xmm6
	movdqa  %xmm3, %xmm7
#ifdef __x86_64__
	movdqa  %xmm4, %xmm8
#endif
	pclmulqdq $0x00, CONSTANT, %xmm1
	pclmulqdq $0x00, CONSTANT, %xmm2
	pclmulqdq $0x00, CONSTANT, %xmm3
#ifdef __x86_64__
	pclmulqdq $0x00, CONSTANT, %xmm4
#endif
	pclmulqdq $0x11, CONSTANT, %xmm5
	pclmulqdq $0x11, CONSTANT, %xmm6
	pclmulqdq $0x11, CONSTANT, %xmm7
#ifdef __x86_64__
	pclmulqdq $0x11, CONSTANT, %xmm8
#endif
	pxor    %xmm5, %xmm1
	pxor    %xmm6, %xmm2
	pxor    %xmm7, %xmm3
#ifdef __x86_64__
	pxor    %xmm8, %xmm4
#else
	/* xmm8 unsupported for x32 */
	movdqa  %xmm4, %xmm5
	pclmulqdq $0x00, CONSTANT, %xmm4
	pclmulqdq $0x11, CONSTANT, %xmm5
	pxor    %xmm5, %xmm4
#endif

	pxor    (BUF), %xmm1
	pxor    0x10(BUF), %xmm2
	pxor    0x20(BUF), %xmm3
	pxor    0x30(BUF), %xmm4

	sub     $0x40, LEN
	add     $0x40, BUF
	cmp     $0x40, LEN
	jge     .Lloop_64
.Lless_64:/*  Folding cache line into 128bit */
#ifdef __x86_64__
	movdqa  .Lconstant_R4R3(%rip), CONSTANT
#else
	movdqa  .Lconstant_R4R3, CONSTANT
#endif
	prefetchnta     (BUF)

	movdqa  %xmm1, %xmm5
	pclmulqdq $0x00, CONSTANT, %xmm1
	pclmulqdq $0x11, CONSTANT, %xmm5
	pxor    %xmm5, %xmm1
	pxor    %xmm2, %xmm1

	movdqa  %xmm1, %xmm5
	pclmulqdq $0x00, CONSTANT, %xmm1
	pclmulqdq $0x11, CONSTANT, %xmm5
	pxor    %xmm5, %xmm1
	pxor    %xmm3, %xmm1

	movdqa  %xmm1, %xmm5
	pclmulqdq $0x00, CONSTANT, %xmm1
	pclmulqdq $0x11, CONSTANT, %xmm5
	pxor    %xmm5, %xmm1
	pxor    %xmm4, %xmm1

	cmp     $0x10, LEN
	jb      .Lfold_64
.Lloop_16:/* Folding rest buffer into 128bit */
	movdqa  %xmm1, %xmm5
	pclmulqdq $0x00, CONSTANT, %xmm1
	pclmulqdq $0x11, CONSTANT, %xmm5
	pxor    %xmm5, %xmm1
	pxor    (BUF), %xmm1
	sub     $0x10, LEN
	add     $0x10, BUF
	cmp     $0x10, LEN
	jge     .Lloop_16

.Lfold_64:
	/* perform the last 64 bit fold, also adds 32 zeroes
	 * to the input stream */
	pclmulqdq $0x01, %xmm1, CONSTANT /* R4 * xmm1.low */
	psrldq  $0x08, %xmm1
	pxor    CONSTANT, %xmm1

	/* final 32-bit fold */
	movdqa  %xmm1, %xmm2
#ifdef __x86_64__
	movdqa  .Lconstant_R5(%rip), CONSTANT
	movdqa  .Lconstant_mask32(%rip), %xmm3
#else
	movdqa  .Lconstant_R5, CONSTANT
	movdqa  .Lconstant_mask32, %xmm3
#endif
	psrldq  $0x04, %xmm2
	pand    %xmm3, %xmm1
	pclmulqdq $0x00, CONSTANT, %xmm1
	pxor    %xmm2, %xmm1

	/* Finish up with the bit-reversed barrett reduction 64 ==> 32 bits */
#ifdef __x86_64__
	movdqa  .Lconstant_RUpoly(%rip), CONSTANT
#else
	movdqa  .Lconstant_RUpoly, CONSTANT
#endif
	movdqa  %xmm1, %xmm2
	pand    %xmm3, %xmm1
	pclmulqdq $0x10, CONSTANT, %xmm1
	pand    %xmm3, %xmm1
	pclmulqdq $0x00, CONSTANT, %xmm1
	pxor    %xmm2, %xmm1
	pextrd  $0x01, %xmm1, %eax

	RET
SYM_FUNC_END(crc32_pclmul_le_16)
