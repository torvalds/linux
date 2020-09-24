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
#include <asm/frame.h>
#include <asm/nospec-branch.h>

/*
 * The following macros are used to move an (un)aligned 16 byte value to/from
 * an XMM register.  This can done for either FP or integer values, for FP use
 * movaps (move aligned packed single) or integer use movdqa (move double quad
 * aligned).  It doesn't make a performance difference which instruction is used
 * since Nehalem (original Core i7) was released.  However, the movaps is a byte
 * shorter, so that is the one we'll use for now. (same for unaligned).
 */
#define MOVADQ	movaps
#define MOVUDQ	movups

#ifdef __x86_64__

# constants in mergeable sections, linker can reorder and merge
.section	.rodata.cst16.gf128mul_x_ble_mask, "aM", @progbits, 16
.align 16
.Lgf128mul_x_ble_mask:
	.octa 0x00000000000000010000000000000087
.section	.rodata.cst16.POLY, "aM", @progbits, 16
.align 16
POLY:   .octa 0xC2000000000000000000000000000001
.section	.rodata.cst16.TWOONE, "aM", @progbits, 16
.align 16
TWOONE: .octa 0x00000001000000000000000000000001

.section	.rodata.cst16.SHUF_MASK, "aM", @progbits, 16
.align 16
SHUF_MASK:  .octa 0x000102030405060708090A0B0C0D0E0F
.section	.rodata.cst16.MASK1, "aM", @progbits, 16
.align 16
MASK1:      .octa 0x0000000000000000ffffffffffffffff
.section	.rodata.cst16.MASK2, "aM", @progbits, 16
.align 16
MASK2:      .octa 0xffffffffffffffff0000000000000000
.section	.rodata.cst16.ONE, "aM", @progbits, 16
.align 16
ONE:        .octa 0x00000000000000000000000000000001
.section	.rodata.cst16.F_MIN_MASK, "aM", @progbits, 16
.align 16
F_MIN_MASK: .octa 0xf1f2f3f4f5f6f7f8f9fafbfcfdfeff0
.section	.rodata.cst16.dec, "aM", @progbits, 16
.align 16
dec:        .octa 0x1
.section	.rodata.cst16.enc, "aM", @progbits, 16
.align 16
enc:        .octa 0x2

# order of these constants should not change.
# more specifically, ALL_F should follow SHIFT_MASK,
# and zero should follow ALL_F
.section	.rodata, "a", @progbits
.align 16
SHIFT_MASK: .octa 0x0f0e0d0c0b0a09080706050403020100
ALL_F:      .octa 0xffffffffffffffffffffffffffffffff
            .octa 0x00000000000000000000000000000000

.text


#define	STACK_OFFSET    8*3

#define AadHash 16*0
#define AadLen 16*1
#define InLen (16*1)+8
#define PBlockEncKey 16*2
#define OrigIV 16*3
#define CurCount 16*4
#define PBlockLen 16*5
#define	HashKey		16*6	// store HashKey <<1 mod poly here
#define	HashKey_2	16*7	// store HashKey^2 <<1 mod poly here
#define	HashKey_3	16*8	// store HashKey^3 <<1 mod poly here
#define	HashKey_4	16*9	// store HashKey^4 <<1 mod poly here
#define	HashKey_k	16*10	// store XOR of High 64 bits and Low 64
				// bits of  HashKey <<1 mod poly here
				//(for Karatsuba purposes)
#define	HashKey_2_k	16*11	// store XOR of High 64 bits and Low 64
				// bits of  HashKey^2 <<1 mod poly here
				// (for Karatsuba purposes)
#define	HashKey_3_k	16*12	// store XOR of High 64 bits and Low 64
				// bits of  HashKey^3 <<1 mod poly here
				// (for Karatsuba purposes)
#define	HashKey_4_k	16*13	// store XOR of High 64 bits and Low 64
				// bits of  HashKey^4 <<1 mod poly here
				// (for Karatsuba purposes)

#define arg1 rdi
#define arg2 rsi
#define arg3 rdx
#define arg4 rcx
#define arg5 r8
#define arg6 r9
#define arg7 STACK_OFFSET+8(%rsp)
#define arg8 STACK_OFFSET+16(%rsp)
#define arg9 STACK_OFFSET+24(%rsp)
#define arg10 STACK_OFFSET+32(%rsp)
#define arg11 STACK_OFFSET+40(%rsp)
#define keysize 2*15*16(%arg1)
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

#define GF128MUL_MASK %xmm10

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

.macro FUNC_SAVE
	push	%r12
	push	%r13
	push	%r14
#
# states of %xmm registers %xmm6:%xmm15 not saved
# all %xmm registers are clobbered
#
.endm


.macro FUNC_RESTORE
	pop	%r14
	pop	%r13
	pop	%r12
.endm

# Precompute hashkeys.
# Input: Hash subkey.
# Output: HashKeys stored in gcm_context_data.  Only needs to be called
# once per key.
# clobbers r12, and tmp xmm registers.
.macro PRECOMPUTE SUBKEY TMP1 TMP2 TMP3 TMP4 TMP5 TMP6 TMP7
	mov	\SUBKEY, %r12
	movdqu	(%r12), \TMP3
	movdqa	SHUF_MASK(%rip), \TMP2
	PSHUFB_XMM \TMP2, \TMP3

	# precompute HashKey<<1 mod poly from the HashKey (required for GHASH)

	movdqa	\TMP3, \TMP2
	psllq	$1, \TMP3
	psrlq	$63, \TMP2
	movdqa	\TMP2, \TMP1
	pslldq	$8, \TMP2
	psrldq	$8, \TMP1
	por	\TMP2, \TMP3

	# reduce HashKey<<1

	pshufd	$0x24, \TMP1, \TMP2
	pcmpeqd TWOONE(%rip), \TMP2
	pand	POLY(%rip), \TMP2
	pxor	\TMP2, \TMP3
	movdqu	\TMP3, HashKey(%arg2)

	movdqa	   \TMP3, \TMP5
	pshufd	   $78, \TMP3, \TMP1
	pxor	   \TMP3, \TMP1
	movdqu	   \TMP1, HashKey_k(%arg2)

	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^2<<1 (mod poly)
	movdqu	   \TMP5, HashKey_2(%arg2)
# HashKey_2 = HashKey^2<<1 (mod poly)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqu	   \TMP1, HashKey_2_k(%arg2)

	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^3<<1 (mod poly)
	movdqu	   \TMP5, HashKey_3(%arg2)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqu	   \TMP1, HashKey_3_k(%arg2)

	GHASH_MUL  \TMP5, \TMP3, \TMP1, \TMP2, \TMP4, \TMP6, \TMP7
# TMP5 = HashKey^3<<1 (mod poly)
	movdqu	   \TMP5, HashKey_4(%arg2)
	pshufd	   $78, \TMP5, \TMP1
	pxor	   \TMP5, \TMP1
	movdqu	   \TMP1, HashKey_4_k(%arg2)
