/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 * Copyright (c) 2010, Intel Corporation.
 *
 * Ported x86_64 version to x86:
 *    Author: Mathias Krause <minipli@googlemail.com>
 */

#include <linux/linkage.h>
#include <linux/objtool.h>
#include <asm/frame.h>

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

#define GF128MUL_MASK %xmm7

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

SYM_FUNC_START_LOCAL(_key_expansion_256a)
	pshufd $0b11111111, %xmm1, %xmm1
	shufps $0b00010000, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	shufps $0b10001100, %xmm0, %xmm4
	pxor %xmm4, %xmm0
	pxor %xmm1, %xmm0
	movaps %xmm0, (TKEYP)
	add $0x10, TKEYP
	RET
SYM_FUNC_END(_key_expansion_256a)
SYM_FUNC_ALIAS_LOCAL(_key_expansion_128, _key_expansion_256a)

SYM_FUNC_START_LOCAL(_key_expansion_192a)
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
	RET
SYM_FUNC_END(_key_expansion_192a)

SYM_FUNC_START_LOCAL(_key_expansion_192b)
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
	RET
SYM_FUNC_END(_key_expansion_192b)

SYM_FUNC_START_LOCAL(_key_expansion_256b)
	pshufd $0b10101010, %xmm1, %xmm1
	shufps $0b00010000, %xmm2, %xmm4
	pxor %xmm4, %xmm2
	shufps $0b10001100, %xmm2, %xmm4
	pxor %xmm4, %xmm2
	pxor %xmm1, %xmm2
	movaps %xmm2, (TKEYP)
	add $0x10, TKEYP
	RET
SYM_FUNC_END(_key_expansion_256b)

/*
 * void aesni_set_key(struct crypto_aes_ctx *ctx, const u8 *in_key,
 *                    unsigned int key_len)
 */
SYM_FUNC_START(aesni_set_key)
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
	aeskeygenassist $0x1, %xmm2, %xmm1	# round 1
	call _key_expansion_256a
	aeskeygenassist $0x1, %xmm0, %xmm1
	call _key_expansion_256b
	aeskeygenassist $0x2, %xmm2, %xmm1	# round 2
	call _key_expansion_256a
	aeskeygenassist $0x2, %xmm0, %xmm1
	call _key_expansion_256b
	aeskeygenassist $0x4, %xmm2, %xmm1	# round 3
	call _key_expansion_256a
	aeskeygenassist $0x4, %xmm0, %xmm1
	call _key_expansion_256b
	aeskeygenassist $0x8, %xmm2, %xmm1	# round 4
	call _key_expansion_256a
	aeskeygenassist $0x8, %xmm0, %xmm1
	call _key_expansion_256b
	aeskeygenassist $0x10, %xmm2, %xmm1	# round 5
	call _key_expansion_256a
	aeskeygenassist $0x10, %xmm0, %xmm1
	call _key_expansion_256b
	aeskeygenassist $0x20, %xmm2, %xmm1	# round 6
	call _key_expansion_256a
	aeskeygenassist $0x20, %xmm0, %xmm1
	call _key_expansion_256b
	aeskeygenassist $0x40, %xmm2, %xmm1	# round 7
	call _key_expansion_256a
	jmp .Ldec_key
.Lenc_key192:
	movq 0x10(UKEYP), %xmm2		# other user key
	aeskeygenassist $0x1, %xmm2, %xmm1	# round 1
	call _key_expansion_192a
	aeskeygenassist $0x2, %xmm2, %xmm1	# round 2
	call _key_expansion_192b
	aeskeygenassist $0x4, %xmm2, %xmm1	# round 3
	call _key_expansion_192a
	aeskeygenassist $0x8, %xmm2, %xmm1	# round 4
	call _key_expansion_192b
	aeskeygenassist $0x10, %xmm2, %xmm1	# round 5
	call _key_expansion_192a
	aeskeygenassist $0x20, %xmm2, %xmm1	# round 6
	call _key_expansion_192b
	aeskeygenassist $0x40, %xmm2, %xmm1	# round 7
	call _key_expansion_192a
	aeskeygenassist $0x80, %xmm2, %xmm1	# round 8
	call _key_expansion_192b
	jmp .Ldec_key
