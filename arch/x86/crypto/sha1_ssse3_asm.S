/*
 * This is a SIMD SHA-1 implementation. It requires the Intel(R) Supplemental
 * SSE3 instruction set extensions introduced in Intel Core Microarchitecture
 * processors. CPUs supporting Intel(R) AVX extensions will get an additional
 * boost.
 *
 * This work was inspired by the vectorized implementation of Dean Gaudet.
 * Additional information on it can be found at:
 *    http://www.arctic.org/~dean/crypto/sha1.html
 *
 * It was improved upon with more efficient vectorization of the message
 * scheduling. This implementation has also been optimized for all current and
 * several future generations of Intel CPUs.
 *
 * See this article for more information about the implementation details:
 *   http://software.intel.com/en-us/articles/improving-the-performance-of-the-secure-hash-algorithm-1/
 *
 * Copyright (C) 2010, Intel Corp.
 *   Authors: Maxim Locktyukhin <maxim.locktyukhin@intel.com>
 *            Ronen Zohar <ronen.zohar@intel.com>
 *
 * Converted to AT&T syntax and adapted for inclusion in the Linux kernel:
 *   Author: Mathias Krause <minipli@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define CTX	%rdi	// arg1
#define BUF	%rsi	// arg2
#define CNT	%rdx	// arg3

#define REG_A	%ecx
#define REG_B	%esi
#define REG_C	%edi
#define REG_D	%ebp
#define REG_E	%edx

#define REG_T1	%eax
#define REG_T2	%ebx

#define K_BASE		%r8
#define HASH_PTR	%r9
#define BUFFER_PTR	%r10
#define BUFFER_END	%r11

#define W_TMP1	%xmm0
#define W_TMP2	%xmm9

#define W0	%xmm1
#define W4	%xmm2
#define W8	%xmm3
#define W12	%xmm4
#define W16	%xmm5
#define W20	%xmm6
#define W24	%xmm7
#define W28	%xmm8

#define XMM_SHUFB_BSWAP	%xmm10

/* we keep window of 64 w[i]+K pre-calculated values in a circular buffer */
#define WK(t)	(((t) & 15) * 4)(%rsp)
#define W_PRECALC_AHEAD	16

/*
 * This macro implements the SHA-1 function's body for single 64-byte block
 * param: function's name
 */
.macro SHA1_VECTOR_ASM  name
	.global	\name
	.type	\name, @function
	.align 32
\name:
	push	%rbx
	push	%rbp
	push	%r12

	mov	%rsp, %r12
	sub	$64, %rsp		# allocate workspace
	and	$~15, %rsp		# align stack

	mov	CTX, HASH_PTR
	mov	BUF, BUFFER_PTR

	shl	$6, CNT			# multiply by 64
	add	BUF, CNT
	mov	CNT, BUFFER_END

	lea	K_XMM_AR(%rip), K_BASE
	xmm_mov	BSWAP_SHUFB_CTL(%rip), XMM_SHUFB_BSWAP

	SHA1_PIPELINED_MAIN_BODY

	# cleanup workspace
	mov	$8, %ecx
	mov	%rsp, %rdi
	xor	%rax, %rax
	rep stosq

	mov	%r12, %rsp		# deallocate workspace

	pop	%r12
	pop	%rbp
	pop	%rbx
	ret

	.size	\name, .-\name
.endm

/*
 * This macro implements 80 rounds of SHA-1 for one 64-byte block
 */
.macro SHA1_PIPELINED_MAIN_BODY
	INIT_REGALLOC

	mov	  (HASH_PTR), A
	mov	 4(HASH_PTR), B
	mov	 8(HASH_PTR), C
	mov	12(HASH_PTR), D
	mov	16(HASH_PTR), E

  .set i, 0
  .rept W_PRECALC_AHEAD
	W_PRECALC i
    .set i, (i+1)
  .endr

