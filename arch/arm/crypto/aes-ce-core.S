/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aes-ce-core.S - AES in CBC/CTR/XTS mode using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a
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

	.macro		enc_dround_4x, key1, key2
	enc_round	q0, \key1
	enc_round	q1, \key1
	enc_round	q2, \key1
	enc_round	q3, \key1
	enc_round	q0, \key2
	enc_round	q1, \key2
	enc_round	q2, \key2
	enc_round	q3, \key2
	.endm

	.macro		dec_dround_4x, key1, key2
	dec_round	q0, \key1
	dec_round	q1, \key1
	dec_round	q2, \key1
	dec_round	q3, \key1
	dec_round	q0, \key2
	dec_round	q1, \key2
	dec_round	q2, \key2
	dec_round	q3, \key2
	.endm

	.macro		enc_fround_4x, key1, key2, key3
	enc_round	q0, \key1
	enc_round	q1, \key1
	enc_round	q2, \key1
	enc_round	q3, \key1
	aese.8		q0, \key2
	aese.8		q1, \key2
	aese.8		q2, \key2
	aese.8		q3, \key2
	veor		q0, q0, \key3
	veor		q1, q1, \key3
	veor		q2, q2, \key3
	veor		q3, q3, \key3
	.endm

	.macro		dec_fround_4x, key1, key2, key3
	dec_round	q0, \key1
	dec_round	q1, \key1
	dec_round	q2, \key1
	dec_round	q3, \key1
	aesd.8		q0, \key2
	aesd.8		q1, \key2
	aesd.8		q2, \key2
	aesd.8		q3, \key2
	veor		q0, q0, \key3
	veor		q1, q1, \key3
	veor		q2, q2, \key3
	veor		q3, q3, \key3
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
	 *   q1        : second in/output block (_4x version only)
	 *   q2        : third in/output block (_4x version only)
	 *   q3        : fourth in/output block (_4x version only)
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
aes_encrypt_4x:
	add		ip, r2, #32		@ 3rd round key
	do_block	enc_dround_4x, enc_fround_4x
ENDPROC(aes_encrypt_4x)

	.align		6
aes_decrypt_4x:
	add		ip, r2, #32		@ 3rd round key
	do_block	dec_dround_4x, dec_fround_4x
ENDPROC(aes_decrypt_4x)

	.macro		prepare_key, rk, rounds
	add		ip, \rk, \rounds, lsl #4
	vld1.32		{q8-q9}, [\rk]		@ load first 2 round keys
	vld1.32		{q14}, [ip]		@ load last round key
	.endm

	/*
	 * aes_ecb_encrypt(u8 out[], u8 const in[], u32 const rk[], int rounds,
	 *		   int blocks)
	 * aes_ecb_decrypt(u8 out[], u8 const in[], u32 const rk[], int rounds,
	 *		   int blocks)
	 */
