/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aes-ce-core.S - AES in CBC/CTR/XTS mode using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.fpu		crypto-neon-fp-armv8
	.align		3

	.macro		enc_round, state, key
	aese.8		\state, \key
	aesmc.8		\state, \state
	.endm

	.macro		dec_round, state, key
	aesd.8		\state, \key
	aesimc.8	\state, \state
	.endm

	.macro		enc_dround, key1, key2
	enc_round	q0, \key1
	enc_round	q0, \key2
	.endm

	.macro		dec_dround, key1, key2
	dec_round	q0, \key1
	dec_round	q0, \key2
	.endm

	.macro		enc_fround, key1, key2, key3
	enc_round	q0, \key1
	aese.8		q0, \key2
	veor		q0, q0, \key3
	.endm

	.macro		dec_fround, key1, key2, key3
	dec_round	q0, \key1
	aesd.8		q0, \key2
	veor		q0, q0, \key3
	.endm

	.macro		enc_dround_3x, key1, key2
	enc_round	q0, \key1
	enc_round	q1, \key1
	enc_round	q2, \key1
	enc_round	q0, \key2
	enc_round	q1, \key2
	enc_round	q2, \key2
	.endm

	.macro		dec_dround_3x, key1, key2
	dec_round	q0, \key1
	dec_round	q1, \key1
	dec_round	q2, \key1
	dec_round	q0, \key2
	dec_round	q1, \key2
	dec_round	q2, \key2
	.endm

	.macro		enc_fround_3x, key1, key2, key3
	enc_round	q0, \key1
	enc_round	q1, \key1
	enc_round	q2, \key1
	aese.8		q0, \key2
	aese.8		q1, \key2
	aese.8		q2, \key2
	veor		q0, q0, \key3
	veor		q1, q1, \key3
	veor		q2, q2, \key3
	.endm

	.macro		dec_fround_3x, key1, key2, key3
	dec_round	q0, \key1
	dec_round	q1, \key1
	dec_round	q2, \key1
	aesd.8		q0, \key2
	aesd.8		q1, \key2
	aesd.8		q2, \key2
	veor		q0, q0, \key3
	veor		q1, q1, \key3
	veor		q2, q2, \key3
	.endm

	.macro		do_block, dround, fround
	cmp		r3, #12			@ which key size?
	vld1.32		{q10-q11}, [ip]!
	\dround		q8, q9
	vld1.32		{q12-q13}, [ip]!
	\dround		q10, q11
	vld1.32		{q10-q11}, [ip]!
	\dround		q12, q13
	vld1.32		{q12-q13}, [ip]!
	\dround		q10, q11
	blo		0f			@ AES-128: 10 rounds
	vld1.32		{q10-q11}, [ip]!
	\dround		q12, q13
	beq		1f			@ AES-192: 12 rounds
	vld1.32		{q12-q13}, [ip]
	\dround		q10, q11
0:	\fround		q12, q13, q14
	bx		lr

1:	\fround		q10, q11, q14
	bx		lr
	.endm

	/*
	 * Internal, non-AAPCS compliant functions that implement the core AES
	 * transforms. These should preserve all registers except q0 - q2 and ip
	 * Arguments:
	 *   q0        : first in/output block
	 *   q1        : second in/output block (_3x version only)
	 *   q2        : third in/output block (_3x version only)
	 *   q8        : first round key
	 *   q9        : secound round key
	 *   q14       : final round key
	 *   r2        : address of round key array
	 *   r3        : number of rounds
	 */
	.align		6
aes_encrypt:
	add		ip, r2, #32		@ 3rd round key
.Laes_encrypt_tweak:
	do_block	enc_dround, enc_fround
ENDPROC(aes_encrypt)

	.align		6
aes_decrypt:
	add		ip, r2, #32		@ 3rd round key
	do_block	dec_dround, dec_fround
ENDPROC(aes_decrypt)

	.align		6
aes_encrypt_3x:
	add		ip, r2, #32		@ 3rd round key
	do_block	enc_dround_3x, enc_fround_3x
ENDPROC(aes_encrypt_3x)

	.align		6
aes_decrypt_3x:
	add		ip, r2, #32		@ 3rd round key
	do_block	dec_dround_3x, dec_fround_3x