.endm

# GCM_INIT initializes a gcm_context struct to prepare for encoding/decoding.
# Clobbers rax, r10-r13 and xmm0-xmm6, %xmm13
.macro GCM_INIT Iv SUBKEY AAD AADLEN
	mov \AADLEN, %r11
	mov %r11, AadLen(%arg2) # ctx_data.aad_length = aad_length
	xor %r11d, %r11d
	mov %r11, InLen(%arg2) # ctx_data.in_length = 0
	mov %r11, PBlockLen(%arg2) # ctx_data.partial_block_length = 0
	mov %r11, PBlockEncKey(%arg2) # ctx_data.partial_block_enc_key = 0
	mov \Iv, %rax
	movdqu (%rax), %xmm0
	movdqu %xmm0, OrigIV(%arg2) # ctx_data.orig_IV = iv

	movdqa  SHUF_MASK(%rip), %xmm2
	PSHUFB_XMM %xmm2, %xmm0
	movdqu %xmm0, CurCount(%arg2) # ctx_data.current_counter = iv

	PRECOMPUTE \SUBKEY, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7
	movdqu HashKey(%arg2), %xmm13

	CALC_AAD_HASH %xmm13, \AAD, \AADLEN, %xmm0, %xmm1, %xmm2, %xmm3, \
	%xmm4, %xmm5, %xmm6
.endm

# GCM_ENC_DEC Encodes/Decodes given data. Assumes that the passed gcm_context
# struct has been initialized by GCM_INIT.
# Requires the input data be at least 1 byte long because of READ_PARTIAL_BLOCK
# Clobbers rax, r10-r13, and xmm0-xmm15
.macro GCM_ENC_DEC operation
	movdqu AadHash(%arg2), %xmm8
	movdqu HashKey(%arg2), %xmm13
	add %arg5, InLen(%arg2)

	xor %r11d, %r11d # initialise the data pointer offset as zero
	PARTIAL_BLOCK %arg3 %arg4 %arg5 %r11 %xmm8 \operation

	sub %r11, %arg5		# sub partial block data used
	mov %arg5, %r13		# save the number of bytes

	and $-16, %r13		# %r13 = %r13 - (%r13 mod 16)
	mov %r13, %r12
	# Encrypt/Decrypt first few blocks

	and	$(3<<4), %r12
	jz	_initial_num_blocks_is_0_\@
	cmp	$(2<<4), %r12
	jb	_initial_num_blocks_is_1_\@
	je	_initial_num_blocks_is_2_\@
_initial_num_blocks_is_3_\@:
	INITIAL_BLOCKS_ENC_DEC	%xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 5, 678, \operation
	sub	$48, %r13
	jmp	_initial_blocks_\@
_initial_num_blocks_is_2_\@:
	INITIAL_BLOCKS_ENC_DEC	%xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 6, 78, \operation
	sub	$32, %r13
	jmp	_initial_blocks_\@
_initial_num_blocks_is_1_\@:
	INITIAL_BLOCKS_ENC_DEC	%xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 7, 8, \operation
	sub	$16, %r13
	jmp	_initial_blocks_\@
_initial_num_blocks_is_0_\@:
	INITIAL_BLOCKS_ENC_DEC	%xmm9, %xmm10, %xmm13, %xmm11, %xmm12, %xmm0, \
%xmm1, %xmm2, %xmm3, %xmm4, %xmm8, %xmm5, %xmm6, 8, 0, \operation
_initial_blocks_\@:

	# Main loop - Encrypt/Decrypt remaining blocks

	cmp	$0, %r13
	je	_zero_cipher_left_\@
	sub	$64, %r13
	je	_four_cipher_left_\@
_crypt_by_4_\@:
	GHASH_4_ENCRYPT_4_PARALLEL_\operation	%xmm9, %xmm10, %xmm11, %xmm12, \
	%xmm13, %xmm14, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, \
	%xmm7, %xmm8, enc
	add	$64, %r11
	sub	$64, %r13
	jne	_crypt_by_4_\@
_four_cipher_left_\@:
	GHASH_LAST_4	%xmm9, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, \
%xmm15, %xmm1, %xmm2, %xmm3, %xmm4, %xmm8
_zero_cipher_left_\@:
	movdqu %xmm8, AadHash(%arg2)
	movdqu %xmm0, CurCount(%arg2)

	mov	%arg5, %r13
	and	$15, %r13			# %r13 = arg5 (mod 16)
	je	_multiple_of_16_bytes_\@

	mov %r13, PBlockLen(%arg2)

	# Handle the last <16 Byte block separately
	paddd ONE(%rip), %xmm0                # INCR CNT to get Yn
	movdqu %xmm0, CurCount(%arg2)
	movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10, %xmm0

	ENCRYPT_SINGLE_BLOCK	%xmm0, %xmm1        # Encrypt(K, Yn)
	movdqu %xmm0, PBlockEncKey(%arg2)

	cmp	$16, %arg5
	jge _large_enough_update_\@

	lea (%arg4,%r11,1), %r10
	mov %r13, %r12
	READ_PARTIAL_BLOCK %r10 %r12 %xmm2 %xmm1
	jmp _data_read_\@

_large_enough_update_\@:
	sub	$16, %r11
	add	%r13, %r11

	# receive the last <16 Byte block
	movdqu	(%arg4, %r11, 1), %xmm1

	sub	%r13, %r11
	add	$16, %r11

	lea	SHIFT_MASK+16(%rip), %r12
	# adjust the shuffle mask pointer to be able to shift 16-r13 bytes
	# (r13 is the number of bytes in plaintext mod 16)
	sub	%r13, %r12
	# get the appropriate shuffle mask
	movdqu	(%r12), %xmm2
	# shift right 16-r13 bytes
	PSHUFB_XMM  %xmm2, %xmm1

_data_read_\@:
	lea ALL_F+16(%rip), %r12
	sub %r13, %r12

.ifc \operation, dec
	movdqa  %xmm1, %xmm2
.endif
	pxor	%xmm1, %xmm0            # XOR Encrypt(K, Yn)
	movdqu	(%r12), %xmm1
	# get the appropriate mask to mask out top 16-r13 bytes of xmm0
	pand	%xmm1, %xmm0            # mask out top 16-r13 bytes of xmm0
.ifc \operation, dec
	pand    %xmm1, %xmm2
	movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10 ,%xmm2

	pxor %xmm2, %xmm8
.else
	movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10,%xmm0

	pxor	%xmm0, %xmm8
.endif

	movdqu %xmm8, AadHash(%arg2)
.ifc \operation, enc
	# GHASH computation for the last <16 byte block
	movdqa SHUF_MASK(%rip), %xmm10
	# shuffle xmm0 back to output as ciphertext
	PSHUFB_XMM %xmm10, %xmm0
