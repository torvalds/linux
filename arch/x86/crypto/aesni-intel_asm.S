/*
 * Implement AES algorithm in Intel AES-NI instructions.
 *
 * The white paper of AES-NI instructions can be downloaded from:
 *   http://softwarecommunity.intel.com/isn/downloads/intelavx/AES-Instructions-Set_WP.pdf
 *
 * Copyright (C) 2008, Intel Corp.
 *    Author: Huang Ying <ying.huang@intel.com>
 *            Vinodh Gopal <vinodh.gopal@intel.com>
 *            Kahraman Akdemir
 *
 * Added RFC4106 AES-GCM support for 128-bit keys under the AEAD
 * interface for 64-bit kernels.
 *    Authors: Erdinc Ozturk (erdinc.ozturk@intel.com)
 *             Aidan O'Mahony (aidan.o.mahony@intel.com)
 *             Adrian Hoban <adrian.hoban@intel.com>
 *             James Guilford (james.guilford@intel.com)
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Tadeusz Struk (tadeusz.struk@intel.com)
 *             Wajdi Feghali (wajdi.k.feghali@intel.com)
 *    Copyright (c) 2010, Intel Corporation.
 *
 * Ported x86_64 version to x86:
 *    Author: Mathias Krause <minipli@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/linkage.h>
#include <asm/inst.h>

#ifdef __x86_64__
.data
POLY:   .octa 0xC2000000000000000000000000000001
TWOONE: .octa 0x00000001000000000000000000000001

# order of these constants should not change.
# more specifically, ALL_F should follow SHIFT_MASK,
# and ZERO should follow ALL_F

SHUF_MASK:  .octa 0x000102030405060708090A0B0C0D0E0F
MASK1:      .octa 0x0000000000000000ffffffffffffffff
MASK2:      .octa 0xffffffffffffffff0000000000000000
SHIFT_MASK: .octa 0x0f0e0d0c0b0a09080706050403020100
ALL_F:      .octa 0xffffffffffffffffffffffffffffffff
ZERO:       .octa 0x00000000000000000000000000000000
ONE:        .octa 0x00000000000000000000000000000001
F_MIN_MASK: .octa 0xf1f2f3f4f5f6f7f8f9fafbfcfdfeff0
dec:        .octa 0x1
enc:        .octa 0x2


.text


#define	STACK_OFFSET    8*3
#define	HashKey		16*0	// store HashKey <<1 mod poly here
#define	HashKey_2	16*1	// store HashKey^2 <<1 mod poly here
#define	HashKey_3	16*2	// store HashKey^3 <<1 mod poly here
#define	HashKey_4	16*3	// store HashKey^4 <<1 mod poly here
#define	HashKey_k	16*4	// store XOR of High 64 bits and Low 64
				// bits of  HashKey <<1 mod poly here
				//(for Karatsuba purposes)
#define	HashKey_2_k	16*5	// store XOR of High 64 bits and Low 64
				// bits of  HashKey^2 <<1 mod poly here
				// (for Karatsuba purposes)
#define	HashKey_3_k	16*6	// store XOR of High 64 bits and Low 64
				// bits of  HashKey^3 <<1 mod poly here
				// (for Karatsuba purposes)
#define	HashKey_4_k	16*7	// store XOR of High 64 bits and Low 64
				// bits of  HashKey^4 <<1 mod poly here
				// (for Karatsuba purposes)
#define	VARIABLE_OFFSET	16*8

#define arg1 rdi
#define arg2 rsi
#define arg3 rdx
#define arg4 rcx
#define arg5 r8
#define arg6 r9
#define arg7 STACK_OFFSET+8(%r14)
#define arg8 STACK_OFFSET+16(%r14)
#define arg9 STACK_OFFSET+24(%r14)
#define arg10 STACK_OFFSET+32(%r14)
#endif


#define STATE1	%xmm0
#define STATE2	%xmm4
#define STATE3	%xmm5
#define STATE4	%xmm6
#define STATE	STATE1
#define IN1	%xmm1
#define IN2	%xmm7
#define IN3	%xmm8
#define IN4	%xmm9
#define IN	IN1
#define KEY	%xmm2
#define IV	%xmm3

#define BSWAP_MASK %xmm10
#define CTR	%xmm11
#define INC	%xmm12

#ifdef __x86_64__
#define AREG	%rax
#define KEYP	%rdi
#define OUTP	%rsi
#define UKEYP	OUTP
#define INP	%rdx
#define LEN	%rcx
#define IVP	%r8
#define KLEN	%r9d
#define T1	%r10
#define TKEYP	T1
#define T2	%r11
#define TCTR_LOW T2
#else
#define AREG	%eax
#define KEYP	%edi
#define OUTP	AREG
#define UKEYP	OUTP
#define INP	%edx
#define LEN	%esi
#define IVP	%ebp
#define KLEN	%ebx
#define T1	%ecx
#define TKEYP	T1
#endif


#ifdef __x86_64__
/* GHASH_MUL MACRO to implement: Data*HashKey mod (128,127,126,121,0)
*
*
* Input: A and B (128-bits each, bit-reflected)
* Output: C = A*B*x mod poly, (i.e. >>1 )
* To compute GH = GH*HashKey mod poly, give HK = HashKey<<1 mod poly as input
* GH = GH * HK * x mod poly which is equivalent to GH*HashKey mod poly.
*
*/
.macro GHASH_MUL GH HK TMP1 TMP2 TMP3 TMP4 TMP5
	movdqa	  \GH, \TMP1
	pshufd	  $78, \GH, \TMP2
	pshufd	  $78, \HK, \TMP3
	pxor	  \GH, \TMP2            # TMP2 = a1+a0
	pxor	  \HK, \TMP3            # TMP3 = b1+b0
	PCLMULQDQ 0x11, \HK, \TMP1     # TMP1 = a1*b1
	PCLMULQDQ 0x00, \HK, \GH       # GH = a0*b0
	PCLMULQDQ 0x00, \TMP3, \TMP2   # TMP2 = (a0+a1)*(b1+b0)
	pxor	  \GH, \TMP2
	pxor	  \TMP1, \TMP2          # TMP2 = (a0*b0)+(a1*b0)
	movdqa	  \TMP2, \TMP3
	pslldq	  $8, \TMP3             # left shift TMP3 2 DWs
	psrldq	  $8, \TMP2             # right shift TMP2 2 DWs
	pxor	  \TMP3, \GH
	pxor	  \TMP2, \TMP1          # TMP2:GH holds the result of GH*HK

        # first phase of the reduction

	movdqa    \GH, \TMP2
	movdqa    \GH, \TMP3
	movdqa    \GH, \TMP4            # copy GH into TMP2,TMP3 and TMP4
					# in in order to perform
					# independent shifts
	pslld     $31, \TMP2            # packed right shift <<31
	pslld     $30, \TMP3            # packed right shift <<30
	pslld     $25, \TMP4            # packed right shift <<25
	pxor      \TMP3, \TMP2          # xor the shifted versions
	pxor      \TMP4, \TMP2
	movdqa    \TMP2, \TMP5
	psrldq    $4, \TMP5             # right shift TMP5 1 DW
	pslldq    $12, \TMP2            # left shift TMP2 3 DWs
	pxor      \TMP2, \GH

        # second phase of the reduction

	movdqa    \GH,\TMP2             # copy GH into TMP2,TMP3 and TMP4
					# in in order to perform
					# independent shifts
	movdqa    \GH,\TMP3
	movdqa    \GH,\TMP4
	psrld     $1,\TMP2              # packed left shift >>1
	psrld     $2,\TMP3              # packed left shift >>2
	psrld     $7,\TMP4              # packed left shift >>7
	pxor      \TMP3,\TMP2		# xor the shifted versions
	pxor      \TMP4,\TMP2
	pxor      \TMP5, \TMP2
	pxor      \TMP2, \GH
	pxor      \TMP1, \GH            # result is in TMP1
.endm

/*
* if a = number of total plaintext bytes
* b = floor(a/16)
* num_initial_blocks = b mod 4
* encrypt the initial num_initial_blocks blocks and apply ghash on
* the ciphertext
* %r10, %r11, %r12, %rax, %xmm5, %xmm6, %xmm7, %xmm8, %xmm9 registers
* are clobbered
* arg1, %arg2, %arg3, %r14 are used as a pointer only, not modified
*/


.macro INITIAL_BLOCKS_DEC num_initial_blocks TMP1 TMP2 TMP3 TMP4 TMP5 XMM0 XMM1 \
XMM2 XMM3 XMM4 XMMDst TMP6 TMP7 i i_seq operation
	mov	   arg7, %r10           # %r10 = AAD
	mov	   arg8, %r12           # %r12 = aadLen
	mov	   %r12, %r11
	pxor	   %xmm\i, %xmm\i
_get_AAD_loop\num_initial_blocks\operation:
	movd	   (%r10), \TMP1
	pslldq	   $12, \TMP1
	psrldq	   $4, %xmm\i
	pxor	   \TMP1, %xmm\i
	add	   $4, %r10
	sub	   $4, %r12
	jne	   _get_AAD_loop\num_initial_blocks\operation
	cmp	   $16, %r11
	je	   _get_AAD_loop2_done\num_initial_blocks\operation
	mov	   $16, %r12
_get_AAD_loop2\num_initial_blocks\operation:
	psrldq	   $4, %xmm\i
	sub	   $4, %r12
	cmp	   %r11, %r12
	jne	   _get_AAD_loop2\num_initial_blocks\operation
_get_AAD_loop2_done\num_initial_blocks\operation:
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM   %xmm14, %xmm\i # byte-reflect the AAD data

	xor	   %r11, %r11 # initialise the data pointer offset as zero

        # start AES for num_initial_blocks blocks

	mov	   %arg5, %rax                      # %rax = *Y0
	movdqu	   (%rax), \XMM0                    # XMM0 = Y0
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM   %xmm14, \XMM0

.if (\i == 5) || (\i == 6) || (\i == 7)
.irpc index, \i_seq
	paddd	   ONE(%rip), \XMM0                 # INCR Y0
	movdqa	   \XMM0, %xmm\index
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM   %xmm14, %xmm\index      # perform a 16 byte swap

.endr
.irpc index, \i_seq
	pxor	   16*0(%arg1), %xmm\index
.endr
.irpc index, \i_seq
	movaps 0x10(%rdi), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 1