ENTRY(ce_aes_ecb_encrypt)
	push		{r4, lr}
	ldr		r4, [sp, #8]
	prepare_key	r2, r3
.Lecbencloop4x:
	subs		r4, r4, #4
	bmi		.Lecbenc1x
	vld1.8		{q0-q1}, [r1]!
	vld1.8		{q2-q3}, [r1]!
	bl		aes_encrypt_4x
	vst1.8		{q0-q1}, [r0]!
	vst1.8		{q2-q3}, [r0]!
	b		.Lecbencloop4x
.Lecbenc1x:
	adds		r4, r4, #4
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
.Lecbdecloop4x:
	subs		r4, r4, #4
	bmi		.Lecbdec1x
	vld1.8		{q0-q1}, [r1]!
	vld1.8		{q2-q3}, [r1]!
	bl		aes_decrypt_4x
	vst1.8		{q0-q1}, [r0]!
	vst1.8		{q2-q3}, [r0]!
	b		.Lecbdecloop4x
.Lecbdec1x:
	adds		r4, r4, #4
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
	 * aes_cbc_encrypt(u8 out[], u8 const in[], u32 const rk[], int rounds,
	 *		   int blocks, u8 iv[])
	 * aes_cbc_decrypt(u8 out[], u8 const in[], u32 const rk[], int rounds,
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
	vld1.8		{q15}, [r5]		@ keep iv in q15
	prepare_key	r2, r3
.Lcbcdecloop4x:
	subs		r4, r4, #4
	bmi		.Lcbcdec1x
	vld1.8		{q0-q1}, [r1]!
	vld1.8		{q2-q3}, [r1]!
	vmov		q4, q0
	vmov		q5, q1
	vmov		q6, q2
	vmov		q7, q3
	bl		aes_decrypt_4x
	veor		q0, q0, q15
	veor		q1, q1, q4
	veor		q2, q2, q5
	veor		q3, q3, q6
	vmov		q15, q7
	vst1.8		{q0-q1}, [r0]!
	vst1.8		{q2-q3}, [r0]!
	b		.Lcbcdecloop4x
.Lcbcdec1x:
	adds		r4, r4, #4
	beq		.Lcbcdecout
	vmov		q6, q14			@ preserve last round key
.Lcbcdecloop:
	vld1.8		{q0}, [r1]!		@ get next ct block
	veor		q14, q15, q6		@ combine prev ct with last key
	vmov		q15, q0
	bl		aes_decrypt
	vst1.8		{q0}, [r0]!
	subs		r4, r4, #1
	bne		.Lcbcdecloop
.Lcbcdecout:
	vst1.8		{q15}, [r5]		@ keep iv in q15
	pop		{r4-r6, pc}
ENDPROC(ce_aes_cbc_decrypt)


	/*
	 * ce_aes_cbc_cts_encrypt(u8 out[], u8 const in[], u32 const rk[],
	 *			  int rounds, int bytes, u8 const iv[])
	 * ce_aes_cbc_cts_decrypt(u8 out[], u8 const in[], u32 const rk[],
	 *			  int rounds, int bytes, u8 const iv[])
	 */

ENTRY(ce_aes_cbc_cts_encrypt)
	push		{r4-r6, lr}
	ldrd		r4, r5, [sp, #16]

	movw		ip, :lower16:.Lcts_permute_table
	movt		ip, :upper16:.Lcts_permute_table
	sub		r4, r4, #16
	add		lr, ip, #32
	add		ip, ip, r4
	sub		lr, lr, r4
	vld1.8		{q5}, [ip]
	vld1.8		{q6}, [lr]

	add		ip, r1, r4
	vld1.8		{q0}, [r1]			@ overlapping loads
	vld1.8		{q3}, [ip]

	vld1.8		{q1}, [r5]			@ get iv
	prepare_key	r2, r3

	veor		q0, q0, q1			@ xor with iv
	bl		aes_encrypt

	vtbl.8		d4, {d0-d1}, d10
	vtbl.8		d5, {d0-d1}, d11
	vtbl.8		d2, {d6-d7}, d12
	vtbl.8		d3, {d6-d7}, d13

	veor		q0, q0, q1
	bl		aes_encrypt

	add		r4, r0, r4
	vst1.8		{q2}, [r4]			@ overlapping stores
	vst1.8		{q0}, [r0]

	pop		{r4-r6, pc}
ENDPROC(ce_aes_cbc_cts_encrypt)

ENTRY(ce_aes_cbc_cts_decrypt)
	push		{r4-r6, lr}
	ldrd		r4, r5, [sp, #16]

	movw		ip, :lower16:.Lcts_permute_table
	movt		ip, :upper16:.Lcts_permute_table
	sub		r4, r4, #16
	add		lr, ip, #32
	add		ip, ip, r4
	sub		lr, lr, r4
	vld1.8		{q5}, [ip]
	vld1.8		{q6}, [lr]

	add		ip, r1, r4
	vld1.8		{q0}, [r1]			@ overlapping loads
	vld1.8		{q1}, [ip]

	vld1.8		{q3}, [r5]			@ get iv
	prepare_key	r2, r3

	bl		aes_decrypt

	vtbl.8		d4, {d0-d1}, d10
	vtbl.8		d5, {d0-d1}, d11
	vtbx.8		d0, {d2-d3}, d12
	vtbx.8		d1, {d2-d3}, d13

	veor		q1, q1, q2
	bl		aes_decrypt
	veor		q0, q0, q3			@ xor with iv

	add		r4, r0, r4
	vst1.8		{q1}, [r4]			@ overlapping stores
	vst1.8		{q0}, [r0]

	pop		{r4-r6, pc}
ENDPROC(ce_aes_cbc_cts_decrypt)


	/*
	 * aes_ctr_encrypt(u8 out[], u8 const in[], u32 const rk[], int rounds,
	 *		   int blocks, u8 ctr[])
	 */
ENTRY(ce_aes_ctr_encrypt)
	push		{r4-r6, lr}
	ldrd		r4, r5, [sp, #16]
	vld1.8		{q7}, [r5]		@ load ctr
	prepare_key	r2, r3
	vmov		r6, s31			@ keep swabbed ctr in r6
	rev		r6, r6
	cmn		r6, r4			@ 32 bit overflow?
	bcs		.Lctrloop
.Lctrloop4x:
	subs		r4, r4, #4
	bmi		.Lctr1x
	add		r6, r6, #1
	vmov		q0, q7
	vmov		q1, q7
	rev		ip, r6
	add		r6, r6, #1
	vmov		q2, q7
	vmov		s7, ip
	rev		ip, r6
	add		r6, r6, #1
	vmov		q3, q7
	vmov		s11, ip
	rev		ip, r6
	add		r6, r6, #1
	vmov		s15, ip
	vld1.8		{q4-q5}, [r1]!
	vld1.8		{q6}, [r1]!
	vld1.8		{q15}, [r1]!
	bl		aes_encrypt_4x
	veor		q0, q0, q4
	veor		q1, q1, q5
	veor		q2, q2, q6
	veor		q3, q3, q15
	rev		ip, r6
	vst1.8		{q0-q1}, [r0]!
	vst1.8		{q2-q3}, [r0]!
	vmov		s31, ip
	b		.Lctrloop4x
.Lctr1x:
	adds		r4, r4, #4
	beq		.Lctrout
.Lctrloop:
	vmov		q0, q7
	bl		aes_encrypt

	adds		r6, r6, #1		@ increment BE ctr
	rev		ip, r6
	vmov		s31, ip
	bcs		.Lctrcarry

.Lctrcarrydone:
	subs		r4, r4, #1
	bmi		.Lctrtailblock		@ blocks < 0 means tail block
	vld1.8		{q3}, [r1]!
	veor		q3, q0, q3
	vst1.8		{q3}, [r0]!
	bne		.Lctrloop

.Lctrout:
	vst1.8		{q7}, [r5]		@ return next CTR value
	pop		{r4-r6, pc}

.Lctrtailblock:
	vst1.8		{q0}, [r0, :64]		@ return the key stream
	b		.Lctrout

.Lctrcarry:
	.irp		sreg, s30, s29, s28
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
	 * aes_xts_encrypt(u8 out[], u8 const in[], u32 const rk1[], int rounds,
	 *		   int bytes, u8 iv[], u32 const rk2[], int first)
	 * aes_xts_decrypt(u8 out[], u8 const in[], u32 const rk1[], int rounds,
	 *		   int bytes, u8 iv[], u32 const rk2[], int first)
	 */

	.macro		next_tweak, out, in, const, tmp
	vshr.s64	\tmp, \in, #63
	vand		\tmp, \tmp, \const
	vadd.u64	\out, \in, \in
	vext.8		\tmp, \tmp, \tmp, #8
	veor		\out, \out, \tmp
	.endm

ce_aes_xts_init:
	vmov.i32	d30, #0x87		@ compose tweak mask vector
	vmovl.u32	q15, d30
	vshr.u64	d30, d31, #7

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
	vmov		q4, q0

	teq		r6, #0			@ start of a block?
	bne		.Lxtsenc4x

.Lxtsencloop4x:
	next_tweak	q4, q4, q15, q10
.Lxtsenc4x:
	subs		r4, r4, #64
	bmi		.Lxtsenc1x
	vld1.8		{q0-q1}, [r1]!		@ get 4 pt blocks
	vld1.8		{q2-q3}, [r1]!
	next_tweak	q5, q4, q15, q10
	veor		q0, q0, q4
	next_tweak	q6, q5, q15, q10
	veor		q1, q1, q5
	next_tweak	q7, q6, q15, q10
	veor		q2, q2, q6
	veor		q3, q3, q7
	bl		aes_encrypt_4x
	veor		q0, q0, q4
	veor		q1, q1, q5
	veor		q2, q2, q6
	veor		q3, q3, q7
	vst1.8		{q0-q1}, [r0]!		@ write 4 ct blocks
	vst1.8		{q2-q3}, [r0]!
	vmov		q4, q7
	teq		r4, #0
	beq		.Lxtsencret
	b		.Lxtsencloop4x
.Lxtsenc1x:
	adds		r4, r4, #64
	beq		.Lxtsencout
	subs		r4, r4, #16
	bmi		.LxtsencctsNx
.Lxtsencloop:
	vld1.8		{q0}, [r1]!
.Lxtsencctsout:
	veor		q0, q0, q4
	bl		aes_encrypt
	veor		q0, q0, q4
	teq		r4, #0
	beq		.Lxtsencout
	subs		r4, r4, #16
	next_tweak	q4, q4, q15, q6
	bmi		.Lxtsenccts
	vst1.8		{q0}, [r0]!
	b		.Lxtsencloop
.Lxtsencout:
	vst1.8		{q0}, [r0]
.Lxtsencret:
	vst1.8		{q4}, [r5]
	pop		{r4-r6, pc}

.LxtsencctsNx:
	vmov		q0, q3
	sub		r0, r0, #16
.Lxtsenccts:
	movw		ip, :lower16:.Lcts_permute_table
	movt		ip, :upper16:.Lcts_permute_table

	add		r1, r1, r4		@ rewind input pointer
	add		r4, r4, #16		@ # bytes in final block
	add		lr, ip, #32
	add		ip, ip, r4
	sub		lr, lr, r4
	add		r4, r0, r4		@ output address of final block

	vld1.8		{q1}, [r1]		@ load final partial block
	vld1.8		{q2}, [ip]
	vld1.8		{q3}, [lr]

	vtbl.8		d4, {d0-d1}, d4
	vtbl.8		d5, {d0-d1}, d5
	vtbx.8		d0, {d2-d3}, d6
	vtbx.8		d1, {d2-d3}, d7

	vst1.8		{q2}, [r4]		@ overlapping stores
	mov		r4, #0
	b		.Lxtsencctsout
ENDPROC(ce_aes_xts_encrypt)


ENTRY(ce_aes_xts_decrypt)
	push		{r4-r6, lr}

	bl		ce_aes_xts_init		@ run shared prologue
	prepare_key	r2, r3
	vmov		q4, q0

	/* subtract 16 bytes if we are doing CTS */
	tst		r4, #0xf
	subne		r4, r4, #0x10

	teq		r6, #0			@ start of a block?
	bne		.Lxtsdec4x

.Lxtsdecloop4x:
	next_tweak	q4, q4, q15, q10
.Lxtsdec4x:
	subs		r4, r4, #64
	bmi		.Lxtsdec1x
	vld1.8		{q0-q1}, [r1]!		@ get 4 ct blocks
	vld1.8		{q2-q3}, [r1]!
	next_tweak	q5, q4, q15, q10
	veor		q0, q0, q4
	next_tweak	q6, q5, q15, q10
	veor		q1, q1, q5
	next_tweak	q7, q6, q15, q10
	veor		q2, q2, q6
	veor		q3, q3, q7
	bl		aes_decrypt_4x
	veor		q0, q0, q4
	veor		q1, q1, q5
	veor		q2, q2, q6
	veor		q3, q3, q7
	vst1.8		{q0-q1}, [r0]!		@ write 4 pt blocks
	vst1.8		{q2-q3}, [r0]!
	vmov		q4, q7
	teq		r4, #0
	beq		.Lxtsdecout
	b		.Lxtsdecloop4x
.Lxtsdec1x:
	adds		r4, r4, #64
	beq		.Lxtsdecout
	subs		r4, r4, #16
.Lxtsdecloop:
	vld1.8		{q0}, [r1]!
	bmi		.Lxtsdeccts
.Lxtsdecctsout:
	veor		q0, q0, q4
	bl		aes_decrypt
	veor		q0, q0, q4
	vst1.8		{q0}, [r0]!
	teq		r4, #0
	beq		.Lxtsdecout
	subs		r4, r4, #16
	next_tweak	q4, q4, q15, q6
	b		.Lxtsdecloop
.Lxtsdecout:
	vst1.8		{q4}, [r5]
	pop		{r4-r6, pc}

.Lxtsdeccts:
	movw		ip, :lower16:.Lcts_permute_table
	movt		ip, :upper16:.Lcts_permute_table

	add		r1, r1, r4		@ rewind input pointer
	add		r4, r4, #16		@ # bytes in final block
	add		lr, ip, #32
	add		ip, ip, r4
	sub		lr, lr, r4
	add		r4, r0, r4		@ output address of final block

	next_tweak	q5, q4, q15, q6

	vld1.8		{q1}, [r1]		@ load final partial block
	vld1.8		{q2}, [ip]
	vld1.8		{q3}, [lr]

	veor		q0, q0, q5
	bl		aes_decrypt
	veor		q0, q0, q5

	vtbl.8		d4, {d0-d1}, d4
	vtbl.8		d5, {d0-d1}, d5
	vtbx.8		d0, {d2-d3}, d6
	vtbx.8		d1, {d2-d3}, d7

	vst1.8		{q2}, [r4]		@ overlapping stores
	mov		r4, #0
	b		.Lxtsdecctsout
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

	.section	".rodata", "a"
	.align		6
.Lcts_permute_table:
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
	.byte		 0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