.endif

	# Output %r13 bytes
	MOVQ_R64_XMM %xmm0, %rax
	cmp $8, %r13
	jle _less_than_8_bytes_left_\@
	mov %rax, (%arg3 , %r11, 1)
	add $8, %r11
	psrldq $8, %xmm0
	MOVQ_R64_XMM %xmm0, %rax
	sub $8, %r13
_less_than_8_bytes_left_\@:
	mov %al,  (%arg3, %r11, 1)
	add $1, %r11
	shr $8, %rax
	sub $1, %r13
	jne _less_than_8_bytes_left_\@
_multiple_of_16_bytes_\@:
.endm

# GCM_COMPLETE Finishes update of tag of last partial block
# Output: Authorization Tag (AUTH_TAG)
# Clobbers rax, r10-r12, and xmm0, xmm1, xmm5-xmm15
.macro GCM_COMPLETE AUTHTAG AUTHTAGLEN
	movdqu AadHash(%arg2), %xmm8
	movdqu HashKey(%arg2), %xmm13

	mov PBlockLen(%arg2), %r12

	cmp $0, %r12
	je _partial_done\@

	GHASH_MUL %xmm8, %xmm13, %xmm9, %xmm10, %xmm11, %xmm5, %xmm6

_partial_done\@:
	mov AadLen(%arg2), %r12  # %r13 = aadLen (number of bytes)
	shl	$3, %r12		  # convert into number of bits
	movd	%r12d, %xmm15		  # len(A) in %xmm15
	mov InLen(%arg2), %r12
	shl     $3, %r12                  # len(C) in bits (*128)
	MOVQ_R64_XMM    %r12, %xmm1

	pslldq	$8, %xmm15		  # %xmm15 = len(A)||0x0000000000000000
	pxor	%xmm1, %xmm15		  # %xmm15 = len(A)||len(C)
	pxor	%xmm15, %xmm8
	GHASH_MUL	%xmm8, %xmm13, %xmm9, %xmm10, %xmm11, %xmm5, %xmm6
	# final GHASH computation
	movdqa SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM %xmm10, %xmm8

	movdqu OrigIV(%arg2), %xmm0       # %xmm0 = Y0
	ENCRYPT_SINGLE_BLOCK	%xmm0,  %xmm1	  # E(K, Y0)
	pxor	%xmm8, %xmm0
_return_T_\@:
	mov	\AUTHTAG, %r10                     # %r10 = authTag
	mov	\AUTHTAGLEN, %r11                    # %r11 = auth_tag_len
	cmp	$16, %r11
	je	_T_16_\@
	cmp	$8, %r11
	jl	_T_4_\@
_T_8_\@:
	MOVQ_R64_XMM	%xmm0, %rax
	mov	%rax, (%r10)
	add	$8, %r10
	sub	$8, %r11
	psrldq	$8, %xmm0
	cmp	$0, %r11
	je	_return_T_done_\@
_T_4_\@:
	movd	%xmm0, %eax
	mov	%eax, (%r10)
	add	$4, %r10
	sub	$4, %r11
	psrldq	$4, %xmm0
	cmp	$0, %r11
	je	_return_T_done_\@
_T_123_\@:
	movd	%xmm0, %eax
	cmp	$2, %r11
	jl	_T_1_\@
	mov	%ax, (%r10)
	cmp	$2, %r11
	je	_return_T_done_\@
	add	$2, %r10
	sar	$16, %eax
_T_1_\@:
	mov	%al, (%r10)
	jmp	_return_T_done_\@
_T_16_\@:
	movdqu	%xmm0, (%r10)
_return_T_done_\@:
.endm

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

# Reads DLEN bytes starting at DPTR and stores in XMMDst
# where 0 < DLEN < 16
# Clobbers %rax, DLEN and XMM1
.macro READ_PARTIAL_BLOCK DPTR DLEN XMM1 XMMDst
        cmp $8, \DLEN
        jl _read_lt8_\@
        mov (\DPTR), %rax
        MOVQ_R64_XMM %rax, \XMMDst
        sub $8, \DLEN
        jz _done_read_partial_block_\@
	xor %eax, %eax
_read_next_byte_\@:
        shl $8, %rax
        mov 7(\DPTR, \DLEN, 1), %al
        dec \DLEN
        jnz _read_next_byte_\@
        MOVQ_R64_XMM %rax, \XMM1
	pslldq $8, \XMM1
        por \XMM1, \XMMDst
	jmp _done_read_partial_block_\@
_read_lt8_\@:
	xor %eax, %eax
_read_next_byte_lt8_\@:
        shl $8, %rax
        mov -1(\DPTR, \DLEN, 1), %al
        dec \DLEN
        jnz _read_next_byte_lt8_\@
        MOVQ_R64_XMM %rax, \XMMDst
_done_read_partial_block_\@:
.endm

# CALC_AAD_HASH: Calculates the hash of the data which will not be encrypted.
# clobbers r10-11, xmm14
.macro CALC_AAD_HASH HASHKEY AAD AADLEN TMP1 TMP2 TMP3 TMP4 TMP5 \
	TMP6 TMP7
	MOVADQ	   SHUF_MASK(%rip), %xmm14
	mov	   \AAD, %r10		# %r10 = AAD
	mov	   \AADLEN, %r11		# %r11 = aadLen
	pxor	   \TMP7, \TMP7
	pxor	   \TMP6, \TMP6

	cmp	   $16, %r11
	jl	   _get_AAD_rest\@
_get_AAD_blocks\@:
	movdqu	   (%r10), \TMP7
	PSHUFB_XMM   %xmm14, \TMP7 # byte-reflect the AAD data
	pxor	   \TMP7, \TMP6
	GHASH_MUL  \TMP6, \HASHKEY, \TMP1, \TMP2, \TMP3, \TMP4, \TMP5
	add	   $16, %r10
	sub	   $16, %r11
	cmp	   $16, %r11
	jge	   _get_AAD_blocks\@

	movdqu	   \TMP6, \TMP7

	/* read the last <16B of AAD */
_get_AAD_rest\@:
	cmp	   $0, %r11
	je	   _get_AAD_done\@

	READ_PARTIAL_BLOCK %r10, %r11, \TMP1, \TMP7
	PSHUFB_XMM   %xmm14, \TMP7 # byte-reflect the AAD data
	pxor	   \TMP6, \TMP7
	GHASH_MUL  \TMP7, \HASHKEY, \TMP1, \TMP2, \TMP3, \TMP4, \TMP5
	movdqu \TMP7, \TMP6

_get_AAD_done\@:
	movdqu \TMP6, AadHash(%arg2)
.endm