.align 4
1:
	RR F1,A,B,C,D,E,0
	RR F1,D,E,A,B,C,2
	RR F1,B,C,D,E,A,4
	RR F1,E,A,B,C,D,6
	RR F1,C,D,E,A,B,8

	RR F1,A,B,C,D,E,10
	RR F1,D,E,A,B,C,12
	RR F1,B,C,D,E,A,14
	RR F1,E,A,B,C,D,16
	RR F1,C,D,E,A,B,18

	RR F2,A,B,C,D,E,20
	RR F2,D,E,A,B,C,22
	RR F2,B,C,D,E,A,24
	RR F2,E,A,B,C,D,26
	RR F2,C,D,E,A,B,28

	RR F2,A,B,C,D,E,30
	RR F2,D,E,A,B,C,32
	RR F2,B,C,D,E,A,34
	RR F2,E,A,B,C,D,36
	RR F2,C,D,E,A,B,38

	RR F3,A,B,C,D,E,40
	RR F3,D,E,A,B,C,42
	RR F3,B,C,D,E,A,44
	RR F3,E,A,B,C,D,46
	RR F3,C,D,E,A,B,48

	RR F3,A,B,C,D,E,50
	RR F3,D,E,A,B,C,52
	RR F3,B,C,D,E,A,54
	RR F3,E,A,B,C,D,56
	RR F3,C,D,E,A,B,58

	add	$64, BUFFER_PTR		# move to the next 64-byte block
	cmp	BUFFER_END, BUFFER_PTR	# if the current is the last one use
	cmovae	K_BASE, BUFFER_PTR	# dummy source to avoid buffer overrun

	RR F4,A,B,C,D,E,60
	RR F4,D,E,A,B,C,62
	RR F4,B,C,D,E,A,64
	RR F4,E,A,B,C,D,66
	RR F4,C,D,E,A,B,68

	RR F4,A,B,C,D,E,70
	RR F4,D,E,A,B,C,72
	RR F4,B,C,D,E,A,74
	RR F4,E,A,B,C,D,76
	RR F4,C,D,E,A,B,78

	UPDATE_HASH   (HASH_PTR), A
	UPDATE_HASH  4(HASH_PTR), B
	UPDATE_HASH  8(HASH_PTR), C
	UPDATE_HASH 12(HASH_PTR), D
	UPDATE_HASH 16(HASH_PTR), E

	RESTORE_RENAMED_REGS
	cmp	K_BASE, BUFFER_PTR	# K_BASE means, we reached the end
	jne	1b
.endm

.macro INIT_REGALLOC
  .set A, REG_A
  .set B, REG_B
  .set C, REG_C
  .set D, REG_D
  .set E, REG_E
  .set T1, REG_T1
  .set T2, REG_T2
.endm

.macro RESTORE_RENAMED_REGS
	# order is important (REG_C is where it should be)
	mov	B, REG_B
	mov	D, REG_D
	mov	A, REG_A
	mov	E, REG_E
.endm

.macro SWAP_REG_NAMES  a, b
  .set _T, \a
  .set \a, \b
  .set \b, _T
.endm

.macro F1  b, c, d
	mov	\c, T1
	SWAP_REG_NAMES \c, T1
	xor	\d, T1
	and	\b, T1
	xor	\d, T1
.endm

.macro F2  b, c, d
	mov	\d, T1
	SWAP_REG_NAMES \d, T1
	xor	\c, T1
	xor	\b, T1
.endm

.macro F3  b, c ,d
	mov	\c, T1
	SWAP_REG_NAMES \c, T1
	mov	\b, T2
	or	\b, T1
	and	\c, T2
	and	\d, T1
	or	T2, T1
.endm

.macro F4  b, c, d
	F2 \b, \c, \d
.endm

.macro UPDATE_HASH  hash, val
	add	\hash, \val
	mov	\val, \hash
.endm

/*
 * RR does two rounds of SHA-1 back to back with W[] pre-calc
 *   t1 = F(b, c, d);   e += w(i)
 *   e += t1;           b <<= 30;   d  += w(i+1);
 *   t1 = F(a, b, c);
 *   d += t1;           a <<= 5;
 *   e += a;
 *   t1 = e;            a >>= 7;
 *   t1 <<= 5;
 *   d += t1;
 */
.macro RR  F, a, b, c, d, e, round
	add	WK(\round), \e
	\F   \b, \c, \d		# t1 = F(b, c, d);
	W_PRECALC (\round + W_PRECALC_AHEAD)
	rol	$30, \b
	add	T1, \e
	add	WK(\round + 1), \d

	\F   \a, \b, \c
	W_PRECALC (\round + W_PRECALC_AHEAD + 1)
	rol	$5, \a
	add	\a, \e
	add	T1, \d
	ror	$7, \a		# (a <<r 5) >>r 7) => a <<r 30)

	mov	\e, T1
	SWAP_REG_NAMES \e, T1

	rol	$5, T1
	add	T1, \d

	# write:  \a, \b
	# rotate: \a<=\d, \b<=\e, \c<=\a, \d<=\b, \e<=\c
.endm