ENDPROC(aes_decrypt_3x)

	.macro		prepare_key, rk, rounds
	add		ip, \rk, \rounds, lsl #4
	vld1.32		{q8-q9}, [\rk]		@ load first 2 round keys
	vld1.32		{q14}, [ip]		@ load last round key
	.endm

	/*
	 * aes_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks)
	 * aes_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks)
	 */
ENTRY(ce_aes_ecb_encrypt)
	push		{r4, lr}
	ldr		r4, [sp, #8]
	prepare_key	r2, r3
.Lecbencloop3x:
	subs		r4, r4, #3
	bmi		.Lecbenc1x
	vld1.8		{q0-q1}, [r1]!
	vld1.8		{q2}, [r1]!
	bl		aes_encrypt_3x
	vst1.8		{q0-q1}, [r0]!
	vst1.8		{q2}, [r0]!
	b		.Lecbencloop3x
.Lecbenc1x:
	adds		r4, r4, #3
	beq		.Lecbencout
.Lecbencloop:
	vld1.8		{q0}, [r1]!
	bl		aes_encrypt
	vst1.8		{q0}, [r0]!
	subs		r4, r4, #1
	bne		.Lecbencloop
.Lecbencout:
	pop		{r4, pc}
ENDPROC(ce_aes_ecb_encrypt)

ENTRY(ce_aes_ecb_decrypt)
	push		{r4, lr}
	ldr		r4, [sp, #8]
	prepare_key	r2, r3
.Lecbdecloop3x:
	subs		r4, r4, #3
	bmi		.Lecbdec1x
	vld1.8		{q0-q1}, [r1]!
	vld1.8		{q2}, [r1]!
	bl		aes_decrypt_3x
	vst1.8		{q0-q1}, [r0]!
	vst1.8		{q2}, [r0]!
	b		.Lecbdecloop3x
.Lecbdec1x:
	adds		r4, r4, #3
	beq		.Lecbdecout
.Lecbdecloop:
	vld1.8		{q0}, [r1]!
	bl		aes_decrypt
	vst1.8		{q0}, [r0]!
	subs		r4, r4, #1
	bne		.Lecbdecloop
.Lecbdecout:
	pop		{r4, pc}
ENDPROC(ce_aes_ecb_decrypt)

	/*
	 * aes_cbc_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 * aes_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 */
ENTRY(ce_aes_cbc_encrypt)
	push		{r4-r6, lr}
	ldrd		r4, r5, [sp, #16]
	vld1.8		{q0}, [r5]
	prepare_key	r2, r3
.Lcbcencloop:
	vld1.8		{q1}, [r1]!		@ get next pt block
	veor		q0, q0, q1		@ ..and xor with iv
	bl		aes_encrypt
	vst1.8		{q0}, [r0]!
	subs		r4, r4, #1
	bne		.Lcbcencloop
	vst1.8		{q0}, [r5]
	pop		{r4-r6, pc}
ENDPROC(ce_aes_cbc_encrypt)

ENTRY(ce_aes_cbc_decrypt)
	push		{r4-r6, lr}
	ldrd		r4, r5, [sp, #16]
	vld1.8		{q6}, [r5]		@ keep iv in q6
	prepare_key	r2, r3
.Lcbcdecloop3x:
	subs		r4, r4, #3
	bmi		.Lcbcdec1x
	vld1.8		{q0-q1}, [r1]!
	vld1.8		{q2}, [r1]!
	vmov		q3, q0
	vmov		q4, q1
	vmov		q5, q2
	bl		aes_decrypt_3x
	veor		q0, q0, q6
	veor		q1, q1, q3
	veor		q2, q2, q4
	vmov		q6, q5
	vst1.8		{q0-q1}, [r0]!
	vst1.8		{q2}, [r0]!
	b		.Lcbcdecloop3x
.Lcbcdec1x:
	adds		r4, r4, #3
	beq		.Lcbcdecout
	vmov		q15, q14		@ preserve last round key
.Lcbcdecloop:
	vld1.8		{q0}, [r1]!		@ get next ct block
	veor		q14, q15, q6		@ combine prev ct with last key
	vmov		q6, q0
	bl		aes_decrypt
	vst1.8		{q0}, [r0]!
	subs		r4, r4, #1
	bne		.Lcbcdecloop
.Lcbcdecout:
	vst1.8		{q6}, [r5]		@ keep iv in q6
	pop		{r4-r6, pc}
ENDPROC(ce_aes_cbc_decrypt)

	/*
	 * aes_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[], int rounds,
	 *		   int blocks, u8 ctr[])
	 */
ENTRY(ce_aes_ctr_encrypt)
	push		{r4-r6, lr}
	ldrd		r4, r5, [sp, #16]
	vld1.8		{q6}, [r5]		@ load ctr
	prepare_key	r2, r3
	vmov		r6, s27			@ keep swabbed ctr in r6
	rev		r6, r6
	cmn		r6, r4			@ 32 bit overflow?
	bcs		.Lctrloop
.Lctrloop3x:
	subs		r4, r4, #3
	bmi		.Lctr1x
	add		r6, r6, #1
	vmov		q0, q6
	vmov		q1, q6
	rev		ip, r6
	add		r6, r6, #1
	vmov		q2, q6
	vmov		s7, ip
	rev		ip, r6
	add		r6, r6, #1
	vmov		s11, ip
	vld1.8		{q3-q4}, [r1]!
	vld1.8		{q5}, [r1]!
	bl		aes_encrypt_3x
	veor		q0, q0, q3
	veor		q1, q1, q4
	veor		q2, q2, q5
	rev		ip, r6
	vst1.8		{q0-q1}, [r0]!
	vst1.8		{q2}, [r0]!
	vmov		s27, ip
	b		.Lctrloop3x
.Lctr1x:
	adds		r4, r4, #3
	beq		.Lctrout
.Lctrloop:
	vmov		q0, q6
	bl		aes_encrypt

	adds		r6, r6, #1		@ increment BE ctr
	rev		ip, r6
	vmov		s27, ip
	bcs		.Lctrcarry

.Lctrcarrydone:
	subs		r4, r4, #1
	bmi		.Lctrtailblock		@ blocks < 0 means tail block
	vld1.8		{q3}, [r1]!
	veor		q3, q0, q3
	vst1.8		{q3}, [r0]!
	bne		.Lctrloop

.Lctrout:
	vst1.8		{q6}, [r5]		@ return next CTR value
	pop		{r4-r6, pc}

.Lctrtailblock:
	vst1.8		{q0}, [r0, :64]		@ return the key stream
	b		.Lctrout

.Lctrcarry:
	.irp		sreg, s26, s25, s24
	vmov		ip, \sreg		@ load next word of ctr
	rev		ip, ip			@ ... to handle the carry
	adds		ip, ip, #1
	rev		ip, ip
	vmov		\sreg, ip
	bcc		.Lctrcarrydone
	.endr
	b		.Lctrcarrydone
ENDPROC(ce_aes_ctr_encrypt)

	/*
	 * aes_xts_encrypt(u8 out[], u8 const in[], u8 const rk1[], int rounds,
	 *		   int blocks, u8 iv[], u8 const rk2[], int first)
	 * aes_xts_decrypt(u8 out[], u8 const in[], u8 const rk1[], int rounds,
	 *		   int blocks, u8 iv[], u8 const rk2[], int first)
	 */

	.macro		next_tweak, out, in, const, tmp
	vshr.s64	\tmp, \in, #63
	vand		\tmp, \tmp, \const
	vadd.u64	\out, \in, \in
	vext.8		\tmp, \tmp, \tmp, #8
	veor		\out, \out, \tmp
	.endm

	.align		3
.Lxts_mul_x:
	.quad		1, 0x87

ce_aes_xts_init:
	vldr		d14, .Lxts_mul_x
	vldr		d15, .Lxts_mul_x + 8

	ldrd		r4, r5, [sp, #16]	@ load args
	ldr		r6, [sp, #28]
	vld1.8		{q0}, [r5]		@ load iv
	teq		r6, #1			@ start of a block?
	bxne		lr

	@ Encrypt the IV in q0 with the second AES key. This should only
	@ be done at the start of a block.
	ldr		r6, [sp, #24]		@ load AES key 2
	prepare_key	r6, r3
	add		ip, r6, #32		@ 3rd round key of key 2
	b		.Laes_encrypt_tweak	@ tail call
ENDPROC(ce_aes_xts_init)

ENTRY(ce_aes_xts_encrypt)
	push		{r4-r6, lr}

	bl		ce_aes_xts_init		@ run shared prologue
	prepare_key	r2, r3
	vmov		q3, q0

	teq		r6, #0			@ start of a block?
	bne		.Lxtsenc3x

.Lxtsencloop3x:
	next_tweak	q3, q3, q7, q6
.Lxtsenc3x:
	subs		r4, r4, #3
	bmi		.Lxtsenc1x
	vld1.8		{q0-q1}, [r1]!		@ get 3 pt blocks
	vld1.8		{q2}, [r1]!
	next_tweak	q4, q3, q7, q6
	veor		q0, q0, q3
	next_tweak	q5, q4, q7, q6
	veor		q1, q1, q4
	veor		q2, q2, q5
	bl		aes_encrypt_3x
	veor		q0, q0, q3
	veor		q1, q1, q4
	veor		q2, q2, q5
	vst1.8		{q0-q1}, [r0]!		@ write 3 ct blocks
	vst1.8		{q2}, [r0]!
	vmov		q3, q5
	teq		r4, #0
	beq		.Lxtsencout
	b		.Lxtsencloop3x
.Lxtsenc1x:
	adds		r4, r4, #3
	beq		.Lxtsencout
.Lxtsencloop:
	vld1.8		{q0}, [r1]!
	veor		q0, q0, q3
	bl		aes_encrypt
	veor		q0, q0, q3
	vst1.8		{q0}, [r0]!
	subs		r4, r4, #1
	beq		.Lxtsencout
	next_tweak	q3, q3, q7, q6
	b		.Lxtsencloop
.Lxtsencout:
	vst1.8		{q3}, [r5]
	pop		{r4-r6, pc}
ENDPROC(ce_aes_xts_encrypt)


ENTRY(ce_aes_xts_decrypt)
	push		{r4-r6, lr}

	bl		ce_aes_xts_init		@ run shared prologue
	prepare_key	r2, r3
	vmov		q3, q0

	teq		r6, #0			@ start of a block?
	bne		.Lxtsdec3x

.Lxtsdecloop3x:
	next_tweak	q3, q3, q7, q6
.Lxtsdec3x:
	subs		r4, r4, #3
	bmi		.Lxtsdec1x
	vld1.8		{q0-q1}, [r1]!		@ get 3 ct blocks
	vld1.8		{q2}, [r1]!
	next_tweak	q4, q3, q7, q6
	veor		q0, q0, q3
	next_tweak	q5, q4, q7, q6
	veor		q1, q1, q4
	veor		q2, q2, q5
	bl		aes_decrypt_3x
	veor		q0, q0, q3
	veor		q1, q1, q4
	veor		q2, q2, q5
	vst1.8		{q0-q1}, [r0]!		@ write 3 pt blocks
	vst1.8		{q2}, [r0]!
	vmov		q3, q5
	teq		r4, #0
	beq		.Lxtsdecout
	b		.Lxtsdecloop3x
.Lxtsdec1x:
	adds		r4, r4, #3
	beq		.Lxtsdecout
.Lxtsdecloop:
	vld1.8		{q0}, [r1]!
	veor		q0, q0, q3
	add		ip, r2, #32		@ 3rd round key
	bl		aes_decrypt
	veor		q0, q0, q3
	vst1.8		{q0}, [r0]!
	subs		r4, r4, #1
	beq		.Lxtsdecout
	next_tweak	q3, q3, q7, q6
	b		.Lxtsdecloop
.Lxtsdecout:
	vst1.8		{q3}, [r5]
	pop		{r4-r6, pc}
ENDPROC(ce_aes_xts_decrypt)

	/*
	 * u32 ce_aes_sub(u32 input) - use the aese instruction to perform the
	 *                             AES sbox substitution on each byte in
	 *                             'input'
	 */
ENTRY(ce_aes_sub)
	vdup.32		q1, r0
	veor		q0, q0, q0
	aese.8		q0, q1
	vmov		r0, s0
	bx		lr
ENDPROC(ce_aes_sub)

	/*
	 * void ce_aes_invert(u8 *dst, u8 *src) - perform the Inverse MixColumns
	 *                                        operation on round key *src
	 */
ENTRY(ce_aes_invert)
	vld1.32		{q0}, [r1]
	aesimc.8	q0, q0
	vst1.32		{q0}, [r0]
	bx		lr
ENDPROC(ce_aes_invert)