# PARTIAL_BLOCK: Handles encryption/decryption and the tag partial blocks
# between update calls.
# Requires the input data be at least 1 byte long due to READ_PARTIAL_BLOCK
# Outputs encrypted bytes, and updates hash and partial info in gcm_data_context
# Clobbers rax, r10, r12, r13, xmm0-6, xmm9-13
.macro PARTIAL_BLOCK CYPH_PLAIN_OUT PLAIN_CYPH_IN PLAIN_CYPH_LEN DATA_OFFSET \
	AAD_HASH operation
	mov 	PBlockLen(%arg2), %r13
	cmp	$0, %r13
	je	_partial_block_done_\@	# Leave Macro if no partial blocks
	# Read in input data without over reading
	cmp	$16, \PLAIN_CYPH_LEN
	jl	_fewer_than_16_bytes_\@
	movups	(\PLAIN_CYPH_IN), %xmm1	# If more than 16 bytes, just fill xmm
	jmp	_data_read_\@

_fewer_than_16_bytes_\@:
	lea	(\PLAIN_CYPH_IN, \DATA_OFFSET, 1), %r10
	mov	\PLAIN_CYPH_LEN, %r12
	READ_PARTIAL_BLOCK %r10 %r12 %xmm0 %xmm1

	mov PBlockLen(%arg2), %r13

_data_read_\@:				# Finished reading in data

	movdqu	PBlockEncKey(%arg2), %xmm9
	movdqu	HashKey(%arg2), %xmm13

	lea	SHIFT_MASK(%rip), %r12

	# adjust the shuffle mask pointer to be able to shift r13 bytes
	# r16-r13 is the number of bytes in plaintext mod 16)
	add	%r13, %r12
	movdqu	(%r12), %xmm2		# get the appropriate shuffle mask
	PSHUFB_XMM %xmm2, %xmm9		# shift right r13 bytes

.ifc \operation, dec
	movdqa	%xmm1, %xmm3
	pxor	%xmm1, %xmm9		# Cyphertext XOR E(K, Yn)

	mov	\PLAIN_CYPH_LEN, %r10
	add	%r13, %r10
	# Set r10 to be the amount of data left in CYPH_PLAIN_IN after filling
	sub	$16, %r10
	# Determine if if partial block is not being filled and
	# shift mask accordingly
	jge	_no_extra_mask_1_\@
	sub	%r10, %r12
_no_extra_mask_1_\@:

	movdqu	ALL_F-SHIFT_MASK(%r12), %xmm1
	# get the appropriate mask to mask out bottom r13 bytes of xmm9
	pand	%xmm1, %xmm9		# mask out bottom r13 bytes of xmm9

	pand	%xmm1, %xmm3
	movdqa	SHUF_MASK(%rip), %xmm10
	PSHUFB_XMM	%xmm10, %xmm3
	PSHUFB_XMM	%xmm2, %xmm3
	pxor	%xmm3, \AAD_HASH

	cmp	$0, %r10
	jl	_partial_incomplete_1_\@

	# GHASH computation for the last <16 Byte block
	GHASH_MUL \AAD_HASH, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6
	xor	%eax, %eax

	mov	%rax, PBlockLen(%arg2)
	jmp	_dec_done_\@
_partial_incomplete_1_\@:
	add	\PLAIN_CYPH_LEN, PBlockLen(%arg2)
_dec_done_\@:
	movdqu	\AAD_HASH, AadHash(%arg2)
.else
	pxor	%xmm1, %xmm9			# Plaintext XOR E(K, Yn)

	mov	\PLAIN_CYPH_LEN, %r10
	add	%r13, %r10
	# Set r10 to be the amount of data left in CYPH_PLAIN_IN after filling
	sub	$16, %r10
	# Determine if if partial block is not being filled and
	# shift mask accordingly
	jge	_no_extra_mask_2_\@
	sub	%r10, %r12
_no_extra_mask_2_\@:

	movdqu	ALL_F-SHIFT_MASK(%r12), %xmm1
	# get the appropriate mask to mask out bottom r13 bytes of xmm9
	pand	%xmm1, %xmm9

	movdqa	SHUF_MASK(%rip), %xmm1
	PSHUFB_XMM %xmm1, %xmm9
	PSHUFB_XMM %xmm2, %xmm9
	pxor	%xmm9, \AAD_HASH

	cmp	$0, %r10
	jl	_partial_incomplete_2_\@

	# GHASH computation for the last <16 Byte block
	GHASH_MUL \AAD_HASH, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6
	xor	%eax, %eax

	mov	%rax, PBlockLen(%arg2)
	jmp	_encode_done_\@
_partial_incomplete_2_\@:
	add	\PLAIN_CYPH_LEN, PBlockLen(%arg2)
_encode_done_\@:
	movdqu	\AAD_HASH, AadHash(%arg2)

	movdqa	SHUF_MASK(%rip), %xmm10
	# shuffle xmm9 back to output as ciphertext
	PSHUFB_XMM	%xmm10, %xmm9
	PSHUFB_XMM	%xmm2, %xmm9
.endif
	# output encrypted Bytes
	cmp	$0, %r10
	jl	_partial_fill_\@
	mov	%r13, %r12
	mov	$16, %r13
	# Set r13 to be the number of bytes to write out
	sub	%r12, %r13
	jmp	_count_set_\@
_partial_fill_\@:
	mov	\PLAIN_CYPH_LEN, %r13
_count_set_\@:
	movdqa	%xmm9, %xmm0
	MOVQ_R64_XMM	%xmm0, %rax
	cmp	$8, %r13
	jle	_less_than_8_bytes_left_\@

	mov	%rax, (\CYPH_PLAIN_OUT, \DATA_OFFSET, 1)
	add	$8, \DATA_OFFSET
	psrldq	$8, %xmm0
	MOVQ_R64_XMM	%xmm0, %rax
	sub	$8, %r13
_less_than_8_bytes_left_\@:
	movb	%al, (\CYPH_PLAIN_OUT, \DATA_OFFSET, 1)
	add	$1, \DATA_OFFSET
	shr	$8, %rax
	sub	$1, %r13
	jne	_less_than_8_bytes_left_\@
_partial_block_done_\@:
.endm # PARTIAL_BLOCK

/*
* if a = number of total plaintext bytes
* b = floor(a/16)
* num_initial_blocks = b mod 4
* encrypt the initial num_initial_blocks blocks and apply ghash on
* the ciphertext
* %r10, %r11, %r12, %rax, %xmm5, %xmm6, %xmm7, %xmm8, %xmm9 registers
* are clobbered
* arg1, %arg2, %arg3 are used as a pointer only, not modified
*/


.macro INITIAL_BLOCKS_ENC_DEC TMP1 TMP2 TMP3 TMP4 TMP5 XMM0 XMM1 \
	XMM2 XMM3 XMM4 XMMDst TMP6 TMP7 i i_seq operation
	MOVADQ		SHUF_MASK(%rip), %xmm14

	movdqu AadHash(%arg2), %xmm\i		    # XMM0 = Y0

	# start AES for num_initial_blocks blocks

	movdqu CurCount(%arg2), \XMM0                # XMM0 = Y0

.if (\i == 5) || (\i == 6) || (\i == 7)

	MOVADQ		ONE(%RIP),\TMP1
	MOVADQ		0(%arg1),\TMP2
.irpc index, \i_seq
	paddd		\TMP1, \XMM0                 # INCR Y0