.macro W_PRECALC  r
  .set i, \r

  .if (i < 20)
    .set K_XMM, 0
  .elseif (i < 40)
    .set K_XMM, 16
  .elseif (i < 60)
    .set K_XMM, 32
  .elseif (i < 80)
    .set K_XMM, 48
  .endif

  .if ((i < 16) || ((i >= 80) && (i < (80 + W_PRECALC_AHEAD))))
    .set i, ((\r) % 80)	    # pre-compute for the next iteration
    .if (i == 0)
	W_PRECALC_RESET
    .endif
	W_PRECALC_00_15
  .elseif (i<32)
	W_PRECALC_16_31
  .elseif (i < 80)   // rounds 32-79
	W_PRECALC_32_79
  .endif
.endm

.macro W_PRECALC_RESET
  .set W,          W0
  .set W_minus_04, W4
  .set W_minus_08, W8
  .set W_minus_12, W12
  .set W_minus_16, W16
  .set W_minus_20, W20
  .set W_minus_24, W24
  .set W_minus_28, W28
  .set W_minus_32, W
.endm

.macro W_PRECALC_ROTATE
  .set W_minus_32, W_minus_28
  .set W_minus_28, W_minus_24
  .set W_minus_24, W_minus_20
  .set W_minus_20, W_minus_16
  .set W_minus_16, W_minus_12
  .set W_minus_12, W_minus_08
  .set W_minus_08, W_minus_04
  .set W_minus_04, W
  .set W,          W_minus_32
.endm

.macro W_PRECALC_SSSE3

.macro W_PRECALC_00_15
	W_PRECALC_00_15_SSSE3
.endm
.macro W_PRECALC_16_31
	W_PRECALC_16_31_SSSE3
.endm
.macro W_PRECALC_32_79
	W_PRECALC_32_79_SSSE3
.endm

/* message scheduling pre-compute for rounds 0-15 */
.macro W_PRECALC_00_15_SSSE3
  .if ((i & 3) == 0)
	movdqu	(i*4)(BUFFER_PTR), W_TMP1
  .elseif ((i & 3) == 1)
	pshufb	XMM_SHUFB_BSWAP, W_TMP1
	movdqa	W_TMP1, W
  .elseif ((i & 3) == 2)
	paddd	(K_BASE), W_TMP1
  .elseif ((i & 3) == 3)
	movdqa  W_TMP1, WK(i&~3)
	W_PRECALC_ROTATE
  .endif
.endm

/* message scheduling pre-compute for rounds 16-31
 *
 * - calculating last 32 w[i] values in 8 XMM registers
 * - pre-calculate K+w[i] values and store to mem, for later load by ALU add
 *   instruction
 *
 * some "heavy-lifting" vectorization for rounds 16-31 due to w[i]->w[i-3]
 * dependency, but improves for 32-79
 */
.macro W_PRECALC_16_31_SSSE3
  # blended scheduling of vector and scalar instruction streams, one 4-wide
  # vector iteration / 4 scalar rounds
  .if ((i & 3) == 0)
	movdqa	W_minus_12, W
	palignr	$8, W_minus_16, W	# w[i-14]
	movdqa	W_minus_04, W_TMP1
	psrldq	$4, W_TMP1		# w[i-3]
	pxor	W_minus_08, W
  .elseif ((i & 3) == 1)
	pxor	W_minus_16, W_TMP1
	pxor	W_TMP1, W
	movdqa	W, W_TMP2
	movdqa	W, W_TMP1
	pslldq	$12, W_TMP2
  .elseif ((i & 3) == 2)
	psrld	$31, W
	pslld	$1, W_TMP1
	por	W, W_TMP1
	movdqa	W_TMP2, W
	psrld	$30, W_TMP2
	pslld	$2, W
  .elseif ((i & 3) == 3)
	pxor	W, W_TMP1
	pxor	W_TMP2, W_TMP1
	movdqa	W_TMP1, W
	paddd	K_XMM(K_BASE), W_TMP1
	movdqa	W_TMP1, WK(i&~3)
	W_PRECALC_ROTATE
  .endif
.endm

/* message scheduling pre-compute for rounds 32-79
 *
 * in SHA-1 specification: w[i] = (w[i-3] ^ w[i-8]  ^ w[i-14] ^ w[i-16]) rol 1
 * instead we do equal:    w[i] = (w[i-6] ^ w[i-16] ^ w[i-28] ^ w[i-32]) rol 2
 * allows more efficient vectorization since w[i]=>w[i-3] dependency is broken
 */
.macro W_PRECALC_32_79_SSSE3
  .if ((i & 3) == 0)
	movdqa	W_minus_04, W_TMP1
	pxor	W_minus_28, W		# W is W_minus_32 before xor
	palignr	$8, W_minus_08, W_TMP1
  .elseif ((i & 3) == 1)
	pxor	W_minus_16, W
	pxor	W_TMP1, W
	movdqa	W, W_TMP1
  .elseif ((i & 3) == 2)
	psrld	$30, W
	pslld	$2, W_TMP1
	por	W, W_TMP1
  .elseif ((i & 3) == 3)
	movdqa	W_TMP1, W
	paddd	K_XMM(K_BASE), W_TMP1
	movdqa	W_TMP1, WK(i&~3)
	W_PRECALC_ROTATE
  .endif