.Lenc_key128:
	aeskeygenassist $0x1, %xmm0, %xmm1	# round 1
	call _key_expansion_128
	aeskeygenassist $0x2, %xmm0, %xmm1	# round 2
	call _key_expansion_128
	aeskeygenassist $0x4, %xmm0, %xmm1	# round 3
	call _key_expansion_128
	aeskeygenassist $0x8, %xmm0, %xmm1	# round 4
	call _key_expansion_128
	aeskeygenassist $0x10, %xmm0, %xmm1	# round 5
	call _key_expansion_128
	aeskeygenassist $0x20, %xmm0, %xmm1	# round 6
	call _key_expansion_128
	aeskeygenassist $0x40, %xmm0, %xmm1	# round 7
	call _key_expansion_128
	aeskeygenassist $0x80, %xmm0, %xmm1	# round 8
	call _key_expansion_128
	aeskeygenassist $0x1b, %xmm0, %xmm1	# round 9
	call _key_expansion_128
	aeskeygenassist $0x36, %xmm0, %xmm1	# round 10
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
	aesimc %xmm0, %xmm1
	movaps %xmm1, (UKEYP)
	add $0x10, KEYP
	sub $0x10, UKEYP
	cmp TKEYP, KEYP
	jb .Ldec_key_loop
#ifndef __x86_64__
	popl KEYP
#endif
	FRAME_END
	RET
SYM_FUNC_END(aesni_set_key)

/*
 * void aesni_enc(const void *ctx, u8 *dst, const u8 *src)
 */
SYM_FUNC_START(aesni_enc)
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
	RET
SYM_FUNC_END(aesni_enc)

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
SYM_FUNC_START_LOCAL(_aesni_enc1)
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
	aesenc KEY, STATE
	movaps -0x50(TKEYP), KEY
	aesenc KEY, STATE
.align 4
.Lenc192:
	movaps -0x40(TKEYP), KEY
	aesenc KEY, STATE
	movaps -0x30(TKEYP), KEY
	aesenc KEY, STATE
.align 4
.Lenc128:
	movaps -0x20(TKEYP), KEY
	aesenc KEY, STATE
	movaps -0x10(TKEYP), KEY
	aesenc KEY, STATE
	movaps (TKEYP), KEY
	aesenc KEY, STATE
	movaps 0x10(TKEYP), KEY
	aesenc KEY, STATE
	movaps 0x20(TKEYP), KEY
	aesenc KEY, STATE
	movaps 0x30(TKEYP), KEY
	aesenc KEY, STATE
	movaps 0x40(TKEYP), KEY
	aesenc KEY, STATE
	movaps 0x50(TKEYP), KEY
	aesenc KEY, STATE
	movaps 0x60(TKEYP), KEY
	aesenc KEY, STATE
	movaps 0x70(TKEYP), KEY
	aesenclast KEY, STATE
	RET