.ifc \operation, dec
        movdqa     \XMM0, %xmm\index
.else
	MOVADQ		\XMM0, %xmm\index
.endif
	PSHUFB_XMM	%xmm14, %xmm\index      # perform a 16 byte swap
	pxor		\TMP2, %xmm\index
.endr
	lea	0x10(%arg1),%r10
	mov	keysize,%eax
	shr	$2,%eax				# 128->4, 192->6, 256->8
	add	$5,%eax			      # 128->9, 192->11, 256->13

aes_loop_initial_\@:
	MOVADQ	(%r10),\TMP1
.irpc	index, \i_seq
	AESENC	\TMP1, %xmm\index
.endr
	add	$16,%r10
	sub	$1,%eax
	jnz	aes_loop_initial_\@

	MOVADQ	(%r10), \TMP1
.irpc index, \i_seq
	AESENCLAST \TMP1, %xmm\index         # Last Round
.endr
.irpc index, \i_seq
	movdqu	   (%arg4 , %r11, 1), \TMP1
	pxor	   \TMP1, %xmm\index
	movdqu	   %xmm\index, (%arg3 , %r11, 1)
	# write back plaintext/ciphertext for num_initial_blocks
	add	   $16, %r11

.ifc \operation, dec
	movdqa     \TMP1, %xmm\index
.endif
	PSHUFB_XMM	   %xmm14, %xmm\index

		# prepare plaintext/ciphertext for GHASH computation
.endr
.endif

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
	jl	_initial_blocks_done\@
	# no need for precomputed values
/*
*
* Precomputations for HashKey parallel with encryption of first 4 blocks.
* Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
*/
	MOVADQ	   ONE(%RIP),\TMP1
	paddd	   \TMP1, \XMM0              # INCR Y0
	MOVADQ	   \XMM0, \XMM1
	PSHUFB_XMM  %xmm14, \XMM1        # perform a 16 byte swap

	paddd	   \TMP1, \XMM0              # INCR Y0
	MOVADQ	   \XMM0, \XMM2
	PSHUFB_XMM  %xmm14, \XMM2        # perform a 16 byte swap

	paddd	   \TMP1, \XMM0              # INCR Y0
	MOVADQ	   \XMM0, \XMM3
	PSHUFB_XMM %xmm14, \XMM3        # perform a 16 byte swap

	paddd	   \TMP1, \XMM0              # INCR Y0
	MOVADQ	   \XMM0, \XMM4
	PSHUFB_XMM %xmm14, \XMM4        # perform a 16 byte swap

	MOVADQ	   0(%arg1),\TMP1
	pxor	   \TMP1, \XMM1
	pxor	   \TMP1, \XMM2
	pxor	   \TMP1, \XMM3
	pxor	   \TMP1, \XMM4
.irpc index, 1234 # do 4 rounds
	movaps 0x10*\index(%arg1), \TMP1
	AESENC	   \TMP1, \XMM1
	AESENC	   \TMP1, \XMM2
	AESENC	   \TMP1, \XMM3
	AESENC	   \TMP1, \XMM4
.endr
.irpc index, 56789 # do next 5 rounds
	movaps 0x10*\index(%arg1), \TMP1
	AESENC	   \TMP1, \XMM1
	AESENC	   \TMP1, \XMM2
	AESENC	   \TMP1, \XMM3
	AESENC	   \TMP1, \XMM4
.endr
	lea	   0xa0(%arg1),%r10
	mov	   keysize,%eax
	shr	   $2,%eax			# 128->4, 192->6, 256->8
	sub	   $4,%eax			# 128->0, 192->2, 256->4
	jz	   aes_loop_pre_done\@

aes_loop_pre_\@:
	MOVADQ	   (%r10),\TMP2
.irpc	index, 1234
	AESENC	   \TMP2, %xmm\index
.endr
	add	   $16,%r10
	sub	   $1,%eax
	jnz	   aes_loop_pre_\@