.endm

.endm		// W_PRECALC_SSSE3


#define K1	0x5a827999
#define K2	0x6ed9eba1
#define K3	0x8f1bbcdc
#define K4	0xca62c1d6

.section .rodata
.align 16

K_XMM_AR:
	.long K1, K1, K1, K1
	.long K2, K2, K2, K2
	.long K3, K3, K3, K3
	.long K4, K4, K4, K4

BSWAP_SHUFB_CTL:
	.long 0x00010203
	.long 0x04050607
	.long 0x08090a0b
	.long 0x0c0d0e0f


.section .text

W_PRECALC_SSSE3
.macro xmm_mov a, b
	movdqu	\a,\b
.endm

/* SSSE3 optimized implementation:
 *  extern "C" void sha1_transform_ssse3(u32 *digest, const char *data, u32 *ws,
 *                                       unsigned int rounds);
 */
SHA1_VECTOR_ASM     sha1_transform_ssse3

#ifdef CONFIG_AS_AVX

.macro W_PRECALC_AVX

.purgem W_PRECALC_00_15
.macro  W_PRECALC_00_15
    W_PRECALC_00_15_AVX
.endm
.purgem W_PRECALC_16_31
.macro  W_PRECALC_16_31
    W_PRECALC_16_31_AVX
.endm
.purgem W_PRECALC_32_79
.macro  W_PRECALC_32_79
    W_PRECALC_32_79_AVX
.endm

.macro W_PRECALC_00_15_AVX
  .if ((i & 3) == 0)
	vmovdqu	(i*4)(BUFFER_PTR), W_TMP1
  .elseif ((i & 3) == 1)
	vpshufb	XMM_SHUFB_BSWAP, W_TMP1, W
  .elseif ((i & 3) == 2)
	vpaddd	(K_BASE), W, W_TMP1
  .elseif ((i & 3) == 3)
	vmovdqa	W_TMP1, WK(i&~3)
	W_PRECALC_ROTATE
  .endif
.endm

.macro W_PRECALC_16_31_AVX
  .if ((i & 3) == 0)
	vpalignr $8, W_minus_16, W_minus_12, W	# w[i-14]
	vpsrldq	$4, W_minus_04, W_TMP1		# w[i-3]
	vpxor	W_minus_08, W, W
	vpxor	W_minus_16, W_TMP1, W_TMP1
  .elseif ((i & 3) == 1)
	vpxor	W_TMP1, W, W
	vpslldq	$12, W, W_TMP2
	vpslld	$1, W, W_TMP1
  .elseif ((i & 3) == 2)
	vpsrld	$31, W, W
	vpor	W, W_TMP1, W_TMP1
	vpslld	$2, W_TMP2, W
	vpsrld	$30, W_TMP2, W_TMP2
  .elseif ((i & 3) == 3)
	vpxor	W, W_TMP1, W_TMP1
	vpxor	W_TMP2, W_TMP1, W
	vpaddd	K_XMM(K_BASE), W, W_TMP1
	vmovdqu	W_TMP1, WK(i&~3)
	W_PRECALC_ROTATE
  .endif
.endm

.macro W_PRECALC_32_79_AVX
  .if ((i & 3) == 0)
	vpalignr $8, W_minus_08, W_minus_04, W_TMP1
	vpxor	W_minus_28, W, W		# W is W_minus_32 before xor
  .elseif ((i & 3) == 1)
	vpxor	W_minus_16, W_TMP1, W_TMP1
	vpxor	W_TMP1, W, W
  .elseif ((i & 3) == 2)
	vpslld	$2, W, W_TMP1
	vpsrld	$30, W, W
	vpor	W, W_TMP1, W
  .elseif ((i & 3) == 3)
	vpaddd	K_XMM(K_BASE), W, W_TMP1
	vmovdqu	W_TMP1, WK(i&~3)
	W_PRECALC_ROTATE
  .endif
.endm

.endm    // W_PRECALC_AVX

W_PRECALC_AVX
.purgem xmm_mov
.macro xmm_mov a, b
	vmovdqu	\a,\b
.endm


/* AVX optimized implementation:
 *  extern "C" void sha1_transform_avx(u32 *digest, const char *data, u32 *ws,
 *                                     unsigned int rounds);
 */
SHA1_VECTOR_ASM     sha1_transform_avx

#endif
