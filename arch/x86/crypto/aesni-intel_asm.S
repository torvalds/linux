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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/linkage.h>

.text

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

#define KEYP	%rdi
#define OUTP	%rsi
#define INP	%rdx
#define LEN	%rcx
#define IVP	%r8
#define KLEN	%r9d
#define T1	%r10
#define TKEYP	T1
#define T2	%r11

_key_expansion_128:
_key_expansion_256a:
	pshufd $0b11111111, %xmm1, %xmm1
	shufps $0b00010000, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	shufps $0b10001100, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	pxor %xmm1, %xmm0
	movaps %xmm0, (%rcx)
	add $0x10, %rcx
	ret

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
	movaps %xmm6, (%rcx)
	shufps $0b01001110, %xmm2, %xmm1
	movaps %xmm1, 16(%rcx)
	add $0x20, %rcx
	ret

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

	movaps %xmm0, (%rcx)
	add $0x10, %rcx
	ret

_key_expansion_256b:
	pshufd $0b10101010, %xmm1, %xmm1
	shufps $0b00010000, %xmm2, %xmm4
	pxor %xmm4, %xmm2
	shufps $0b10001100, %xmm2, %xmm4
	pxor %xmm4, %xmm2
	pxor %xmm1, %xmm2
	movaps %xmm2, (%rcx)
	add $0x10, %rcx
	ret

/*
 * int aesni_set_key(struct crypto_aes_ctx *ctx, const u8 *in_key,
 *                   unsigned int key_len)
 */
ENTRY(aesni_set_key)
	movups (%rsi), %xmm0		# user key (first 16 bytes)
	movaps %xmm0, (%rdi)
	lea 0x10(%rdi), %rcx		# key addr
	movl %edx, 480(%rdi)
	pxor %xmm4, %xmm4		# xmm4 is assumed 0 in _key_expansion_x
	cmp $24, %dl
	jb .Lenc_key128
	je .Lenc_key192
	movups 0x10(%rsi), %xmm2	# other user key
	movaps %xmm2, (%rcx)
	add $0x10, %rcx
	# aeskeygenassist $0x1, %xmm2, %xmm1	# round 1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x01
	call _key_expansion_256a
	# aeskeygenassist $0x1, %xmm0, %xmm1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x01
	call _key_expansion_256b
	# aeskeygenassist $0x2, %xmm2, %xmm1	# round 2
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x02
	call _key_expansion_256a
	# aeskeygenassist $0x2, %xmm0, %xmm1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x02
	call _key_expansion_256b
	# aeskeygenassist $0x4, %xmm2, %xmm1	# round 3
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x04
	call _key_expansion_256a
	# aeskeygenassist $0x4, %xmm0, %xmm1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x04
	call _key_expansion_256b
	# aeskeygenassist $0x8, %xmm2, %xmm1	# round 4
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x08
	call _key_expansion_256a
	# aeskeygenassist $0x8, %xmm0, %xmm1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x08
	call _key_expansion_256b
	# aeskeygenassist $0x10, %xmm2, %xmm1	# round 5
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x10
	call _key_expansion_256a
	# aeskeygenassist $0x10, %xmm0, %xmm1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x10
	call _key_expansion_256b
	# aeskeygenassist $0x20, %xmm2, %xmm1	# round 6
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x20
	call _key_expansion_256a
	# aeskeygenassist $0x20, %xmm0, %xmm1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x20
	call _key_expansion_256b
	# aeskeygenassist $0x40, %xmm2, %xmm1	# round 7
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x40
	call _key_expansion_256a
	jmp .Ldec_key
.Lenc_key192:
	movq 0x10(%rsi), %xmm2		# other user key
	# aeskeygenassist $0x1, %xmm2, %xmm1	# round 1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x01
	call _key_expansion_192a
	# aeskeygenassist $0x2, %xmm2, %xmm1	# round 2
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x02
	call _key_expansion_192b
	# aeskeygenassist $0x4, %xmm2, %xmm1	# round 3
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x04
	call _key_expansion_192a
	# aeskeygenassist $0x8, %xmm2, %xmm1	# round 4
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x08
	call _key_expansion_192b
	# aeskeygenassist $0x10, %xmm2, %xmm1	# round 5
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x10
	call _key_expansion_192a
	# aeskeygenassist $0x20, %xmm2, %xmm1	# round 6
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x20
	call _key_expansion_192b
	# aeskeygenassist $0x40, %xmm2, %xmm1	# round 7
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x40
	call _key_expansion_192a
	# aeskeygenassist $0x80, %xmm2, %xmm1	# round 8
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xca, 0x80
	call _key_expansion_192b
	jmp .Ldec_key
.Lenc_key128:
	# aeskeygenassist $0x1, %xmm0, %xmm1	# round 1
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x01
	call _key_expansion_128
	# aeskeygenassist $0x2, %xmm0, %xmm1	# round 2
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x02
	call _key_expansion_128
	# aeskeygenassist $0x4, %xmm0, %xmm1	# round 3
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x04
	call _key_expansion_128
	# aeskeygenassist $0x8, %xmm0, %xmm1	# round 4
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x08
	call _key_expansion_128
	# aeskeygenassist $0x10, %xmm0, %xmm1	# round 5
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x10
	call _key_expansion_128
	# aeskeygenassist $0x20, %xmm0, %xmm1	# round 6
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x20
	call _key_expansion_128
	# aeskeygenassist $0x40, %xmm0, %xmm1	# round 7
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x40
	call _key_expansion_128
	# aeskeygenassist $0x80, %xmm0, %xmm1	# round 8
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x80
	call _key_expansion_128
	# aeskeygenassist $0x1b, %xmm0, %xmm1	# round 9
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x1b
	call _key_expansion_128
	# aeskeygenassist $0x36, %xmm0, %xmm1	# round 10
	.byte 0x66, 0x0f, 0x3a, 0xdf, 0xc8, 0x36
	call _key_expansion_128