aes_loop_pre_done\@:
	MOVADQ	   (%r10), \TMP2
	AESENCLAST \TMP2, \XMM1
	AESENCLAST \TMP2, \XMM2
	AESENCLAST \TMP2, \XMM3
	AESENCLAST \TMP2, \XMM4
	movdqu	   16*0(%arg4 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM1
.ifc \operation, dec
	movdqu     \XMM1, 16*0(%arg3 , %r11 , 1)
	movdqa     \TMP1, \XMM1
.endif
	movdqu	   16*1(%arg4 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM2
.ifc \operation, dec
	movdqu     \XMM2, 16*1(%arg3 , %r11 , 1)
	movdqa     \TMP1, \XMM2
.endif
	movdqu	   16*2(%arg4 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM3
.ifc \operation, dec
	movdqu     \XMM3, 16*2(%arg3 , %r11 , 1)
	movdqa     \TMP1, \XMM3
.endif
	movdqu	   16*3(%arg4 , %r11 , 1), \TMP1
	pxor	   \TMP1, \XMM4
.ifc \operation, dec
	movdqu     \XMM4, 16*3(%arg3 , %r11 , 1)
	movdqa     \TMP1, \XMM4
.else
	movdqu     \XMM1, 16*0(%arg3 , %r11 , 1)
	movdqu     \XMM2, 16*1(%arg3 , %r11 , 1)
	movdqu     \XMM3, 16*2(%arg3 , %r11 , 1)
	movdqu     \XMM4, 16*3(%arg3 , %r11 , 1)
.endif

	add	   $64, %r11
	PSHUFB_XMM %xmm14, \XMM1 # perform a 16 byte swap
	pxor	   \XMMDst, \XMM1
# combine GHASHed value with the corresponding ciphertext
	PSHUFB_XMM %xmm14, \XMM2 # perform a 16 byte swap
	PSHUFB_XMM %xmm14, \XMM3 # perform a 16 byte swap
	PSHUFB_XMM %xmm14, \XMM4 # perform a 16 byte swap

_initial_blocks_done\@:

.endm

/*
* encrypt 4 blocks at a time
* ghash the 4 previously encrypted ciphertext blocks
* arg1, %arg3, %arg4 are used as pointers only, not modified
* %r11 is the data offset value
*/
.macro GHASH_4_ENCRYPT_4_PARALLEL_enc TMP1 TMP2 TMP3 TMP4 TMP5 \
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
	movdqu	  HashKey_4(%arg2), \TMP5
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
	movdqu	  HashKey_4_k(%arg2), \TMP5
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
	movdqu	  HashKey_3(%arg2), \TMP5
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
	movdqu	  HashKey_3_k(%arg2), \TMP5
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
	movdqu	  HashKey_2(%arg2), \TMP5

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
	movdqu	  HashKey_2_k(%arg2), \TMP5
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
	movdqu	  HashKey(%arg2), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1          # TMP1 = a1*b1
	movaps 0x90(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1            # Round 9
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	PCLMULQDQ 0x00, \TMP5, \XMM8          # XMM8 = a0*b0
	lea	  0xa0(%arg1),%r10
	mov	  keysize,%eax
	shr	  $2,%eax			# 128->4, 192->6, 256->8
	sub	  $4,%eax			# 128->0, 192->2, 256->4
	jz	  aes_loop_par_enc_done\@

aes_loop_par_enc\@:
	MOVADQ	  (%r10),\TMP3
.irpc	index, 1234
	AESENC	  \TMP3, %xmm\index
.endr
	add	  $16,%r10
	sub	  $1,%eax
	jnz	  aes_loop_par_enc\@

aes_loop_par_enc_done\@:
	MOVADQ	  (%r10), \TMP3
	AESENCLAST \TMP3, \XMM1           # Round 10
	AESENCLAST \TMP3, \XMM2
	AESENCLAST \TMP3, \XMM3
	AESENCLAST \TMP3, \XMM4
	movdqu    HashKey_k(%arg2), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP2          # TMP2 = (a1+a0)*(b1+b0)
	movdqu	  (%arg4,%r11,1), \TMP3
	pxor	  \TMP3, \XMM1                 # Ciphertext/Plaintext XOR EK
	movdqu	  16(%arg4,%r11,1), \TMP3
	pxor	  \TMP3, \XMM2                 # Ciphertext/Plaintext XOR EK
	movdqu	  32(%arg4,%r11,1), \TMP3
	pxor	  \TMP3, \XMM3                 # Ciphertext/Plaintext XOR EK
	movdqu	  48(%arg4,%r11,1), \TMP3
	pxor	  \TMP3, \XMM4                 # Ciphertext/Plaintext XOR EK
        movdqu    \XMM1, (%arg3,%r11,1)        # Write to the ciphertext buffer
        movdqu    \XMM2, 16(%arg3,%r11,1)      # Write to the ciphertext buffer
        movdqu    \XMM3, 32(%arg3,%r11,1)      # Write to the ciphertext buffer
        movdqu    \XMM4, 48(%arg3,%r11,1)      # Write to the ciphertext buffer
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
* arg1, %arg3, %arg4 are used as pointers only, not modified
* %r11 is the data offset value
*/
.macro GHASH_4_ENCRYPT_4_PARALLEL_dec TMP1 TMP2 TMP3 TMP4 TMP5 \
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
	movdqu	  HashKey_4(%arg2), \TMP5
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
	movdqu	  HashKey_4_k(%arg2), \TMP5
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
	movdqu	  HashKey_3(%arg2), \TMP5
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
	movdqu	  HashKey_3_k(%arg2), \TMP5
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
	movdqu	  HashKey_2(%arg2), \TMP5

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
	movdqu	  HashKey_2_k(%arg2), \TMP5
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
	movdqu	  HashKey(%arg2), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1          # TMP1 = a1*b1
	movaps 0x90(%arg1), \TMP3
	AESENC	  \TMP3, \XMM1            # Round 9
	AESENC	  \TMP3, \XMM2
	AESENC	  \TMP3, \XMM3
	AESENC	  \TMP3, \XMM4
	PCLMULQDQ 0x00, \TMP5, \XMM8          # XMM8 = a0*b0
	lea	  0xa0(%arg1),%r10
	mov	  keysize,%eax
	shr	  $2,%eax		        # 128->4, 192->6, 256->8
	sub	  $4,%eax			# 128->0, 192->2, 256->4
	jz	  aes_loop_par_dec_done\@

aes_loop_par_dec\@:
	MOVADQ	  (%r10),\TMP3
.irpc	index, 1234
	AESENC	  \TMP3, %xmm\index
.endr
	add	  $16,%r10
	sub	  $1,%eax
	jnz	  aes_loop_par_dec\@

aes_loop_par_dec_done\@:
	MOVADQ	  (%r10), \TMP3
	AESENCLAST \TMP3, \XMM1           # last round
	AESENCLAST \TMP3, \XMM2
	AESENCLAST \TMP3, \XMM3
	AESENCLAST \TMP3, \XMM4
	movdqu    HashKey_k(%arg2), \TMP5
	PCLMULQDQ 0x00, \TMP5, \TMP2          # TMP2 = (a1+a0)*(b1+b0)
	movdqu	  (%arg4,%r11,1), \TMP3
	pxor	  \TMP3, \XMM1                 # Ciphertext/Plaintext XOR EK
	movdqu	  \XMM1, (%arg3,%r11,1)        # Write to plaintext buffer
	movdqa    \TMP3, \XMM1
	movdqu	  16(%arg4,%r11,1), \TMP3
	pxor	  \TMP3, \XMM2                 # Ciphertext/Plaintext XOR EK
	movdqu	  \XMM2, 16(%arg3,%r11,1)      # Write to plaintext buffer
	movdqa    \TMP3, \XMM2
	movdqu	  32(%arg4,%r11,1), \TMP3
	pxor	  \TMP3, \XMM3                 # Ciphertext/Plaintext XOR EK
	movdqu	  \XMM3, 32(%arg3,%r11,1)      # Write to plaintext buffer
	movdqa    \TMP3, \XMM3
	movdqu	  48(%arg4,%r11,1), \TMP3
	pxor	  \TMP3, \XMM4                 # Ciphertext/Plaintext XOR EK
	movdqu	  \XMM4, 48(%arg3,%r11,1)      # Write to plaintext buffer
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
	movdqu	  HashKey_4(%arg2), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP6       # TMP6 = a1*b1
	PCLMULQDQ 0x00, \TMP5, \XMM1       # XMM1 = a0*b0
	movdqu	  HashKey_4_k(%arg2), \TMP4
	PCLMULQDQ 0x00, \TMP4, \TMP2       # TMP2 = (a1+a0)*(b1+b0)
	movdqa	  \XMM1, \XMMDst
	movdqa	  \TMP2, \XMM1              # result in TMP6, XMMDst, XMM1

        # Multiply TMP1 * HashKey (using Karatsuba)

	movdqa	  \XMM2, \TMP1
	pshufd	  $78, \XMM2, \TMP2
	pxor	  \XMM2, \TMP2
	movdqu	  HashKey_3(%arg2), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1       # TMP1 = a1*b1
	PCLMULQDQ 0x00, \TMP5, \XMM2       # XMM2 = a0*b0
	movdqu	  HashKey_3_k(%arg2), \TMP4
	PCLMULQDQ 0x00, \TMP4, \TMP2       # TMP2 = (a1+a0)*(b1+b0)
	pxor	  \TMP1, \TMP6
	pxor	  \XMM2, \XMMDst
	pxor	  \TMP2, \XMM1
# results accumulated in TMP6, XMMDst, XMM1

        # Multiply TMP1 * HashKey (using Karatsuba)

	movdqa	  \XMM3, \TMP1
	pshufd	  $78, \XMM3, \TMP2
	pxor	  \XMM3, \TMP2
	movdqu	  HashKey_2(%arg2), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1       # TMP1 = a1*b1
	PCLMULQDQ 0x00, \TMP5, \XMM3       # XMM3 = a0*b0
	movdqu	  HashKey_2_k(%arg2), \TMP4
	PCLMULQDQ 0x00, \TMP4, \TMP2       # TMP2 = (a1+a0)*(b1+b0)
	pxor	  \TMP1, \TMP6
	pxor	  \XMM3, \XMMDst
	pxor	  \TMP2, \XMM1   # results accumulated in TMP6, XMMDst, XMM1

        # Multiply TMP1 * HashKey (using Karatsuba)
	movdqa	  \XMM4, \TMP1
	pshufd	  $78, \XMM4, \TMP2
	pxor	  \XMM4, \TMP2
	movdqu	  HashKey(%arg2), \TMP5
	PCLMULQDQ 0x11, \TMP5, \TMP1	    # TMP1 = a1*b1
	PCLMULQDQ 0x00, \TMP5, \XMM4       # XMM4 = a0*b0
	movdqu	  HashKey_k(%arg2), \TMP4
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


/* Encryption of a single block
* uses eax & r10
*/

.macro ENCRYPT_SINGLE_BLOCK XMM0 TMP1

	pxor		(%arg1), \XMM0
	mov		keysize,%eax
	shr		$2,%eax			# 128->4, 192->6, 256->8
	add		$5,%eax			# 128->9, 192->11, 256->13
	lea		16(%arg1), %r10	  # get first expanded key address

_esb_loop_\@:
	MOVADQ		(%r10),\TMP1
	AESENC		\TMP1,\XMM0
	add		$16,%r10
	sub		$1,%eax
	jnz		_esb_loop_\@

	MOVADQ		(%r10),\TMP1
	AESENCLAST	\TMP1,\XMM0
.endm
/*****************************************************************************
* void aesni_gcm_dec(void *aes_ctx,    // AES Key schedule. Starts on a 16 byte boundary.
*                   struct gcm_context_data *data
*                                      // Context data
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
* poly = x^128 + x^127 + x^126 + x^121 + 1
*
*****************************************************************************/
ENTRY(aesni_gcm_dec)
	FUNC_SAVE

	GCM_INIT %arg6, arg7, arg8, arg9
	GCM_ENC_DEC dec
	GCM_COMPLETE arg10, arg11
	FUNC_RESTORE
	ret
ENDPROC(aesni_gcm_dec)


/*****************************************************************************
* void aesni_gcm_enc(void *aes_ctx,      // AES Key schedule. Starts on a 16 byte boundary.
*                    struct gcm_context_data *data
*                                        // Context data
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
* poly = x^128 + x^127 + x^126 + x^121 + 1
***************************************************************************/
ENTRY(aesni_gcm_enc)
	FUNC_SAVE

	GCM_INIT %arg6, arg7, arg8, arg9
	GCM_ENC_DEC enc

	GCM_COMPLETE arg10, arg11
	FUNC_RESTORE
	ret
ENDPROC(aesni_gcm_enc)

/*****************************************************************************
* void aesni_gcm_init(void *aes_ctx,      // AES Key schedule. Starts on a 16 byte boundary.
*                     struct gcm_context_data *data,
*                                         // context data
*                     u8 *iv,             // Pre-counter block j0: 4 byte salt (from Security Association)
*                                         // concatenated with 8 byte Initialisation Vector (from IPSec ESP Payload)
*                                         // concatenated with 0x00000001. 16-byte aligned pointer.
*                     u8 *hash_subkey,    // H, the Hash sub key input. Data starts on a 16-byte boundary.
*                     const u8 *aad,      // Additional Authentication Data (AAD)
*                     u64 aad_len)        // Length of AAD in bytes.
*/
ENTRY(aesni_gcm_init)
	FUNC_SAVE
	GCM_INIT %arg3, %arg4,%arg5, %arg6
	FUNC_RESTORE
	ret
ENDPROC(aesni_gcm_init)

/*****************************************************************************
* void aesni_gcm_enc_update(void *aes_ctx,      // AES Key schedule. Starts on a 16 byte boundary.
*                    struct gcm_context_data *data,
*                                        // context data
*                    u8 *out,            // Ciphertext output. Encrypt in-place is allowed.
*                    const u8 *in,       // Plaintext input
*                    u64 plaintext_len,  // Length of data in bytes for encryption.
*/
ENTRY(aesni_gcm_enc_update)
	FUNC_SAVE
	GCM_ENC_DEC enc
	FUNC_RESTORE
	ret
ENDPROC(aesni_gcm_enc_update)

/*****************************************************************************
* void aesni_gcm_dec_update(void *aes_ctx,      // AES Key schedule. Starts on a 16 byte boundary.
*                    struct gcm_context_data *data,
*                                        // context data
*                    u8 *out,            // Ciphertext output. Encrypt in-place is allowed.
*                    const u8 *in,       // Plaintext input
*                    u64 plaintext_len,  // Length of data in bytes for encryption.
*/
ENTRY(aesni_gcm_dec_update)
	FUNC_SAVE
	GCM_ENC_DEC dec
	FUNC_RESTORE
	ret
ENDPROC(aesni_gcm_dec_update)

/*****************************************************************************
* void aesni_gcm_finalize(void *aes_ctx,      // AES Key schedule. Starts on a 16 byte boundary.
*                    struct gcm_context_data *data,
*                                        // context data
*                    u8 *auth_tag,       // Authenticated Tag output.
*                    u64 auth_tag_len);  // Authenticated Tag Length in bytes. Valid values are 16 (most likely),
*                                        // 12 or 8.
*/
ENTRY(aesni_gcm_finalize)
	FUNC_SAVE
	GCM_COMPLETE %arg3 %arg4
	FUNC_RESTORE
	ret
ENDPROC(aesni_gcm_finalize)

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
	FRAME_BEGIN
#ifndef __x86_64__
	pushl KEYP
	movl (FRAME_OFFSET+8)(%esp), KEYP	# ctx
	movl (FRAME_OFFSET+12)(%esp), UKEYP	# in_key
	movl (FRAME_OFFSET+16)(%esp), %edx	# key_len
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
	FRAME_END
	ret
ENDPROC(aesni_set_key)

/*
 * void aesni_enc(struct crypto_aes_ctx *ctx, u8 *dst, const u8 *src)
 */
ENTRY(aesni_enc)
	FRAME_BEGIN
#ifndef __x86_64__
	pushl KEYP
	pushl KLEN
	movl (FRAME_OFFSET+12)(%esp), KEYP	# ctx
	movl (FRAME_OFFSET+16)(%esp), OUTP	# dst
	movl (FRAME_OFFSET+20)(%esp), INP	# src
#endif
	movl 480(KEYP), KLEN		# key length
	movups (INP), STATE		# input
	call _aesni_enc1
	movups STATE, (OUTP)		# output
#ifndef __x86_64__
	popl KLEN
	popl KEYP
#endif
	FRAME_END
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
	FRAME_BEGIN
#ifndef __x86_64__
	pushl KEYP
	pushl KLEN
	movl (FRAME_OFFSET+12)(%esp), KEYP	# ctx
	movl (FRAME_OFFSET+16)(%esp), OUTP	# dst
	movl (FRAME_OFFSET+20)(%esp), INP	# src
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
	FRAME_END
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
	FRAME_BEGIN
#ifndef __x86_64__
	pushl LEN
	pushl KEYP
	pushl KLEN
	movl (FRAME_OFFSET+16)(%esp), KEYP	# ctx
	movl (FRAME_OFFSET+20)(%esp), OUTP	# dst
	movl (FRAME_OFFSET+24)(%esp), INP	# src
	movl (FRAME_OFFSET+28)(%esp), LEN	# len
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
	FRAME_END
	ret
ENDPROC(aesni_ecb_enc)

/*
 * void aesni_ecb_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len);
 */
ENTRY(aesni_ecb_dec)
	FRAME_BEGIN
#ifndef __x86_64__
	pushl LEN
	pushl KEYP
	pushl KLEN
	movl (FRAME_OFFSET+16)(%esp), KEYP	# ctx
	movl (FRAME_OFFSET+20)(%esp), OUTP	# dst
	movl (FRAME_OFFSET+24)(%esp), INP	# src
	movl (FRAME_OFFSET+28)(%esp), LEN	# len
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
	FRAME_END
	ret
ENDPROC(aesni_ecb_dec)

/*
 * void aesni_cbc_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
ENTRY(aesni_cbc_enc)
	FRAME_BEGIN
#ifndef __x86_64__
	pushl IVP
	pushl LEN
	pushl KEYP
	pushl KLEN
	movl (FRAME_OFFSET+20)(%esp), KEYP	# ctx
	movl (FRAME_OFFSET+24)(%esp), OUTP	# dst
	movl (FRAME_OFFSET+28)(%esp), INP	# src
	movl (FRAME_OFFSET+32)(%esp), LEN	# len
	movl (FRAME_OFFSET+36)(%esp), IVP	# iv
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
	FRAME_END
	ret
ENDPROC(aesni_cbc_enc)

/*
 * void aesni_cbc_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
ENTRY(aesni_cbc_dec)
	FRAME_BEGIN
#ifndef __x86_64__
	pushl IVP
	pushl LEN
	pushl KEYP
	pushl KLEN
	movl (FRAME_OFFSET+20)(%esp), KEYP	# ctx
	movl (FRAME_OFFSET+24)(%esp), OUTP	# dst
	movl (FRAME_OFFSET+28)(%esp), INP	# src
	movl (FRAME_OFFSET+32)(%esp), LEN	# len
	movl (FRAME_OFFSET+36)(%esp), IVP	# iv
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
	FRAME_END
	ret
ENDPROC(aesni_cbc_dec)

#ifdef __x86_64__
.pushsection .rodata
.align 16
.Lbswap_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
.popsection

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
	FRAME_BEGIN
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
	FRAME_END
	ret
ENDPROC(aesni_ctr_enc)

/*
 * _aesni_gf128mul_x_ble:		internal ABI
 *	Multiply in GF(2^128) for XTS IVs
 * input:
 *	IV:	current IV
 *	GF128MUL_MASK == mask with 0x87 and 0x01
 * output:
 *	IV:	next IV
 * changed:
 *	CTR:	== temporary value
 */
#define _aesni_gf128mul_x_ble() \
	pshufd $0x13, IV, CTR; \
	paddq IV, IV; \
	psrad $31, CTR; \
	pand GF128MUL_MASK, CTR; \
	pxor CTR, IV;

/*
 * void aesni_xts_crypt8(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *			 bool enc, u8 *iv)
 */
ENTRY(aesni_xts_crypt8)
	FRAME_BEGIN
	cmpb $0, %cl
	movl $0, %ecx
	movl $240, %r10d
	leaq _aesni_enc4, %r11
	leaq _aesni_dec4, %rax
	cmovel %r10d, %ecx
	cmoveq %rax, %r11

	movdqa .Lgf128mul_x_ble_mask, GF128MUL_MASK
	movups (IVP), IV

	mov 480(KEYP), KLEN
	addq %rcx, KEYP

	movdqa IV, STATE1
	movdqu 0x00(INP), INC
	pxor INC, STATE1
	movdqu IV, 0x00(OUTP)

	_aesni_gf128mul_x_ble()
	movdqa IV, STATE2
	movdqu 0x10(INP), INC
	pxor INC, STATE2
	movdqu IV, 0x10(OUTP)

	_aesni_gf128mul_x_ble()
	movdqa IV, STATE3
	movdqu 0x20(INP), INC
	pxor INC, STATE3
	movdqu IV, 0x20(OUTP)

	_aesni_gf128mul_x_ble()
	movdqa IV, STATE4
	movdqu 0x30(INP), INC
	pxor INC, STATE4
	movdqu IV, 0x30(OUTP)

	CALL_NOSPEC %r11

	movdqu 0x00(OUTP), INC
	pxor INC, STATE1
	movdqu STATE1, 0x00(OUTP)

	_aesni_gf128mul_x_ble()
	movdqa IV, STATE1
	movdqu 0x40(INP), INC
	pxor INC, STATE1
	movdqu IV, 0x40(OUTP)

	movdqu 0x10(OUTP), INC
	pxor INC, STATE2
	movdqu STATE2, 0x10(OUTP)

	_aesni_gf128mul_x_ble()
	movdqa IV, STATE2
	movdqu 0x50(INP), INC
	pxor INC, STATE2
	movdqu IV, 0x50(OUTP)

	movdqu 0x20(OUTP), INC
	pxor INC, STATE3
	movdqu STATE3, 0x20(OUTP)

	_aesni_gf128mul_x_ble()
	movdqa IV, STATE3
	movdqu 0x60(INP), INC
	pxor INC, STATE3
	movdqu IV, 0x60(OUTP)

	movdqu 0x30(OUTP), INC
	pxor INC, STATE4
	movdqu STATE4, 0x30(OUTP)

	_aesni_gf128mul_x_ble()
	movdqa IV, STATE4
	movdqu 0x70(INP), INC
	pxor INC, STATE4
	movdqu IV, 0x70(OUTP)

	_aesni_gf128mul_x_ble()
	movups IV, (IVP)

	CALL_NOSPEC %r11

	movdqu 0x40(OUTP), INC
	pxor INC, STATE1
	movdqu STATE1, 0x40(OUTP)

	movdqu 0x50(OUTP), INC
	pxor INC, STATE2
	movdqu STATE2, 0x50(OUTP)

	movdqu 0x60(OUTP), INC
	pxor INC, STATE3
	movdqu STATE3, 0x60(OUTP)

	movdqu 0x70(OUTP), INC
	pxor INC, STATE4
	movdqu STATE4, 0x70(OUTP)

	FRAME_END
	ret
ENDPROC(aesni_xts_crypt8)

#endif