.endr
.irpc index, \i_seq
	movaps 0x20(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x30(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x40(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x50(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x60(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x70(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x80(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x90(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0xa0(%arg1), \TMP1
	AESENCLAST \TMP1, %xmm\index         # Round 10
.endr
.irpc index, \i_seq
	movdqu	   (%arg3 , %r11, 1), \TMP1
	pxor	   \TMP1, %xmm\index
	movdqu	   %xmm\index, (%arg2 , %r11, 1)
	# write back plaintext/ciphertext for num_initial_blocks
	add	   $16, %r11

	movdqa     \TMP1, %xmm\index
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM	   %xmm14, %xmm\index

		# prepare plaintext/ciphertext for GHASH computation
.endr
.endif
	GHASH_MUL  %xmm\i, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
        # apply GHASH on num_initial_blocks blocks

.if \i == 5
        pxor       %xmm5, %xmm6
	GHASH_MUL  %xmm6, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
        pxor       %xmm6, %xmm7
	GHASH_MUL  %xmm7, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
        pxor       %xmm7, %xmm8
	GHASH_MUL  %xmm8, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
.elseif \i == 6
        pxor       %xmm6, %xmm7
	GHASH_MUL  %xmm7, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
        pxor       %xmm7, %xmm8
	GHASH_MUL  %xmm8, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
.elseif \i == 7
        pxor       %xmm7, %xmm8
	GHASH_MUL  %xmm8, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
.endif
	cmp	   $64, %r13
	jl	_initial_blocks_done\num_initial_blocks\operation
	# no need for precomputed values
/*
*
* Precomputations for HashKey parallel with encryption of first 4 blocks.
* Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
*/
	paddd	   ONE(%rip), \XMM0              # INCR Y0
	movdqa	   \XMM0, \XMM1
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM  %xmm14, \XMM1        # perform a 16 byte swap

	paddd	   ONE(%rip), \XMM0              # INCR Y0
	movdqa	   \XMM0, \XMM2
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM  %xmm14, \XMM2        # perform a 16 byte swap

	paddd	   ONE(%rip), \XMM0              # INCR Y0
	movdqa	   \XMM0, \XMM3
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM3        # perform a 16 byte swap

	paddd	   ONE(%rip), \XMM0              # INCR Y0
	movdqa	   \XMM0, \XMM4
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM4        # perform a 16 byte swap

	pxor	   16*0(%arg1), \XMM1
	pxor	   16*0(%arg1), \XMM2
	pxor	   16*0(%arg1), \XMM3
	pxor	   16*0(%arg1), \XMM4
	movdqa	   \TMP3, \TMP5
	pshufd	   $78, \TMP3, \TMP1
	pxor	   \TMP3, \TMP1
	movdqa	   \TMP1, HashKey_k(%rsp)
	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^2<<1 (mod poly)
	movdqa	   \TMP5, HashKey_2(%rsp)
# HashKey_2 = HashKey^2<<1 (mod poly)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqa	   \TMP1, HashKey_2_k(%rsp)
.irpc index, 1234 # do 4 rounds
	movaps 0x10*\index(%arg1), \TMP1
	AESENC	   \TMP1, \XMM1
	AESENC	   \TMP1, \XMM2
	AESENC	   \TMP1, \XMM3
	AESENC	   \TMP1, \XMM4
.endr
	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^3<<1 (mod poly)
	movdqa	   \TMP5, HashKey_3(%rsp)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqa	   \TMP1, HashKey_3_k(%rsp)
.irpc index, 56789 # do next 5 rounds
	movaps 0x10*\index(%arg1), \TMP1
	AESENC	   \TMP1, \XMM1
	AESENC	   \TMP1, \XMM2
	AESENC	   \TMP1, \XMM3
	AESENC	   \TMP1, \XMM4
.endr
	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^3<<1 (mod poly)
	movdqa	   \TMP5, HashKey_4(%rsp)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqa	   \TMP1, HashKey_4_k(%rsp)
	movaps 0xa0(%arg1), \TMP2
	AESENCLAST \TMP2, \XMM1
	AESENCLAST \TMP2, \XMM2
	AESENCLAST \TMP2, \XMM3
	AESENCLAST \TMP2, \XMM4
	movdqu	   16*0(%arg3 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM1
	movdqu	   \XMM1, 16*0(%arg2 , %r11 , 1)
	movdqa     \TMP1, \XMM1
	movdqu	   16*1(%arg3 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM2
	movdqu	   \XMM2, 16*1(%arg2 , %r11 , 1)
	movdqa     \TMP1, \XMM2
	movdqu	   16*2(%arg3 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM3
	movdqu	   \XMM3, 16*2(%arg2 , %r11 , 1)
	movdqa     \TMP1, \XMM3
	movdqu	   16*3(%arg3 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM4
	movdqu	   \XMM4, 16*3(%arg2 , %r11 , 1)
	movdqa     \TMP1, \XMM4
	add	   $64, %r11
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM1 # perform a 16 byte swap
	pxor	   \XMMDst, \XMM1
# combine GHASHed value with the corresponding ciphertext
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM2 # perform a 16 byte swap
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM3 # perform a 16 byte swap
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM4 # perform a 16 byte swap

_initial_blocks_done\num_initial_blocks\operation:

.endm


/*
* if a = number of total plaintext bytes
* b = floor(a/16)
* num_initial_blocks = b mod 4
* encrypt the initial num_initial_blocks blocks and apply ghash on
* the ciphertext
* %r10, %r11, %r12, %rax, %xmm5, %xmm6, %xmm7, %xmm8, %xmm9 registers
* are clobbered
* arg1, %arg2, %arg3, %r14 are used as a pointer only, not modified
*/


.macro INITIAL_BLOCKS_ENC num_initial_blocks TMP1 TMP2 TMP3 TMP4 TMP5 XMM0 XMM1 \
XMM2 XMM3 XMM4 XMMDst TMP6 TMP7 i i_seq operation
	mov	   arg7, %r10           # %r10 = AAD
	mov	   arg8, %r12           # %r12 = aadLen
	mov	   %r12, %r11
	pxor	   %xmm\i, %xmm\i
_get_AAD_loop\num_initial_blocks\operation:
	movd	   (%r10), \TMP1
	pslldq	   $12, \TMP1
	psrldq	   $4, %xmm\i
	pxor	   \TMP1, %xmm\i
	add	   $4, %r10
	sub	   $4, %r12
	jne	   _get_AAD_loop\num_initial_blocks\operation
	cmp	   $16, %r11
	je	   _get_AAD_loop2_done\num_initial_blocks\operation
	mov	   $16, %r12
_get_AAD_loop2\num_initial_blocks\operation:
	psrldq	   $4, %xmm\i
	sub	   $4, %r12
	cmp	   %r11, %r12
	jne	   _get_AAD_loop2\num_initial_blocks\operation
_get_AAD_loop2_done\num_initial_blocks\operation:
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM   %xmm14, %xmm\i # byte-reflect the AAD data

	xor	   %r11, %r11 # initialise the data pointer offset as zero

        # start AES for num_initial_blocks blocks

	mov	   %arg5, %rax                      # %rax = *Y0
	movdqu	   (%rax), \XMM0                    # XMM0 = Y0
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM   %xmm14, \XMM0

.if (\i == 5) || (\i == 6) || (\i == 7)
.irpc index, \i_seq
	paddd	   ONE(%rip), \XMM0                 # INCR Y0
	movdqa	   \XMM0, %xmm\index
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM   %xmm14, %xmm\index      # perform a 16 byte swap

.endr
.irpc index, \i_seq
	pxor	   16*0(%arg1), %xmm\index
.endr
.irpc index, \i_seq
	movaps 0x10(%rdi), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 1
.endr
.irpc index, \i_seq
	movaps 0x20(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x30(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x40(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x50(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x60(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x70(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x80(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0x90(%arg1), \TMP1
	AESENC     \TMP1, %xmm\index          # Round 2
.endr
.irpc index, \i_seq
	movaps 0xa0(%arg1), \TMP1
	AESENCLAST \TMP1, %xmm\index         # Round 10
.endr
.irpc index, \i_seq
	movdqu	   (%arg3 , %r11, 1), \TMP1
	pxor	   \TMP1, %xmm\index
	movdqu	   %xmm\index, (%arg2 , %r11, 1)
	# write back plaintext/ciphertext for num_initial_blocks
	add	   $16, %r11

        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM	   %xmm14, %xmm\index

		# prepare plaintext/ciphertext for GHASH computation
.endr
.endif
	GHASH_MUL  %xmm\i, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
        # apply GHASH on num_initial_blocks blocks

.if \i == 5
        pxor       %xmm5, %xmm6
	GHASH_MUL  %xmm6, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
        pxor       %xmm6, %xmm7
	GHASH_MUL  %xmm7, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
        pxor       %xmm7, %xmm8
	GHASH_MUL  %xmm8, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
.elseif \i == 6
        pxor       %xmm6, %xmm7
	GHASH_MUL  %xmm7, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
        pxor       %xmm7, %xmm8
	GHASH_MUL  %xmm8, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
.elseif \i == 7
        pxor       %xmm7, %xmm8
	GHASH_MUL  %xmm8, \TMP3, \TMP1, \TMP2, \TMP4, \TMP5, \XMM1
.endif
	cmp	   $64, %r13
	jl	_initial_blocks_done\num_initial_blocks\operation
	# no need for precomputed values
/*
*
* Precomputations for HashKey parallel with encryption of first 4 blocks.
* Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
*/
	paddd	   ONE(%rip), \XMM0              # INCR Y0
	movdqa	   \XMM0, \XMM1
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM  %xmm14, \XMM1        # perform a 16 byte swap

	paddd	   ONE(%rip), \XMM0              # INCR Y0
	movdqa	   \XMM0, \XMM2
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM  %xmm14, \XMM2        # perform a 16 byte swap

	paddd	   ONE(%rip), \XMM0              # INCR Y0
	movdqa	   \XMM0, \XMM3
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM3        # perform a 16 byte swap

	paddd	   ONE(%rip), \XMM0              # INCR Y0
	movdqa	   \XMM0, \XMM4
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM4        # perform a 16 byte swap

	pxor	   16*0(%arg1), \XMM1
	pxor	   16*0(%arg1), \XMM2
	pxor	   16*0(%arg1), \XMM3
	pxor	   16*0(%arg1), \XMM4
	movdqa	   \TMP3, \TMP5
	pshufd	   $78, \TMP3, \TMP1
	pxor	   \TMP3, \TMP1
	movdqa	   \TMP1, HashKey_k(%rsp)
	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^2<<1 (mod poly)
	movdqa	   \TMP5, HashKey_2(%rsp)
# HashKey_2 = HashKey^2<<1 (mod poly)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqa	   \TMP1, HashKey_2_k(%rsp)
.irpc index, 1234 # do 4 rounds
	movaps 0x10*\index(%arg1), \TMP1
	AESENC	   \TMP1, \XMM1
	AESENC	   \TMP1, \XMM2
	AESENC	   \TMP1, \XMM3
	AESENC	   \TMP1, \XMM4
.endr
	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^3<<1 (mod poly)
	movdqa	   \TMP5, HashKey_3(%rsp)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqa	   \TMP1, HashKey_3_k(%rsp)
.irpc index, 56789 # do next 5 rounds
	movaps 0x10*\index(%arg1), \TMP1
	AESENC	   \TMP1, \XMM1
	AESENC	   \TMP1, \XMM2
	AESENC	   \TMP1, \XMM3
	AESENC	   \TMP1, \XMM4
.endr
	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^3<<1 (mod poly)
	movdqa	   \TMP5, HashKey_4(%rsp)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqa	   \TMP1, HashKey_4_k(%rsp)
	movaps 0xa0(%arg1), \TMP2
	AESENCLAST \TMP2, \XMM1
	AESENCLAST \TMP2, \XMM2
	AESENCLAST \TMP2, \XMM3
	AESENCLAST \TMP2, \XMM4
	movdqu	   16*0(%arg3 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM1
	movdqu	   16*1(%arg3 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM2
	movdqu	   16*2(%arg3 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM3
	movdqu	   16*3(%arg3 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM4
	movdqu     \XMM1, 16*0(%arg2 , %r11 , 1)
	movdqu     \XMM2, 16*1(%arg2 , %r11 , 1)
	movdqu     \XMM3, 16*2(%arg2 , %r11 , 1)
	movdqu     \XMM4, 16*3(%arg2 , %r11 , 1)

	add	   $64, %r11
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM1 # perform a 16 byte swap
	pxor	   \XMMDst, \XMM1
# combine GHASHed value with the corresponding ciphertext
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM2 # perform a 16 byte swap
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM3 # perform a 16 byte swap
        movdqa     SHUF_MASK(%rip), %xmm14
	PSHUFB_XMM %xmm14, \XMM4 # perform a 16 byte swap

_initial_blocks_done\num_initial_blocks\operation:

.endm

/*
* encrypt 4 blocks at a time
* ghash the 4 previously encrypted ciphertext blocks
* arg1, %arg2, %arg3 are used as pointers only, not modified
* %r11 is the data offset value
*/
.macro GHASH_4_ENCRYPT_4_PARALLEL_ENC TMP1 TMP2 TMP3 TMP4 TMP5 \
TMP6 XMM0 XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7 XMM8 operation

	movdqa	  \XMM1, \XMM5
	movdqa	  \XMM2, \XMM6
	movdqa	  \XMM3, \XMM7
	movdqa	  \XMM4, \XMM8

        movdqa    SHUF_MASK(%rip), %xmm15
        # multiply TMP5 * HashKey using karatsuba

	movdqa	  \XMM5, \TMP4
	pshufd	  $78, \XMM5, \TMP6
	pxor	  \XMM5, \TMP6
	paddd     ONE(%rip), \XMM0		# INCR CNT
	movdqa	  HashKey_4(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP4           # TMP4 = a1*b1
	movdqa    \XMM0, \XMM1
	paddd     ONE(%rip), \XMM0		# INCR CNT
	movdqa    \XMM0, \XMM2
	paddd     ONE(%rip), \XMM0		# INCR CNT
	movdqa    \XMM0, \XMM3
	paddd     ONE(%rip), \XMM0		# INCR CNT
	movdqa    \XMM0, \XMM4
	PSHUFB_XMM %xmm15, \XMM1	# perform a 16 byte swap
	PCLMULQDQ 0x00, \TMP5, \XMM5           # XMM5 = a0*b0
	PSHUFB_XMM %xmm15, \XMM2	# perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM3	# perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM4	# perform a 16 byte swap

	pxor	  (%arg1), \XMM1
	pxor	  (%arg1), \XMM2
	pxor	  (%arg1), \XMM3
	pxor	  (%arg1), \XMM4
	movdqa	  HashKey_4_k(%rsp), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP6           # TMP6 = (a1+a0)*(b1+b0)
	movaps 0x10(%arg1), \TMP1
	AESENC	  \TMP1, \XMM1              # Round 1
	AESENC	  \TMP1, \XMM2
	AESENC	  \TMP1, \XMM3
	AESENC	  \TMP1, \XMM4
	movaps 0x20(%arg1), \TMP1
	AESENC	  \TMP1, \XMM1              # Round 2
	AESENC	  \TMP1, \XMM2
	AESENC	  \TMP1, \XMM3
	AESENC	  \TMP1, \XMM4
	movdqa	  \XMM6, \TMP1
	pshufd	  $78, \XMM6, \TMP2
	pxor	  \XMM6, \TMP2
	movdqa	  HashKey_3(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1           # TMP1 = a1 * b1
	movaps 0x30(%arg1), \TMP3
	AESENC    \TMP3, \XMM1              # Round 3
	AESENC    \TMP3, \XMM2
	AESENC    \TMP3, \XMM3
	AESENC    \TMP3, \XMM4
	PCLMULQDQ 0x00, \TMP5, \XMM6           # XMM6 = a0*b0
	movaps 0x40(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1              # Round 4
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	movdqa	  HashKey_3_k(%rsp), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP2           # TMP2 = (a1+a0)*(b1+b0)
	movaps 0x50(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1              # Round 5
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	pxor	  \TMP1, \TMP4
# accumulate the results in TMP4:XMM5, TMP6 holds the middle part
	pxor	  \XMM6, \XMM5
	pxor	  \TMP2, \TMP6
	movdqa	  \XMM7, \TMP1
	pshufd	  $78, \XMM7, \TMP2
	pxor	  \XMM7, \TMP2
	movdqa	  HashKey_2(%rsp ), \TMP5

        # Multiply TMP5 * HashKey using karatsuba

	PCLMULQDQ 0x11, \TMP5, \TMP1           # TMP1 = a1*b1
	movaps 0x60(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1              # Round 6
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	PCLMULQDQ 0x00, \TMP5, \XMM7           # XMM7 = a0*b0
	movaps 0x70(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1             # Round 7
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	movdqa	  HashKey_2_k(%rsp), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP2           # TMP2 = (a1+a0)*(b1+b0)
	movaps 0x80(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1             # Round 8
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	pxor	  \TMP1, \TMP4
# accumulate the results in TMP4:XMM5, TMP6 holds the middle part
	pxor	  \XMM7, \XMM5
	pxor	  \TMP2, \TMP6

        # Multiply XMM8 * HashKey
        # XMM8 and TMP5 hold the values for the two operands

	movdqa	  \XMM8, \TMP1
	pshufd	  $78, \XMM8, \TMP2
	pxor	  \XMM8, \TMP2
	movdqa	  HashKey(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1          # TMP1 = a1*b1
	movaps 0x90(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1            # Round 9
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	PCLMULQDQ 0x00, \TMP5, \XMM8          # XMM8 = a0*b0
	movaps 0xa0(%arg1), \TMP3
	AESENCLAST \TMP3, \XMM1           # Round 10
	AESENCLAST \TMP3, \XMM2
	AESENCLAST \TMP3, \XMM3
	AESENCLAST \TMP3, \XMM4
	movdqa    HashKey_k(%rsp), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP2          # TMP2 = (a1+a0)*(b1+b0)
	movdqu	  (%arg3,%r11,1), \TMP3
	pxor	  \TMP3, \XMM1                 # Ciphertext/Plaintext XOR EK
	movdqu	  16(%arg3,%r11,1), \TMP3
	pxor	  \TMP3, \XMM2                 # Ciphertext/Plaintext XOR EK
	movdqu	  32(%arg3,%r11,1), \TMP3
	pxor	  \TMP3, \XMM3                 # Ciphertext/Plaintext XOR EK
	movdqu	  48(%arg3,%r11,1), \TMP3
	pxor	  \TMP3, \XMM4                 # Ciphertext/Plaintext XOR EK
        movdqu    \XMM1, (%arg2,%r11,1)        # Write to the ciphertext buffer
        movdqu    \XMM2, 16(%arg2,%r11,1)      # Write to the ciphertext buffer
        movdqu    \XMM3, 32(%arg2,%r11,1)      # Write to the ciphertext buffer
        movdqu    \XMM4, 48(%arg2,%r11,1)      # Write to the ciphertext buffer
	PSHUFB_XMM %xmm15, \XMM1        # perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM2	# perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM3	# perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM4	# perform a 16 byte swap

	pxor	  \TMP4, \TMP1
	pxor	  \XMM8, \XMM5
	pxor	  \TMP6, \TMP2
	pxor	  \TMP1, \TMP2
	pxor	  \XMM5, \TMP2
	movdqa	  \TMP2, \TMP3
	pslldq	  $8, \TMP3                    # left shift TMP3 2 DWs
	psrldq	  $8, \TMP2                    # right shift TMP2 2 DWs
	pxor	  \TMP3, \XMM5
	pxor	  \TMP2, \TMP1	  # accumulate the results in TMP1:XMM5

        # first phase of reduction

	movdqa    \XMM5, \TMP2
	movdqa    \XMM5, \TMP3
	movdqa    \XMM5, \TMP4
# move XMM5 into TMP2, TMP3, TMP4 in order to perform shifts independently
	pslld     $31, \TMP2                   # packed right shift << 31
	pslld     $30, \TMP3                   # packed right shift << 30
	pslld     $25, \TMP4                   # packed right shift << 25
	pxor      \TMP3, \TMP2	               # xor the shifted versions
	pxor      \TMP4, \TMP2
	movdqa    \TMP2, \TMP5
	psrldq    $4, \TMP5                    # right shift T5 1 DW
	pslldq    $12, \TMP2                   # left shift T2 3 DWs
	pxor      \TMP2, \XMM5

        # second phase of reduction

	movdqa    \XMM5,\TMP2 # make 3 copies of XMM5 into TMP2, TMP3, TMP4
	movdqa    \XMM5,\TMP3
	movdqa    \XMM5,\TMP4
	psrld     $1, \TMP2                    # packed left shift >>1
	psrld     $2, \TMP3                    # packed left shift >>2
	psrld     $7, \TMP4                    # packed left shift >>7
	pxor      \TMP3,\TMP2		       # xor the shifted versions
	pxor      \TMP4,\TMP2
	pxor      \TMP5, \TMP2
	pxor      \TMP2, \XMM5
	pxor      \TMP1, \XMM5                 # result is in TMP1

	pxor	  \XMM5, \XMM1
.endm

/*
* decrypt 4 blocks at a time
* ghash the 4 previously decrypted ciphertext blocks
* arg1, %arg2, %arg3 are used as pointers only, not modified
* %r11 is the data offset value
*/
.macro GHASH_4_ENCRYPT_4_PARALLEL_DEC TMP1 TMP2 TMP3 TMP4 TMP5 \
TMP6 XMM0 XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7 XMM8 operation

	movdqa	  \XMM1, \XMM5
	movdqa	  \XMM2, \XMM6
	movdqa	  \XMM3, \XMM7
	movdqa	  \XMM4, \XMM8

        movdqa    SHUF_MASK(%rip), %xmm15
        # multiply TMP5 * HashKey using karatsuba

	movdqa	  \XMM5, \TMP4
	pshufd	  $78, \XMM5, \TMP6
	pxor	  \XMM5, \TMP6
	paddd     ONE(%rip), \XMM0		# INCR CNT
	movdqa	  HashKey_4(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP4           # TMP4 = a1*b1
	movdqa    \XMM0, \XMM1
	paddd     ONE(%rip), \XMM0		# INCR CNT
	movdqa    \XMM0, \XMM2
	paddd     ONE(%rip), \XMM0		# INCR CNT
	movdqa    \XMM0, \XMM3
	paddd     ONE(%rip), \XMM0		# INCR CNT
	movdqa    \XMM0, \XMM4
	PSHUFB_XMM %xmm15, \XMM1	# perform a 16 byte swap
	PCLMULQDQ 0x00, \TMP5, \XMM5           # XMM5 = a0*b0
	PSHUFB_XMM %xmm15, \XMM2	# perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM3	# perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM4	# perform a 16 byte swap

	pxor	  (%arg1), \XMM1
	pxor	  (%arg1), \XMM2
	pxor	  (%arg1), \XMM3
	pxor	  (%arg1), \XMM4
	movdqa	  HashKey_4_k(%rsp), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP6           # TMP6 = (a1+a0)*(b1+b0)
	movaps 0x10(%arg1), \TMP1
	AESENC	  \TMP1, \XMM1              # Round 1
	AESENC	  \TMP1, \XMM2
	AESENC	  \TMP1, \XMM3
	AESENC	  \TMP1, \XMM4
	movaps 0x20(%arg1), \TMP1
	AESENC	  \TMP1, \XMM1              # Round 2
	AESENC	  \TMP1, \XMM2
	AESENC	  \TMP1, \XMM3
	AESENC	  \TMP1, \XMM4
	movdqa	  \XMM6, \TMP1
	pshufd	  $78, \XMM6, \TMP2
	pxor	  \XMM6, \TMP2
	movdqa	  HashKey_3(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1           # TMP1 = a1 * b1
	movaps 0x30(%arg1), \TMP3
	AESENC    \TMP3, \XMM1              # Round 3
	AESENC    \TMP3, \XMM2
	AESENC    \TMP3, \XMM3
	AESENC    \TMP3, \XMM4
	PCLMULQDQ 0x00, \TMP5, \XMM6           # XMM6 = a0*b0
	movaps 0x40(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1              # Round 4
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	movdqa	  HashKey_3_k(%rsp), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP2           # TMP2 = (a1+a0)*(b1+b0)
	movaps 0x50(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1              # Round 5
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	pxor	  \TMP1, \TMP4
# accumulate the results in TMP4:XMM5, TMP6 holds the middle part
	pxor	  \XMM6, \XMM5
	pxor	  \TMP2, \TMP6
	movdqa	  \XMM7, \TMP1
	pshufd	  $78, \XMM7, \TMP2
	pxor	  \XMM7, \TMP2
	movdqa	  HashKey_2(%rsp ), \TMP5

        # Multiply TMP5 * HashKey using karatsuba

	PCLMULQDQ 0x11, \TMP5, \TMP1           # TMP1 = a1*b1
	movaps 0x60(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1              # Round 6
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	PCLMULQDQ 0x00, \TMP5, \XMM7           # XMM7 = a0*b0
	movaps 0x70(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1             # Round 7
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	movdqa	  HashKey_2_k(%rsp), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP2           # TMP2 = (a1+a0)*(b1+b0)
	movaps 0x80(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1             # Round 8
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	pxor	  \TMP1, \TMP4
# accumulate the results in TMP4:XMM5, TMP6 holds the middle part
	pxor	  \XMM7, \XMM5
	pxor	  \TMP2, \TMP6

        # Multiply XMM8 * HashKey
        # XMM8 and TMP5 hold the values for the two operands

	movdqa	  \XMM8, \TMP1
	pshufd	  $78, \XMM8, \TMP2
	pxor	  \XMM8, \TMP2
	movdqa	  HashKey(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1          # TMP1 = a1*b1
	movaps 0x90(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1            # Round 9
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	PCLMULQDQ 0x00, \TMP5, \XMM8          # XMM8 = a0*b0
	movaps 0xa0(%arg1), \TMP3
	AESENCLAST \TMP3, \XMM1           # Round 10
	AESENCLAST \TMP3, \XMM2
	AESENCLAST \TMP3, \XMM3
	AESENCLAST \TMP3, \XMM4
	movdqa    HashKey_k(%rsp), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP2          # TMP2 = (a1+a0)*(b1+b0)
	movdqu	  (%arg3,%r11,1), \TMP3
	pxor	  \TMP3, \XMM1                 # Ciphertext/Plaintext XOR EK
	movdqu	  \XMM1, (%arg2,%r11,1)        # Write to plaintext buffer
	movdqa    \TMP3, \XMM1
	movdqu	  16(%arg3,%r11,1), \TMP3
	pxor	  \TMP3, \XMM2                 # Ciphertext/Plaintext XOR EK
	movdqu	  \XMM2, 16(%arg2,%r11,1)      # Write to plaintext buffer
	movdqa    \TMP3, \XMM2
	movdqu	  32(%arg3,%r11,1), \TMP3
	pxor	  \TMP3, \XMM3                 # Ciphertext/Plaintext XOR EK
	movdqu	  \XMM3, 32(%arg2,%r11,1)      # Write to plaintext buffer
	movdqa    \TMP3, \XMM3
	movdqu	  48(%arg3,%r11,1), \TMP3
	pxor	  \TMP3, \XMM4                 # Ciphertext/Plaintext XOR EK
	movdqu	  \XMM4, 48(%arg2,%r11,1)      # Write to plaintext buffer
	movdqa    \TMP3, \XMM4
	PSHUFB_XMM %xmm15, \XMM1        # perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM2	# perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM3	# perform a 16 byte swap
	PSHUFB_XMM %xmm15, \XMM4	# perform a 16 byte swap

	pxor	  \TMP4, \TMP1
	pxor	  \XMM8, \XMM5
	pxor	  \TMP6, \TMP2
	pxor	  \TMP1, \TMP2
	pxor	  \XMM5, \TMP2
	movdqa	  \TMP2, \TMP3
	pslldq	  $8, \TMP3                    # left shift TMP3 2 DWs
	psrldq	  $8, \TMP2                    # right shift TMP2 2 DWs
	pxor	  \TMP3, \XMM5
	pxor	  \TMP2, \TMP1	  # accumulate the results in TMP1:XMM5

        # first phase of reduction

	movdqa    \XMM5, \TMP2
	movdqa    \XMM5, \TMP3
	movdqa    \XMM5, \TMP4
# move XMM5 into TMP2, TMP3, TMP4 in order to perform shifts independently
	pslld     $31, \TMP2                   # packed right shift << 31
	pslld     $30, \TMP3                   # packed right shift << 30
	pslld     $25, \TMP4                   # packed right shift << 25
	pxor      \TMP3, \TMP2	               # xor the shifted versions
	pxor      \TMP4, \TMP2
	movdqa    \TMP2, \TMP5
	psrldq    $4, \TMP5                    # right shift T5 1 DW
	pslldq    $12, \TMP2                   # left shift T2 3 DWs
	pxor      \TMP2, \XMM5

        # second phase of reduction

	movdqa    \XMM5,\TMP2 # make 3 copies of XMM5 into TMP2, TMP3, TMP4
	movdqa    \XMM5,\TMP3
	movdqa    \XMM5,\TMP4
	psrld     $1, \TMP2                    # packed left shift >>1
	psrld     $2, \TMP3                    # packed left shift >>2
	psrld     $7, \TMP4                    # packed left shift >>7
	pxor      \TMP3,\TMP2		       # xor the shifted versions
	pxor      \TMP4,\TMP2
	pxor      \TMP5, \TMP2
	pxor      \TMP2, \XMM5
	pxor      \TMP1, \XMM5                 # result is in TMP1

	pxor	  \XMM5, \XMM1
.endm

/* GHASH the last 4 ciphertext blocks. */
.macro	GHASH_LAST_4 TMP1 TMP2 TMP3 TMP4 TMP5 TMP6 \
TMP7 XMM1 XMM2 XMM3 XMM4 XMMDst

        # Multiply TMP6 * HashKey (using Karatsuba)

	movdqa	  \XMM1, \TMP6
	pshufd	  $78, \XMM1, \TMP2
	pxor	  \XMM1, \TMP2
	movdqa	  HashKey_4(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP6       # TMP6 = a1*b1
	PCLMULQDQ 0x00, \TMP5, \XMM1       # XMM1 = a0*b0
	movdqa	  HashKey_4_k(%rsp), \TMP4
	PCLMULQDQ 0x00, \TMP4, \TMP2       # TMP2 = (a1+a0)*(b1+b0)
	movdqa	  \XMM1, \XMMDst
	movdqa	  \TMP2, \XMM1              # result in TMP6, XMMDst, XMM1

        # Multiply TMP1 * HashKey (using Karatsuba)

	movdqa	  \XMM2, \TMP1
	pshufd	  $78, \XMM2, \TMP2
	pxor	  \XMM2, \TMP2
	movdqa	  HashKey_3(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1       # TMP1 = a1*b1
	PCLMULQDQ 0x00, \TMP5, \XMM2       # XMM2 = a0*b0
	movdqa	  HashKey_3_k(%rsp), \TMP4
	PCLMULQDQ 0x00, \TMP4, \TMP2       # TMP2 = (a1+a0)*(b1+b0)
	pxor	  \TMP1, \TMP6
	pxor	  \XMM2, \XMMDst
	pxor	  \TMP2, \XMM1
# results accumulated in TMP6, XMMDst, XMM1

        # Multiply TMP1 * HashKey (using Karatsuba)

	movdqa	  \XMM3, \TMP1
	pshufd	  $78, \XMM3, \TMP2
	pxor	  \XMM3, \TMP2
	movdqa	  HashKey_2(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1       # TMP1 = a1*b1
	PCLMULQDQ 0x00, \TMP5, \XMM3       # XMM3 = a0*b0
	movdqa	  HashKey_2_k(%rsp), \TMP4
	PCLMULQDQ 0x00, \TMP4, \TMP2       # TMP2 = (a1+a0)*(b1+b0)
	pxor	  \TMP1, \TMP6
	pxor	  \XMM3, \XMMDst
	pxor	  \TMP2, \XMM1   # results accumulated in TMP6, XMMDst, XMM1

        # Multiply TMP1 * HashKey (using Karatsuba)
	movdqa	  \XMM4, \TMP1
	pshufd	  $78, \XMM4, \TMP2
	pxor	  \XMM4, \TMP2
	movdqa	  HashKey(%rsp), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1	    # TMP1 = a1*b1
	PCLMULQDQ 0x00, \TMP5, \XMM4       # XMM4 = a0*b0
	movdqa	  HashKey_k(%rsp), \TMP4
	PCLMULQDQ 0x00, \TMP4, \TMP2       # TMP2 = (a1+a0)*(b1+b0)
	pxor	  \TMP1, \TMP6
	pxor	  \XMM4, \XMMDst
	pxor	  \XMM1, \TMP2
	pxor	  \TMP6, \TMP2
	pxor	  \XMMDst, \TMP2
	# middle section of the temp results combined as in karatsuba algorithm
	movdqa	  \TMP2, \TMP4
	pslldq	  $8, \TMP4                 # left shift TMP4 2 DWs
	psrldq	  $8, \TMP2                 # right shift TMP2 2 DWs
	pxor	  \TMP4, \XMMDst
	pxor	  \TMP2, \TMP6
# TMP6:XMMDst holds the result of the accumulated carry-less multiplications
	# first phase of the reduction
	movdqa    \XMMDst, \TMP2
	movdqa    \XMMDst, \TMP3
	movdqa    \XMMDst, \TMP4
# move XMMDst into TMP2, TMP3, TMP4 in order to perform 3 shifts independently
	pslld     $31, \TMP2                # packed right shifting << 31
	pslld     $30, \TMP3                # packed right shifting << 30
	pslld     $25, \TMP4                # packed right shifting << 25
	pxor      \TMP3, \TMP2              # xor the shifted versions
	pxor      \TMP4, \TMP2
	movdqa    \TMP2, \TMP7
	psrldq    $4, \TMP7                 # right shift TMP7 1 DW
	pslldq    $12, \TMP2                # left shift TMP2 3 DWs
	pxor      \TMP2, \XMMDst

        # second phase of the reduction
	movdqa    \XMMDst, \TMP2
	# make 3 copies of XMMDst for doing 3 shift operations
	movdqa    \XMMDst, \TMP3
	movdqa    \XMMDst, \TMP4
	psrld     $1, \TMP2                 # packed left shift >> 1
	psrld     $2, \TMP3                 # packed left shift >> 2
	psrld     $7, \TMP4                 # packed left shift >> 7
	pxor      \TMP3, \TMP2              # xor the shifted versions
	pxor      \TMP4, \TMP2
	pxor      \TMP7, \TMP2
	pxor      \TMP2, \XMMDst
	pxor      \TMP6, \XMMDst            # reduced result is in XMMDst
.endm

/* Encryption of a single block done*/
.macro ENCRYPT_SINGLE_BLOCK XMM0 TMP1

	pxor	(%arg1), \XMM0
        movaps 16(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 32(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 48(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 64(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 80(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 96(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 112(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 128(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 144(%arg1), \TMP1
	AESENC	\TMP1, \XMM0
        movaps 160(%arg1), \TMP1
	AESENCLAST	\TMP1, \XMM0
.endm


/*****************************************************************************
* void aesni_gcm_dec(void *aes_ctx,    // AES Key schedule. Starts on a 16 byte boundary.
*                   u8 *out,           // Plaintext output. Encrypt in-place is allowed.
*                   const u8 *in,      // Ciphertext input
*                   u64 plaintext_len, // Length of data in bytes for decryption.
*                   u8 *iv,            // Pre-counter block j0: 4 byte salt (from Security Association)
*                                      // concatenated with 8 byte Initialisation Vector (from IPSec ESP Payload)
*                                      // concatenated with 0x00000001. 16-byte aligned pointer.
*                   u8 *hash_subkey,   // H, the Hash sub key input. Data starts on a 16-byte boundary.
*                   const u8 *aad,     // Additional Authentication Data (AAD)
*                   u64 aad_len,       // Length of AAD in bytes. With RFC4106 this is going to be 8 or 12 bytes
*                   u8  *auth_tag,     // Authenticated Tag output. The driver will compare this to the
*                                      // given authentication tag and only return the plaintext if they match.
*                   u64 auth_tag_len); // Authenticated Tag Length in bytes. Valid values are 16
*                                      // (most likely), 12 or 8.
*
* Assumptions:
*
* keys:
*       keys are pre-expanded and aligned to 16 bytes. we are using the first
*       set of 11 keys in the data structure void *aes_ctx
*
* iv:
*       0                   1                   2                   3
*       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                             Salt  (From the SA)               |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                     Initialization Vector                     |
*       |         (This is the sequence number from IPSec header)       |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                              0x1                              |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*
*
*
* AAD:
*       AAD padded to 128 bits with 0
*       for example, assume AAD is a u32 vector
*
*       if AAD is 8 bytes:
*       AAD[3] = {A0, A1};
*       padded AAD in xmm register = {A1 A0 0 0}
*
*       0                   1                   2                   3
*       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                               SPI (A1)                        |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                     32-bit Sequence Number (A0)               |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                              0x0                              |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*
*                                       AAD Format with 32-bit Sequence Number
*
*       if AAD is 12 bytes:
*       AAD[3] = {A0, A1, A2};
*       padded AAD in xmm register = {A2 A1 A0 0}
*
*       0                   1                   2                   3
*       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                               SPI (A2)                        |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                 64-bit Extended Sequence Number {A1,A0}       |
*       |                                                               |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                              0x0                              |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*
*                        AAD Format with 64-bit Extended Sequence Number
*
* aadLen:
*       from the definition of the spec, aadLen can only be 8 or 12 bytes.
*       The code supports 16 too but for other sizes, the code will fail.
*
* TLen:
*       from the definition of the spec, TLen can only be 8, 12 or 16 bytes.
*       For other sizes, the code will fail.
*
* poly = x^128 + x^127 + x^126 + x^121 + 1
*
*****************************************************************************/
ENTRY(aesni_gcm_dec)
	push	%r12
	push	%r13
	push	%r14
	mov	%rsp, %r14
/*
* states of %xmm registers %xmm6:%xmm15 not saved
* all %xmm registers are clobbered
*/
	sub	$VARIABLE_OFFSET, %rsp
	and	$~63, %rsp                        # align rsp to 64 bytes
	mov	%arg6, %r12
	movdqu	(%r12), %xmm13			  # %xmm13 = HashKey
        movdqa  SHUF_MASK(%rip), %xmm2
	PSHUFB_XMM %xmm2, %xmm13


# Precompute HashKey<<1 (mod poly) from the hash key (required for GHASH)

	movdqa	%xmm13, %xmm2
	psllq	$1, %xmm13
	psrlq	$63, %xmm2
	movdqa	%xmm2, %xmm1
	pslldq	$8, %xmm2
	psrldq	$8, %xmm1
	por	%xmm2, %xmm13

        # Reduction

	pshufd	$0x24, %xmm1, %xmm2
	pcmpeqd TWOONE(%rip), %xmm2
	pand	POLY(%rip), %xmm2
	pxor	%xmm2, %xmm13     # %xmm13 holds the HashKey<<1 (mod poly)


        # Decrypt first few blocks

	movdqa %xmm13, HashKey(%rsp)           # store HashKey<<1 (mod poly)
	mov %arg4, %r13    # save the number of bytes of plaintext/ciphertext
	and $-16, %r13                      # %r13 = %r13 - (%r13 mod 16)
	mov %r13, %r12
	and $(3<<4), %r12
	jz _initial_num_blocks_is_0_decrypt
	cmp $(2<<4), %r12
	jb _initial_num_blocks_is_1_decrypt
	je _initial_num_blocks_is_2_decrypt
_initial_num_blocks_is_3_decrypt:
	INITIAL_BLOCKS_DEC 3, %xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 5, 678, dec
	sub	$48, %r13
	jmp	_initial_blocks_decrypted
_initial_num_blocks_is_2_decrypt:
	INITIAL_BLOCKS_DEC	2, %xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 6, 78, dec
	sub	$32, %r13
	jmp	_initial_blocks_decrypted
_initial_num_blocks_is_1_decrypt:
	INITIAL_BLOCKS_DEC	1, %xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 7, 8, dec
	sub	$16, %r13
	jmp	_initial_blocks_decrypted
_initial_num_blocks_is_0_decrypt:
	INITIAL_BLOCKS_DEC	0, %xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 8, 0, dec
_initial_blocks_decrypted:
	cmp	$0, %r13
	je	_zero_cipher_left_decrypt
	sub	$64, %r13
	je	_four_cipher_left_decrypt
_decrypt_by_4:
	GHASH_4_ENCRYPT_4_PARALLEL_DEC	%xmm9, %xmm10, %xmm11, %xmm12, %xmm13, \
%xmm14, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, dec
	add	$64, %r11
	sub	$64, %r13
	jne	_decrypt_by_4
_four_cipher_left_decrypt:
	GHASH_LAST_4	%xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, \
%xmm15, %xmm1, %xmm2, %xmm3, %xmm4, %xmm8
_zero_cipher_left_decrypt:
	mov	%arg4, %r13
	and	$15, %r13				# %r13 = arg4 (mod 16)
	je	_multiple_of_16_bytes_decrypt

        # Handle the last <16 byte block separately

	paddd ONE(%rip), %xmm0         # increment CNT to get Yn
        movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10, %xmm0

	ENCRYPT_SINGLE_BLOCK  %xmm0, %xmm1    # E(K, Yn)
	sub $16, %r11
	add %r13, %r11
	movdqu (%arg3,%r11,1), %xmm1   # receive the last <16 byte block
	lea SHIFT_MASK+16(%rip), %r12
	sub %r13, %r12
# adjust the shuffle mask pointer to be able to shift 16-%r13 bytes
# (%r13 is the number of bytes in plaintext mod 16)
	movdqu (%r12), %xmm2           # get the appropriate shuffle mask
	PSHUFB_XMM %xmm2, %xmm1            # right shift 16-%r13 butes

	movdqa  %xmm1, %xmm2
	pxor %xmm1, %xmm0            # Ciphertext XOR E(K, Yn)
	movdqu ALL_F-SHIFT_MASK(%r12), %xmm1
	# get the appropriate mask to mask out top 16-%r13 bytes of %xmm0
	pand %xmm1, %xmm0            # mask out top 16-%r13 bytes of %xmm0
	pand    %xmm1, %xmm2
        movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10 ,%xmm2

	pxor %xmm2, %xmm8
	GHASH_MUL %xmm8, %xmm13, %xmm9, %xmm10, %xmm11, %xmm5, %xmm6
	          # GHASH computation for the last <16 byte block
	sub %r13, %r11
	add $16, %r11

        # output %r13 bytes
	MOVQ_R64_XMM	%xmm0, %rax
	cmp	$8, %r13
	jle	_less_than_8_bytes_left_decrypt
	mov	%rax, (%arg2 , %r11, 1)
	add	$8, %r11
	psrldq	$8, %xmm0
	MOVQ_R64_XMM	%xmm0, %rax
	sub	$8, %r13
_less_than_8_bytes_left_decrypt:
	mov	%al,  (%arg2, %r11, 1)
	add	$1, %r11
	shr	$8, %rax
	sub	$1, %r13
	jne	_less_than_8_bytes_left_decrypt
_multiple_of_16_bytes_decrypt:
	mov	arg8, %r12		  # %r13 = aadLen (number of bytes)
	shl	$3, %r12		  # convert into number of bits
	movd	%r12d, %xmm15		  # len(A) in %xmm15
	shl	$3, %arg4		  # len(C) in bits (*128)
	MOVQ_R64_XMM	%arg4, %xmm1
	pslldq	$8, %xmm15		  # %xmm15 = len(A)||0x0000000000000000
	pxor	%xmm1, %xmm15		  # %xmm15 = len(A)||len(C)
	pxor	%xmm15, %xmm8
	GHASH_MUL	%xmm8, %xmm13, %xmm9, %xmm10, %xmm11, %xmm5, %xmm6
	         # final GHASH computation
        movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10, %xmm8

	mov	%arg5, %rax		  # %rax = *Y0
	movdqu	(%rax), %xmm0		  # %xmm0 = Y0
	ENCRYPT_SINGLE_BLOCK	%xmm0,  %xmm1	  # E(K, Y0)
	pxor	%xmm8, %xmm0
_return_T_decrypt:
	mov	arg9, %r10                # %r10 = authTag
	mov	arg10, %r11               # %r11 = auth_tag_len
	cmp	$16, %r11
	je	_T_16_decrypt
	cmp	$12, %r11
	je	_T_12_decrypt
_T_8_decrypt:
	MOVQ_R64_XMM	%xmm0, %rax
	mov	%rax, (%r10)
	jmp	_return_T_done_decrypt
_T_12_decrypt:
	MOVQ_R64_XMM	%xmm0, %rax
	mov	%rax, (%r10)
	psrldq	$8, %xmm0
	movd	%xmm0, %eax
	mov	%eax, 8(%r10)
	jmp	_return_T_done_decrypt
_T_16_decrypt:
	movdqu	%xmm0, (%r10)
_return_T_done_decrypt:
	mov	%r14, %rsp
	pop	%r14
	pop	%r13
	pop	%r12
	ret
ENDPROC(aesni_gcm_dec)


/*****************************************************************************
* void aesni_gcm_enc(void *aes_ctx,      // AES Key schedule. Starts on a 16 byte boundary.
*                    u8 *out,            // Ciphertext output. Encrypt in-place is allowed.
*                    const u8 *in,       // Plaintext input
*                    u64 plaintext_len,  // Length of data in bytes for encryption.
*                    u8 *iv,             // Pre-counter block j0: 4 byte salt (from Security Association)
*                                        // concatenated with 8 byte Initialisation Vector (from IPSec ESP Payload)
*                                        // concatenated with 0x00000001. 16-byte aligned pointer.
*                    u8 *hash_subkey,    // H, the Hash sub key input. Data starts on a 16-byte boundary.
*                    const u8 *aad,      // Additional Authentication Data (AAD)
*                    u64 aad_len,        // Length of AAD in bytes. With RFC4106 this is going to be 8 or 12 bytes
*                    u8 *auth_tag,       // Authenticated Tag output.
*                    u64 auth_tag_len);  // Authenticated Tag Length in bytes. Valid values are 16 (most likely),
*                                        // 12 or 8.
*
* Assumptions:
*
* keys:
*       keys are pre-expanded and aligned to 16 bytes. we are using the
*       first set of 11 keys in the data structure void *aes_ctx
*
*
* iv:
*       0                   1                   2                   3
*       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                             Salt  (From the SA)               |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                     Initialization Vector                     |
*       |         (This is the sequence number from IPSec header)       |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                              0x1                              |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*
*
*
* AAD:
*       AAD padded to 128 bits with 0
*       for example, assume AAD is a u32 vector
*
*       if AAD is 8 bytes:
*       AAD[3] = {A0, A1};
*       padded AAD in xmm register = {A1 A0 0 0}
*
*       0                   1                   2                   3
*       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                               SPI (A1)                        |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                     32-bit Sequence Number (A0)               |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                              0x0                              |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*
*                                 AAD Format with 32-bit Sequence Number
*
*       if AAD is 12 bytes:
*       AAD[3] = {A0, A1, A2};
*       padded AAD in xmm register = {A2 A1 A0 0}
*
*       0                   1                   2                   3
*       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                               SPI (A2)                        |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                 64-bit Extended Sequence Number {A1,A0}       |
*       |                                                               |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*       |                              0x0                              |
*       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*
*                         AAD Format with 64-bit Extended Sequence Number
*
* aadLen:
*       from the definition of the spec, aadLen can only be 8 or 12 bytes.
*       The code supports 16 too but for other sizes, the code will fail.
*
* TLen:
*       from the definition of the spec, TLen can only be 8, 12 or 16 bytes.
*       For other sizes, the code will fail.
*
* poly = x^128 + x^127 + x^126 + x^121 + 1
***************************************************************************/
ENTRY(aesni_gcm_enc)
	push	%r12
	push	%r13
	push	%r14
	mov	%rsp, %r14
#
# states of %xmm registers %xmm6:%xmm15 not saved
# all %xmm registers are clobbered
#
	sub	$VARIABLE_OFFSET, %rsp
	and	$~63, %rsp
	mov	%arg6, %r12
	movdqu	(%r12), %xmm13
        movdqa  SHUF_MASK(%rip), %xmm2
	PSHUFB_XMM %xmm2, %xmm13


# precompute HashKey<<1 mod poly from the HashKey (required for GHASH)

	movdqa	%xmm13, %xmm2
	psllq	$1, %xmm13
	psrlq	$63, %xmm2
	movdqa	%xmm2, %xmm1
	pslldq	$8, %xmm2
	psrldq	$8, %xmm1
	por	%xmm2, %xmm13

        # reduce HashKey<<1

	pshufd	$0x24, %xmm1, %xmm2
	pcmpeqd TWOONE(%rip), %xmm2
	pand	POLY(%rip), %xmm2
	pxor	%xmm2, %xmm13
	movdqa	%xmm13, HashKey(%rsp)
	mov	%arg4, %r13            # %xmm13 holds HashKey<<1 (mod poly)
	and	$-16, %r13
	mov	%r13, %r12

        # Encrypt first few blocks

	and	$(3<<4), %r12
	jz	_initial_num_blocks_is_0_encrypt
	cmp	$(2<<4), %r12
	jb	_initial_num_blocks_is_1_encrypt
	je	_initial_num_blocks_is_2_encrypt
_initial_num_blocks_is_3_encrypt:
	INITIAL_BLOCKS_ENC	3, %xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 5, 678, enc
	sub	$48, %r13
	jmp	_initial_blocks_encrypted
_initial_num_blocks_is_2_encrypt:
	INITIAL_BLOCKS_ENC	2, %xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 6, 78, enc
	sub	$32, %r13
	jmp	_initial_blocks_encrypted
_initial_num_blocks_is_1_encrypt:
	INITIAL_BLOCKS_ENC	1, %xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 7, 8, enc
	sub	$16, %r13
	jmp	_initial_blocks_encrypted
_initial_num_blocks_is_0_encrypt:
	INITIAL_BLOCKS_ENC	0, %xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 8, 0, enc
_initial_blocks_encrypted:

        # Main loop - Encrypt remaining blocks

	cmp	$0, %r13
	je	_zero_cipher_left_encrypt
	sub	$64, %r13
	je	_four_cipher_left_encrypt
_encrypt_by_4_encrypt:
	GHASH_4_ENCRYPT_4_PARALLEL_ENC	%xmm9, %xmm10, %xmm11, %xmm12, %xmm13, \
%xmm14, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, enc
	add	$64, %r11
	sub	$64, %r13
	jne	_encrypt_by_4_encrypt
_four_cipher_left_encrypt:
	GHASH_LAST_4	%xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, \
%xmm15, %xmm1, %xmm2, %xmm3, %xmm4, %xmm8
_zero_cipher_left_encrypt:
	mov	%arg4, %r13
	and	$15, %r13			# %r13 = arg4 (mod 16)
	je	_multiple_of_16_bytes_encrypt

         # Handle the last <16 Byte block separately
	paddd ONE(%rip), %xmm0                # INCR CNT to get Yn
        movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10, %xmm0


	ENCRYPT_SINGLE_BLOCK	%xmm0, %xmm1        # Encrypt(K, Yn)
	sub $16, %r11
	add %r13, %r11
	movdqu (%arg3,%r11,1), %xmm1     # receive the last <16 byte blocks
	lea SHIFT_MASK+16(%rip), %r12
	sub %r13, %r12
	# adjust the shuffle mask pointer to be able to shift 16-r13 bytes
	# (%r13 is the number of bytes in plaintext mod 16)
	movdqu	(%r12), %xmm2           # get the appropriate shuffle mask
	PSHUFB_XMM	%xmm2, %xmm1            # shift right 16-r13 byte
	pxor	%xmm1, %xmm0            # Plaintext XOR Encrypt(K, Yn)
	movdqu	ALL_F-SHIFT_MASK(%r12), %xmm1
	# get the appropriate mask to mask out top 16-r13 bytes of xmm0
	pand	%xmm1, %xmm0            # mask out top 16-r13 bytes of xmm0
        movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10,%xmm0

	pxor	%xmm0, %xmm8
	GHASH_MUL %xmm8, %xmm13, %xmm9, %xmm10, %xmm11, %xmm5, %xmm6
	# GHASH computation for the last <16 byte block
	sub	%r13, %r11
	add	$16, %r11

	movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10, %xmm0

	# shuffle xmm0 back to output as ciphertext

        # Output %r13 bytes
	MOVQ_R64_XMM %xmm0, %rax
	cmp $8, %r13
	jle _less_than_8_bytes_left_encrypt
	mov %rax, (%arg2 , %r11, 1)
	add $8, %r11
	psrldq $8, %xmm0
	MOVQ_R64_XMM %xmm0, %rax
	sub $8, %r13
_less_than_8_bytes_left_encrypt:
	mov %al,  (%arg2, %r11, 1)
	add $1, %r11
	shr $8, %rax
	sub $1, %r13
	jne _less_than_8_bytes_left_encrypt
_multiple_of_16_bytes_encrypt:
	mov	arg8, %r12    # %r12 = addLen (number of bytes)
	shl	$3, %r12
	movd	%r12d, %xmm15       # len(A) in %xmm15
	shl	$3, %arg4               # len(C) in bits (*128)
	MOVQ_R64_XMM	%arg4, %xmm1
	pslldq	$8, %xmm15          # %xmm15 = len(A)||0x0000000000000000
	pxor	%xmm1, %xmm15       # %xmm15 = len(A)||len(C)
	pxor	%xmm15, %xmm8
	GHASH_MUL	%xmm8, %xmm13, %xmm9, %xmm10, %xmm11, %xmm5, %xmm6
	# final GHASH computation
        movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10, %xmm8         # perform a 16 byte swap

	mov	%arg5, %rax		       # %rax  = *Y0
	movdqu	(%rax), %xmm0		       # %xmm0 = Y0
	ENCRYPT_SINGLE_BLOCK	%xmm0, %xmm15         # Encrypt(K, Y0)
	pxor	%xmm8, %xmm0
_return_T_encrypt:
	mov	arg9, %r10                     # %r10 = authTag
	mov	arg10, %r11                    # %r11 = auth_tag_len
	cmp	$16, %r11
	je	_T_16_encrypt
	cmp	$12, %r11
	je	_T_12_encrypt
_T_8_encrypt:
	MOVQ_R64_XMM	%xmm0, %rax
	mov	%rax, (%r10)
	jmp	_return_T_done_encrypt
_T_12_encrypt:
	MOVQ_R64_XMM	%xmm0, %rax
	mov	%rax, (%r10)
	psrldq	$8, %xmm0
	movd	%xmm0, %eax
	mov	%eax, 8(%r10)
	jmp	_return_T_done_encrypt
_T_16_encrypt:
	movdqu	%xmm0, (%r10)
_return_T_done_encrypt:
	mov	%r14, %rsp
	pop	%r14
	pop	%r13
	pop	%r12
	ret
ENDPROC(aesni_gcm_enc)

#endif


.align 4
_key_expansion_128:
_key_expansion_256a:
	pshufd $0b11111111, %xmm1, %xmm1
	shufps $0b00010000, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	shufps $0b10001100, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	pxor %xmm1, %xmm0
	movaps %xmm0, (TKEYP)
	add $0x10, TKEYP
	ret
ENDPROC(_key_expansion_128)
ENDPROC(_key_expansion_256a)

.align 4
_key_expansion_192a:
	pshufd $0b01010101, %xmm1, %xmm1
	shufps $0b00010000, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	shufps $0b10001100, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	pxor %xmm1, %xmm0

	movaps %xmm2, %xmm5
	movaps %xmm2, %xmm6
	pslldq $4, %xmm5
	pshufd $0b11111111, %xmm0, %xmm3
	pxor %xmm3, %xmm2
	pxor %xmm5, %xmm2

	movaps %xmm0, %xmm1
	shufps $0b01000100, %xmm0, %xmm6
	movaps %xmm6, (TKEYP)
	shufps $0b01001110, %xmm2, %xmm1
	movaps %xmm1, 0x10(TKEYP)
	add $0x20, TKEYP
	ret
ENDPROC(_key_expansion_192a)

.align 4
_key_expansion_192b:
	pshufd $0b01010101, %xmm1, %xmm1
	shufps $0b00010000, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	shufps $0b10001100, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	pxor %xmm1, %xmm0

	movaps %xmm2, %xmm5
	pslldq $4, %xmm5
	pshufd $0b11111111, %xmm0, %xmm3
	pxor %xmm3, %xmm2
	pxor %xmm5, %xmm2

	movaps %xmm0, (TKEYP)
	add $0x10, TKEYP
	ret
ENDPROC(_key_expansion_192b)

.align 4
_key_expansion_256b:
	pshufd $0b10101010, %xmm1, %xmm1
	shufps $0b00010000, %xmm2, %xmm4
	pxor %xmm4, %xmm2
	shufps $0b10001100, %xmm2, %xmm4
	pxor %xmm4, %xmm2
	pxor %xmm1, %xmm2
	movaps %xmm2, (TKEYP)
	add $0x10, TKEYP
	ret
ENDPROC(_key_expansion_256b)

/*
 * int aesni_set_key(struct crypto_aes_ctx *ctx, const u8 *in_key,
 *                   unsigned int key_len)
 */
ENTRY(aesni_set_key)
#ifndef __x86_64__
	pushl KEYP
	movl 8(%esp), KEYP		# ctx
	movl 12(%esp), UKEYP		# in_key
	movl 16(%esp), %edx		# key_len
#endif
	movups (UKEYP), %xmm0		# user key (first 16 bytes)
	movaps %xmm0, (KEYP)
	lea 0x10(KEYP), TKEYP		# key addr
	movl %edx, 480(KEYP)
	pxor %xmm4, %xmm4		# xmm4 is assumed 0 in _key_expansion_x
	cmp $24, %dl
	jb .Lenc_key128
	je .Lenc_key192
	movups 0x10(UKEYP), %xmm2	# other user key
	movaps %xmm2, (TKEYP)
	add $0x10, TKEYP
	AESKEYGENASSIST 0x1 %xmm2 %xmm1		# round 1
	call _key_expansion_256a
	AESKEYGENASSIST 0x1 %xmm0 %xmm1
	call _key_expansion_256b
	AESKEYGENASSIST 0x2 %xmm2 %xmm1		# round 2
	call _key_expansion_256a
	AESKEYGENASSIST 0x2 %xmm0 %xmm1
	call _key_expansion_256b
	AESKEYGENASSIST 0x4 %xmm2 %xmm1		# round 3
	call _key_expansion_256a
	AESKEYGENASSIST 0x4 %xmm0 %xmm1
	call _key_expansion_256b
	AESKEYGENASSIST 0x8 %xmm2 %xmm1		# round 4
	call _key_expansion_256a
	AESKEYGENASSIST 0x8 %xmm0 %xmm1
	call _key_expansion_256b
	AESKEYGENASSIST 0x10 %xmm2 %xmm1	# round 5
	call _key_expansion_256a
	AESKEYGENASSIST 0x10 %xmm0 %xmm1
	call _key_expansion_256b
	AESKEYGENASSIST 0x20 %xmm2 %xmm1	# round 6
	call _key_expansion_256a
	AESKEYGENASSIST 0x20 %xmm0 %xmm1
	call _key_expansion_256b
	AESKEYGENASSIST 0x40 %xmm2 %xmm1	# round 7
	call _key_expansion_256a
	jmp .Ldec_key
.Lenc_key192:
	movq 0x10(UKEYP), %xmm2		# other user key
	AESKEYGENASSIST 0x1 %xmm2 %xmm1		# round 1
	call _key_expansion_192a
	AESKEYGENASSIST 0x2 %xmm2 %xmm1		# round 2
	call _key_expansion_192b
	AESKEYGENASSIST 0x4 %xmm2 %xmm1		# round 3
	call _key_expansion_192a
	AESKEYGENASSIST 0x8 %xmm2 %xmm1		# round 4
	call _key_expansion_192b
	AESKEYGENASSIST 0x10 %xmm2 %xmm1	# round 5
	call _key_expansion_192a
	AESKEYGENASSIST 0x20 %xmm2 %xmm1	# round 6
	call _key_expansion_192b
	AESKEYGENASSIST 0x40 %xmm2 %xmm1	# round 7
	call _key_expansion_192a
	AESKEYGENASSIST 0x80 %xmm2 %xmm1	# round 8
	call _key_expansion_192b
	jmp .Ldec_key
.Lenc_key128:
	AESKEYGENASSIST 0x1 %xmm0 %xmm1		# round 1
	call _key_expansion_128
	AESKEYGENASSIST 0x2 %xmm0 %xmm1		# round 2
	call _key_expansion_128
	AESKEYGENASSIST 0x4 %xmm0 %xmm1		# round 3
	call _key_expansion_128
	AESKEYGENASSIST 0x8 %xmm0 %xmm1		# round 4
	call _key_expansion_128
	AESKEYGENASSIST 0x10 %xmm0 %xmm1	# round 5
	call _key_expansion_128
	AESKEYGENASSIST 0x20 %xmm0 %xmm1	# round 6
	call _key_expansion_128
	AESKEYGENASSIST 0x40 %xmm0 %xmm1	# round 7
	call _key_expansion_128
	AESKEYGENASSIST 0x80 %xmm0 %xmm1	# round 8
	call _key_expansion_128
	AESKEYGENASSIST 0x1b %xmm0 %xmm1	# round 9
	call _key_expansion_128
	AESKEYGENASSIST 0x36 %xmm0 %xmm1	# round 10
	call _key_expansion_128
.Ldec_key:
	sub $0x10, TKEYP
	movaps (KEYP), %xmm0
	movaps (TKEYP), %xmm1
	movaps %xmm0, 240(TKEYP)
	movaps %xmm1, 240(KEYP)
	add $0x10, KEYP
	lea 240-16(TKEYP), UKEYP
.align 4
.Ldec_key_loop:
	movaps (KEYP), %xmm0
	AESIMC %xmm0 %xmm1
	movaps %xmm1, (UKEYP)
	add $0x10, KEYP
	sub $0x10, UKEYP
	cmp TKEYP, KEYP
	jb .Ldec_key_loop
	xor AREG, AREG
#ifndef __x86_64__
	popl KEYP
#endif
	ret
ENDPROC(aesni_set_key)

/*
 * void aesni_enc(struct crypto_aes_ctx *ctx, u8 *dst, const u8 *src)
 */
ENTRY(aesni_enc)
#ifndef __x86_64__
	pushl KEYP
	pushl KLEN
	movl 12(%esp), KEYP
	movl 16(%esp), OUTP
	movl 20(%esp), INP
#endif
	movl 480(KEYP), KLEN		# key length
	movups (INP), STATE		# input
	call _aesni_enc1
	movups STATE, (OUTP)		# output
#ifndef __x86_64__
	popl KLEN
	popl KEYP
#endif
	ret
ENDPROC(aesni_enc)

/*
 * _aesni_enc1:		internal ABI
 * input:
 *	KEYP:		key struct pointer
 *	KLEN:		round count
 *	STATE:		initial state (input)
 * output:
 *	STATE:		finial state (output)
 * changed:
 *	KEY
 *	TKEYP (T1)
 */
.align 4
_aesni_enc1:
	movaps (KEYP), KEY		# key
	mov KEYP, TKEYP
	pxor KEY, STATE		# round 0
	add $0x30, TKEYP
	cmp $24, KLEN
	jb .Lenc128
	lea 0x20(TKEYP), TKEYP
	je .Lenc192
	add $0x20, TKEYP
	movaps -0x60(TKEYP), KEY
	AESENC KEY STATE
	movaps -0x50(TKEYP), KEY
	AESENC KEY STATE
.align 4
.Lenc192:
	movaps -0x40(TKEYP), KEY
	AESENC KEY STATE
	movaps -0x30(TKEYP), KEY
	AESENC KEY STATE
.align 4
.Lenc128:
	movaps -0x20(TKEYP), KEY
	AESENC KEY STATE
	movaps -0x10(TKEYP), KEY
	AESENC KEY STATE
	movaps (TKEYP), KEY
	AESENC KEY STATE
	movaps 0x10(TKEYP), KEY
	AESENC KEY STATE
	movaps 0x20(TKEYP), KEY
	AESENC KEY STATE
	movaps 0x30(TKEYP), KEY
	AESENC KEY STATE
	movaps 0x40(TKEYP), KEY
	AESENC KEY STATE
	movaps 0x50(TKEYP), KEY
	AESENC KEY STATE
	movaps 0x60(TKEYP), KEY
	AESENC KEY STATE
	movaps 0x70(TKEYP), KEY
	AESENCLAST KEY STATE
	ret
ENDPROC(_aesni_enc1)

/*
 * _aesni_enc4:	internal ABI
 * input:
 *	KEYP:		key struct pointer
 *	KLEN:		round count
 *	STATE1:		initial state (input)
 *	STATE2
 *	STATE3
 *	STATE4
 * output:
 *	STATE1:		finial state (output)
 *	STATE2
 *	STATE3
 *	STATE4
 * changed:
 *	KEY
 *	TKEYP (T1)
 */
.align 4
_aesni_enc4:
	movaps (KEYP), KEY		# key
	mov KEYP, TKEYP
	pxor KEY, STATE1		# round 0
	pxor KEY, STATE2
	pxor KEY, STATE3
	pxor KEY, STATE4
	add $0x30, TKEYP
	cmp $24, KLEN
	jb .L4enc128
	lea 0x20(TKEYP), TKEYP
	je .L4enc192
	add $0x20, TKEYP
	movaps -0x60(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps -0x50(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
#.align 4
.L4enc192:
	movaps -0x40(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps -0x30(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
#.align 4
.L4enc128:
	movaps -0x20(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps -0x10(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps (TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps 0x10(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps 0x20(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps 0x30(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps 0x40(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps 0x50(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps 0x60(TKEYP), KEY
	AESENC KEY STATE1
	AESENC KEY STATE2
	AESENC KEY STATE3
	AESENC KEY STATE4
	movaps 0x70(TKEYP), KEY
	AESENCLAST KEY STATE1		# last round
	AESENCLAST KEY STATE2
	AESENCLAST KEY STATE3
	AESENCLAST KEY STATE4
	ret
ENDPROC(_aesni_enc4)

/*
 * void aesni_dec (struct crypto_aes_ctx *ctx, u8 *dst, const u8 *src)
 */
ENTRY(aesni_dec)
#ifndef __x86_64__
	pushl KEYP
	pushl KLEN
	movl 12(%esp), KEYP
	movl 16(%esp), OUTP
	movl 20(%esp), INP
#endif
	mov 480(KEYP), KLEN		# key length
	add $240, KEYP
	movups (INP), STATE		# input
	call _aesni_dec1
	movups STATE, (OUTP)		#output
#ifndef __x86_64__
	popl KLEN
	popl KEYP
#endif
	ret
ENDPROC(aesni_dec)

/*
 * _aesni_dec1:		internal ABI
 * input:
 *	KEYP:		key struct pointer
 *	KLEN:		key length
 *	STATE:		initial state (input)
 * output:
 *	STATE:		finial state (output)
 * changed:
 *	KEY
 *	TKEYP (T1)
 */
.align 4
_aesni_dec1:
	movaps (KEYP), KEY		# key
	mov KEYP, TKEYP
	pxor KEY, STATE		# round 0
	add $0x30, TKEYP
	cmp $24, KLEN
	jb .Ldec128
	lea 0x20(TKEYP), TKEYP
	je .Ldec192
	add $0x20, TKEYP
	movaps -0x60(TKEYP), KEY
	AESDEC KEY STATE
	movaps -0x50(TKEYP), KEY
	AESDEC KEY STATE
.align 4
.Ldec192:
	movaps -0x40(TKEYP), KEY
	AESDEC KEY STATE
	movaps -0x30(TKEYP), KEY
	AESDEC KEY STATE
.align 4
.Ldec128:
	movaps -0x20(TKEYP), KEY
	AESDEC KEY STATE
	movaps -0x10(TKEYP), KEY
	AESDEC KEY STATE
	movaps (TKEYP), KEY
	AESDEC KEY STATE
	movaps 0x10(TKEYP), KEY
	AESDEC KEY STATE
	movaps 0x20(TKEYP), KEY
	AESDEC KEY STATE
	movaps 0x30(TKEYP), KEY
	AESDEC KEY STATE
	movaps 0x40(TKEYP), KEY
	AESDEC KEY STATE
	movaps 0x50(TKEYP), KEY
	AESDEC KEY STATE
	movaps 0x60(TKEYP), KEY
	AESDEC KEY STATE
	movaps 0x70(TKEYP), KEY
	AESDECLAST KEY STATE
	ret
ENDPROC(_aesni_dec1)

/*
 * _aesni_dec4:	internal ABI
 * input:
 *	KEYP:		key struct pointer
 *	KLEN:		key length
 *	STATE1:		initial state (input)
 *	STATE2
 *	STATE3
 *	STATE4
 * output:
 *	STATE1:		finial state (output)
 *	STATE2
 *	STATE3
 *	STATE4
 * changed:
 *	KEY
 *	TKEYP (T1)
 */
.align 4
_aesni_dec4:
	movaps (KEYP), KEY		# key
	mov KEYP, TKEYP
	pxor KEY, STATE1		# round 0
	pxor KEY, STATE2
	pxor KEY, STATE3
	pxor KEY, STATE4
	add $0x30, TKEYP
	cmp $24, KLEN
	jb .L4dec128
	lea 0x20(TKEYP), TKEYP
	je .L4dec192
	add $0x20, TKEYP
	movaps -0x60(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps -0x50(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
.align 4
.L4dec192:
	movaps -0x40(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps -0x30(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
.align 4
.L4dec128:
	movaps -0x20(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps -0x10(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps (TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps 0x10(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps 0x20(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps 0x30(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps 0x40(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps 0x50(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps 0x60(TKEYP), KEY
	AESDEC KEY STATE1
	AESDEC KEY STATE2
	AESDEC KEY STATE3
	AESDEC KEY STATE4
	movaps 0x70(TKEYP), KEY
	AESDECLAST KEY STATE1		# last round
	AESDECLAST KEY STATE2
	AESDECLAST KEY STATE3
	AESDECLAST KEY STATE4
	ret
ENDPROC(_aesni_dec4)

/*
 * void aesni_ecb_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len)
 */
ENTRY(aesni_ecb_enc)
#ifndef __x86_64__
	pushl LEN
	pushl KEYP
	pushl KLEN
	movl 16(%esp), KEYP
	movl 20(%esp), OUTP
	movl 24(%esp), INP
	movl 28(%esp), LEN
#endif
	test LEN, LEN		# check length
	jz .Lecb_enc_ret
	mov 480(KEYP), KLEN
	cmp $16, LEN
	jb .Lecb_enc_ret
	cmp $64, LEN
	jb .Lecb_enc_loop1
.align 4
.Lecb_enc_loop4:
	movups (INP), STATE1
	movups 0x10(INP), STATE2
	movups 0x20(INP), STATE3
	movups 0x30(INP), STATE4
	call _aesni_enc4
	movups STATE1, (OUTP)
	movups STATE2, 0x10(OUTP)
	movups STATE3, 0x20(OUTP)
	movups STATE4, 0x30(OUTP)
	sub $64, LEN
	add $64, INP
	add $64, OUTP
	cmp $64, LEN
	jge .Lecb_enc_loop4
	cmp $16, LEN
	jb .Lecb_enc_ret
.align 4
.Lecb_enc_loop1:
	movups (INP), STATE1
	call _aesni_enc1
	movups STATE1, (OUTP)
	sub $16, LEN
	add $16, INP
	add $16, OUTP
	cmp $16, LEN
	jge .Lecb_enc_loop1
.Lecb_enc_ret:
#ifndef __x86_64__
	popl KLEN
	popl KEYP
	popl LEN
#endif
	ret
ENDPROC(aesni_ecb_enc)

/*
 * void aesni_ecb_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len);
 */
ENTRY(aesni_ecb_dec)
#ifndef __x86_64__
	pushl LEN
	pushl KEYP
	pushl KLEN
	movl 16(%esp), KEYP
	movl 20(%esp), OUTP
	movl 24(%esp), INP
	movl 28(%esp), LEN
#endif
	test LEN, LEN
	jz .Lecb_dec_ret
	mov 480(KEYP), KLEN
	add $240, KEYP
	cmp $16, LEN
	jb .Lecb_dec_ret
	cmp $64, LEN
	jb .Lecb_dec_loop1
.align 4
.Lecb_dec_loop4:
	movups (INP), STATE1
	movups 0x10(INP), STATE2
	movups 0x20(INP), STATE3
	movups 0x30(INP), STATE4
	call _aesni_dec4
	movups STATE1, (OUTP)
	movups STATE2, 0x10(OUTP)
	movups STATE3, 0x20(OUTP)
	movups STATE4, 0x30(OUTP)
	sub $64, LEN
	add $64, INP
	add $64, OUTP
	cmp $64, LEN
	jge .Lecb_dec_loop4
	cmp $16, LEN
	jb .Lecb_dec_ret
.align 4
.Lecb_dec_loop1:
	movups (INP), STATE1
	call _aesni_dec1
	movups STATE1, (OUTP)
	sub $16, LEN
	add $16, INP
	add $16, OUTP
	cmp $16, LEN
	jge .Lecb_dec_loop1
.Lecb_dec_ret:
#ifndef __x86_64__
	popl KLEN
	popl KEYP
	popl LEN
#endif
	ret
ENDPROC(aesni_ecb_dec)

/*
 * void aesni_cbc_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
ENTRY(aesni_cbc_enc)
#ifndef __x86_64__
	pushl IVP
	pushl LEN
	pushl KEYP
	pushl KLEN
	movl 20(%esp), KEYP
	movl 24(%esp), OUTP
	movl 28(%esp), INP
	movl 32(%esp), LEN
	movl 36(%esp), IVP
#endif
	cmp $16, LEN
	jb .Lcbc_enc_ret
	mov 480(KEYP), KLEN
	movups (IVP), STATE	# load iv as initial state
.align 4
.Lcbc_enc_loop:
	movups (INP), IN	# load input
	pxor IN, STATE
	call _aesni_enc1
	movups STATE, (OUTP)	# store output
	sub $16, LEN
	add $16, INP
	add $16, OUTP
	cmp $16, LEN
	jge .Lcbc_enc_loop
	movups STATE, (IVP)
.Lcbc_enc_ret:
#ifndef __x86_64__
	popl KLEN
	popl KEYP
	popl LEN
	popl IVP
#endif
	ret
ENDPROC(aesni_cbc_enc)

/*
 * void aesni_cbc_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
ENTRY(aesni_cbc_dec)
#ifndef __x86_64__
	pushl IVP
	pushl LEN
	pushl KEYP
	pushl KLEN
	movl 20(%esp), KEYP
	movl 24(%esp), OUTP
	movl 28(%esp), INP
	movl 32(%esp), LEN
	movl 36(%esp), IVP
#endif
	cmp $16, LEN
	jb .Lcbc_dec_just_ret
	mov 480(KEYP), KLEN
	add $240, KEYP
	movups (IVP), IV
	cmp $64, LEN
	jb .Lcbc_dec_loop1
.align 4
.Lcbc_dec_loop4:
	movups (INP), IN1
	movaps IN1, STATE1
	movups 0x10(INP), IN2
	movaps IN2, STATE2
#ifdef __x86_64__
	movups 0x20(INP), IN3
	movaps IN3, STATE3
	movups 0x30(INP), IN4
	movaps IN4, STATE4
#else
	movups 0x20(INP), IN1
	movaps IN1, STATE3
	movups 0x30(INP), IN2
	movaps IN2, STATE4
#endif
	call _aesni_dec4
	pxor IV, STATE1
#ifdef __x86_64__
	pxor IN1, STATE2
	pxor IN2, STATE3
	pxor IN3, STATE4
	movaps IN4, IV
#else
	pxor IN1, STATE4
	movaps IN2, IV
	movups (INP), IN1
	pxor IN1, STATE2
	movups 0x10(INP), IN2
	pxor IN2, STATE3
#endif
	movups STATE1, (OUTP)
	movups STATE2, 0x10(OUTP)
	movups STATE3, 0x20(OUTP)
	movups STATE4, 0x30(OUTP)
	sub $64, LEN
	add $64, INP
	add $64, OUTP
	cmp $64, LEN
	jge .Lcbc_dec_loop4
	cmp $16, LEN
	jb .Lcbc_dec_ret
.align 4
.Lcbc_dec_loop1:
	movups (INP), IN
	movaps IN, STATE
	call _aesni_dec1
	pxor IV, STATE
	movups STATE, (OUTP)
	movaps IN, IV
	sub $16, LEN
	add $16, INP
	add $16, OUTP
	cmp $16, LEN
	jge .Lcbc_dec_loop1
.Lcbc_dec_ret:
	movups IV, (IVP)
.Lcbc_dec_just_ret:
#ifndef __x86_64__
	popl KLEN
	popl KEYP
	popl LEN
	popl IVP
#endif
	ret
ENDPROC(aesni_cbc_dec)

#ifdef __x86_64__
.align 16
.Lbswap_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

/*
 * _aesni_inc_init:	internal ABI
 *	setup registers used by _aesni_inc
 * input:
 *	IV
 * output:
 *	CTR:	== IV, in little endian
 *	TCTR_LOW: == lower qword of CTR
 *	INC:	== 1, in little endian
 *	BSWAP_MASK == endian swapping mask
 */
.align 4
_aesni_inc_init:
	movaps .Lbswap_mask, BSWAP_MASK
	movaps IV, CTR
	PSHUFB_XMM BSWAP_MASK CTR
	mov $1, TCTR_LOW
	MOVQ_R64_XMM TCTR_LOW INC
	MOVQ_R64_XMM CTR TCTR_LOW
	ret
ENDPROC(_aesni_inc_init)

/*
 * _aesni_inc:		internal ABI
 *	Increase IV by 1, IV is in big endian
 * input:
 *	IV
 *	CTR:	== IV, in little endian
 *	TCTR_LOW: == lower qword of CTR
 *	INC:	== 1, in little endian
 *	BSWAP_MASK == endian swapping mask
 * output:
 *	IV:	Increase by 1
 * changed:
 *	CTR:	== output IV, in little endian
 *	TCTR_LOW: == lower qword of CTR
 */
.align 4
_aesni_inc:
	paddq INC, CTR
	add $1, TCTR_LOW
	jnc .Linc_low
	pslldq $8, INC
	paddq INC, CTR
	psrldq $8, INC
.Linc_low:
	movaps CTR, IV
	PSHUFB_XMM BSWAP_MASK IV
	ret
ENDPROC(_aesni_inc)

/*
 * void aesni_ctr_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
ENTRY(aesni_ctr_enc)
	cmp $16, LEN
	jb .Lctr_enc_just_ret
	mov 480(KEYP), KLEN
	movups (IVP), IV
	call _aesni_inc_init
	cmp $64, LEN
	jb .Lctr_enc_loop1
.align 4
.Lctr_enc_loop4:
	movaps IV, STATE1
	call _aesni_inc
	movups (INP), IN1
	movaps IV, STATE2
	call _aesni_inc
	movups 0x10(INP), IN2
	movaps IV, STATE3
	call _aesni_inc
	movups 0x20(INP), IN3
	movaps IV, STATE4
	call _aesni_inc
	movups 0x30(INP), IN4
	call _aesni_enc4
	pxor IN1, STATE1
	movups STATE1, (OUTP)
	pxor IN2, STATE2
	movups STATE2, 0x10(OUTP)
	pxor IN3, STATE3
	movups STATE3, 0x20(OUTP)
	pxor IN4, STATE4
	movups STATE4, 0x30(OUTP)
	sub $64, LEN
	add $64, INP
	add $64, OUTP
	cmp $64, LEN
	jge .Lctr_enc_loop4
	cmp $16, LEN
	jb .Lctr_enc_ret
.align 4
.Lctr_enc_loop1:
	movaps IV, STATE
	call _aesni_inc
	movups (INP), IN
	call _aesni_enc1
	pxor IN, STATE
	movups STATE, (OUTP)
	sub $16, LEN
	add $16, INP
	add $16, OUTP
	cmp $16, LEN
	jge .Lctr_enc_loop1
.Lctr_enc_ret:
	movups IV, (IVP)
.Lctr_enc_just_ret:
	ret
ENDPROC(aesni_ctr_enc)
#endif