SYM_FUNC_END(_aesni_enc1)

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
SYM_FUNC_START_LOCAL(_aesni_enc4)
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
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps -0x50(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
#.align 4
.L4enc192:
	movaps -0x40(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps -0x30(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
#.align 4
.L4enc128:
	movaps -0x20(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps -0x10(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps (TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps 0x10(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps 0x20(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps 0x30(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps 0x40(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps 0x50(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps 0x60(TKEYP), KEY
	aesenc KEY, STATE1
	aesenc KEY, STATE2
	aesenc KEY, STATE3
	aesenc KEY, STATE4
	movaps 0x70(TKEYP), KEY
	aesenclast KEY, STATE1		# last round
	aesenclast KEY, STATE2
	aesenclast KEY, STATE3
	aesenclast KEY, STATE4
	RET
SYM_FUNC_END(_aesni_enc4)

/*
 * void aesni_dec (const void *ctx, u8 *dst, const u8 *src)
 */
SYM_FUNC_START(aesni_dec)
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
	RET
SYM_FUNC_END(aesni_dec)

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
SYM_FUNC_START_LOCAL(_aesni_dec1)
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
	aesdec KEY, STATE
	movaps -0x50(TKEYP), KEY
	aesdec KEY, STATE
.align 4
.Ldec192:
	movaps -0x40(TKEYP), KEY
	aesdec KEY, STATE
	movaps -0x30(TKEYP), KEY
	aesdec KEY, STATE
.align 4
.Ldec128:
	movaps -0x20(TKEYP), KEY
	aesdec KEY, STATE
	movaps -0x10(TKEYP), KEY
	aesdec KEY, STATE
	movaps (TKEYP), KEY
	aesdec KEY, STATE
	movaps 0x10(TKEYP), KEY
	aesdec KEY, STATE
	movaps 0x20(TKEYP), KEY
	aesdec KEY, STATE
	movaps 0x30(TKEYP), KEY
	aesdec KEY, STATE
	movaps 0x40(TKEYP), KEY
	aesdec KEY, STATE
	movaps 0x50(TKEYP), KEY
	aesdec KEY, STATE
	movaps 0x60(TKEYP), KEY
	aesdec KEY, STATE
	movaps 0x70(TKEYP), KEY
	aesdeclast KEY, STATE
	RET
SYM_FUNC_END(_aesni_dec1)

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
SYM_FUNC_START_LOCAL(_aesni_dec4)
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
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps -0x50(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
.align 4
.L4dec192:
	movaps -0x40(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps -0x30(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
.align 4
.L4dec128:
	movaps -0x20(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps -0x10(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps (TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps 0x10(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps 0x20(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps 0x30(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps 0x40(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps 0x50(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps 0x60(TKEYP), KEY
	aesdec KEY, STATE1
	aesdec KEY, STATE2
	aesdec KEY, STATE3
	aesdec KEY, STATE4
	movaps 0x70(TKEYP), KEY
	aesdeclast KEY, STATE1		# last round
	aesdeclast KEY, STATE2
	aesdeclast KEY, STATE3
	aesdeclast KEY, STATE4
	RET
SYM_FUNC_END(_aesni_dec4)

/*
 * void aesni_ecb_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len)
 */
SYM_FUNC_START(aesni_ecb_enc)
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
	RET
SYM_FUNC_END(aesni_ecb_enc)

/*
 * void aesni_ecb_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len);
 */
SYM_FUNC_START(aesni_ecb_dec)
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
	RET
SYM_FUNC_END(aesni_ecb_dec)

/*
 * void aesni_cbc_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
SYM_FUNC_START(aesni_cbc_enc)
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
	RET
SYM_FUNC_END(aesni_cbc_enc)

/*
 * void aesni_cbc_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
SYM_FUNC_START(aesni_cbc_dec)
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
	RET
SYM_FUNC_END(aesni_cbc_dec)

/*
 * void aesni_cts_cbc_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *			  size_t len, u8 *iv)
 */
SYM_FUNC_START(aesni_cts_cbc_enc)
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
	lea .Lcts_permute_table, T1
#else
	lea .Lcts_permute_table(%rip), T1
#endif
	mov 480(KEYP), KLEN
	movups (IVP), STATE
	sub $16, LEN
	mov T1, IVP
	add $32, IVP
	add LEN, T1
	sub LEN, IVP
	movups (T1), %xmm4
	movups (IVP), %xmm5

	movups (INP), IN1
	add LEN, INP
	movups (INP), IN2

	pxor IN1, STATE
	call _aesni_enc1

	pshufb %xmm5, IN2
	pxor STATE, IN2
	pshufb %xmm4, STATE
	add OUTP, LEN
	movups STATE, (LEN)

	movaps IN2, STATE
	call _aesni_enc1
	movups STATE, (OUTP)

#ifndef __x86_64__
	popl KLEN
	popl KEYP
	popl LEN
	popl IVP
#endif
	FRAME_END
	RET
SYM_FUNC_END(aesni_cts_cbc_enc)

/*
 * void aesni_cts_cbc_dec(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *			  size_t len, u8 *iv)
 */
SYM_FUNC_START(aesni_cts_cbc_dec)
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
	lea .Lcts_permute_table, T1
#else
	lea .Lcts_permute_table(%rip), T1
#endif
	mov 480(KEYP), KLEN
	add $240, KEYP
	movups (IVP), IV
	sub $16, LEN
	mov T1, IVP
	add $32, IVP
	add LEN, T1
	sub LEN, IVP
	movups (T1), %xmm4

	movups (INP), STATE
	add LEN, INP
	movups (INP), IN1

	call _aesni_dec1
	movaps STATE, IN2
	pshufb %xmm4, STATE
	pxor IN1, STATE

	add OUTP, LEN
	movups STATE, (LEN)

	movups (IVP), %xmm0
	pshufb %xmm0, IN1
	pblendvb IN2, IN1
	movaps IN1, STATE
	call _aesni_dec1

	pxor IV, STATE
	movups STATE, (OUTP)

#ifndef __x86_64__
	popl KLEN
	popl KEYP
	popl LEN
	popl IVP
#endif
	FRAME_END
	RET
SYM_FUNC_END(aesni_cts_cbc_dec)

.pushsection .rodata
.align 16
.Lcts_permute_table:
	.byte		0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte		0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	.byte		0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
	.byte		0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
#ifdef __x86_64__
.Lbswap_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
#endif
.popsection

#ifdef __x86_64__
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
SYM_FUNC_START_LOCAL(_aesni_inc_init)
	movaps .Lbswap_mask(%rip), BSWAP_MASK
	movaps IV, CTR
	pshufb BSWAP_MASK, CTR
	mov $1, TCTR_LOW
	movq TCTR_LOW, INC
	movq CTR, TCTR_LOW
	RET
SYM_FUNC_END(_aesni_inc_init)

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
SYM_FUNC_START_LOCAL(_aesni_inc)
	paddq INC, CTR
	add $1, TCTR_LOW
	jnc .Linc_low
	pslldq $8, INC
	paddq INC, CTR
	psrldq $8, INC
.Linc_low:
	movaps CTR, IV
	pshufb BSWAP_MASK, IV
	RET
SYM_FUNC_END(_aesni_inc)

/*
 * void aesni_ctr_enc(struct crypto_aes_ctx *ctx, const u8 *dst, u8 *src,
 *		      size_t len, u8 *iv)
 */
SYM_FUNC_START(aesni_ctr_enc)
	ANNOTATE_NOENDBR
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
	RET
SYM_FUNC_END(aesni_ctr_enc)

#endif

.section	.rodata.cst16.gf128mul_x_ble_mask, "aM", @progbits, 16
.align 16
.Lgf128mul_x_ble_mask:
	.octa 0x00000000000000010000000000000087
.previous

/*
 * _aesni_gf128mul_x_ble: Multiply in GF(2^128) for XTS IVs
 * input:
 *	IV:	current IV
 *	GF128MUL_MASK == mask with 0x87 and 0x01
 * output:
 *	IV:	next IV
 * changed:
 *	KEY:	== temporary value
 */
.macro _aesni_gf128mul_x_ble
	pshufd $0x13, IV, KEY
	paddq IV, IV
	psrad $31, KEY
	pand GF128MUL_MASK, KEY
	pxor KEY, IV
.endm

.macro	_aesni_xts_crypt	enc
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
	movdqa .Lgf128mul_x_ble_mask, GF128MUL_MASK
#else
	movdqa .Lgf128mul_x_ble_mask(%rip), GF128MUL_MASK
#endif
	movups (IVP), IV

	mov 480(KEYP), KLEN
.if !\enc
	add $240, KEYP

	test $15, LEN
	jz .Lxts_loop4\@
	sub $16, LEN
.endif

.Lxts_loop4\@:
	sub $64, LEN
	jl .Lxts_1x\@

	movdqa IV, STATE1
	movdqu 0x00(INP), IN
	pxor IN, STATE1
	movdqu IV, 0x00(OUTP)

	_aesni_gf128mul_x_ble
	movdqa IV, STATE2
	movdqu 0x10(INP), IN
	pxor IN, STATE2
	movdqu IV, 0x10(OUTP)

	_aesni_gf128mul_x_ble
	movdqa IV, STATE3
	movdqu 0x20(INP), IN
	pxor IN, STATE3
	movdqu IV, 0x20(OUTP)

	_aesni_gf128mul_x_ble
	movdqa IV, STATE4
	movdqu 0x30(INP), IN
	pxor IN, STATE4
	movdqu IV, 0x30(OUTP)

.if \enc
	call _aesni_enc4
.else
	call _aesni_dec4
.endif

	movdqu 0x00(OUTP), IN
	pxor IN, STATE1
	movdqu STATE1, 0x00(OUTP)

	movdqu 0x10(OUTP), IN
	pxor IN, STATE2
	movdqu STATE2, 0x10(OUTP)

	movdqu 0x20(OUTP), IN
	pxor IN, STATE3
	movdqu STATE3, 0x20(OUTP)

	movdqu 0x30(OUTP), IN
	pxor IN, STATE4
	movdqu STATE4, 0x30(OUTP)

	_aesni_gf128mul_x_ble

	add $64, INP
	add $64, OUTP
	test LEN, LEN
	jnz .Lxts_loop4\@

.Lxts_ret_iv\@:
	movups IV, (IVP)

.Lxts_ret\@:
#ifndef __x86_64__
	popl KLEN
	popl KEYP
	popl LEN
	popl IVP
#endif
	FRAME_END
	RET

.Lxts_1x\@:
	add $64, LEN
	jz .Lxts_ret_iv\@
.if \enc
	sub $16, LEN
	jl .Lxts_cts4\@
.endif

.Lxts_loop1\@:
	movdqu (INP), STATE
.if \enc
	pxor IV, STATE
	call _aesni_enc1
.else
	add $16, INP
	sub $16, LEN
	jl .Lxts_cts1\@
	pxor IV, STATE
	call _aesni_dec1
.endif
	pxor IV, STATE
	_aesni_gf128mul_x_ble

	test LEN, LEN
	jz .Lxts_out\@

.if \enc
	add $16, INP
	sub $16, LEN
	jl .Lxts_cts1\@
.endif

	movdqu STATE, (OUTP)
	add $16, OUTP
	jmp .Lxts_loop1\@

.Lxts_out\@:
	movdqu STATE, (OUTP)
	jmp .Lxts_ret_iv\@

.if \enc
.Lxts_cts4\@:
	movdqa STATE4, STATE
	sub $16, OUTP
.Lxts_cts1\@:
.else
.Lxts_cts1\@:
	movdqa IV, STATE4
	_aesni_gf128mul_x_ble

	pxor IV, STATE
	call _aesni_dec1
	pxor IV, STATE
.endif
#ifndef __x86_64__
	lea .Lcts_permute_table, T1
#else
	lea .Lcts_permute_table(%rip), T1
#endif
	add LEN, INP		/* rewind input pointer */
	add $16, LEN		/* # bytes in final block */
	movups (INP), IN1

	mov T1, IVP
	add $32, IVP
	add LEN, T1
	sub LEN, IVP
	add OUTP, LEN

	movups (T1), %xmm4
	movaps STATE, IN2
	pshufb %xmm4, STATE
	movups STATE, (LEN)

	movups (IVP), %xmm0
	pshufb %xmm0, IN1
	pblendvb IN2, IN1
	movaps IN1, STATE

.if \enc
	pxor IV, STATE
	call _aesni_enc1
	pxor IV, STATE
.else
	pxor STATE4, STATE
	call _aesni_dec1
	pxor STATE4, STATE
.endif

	movups STATE, (OUTP)
	jmp .Lxts_ret\@
.endm

/*
 * void aesni_xts_enc(const struct crypto_aes_ctx *ctx, u8 *dst,
 *		      const u8 *src, unsigned int len, le128 *iv)
 */
SYM_FUNC_START(aesni_xts_enc)
	_aesni_xts_crypt	1
SYM_FUNC_END(aesni_xts_enc)

/*
 * void aesni_xts_dec(const struct crypto_aes_ctx *ctx, u8 *dst,
 *		      const u8 *src, unsigned int len, le128 *iv)
 */
SYM_FUNC_START(aesni_xts_dec)
	_aesni_xts_crypt	0
SYM_FUNC_END(aesni_xts_dec)