.Ldec_key:
	sub $0x10, %rcx
	movaps (%rdi), %xmm0
	movaps (%rcx), %xmm1
	movaps %xmm0, 240(%rcx)
	movaps %xmm1, 240(%rdi)
	add $0x10, %rdi
	lea 240-16(%rcx), %rsi
.align 4
.Ldec_key_loop:
	movaps (%rdi), %xmm0
	# aesimc %xmm0, %xmm1
	.byte 0x66, 0x0f, 0x38, 0xdb, 0xc8
	movaps %xmm1, (%rsi)
	add $0x10, %rdi
	sub $0x10, %rsi
	cmp %rcx, %rdi
	jb .Ldec_key_loop
	xor %rax, %rax
	ret

/*
 * void aesni_enc(struct crypto_aes_ctx *ctx, u8 *dst, const u8 *src)
 */
ENTRY(aesni_enc)
	movl 480(KEYP), KLEN		# key length
	movups (INP), STATE		# input
	call _aesni_enc1
	movups STATE, (OUTP)		# output
	ret

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
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps -0x50(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
.align 4
.Lenc192:
	movaps -0x40(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps -0x30(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
.align 4
.Lenc128:
	movaps -0x20(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps -0x10(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps (TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps 0x10(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps 0x20(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps 0x30(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps 0x40(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps 0x50(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps 0x60(TKEYP), KEY
	# aesenc KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	movaps 0x70(TKEYP), KEY
	# aesenclast KEY, STATE	# last round
	.byte 0x66, 0x0f, 0x38, 0xdd, 0xc2
	ret

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
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps -0x50(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
#.align 4
.L4enc192:
	movaps -0x40(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps -0x30(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
#.align 4
.L4enc128:
	movaps -0x20(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps -0x10(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps (TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps 0x10(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps 0x20(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps 0x30(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps 0x40(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps 0x50(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps 0x60(TKEYP), KEY
	# aesenc KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xc2
	# aesenc KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xe2
	# aesenc KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xea
	# aesenc KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdc, 0xf2
	movaps 0x70(TKEYP), KEY
	# aesenclast KEY, STATE1	# last round
	.byte 0x66, 0x0f, 0x38, 0xdd, 0xc2
	# aesenclast KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdd, 0xe2
	# aesenclast KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdd, 0xea
	# aesenclast KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdd, 0xf2
	ret

/*
 * void aesni_dec (struct crypto_aes_ctx *ctx, u8 *dst, const u8 *src)
 */
ENTRY(aesni_dec)
	mov 480(KEYP), KLEN		# key length
	add $240, KEYP
	movups (INP), STATE		# input
	call _aesni_dec1
	movups STATE, (OUTP)		#output
	ret

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
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps -0x50(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
.align 4
.Ldec192:
	movaps -0x40(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps -0x30(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
.align 4
.Ldec128:
	movaps -0x20(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps -0x10(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps (TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps 0x10(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps 0x20(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps 0x30(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps 0x40(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps 0x50(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps 0x60(TKEYP), KEY
	# aesdec KEY, STATE
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	movaps 0x70(TKEYP), KEY
	# aesdeclast KEY, STATE		# last round
	.byte 0x66, 0x0f, 0x38, 0xdf, 0xc2
	ret

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
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps -0x50(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
.align 4
.L4dec192:
	movaps -0x40(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps -0x30(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
.align 4
.L4dec128:
	movaps -0x20(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps -0x10(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps (TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps 0x10(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps 0x20(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps 0x30(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps 0x40(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps 0x50(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps 0x60(TKEYP), KEY
	# aesdec KEY, STATE1
	.byte 0x66, 0x0f, 0x38, 0xde, 0xc2
	# aesdec KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xde, 0xe2
	# aesdec KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xde, 0xea
	# aesdec KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xde, 0xf2
	movaps 0x70(TKEYP), KEY
	# aesdeclast KEY, STATE1	# last round
	.byte 0x66, 0x0f, 0x38, 0xdf, 0xc2
	# aesdeclast KEY, STATE2
	.byte 0x66, 0x0f, 0x38, 0xdf, 0xe2
	# aesdeclast KEY, STATE3
	.byte 0x66, 0x0f, 0x38, 0xdf, 0xea
	# aesdeclast KEY, STATE4
	.byte 0x66, 0x0f, 0x38, 0xdf, 0xf2
	ret

/*
 * void aesni_ecb_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len)
 */
ENTRY(aesni_ecb_enc)
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
	ret

/*
 * void aesni_ecb_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len);
 */
ENTRY(aesni_ecb_dec)
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
	ret

/*
 * void aesni_cbc_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
ENTRY(aesni_cbc_enc)
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
	ret

/*
 * void aesni_cbc_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
ENTRY(aesni_cbc_dec)
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
	movups 0x20(INP), IN3
	movaps IN3, STATE3
	movups 0x30(INP), IN4
	movaps IN4, STATE4
	call _aesni_dec4
	pxor IV, STATE1
	pxor IN1, STATE2
	pxor IN2, STATE3
	pxor IN3, STATE4
	movaps IN4, IV
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
	ret
