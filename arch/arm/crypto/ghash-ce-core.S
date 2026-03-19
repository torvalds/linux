/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated AES-GCM implementation with ARMv8 Crypto Extensions.
 *
 * Copyright (C) 2015 - 2017 Linaro Ltd.
 * Copyright (C) 2023 Google LLC. <ardb@google.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.arch		armv8-a
	.fpu		crypto-neon-fp-armv8

	SHASH		.req	q0
	T1		.req	q1
	XL		.req	q2
	XM		.req	q3
	XH		.req	q4
	IN1		.req	q4

	SHASH_L		.req	d0
	SHASH_H		.req	d1
	T1_L		.req	d2
	T1_H		.req	d3
	XL_L		.req	d4
	XL_H		.req	d5
	XM_L		.req	d6
	XM_H		.req	d7
	XH_L		.req	d8

	XH2		.req	q9

	MASK		.req	d28

	SHASH2_p64	.req	d31

	HH		.req	q10
	HH3		.req	q11
	HH4		.req	q12
	HH34		.req	q13

	HH_L		.req	d20
	HH_H		.req	d21
	HH3_L		.req	d22
	HH3_H		.req	d23
	HH4_L		.req	d24
	HH4_H		.req	d25
	HH34_L		.req	d26
	HH34_H		.req	d27
	SHASH2_H	.req	d29

	XL2		.req	q5
	XM2		.req	q6
	T2		.req	q7
	T3		.req	q8

	XL2_L		.req	d10
	XL2_H		.req	d11
	XM2_L		.req	d12
	XM2_H		.req	d13
	T3_L		.req	d16
	T3_H		.req	d17

	.text

	.macro		__pmull_reduce_p64
	vmull.p64	T1, XL_L, MASK

	veor		XH_L, XH_L, XM_H
	vext.8		T1, T1, T1, #8
	veor		XL_H, XL_H, XM_L
	veor		T1, T1, XL

	vmull.p64	XL, T1_H, MASK
	.endm

	.macro		ghash_update, enc, aggregate=1, head=1
	vld1.64		{XL}, [r1]

	.if		\head
	/* do the head block first, if supplied */
	ldr		ip, [sp]
	teq		ip, #0
	beq		0f
	vld1.64		{T1}, [ip]
	teq		r0, #0
	b		3f
	.endif

0:	.if		\aggregate
	tst		r0, #3			// skip until #blocks is a
	bne		2f			// round multiple of 4

	vld1.8		{XL2-XM2}, [r2]!
1:	vld1.8		{T2-T3}, [r2]!

	.ifnb		\enc
	\enc\()_4x	XL2, XM2, T2, T3

	add		ip, r3, #16
	vld1.64		{HH}, [ip, :128]!
	vld1.64		{HH3-HH4}, [ip, :128]

	veor		SHASH2_p64, SHASH_L, SHASH_H
	veor		SHASH2_H, HH_L, HH_H
	veor		HH34_L, HH3_L, HH3_H
	veor		HH34_H, HH4_L, HH4_H

	vmov.i8		MASK, #0xe1
	vshl.u64	MASK, MASK, #57
	.endif

	vrev64.8	XL2, XL2
	vrev64.8	XM2, XM2

	subs		r0, r0, #4

	vext.8		T1, XL2, XL2, #8
	veor		XL2_H, XL2_H, XL_L
	veor		XL, XL, T1

	vrev64.8	T1, T3
	vrev64.8	T3, T2

	vmull.p64	XH, HH4_H, XL_H			// a1 * b1
	veor		XL2_H, XL2_H, XL_H
	vmull.p64	XL, HH4_L, XL_L			// a0 * b0
	vmull.p64	XM, HH34_H, XL2_H		// (a1 + a0)(b1 + b0)

	vmull.p64	XH2, HH3_H, XM2_L		// a1 * b1
	veor		XM2_L, XM2_L, XM2_H
	vmull.p64	XL2, HH3_L, XM2_H		// a0 * b0
	vmull.p64	XM2, HH34_L, XM2_L		// (a1 + a0)(b1 + b0)

	veor		XH, XH, XH2
	veor		XL, XL, XL2
	veor		XM, XM, XM2

	vmull.p64	XH2, HH_H, T3_L			// a1 * b1
	veor		T3_L, T3_L, T3_H
	vmull.p64	XL2, HH_L, T3_H			// a0 * b0
	vmull.p64	XM2, SHASH2_H, T3_L		// (a1 + a0)(b1 + b0)

	veor		XH, XH, XH2
	veor		XL, XL, XL2
	veor		XM, XM, XM2

	vmull.p64	XH2, SHASH_H, T1_L		// a1 * b1
	veor		T1_L, T1_L, T1_H
	vmull.p64	XL2, SHASH_L, T1_H		// a0 * b0
	vmull.p64	XM2, SHASH2_p64, T1_L		// (a1 + a0)(b1 + b0)

	veor		XH, XH, XH2
	veor		XL, XL, XL2
	veor		XM, XM, XM2

	beq		4f

	vld1.8		{XL2-XM2}, [r2]!

	veor		T1, XL, XH
	veor		XM, XM, T1

	__pmull_reduce_p64

	veor		T1, T1, XH
	veor		XL, XL, T1

	b		1b
	.endif

2:	vld1.8		{T1}, [r2]!

	.ifnb		\enc
	\enc\()_1x	T1
	veor		SHASH2_p64, SHASH_L, SHASH_H
	vmov.i8		MASK, #0xe1
	vshl.u64	MASK, MASK, #57
	.endif

	subs		r0, r0, #1

3:	/* multiply XL by SHASH in GF(2^128) */
	vrev64.8	T1, T1

	vext.8		IN1, T1, T1, #8
	veor		T1_L, T1_L, XL_H
	veor		XL, XL, IN1

	vmull.p64	XH, XL_H, SHASH_H		@ a1 * b1
	veor		T1, T1, XL
	vmull.p64	XL, XL_L, SHASH_L		@ a0 * b0
	vmull.p64	XM, T1_L, SHASH2_p64		@ (a1+a0)(b1+b0)

4:	veor		T1, XL, XH
	veor		XM, XM, T1

	__pmull_reduce_p64

	veor		T1, T1, XH
	veor		XL, XL, T1

	bne		0b
	.endm

	/*
	 * void pmull_ghash_update_p64(int blocks, u64 dg[], const char *src,
	 *			       u64 const h[4][2], const char *head)
	 */
ENTRY(pmull_ghash_update_p64)
	vld1.64		{SHASH}, [r3]!
	vld1.64		{HH}, [r3]!
	vld1.64		{HH3-HH4}, [r3]

	veor		SHASH2_p64, SHASH_L, SHASH_H
	veor		SHASH2_H, HH_L, HH_H
	veor		HH34_L, HH3_L, HH3_H
	veor		HH34_H, HH4_L, HH4_H

	vmov.i8		MASK, #0xe1
	vshl.u64	MASK, MASK, #57

	ghash_update
	vst1.64		{XL}, [r1]

	bx		lr
ENDPROC(pmull_ghash_update_p64)

	e0		.req	q9
	e1		.req	q10
	e2		.req	q11
	e3		.req	q12
	e0l		.req	d18
	e0h		.req	d19
	e2l		.req	d22
	e2h		.req	d23
	e3l		.req	d24
	e3h		.req	d25
	ctr		.req	q13
	ctr0		.req	d26
	ctr1		.req	d27

	ek0		.req	q14
	ek1		.req	q15

	.macro		round, rk:req, regs:vararg
	.irp		r, \regs
	aese.8		\r, \rk
	aesmc.8		\r, \r
	.endr
	.endm

	.macro		aes_encrypt, rkp, rounds, regs:vararg
	vld1.8		{ek0-ek1}, [\rkp, :128]!
	cmp		\rounds, #12
	blt		.L\@			// AES-128

	round		ek0, \regs
	vld1.8		{ek0}, [\rkp, :128]!
	round		ek1, \regs
	vld1.8		{ek1}, [\rkp, :128]!

	beq		.L\@			// AES-192

	round		ek0, \regs
	vld1.8		{ek0}, [\rkp, :128]!
	round		ek1, \regs
	vld1.8		{ek1}, [\rkp, :128]!

.L\@:	.rept		4
	round		ek0, \regs
	vld1.8		{ek0}, [\rkp, :128]!
	round		ek1, \regs
	vld1.8		{ek1}, [\rkp, :128]!
	.endr

	round		ek0, \regs
	vld1.8		{ek0}, [\rkp, :128]

	.irp		r, \regs
	aese.8		\r, ek1
	.endr
	.irp		r, \regs
	veor		\r, \r, ek0
	.endr
	.endm

pmull_aes_encrypt:
	add		ip, r5, #4
	vld1.8		{ctr0}, [r5]		// load 12 byte IV
	vld1.8		{ctr1}, [ip]
	rev		r8, r7
	vext.8		ctr1, ctr1, ctr1, #4
	add		r7, r7, #1
	vmov.32		ctr1[1], r8
	vmov		e0, ctr

	add		ip, r3, #64
	aes_encrypt	ip, r6, e0
	bx		lr
ENDPROC(pmull_aes_encrypt)

pmull_aes_encrypt_4x:
	add		ip, r5, #4
	vld1.8		{ctr0}, [r5]
	vld1.8		{ctr1}, [ip]
	rev		r8, r7
	vext.8		ctr1, ctr1, ctr1, #4
	add		r7, r7, #1
	vmov.32		ctr1[1], r8
	rev		ip, r7
	vmov		e0, ctr
	add		r7, r7, #1
	vmov.32		ctr1[1], ip
	rev		r8, r7
	vmov		e1, ctr
	add		r7, r7, #1
	vmov.32		ctr1[1], r8
	rev		ip, r7
	vmov		e2, ctr
	add		r7, r7, #1
	vmov.32		ctr1[1], ip
	vmov		e3, ctr

	add		ip, r3, #64
	aes_encrypt	ip, r6, e0, e1, e2, e3
	bx		lr
ENDPROC(pmull_aes_encrypt_4x)

pmull_aes_encrypt_final:
	add		ip, r5, #4
	vld1.8		{ctr0}, [r5]
	vld1.8		{ctr1}, [ip]
	rev		r8, r7
	vext.8		ctr1, ctr1, ctr1, #4
	mov		r7, #1 << 24		// BE #1 for the tag
	vmov.32		ctr1[1], r8
	vmov		e0, ctr
	vmov.32		ctr1[1], r7
	vmov		e1, ctr

	add		ip, r3, #64
	aes_encrypt	ip, r6, e0, e1
	bx		lr
ENDPROC(pmull_aes_encrypt_final)

	.macro		enc_1x, in0
	bl		pmull_aes_encrypt
	veor		\in0, \in0, e0
	vst1.8		{\in0}, [r4]!
	.endm

	.macro		dec_1x, in0
	bl		pmull_aes_encrypt
	veor		e0, e0, \in0
	vst1.8		{e0}, [r4]!
	.endm

	.macro		enc_4x, in0, in1, in2, in3
	bl		pmull_aes_encrypt_4x

	veor		\in0, \in0, e0
	veor		\in1, \in1, e1
	veor		\in2, \in2, e2
	veor		\in3, \in3, e3

	vst1.8		{\in0-\in1}, [r4]!
	vst1.8		{\in2-\in3}, [r4]!
	.endm

	.macro		dec_4x, in0, in1, in2, in3
	bl		pmull_aes_encrypt_4x

	veor		e0, e0, \in0
	veor		e1, e1, \in1
	veor		e2, e2, \in2
	veor		e3, e3, \in3

	vst1.8		{e0-e1}, [r4]!
	vst1.8		{e2-e3}, [r4]!
	.endm

	/*
	 * void pmull_gcm_encrypt(int blocks, u64 dg[], const char *src,
	 *			  struct gcm_key const *k, char *dst,
	 *			  char *iv, int rounds, u32 counter)
	 */
ENTRY(pmull_gcm_encrypt)
	push		{r4-r8, lr}
	ldrd		r4, r5, [sp, #24]
	ldrd		r6, r7, [sp, #32]

	vld1.64		{SHASH}, [r3]

	ghash_update	enc, head=0
	vst1.64		{XL}, [r1]

	pop		{r4-r8, pc}
ENDPROC(pmull_gcm_encrypt)

	/*
	 * void pmull_gcm_decrypt(int blocks, u64 dg[], const char *src,
	 *			  struct gcm_key const *k, char *dst,
	 *			  char *iv, int rounds, u32 counter)
	 */
ENTRY(pmull_gcm_decrypt)
	push		{r4-r8, lr}
	ldrd		r4, r5, [sp, #24]
	ldrd		r6, r7, [sp, #32]

	vld1.64		{SHASH}, [r3]

	ghash_update	dec, head=0
	vst1.64		{XL}, [r1]

	pop		{r4-r8, pc}
ENDPROC(pmull_gcm_decrypt)

	/*
	 * void pmull_gcm_enc_final(int bytes, u64 dg[], char *tag,
	 *			    struct gcm_key const *k, char *head,
	 *			    char *iv, int rounds, u32 counter)
	 */
ENTRY(pmull_gcm_enc_final)
	push		{r4-r8, lr}
	ldrd		r4, r5, [sp, #24]
	ldrd		r6, r7, [sp, #32]

	bl		pmull_aes_encrypt_final

	cmp		r0, #0
	beq		.Lenc_final

	mov_l		ip, .Lpermute
	sub		r4, r4, #16
	add		r8, ip, r0
	add		ip, ip, #32
	add		r4, r4, r0
	sub		ip, ip, r0

	vld1.8		{e3}, [r8]		// permute vector for key stream
	vld1.8		{e2}, [ip]		// permute vector for ghash input

	vtbl.8		e3l, {e0}, e3l
	vtbl.8		e3h, {e0}, e3h

	vld1.8		{e0}, [r4]		// encrypt tail block
	veor		e0, e0, e3
	vst1.8		{e0}, [r4]

	vtbl.8		T1_L, {e0}, e2l
	vtbl.8		T1_H, {e0}, e2h

	vld1.64		{XL}, [r1]
.Lenc_final:
	vld1.64		{SHASH}, [r3, :128]
	vmov.i8		MASK, #0xe1
	veor		SHASH2_p64, SHASH_L, SHASH_H
	vshl.u64	MASK, MASK, #57
	mov		r0, #1
	bne		3f			// process head block first
	ghash_update	aggregate=0, head=0

	vrev64.8	XL, XL
	vext.8		XL, XL, XL, #8
	veor		XL, XL, e1

	sub		r2, r2, #16		// rewind src pointer
	vst1.8		{XL}, [r2]		// store tag

	pop		{r4-r8, pc}
ENDPROC(pmull_gcm_enc_final)

	/*
	 * int pmull_gcm_dec_final(int bytes, u64 dg[], char *tag,
	 *			   struct gcm_key const *k, char *head,
	 *			   char *iv, int rounds, u32 counter,
	 *			   const char *otag, int authsize)
	 */
ENTRY(pmull_gcm_dec_final)
	push		{r4-r8, lr}
	ldrd		r4, r5, [sp, #24]
	ldrd		r6, r7, [sp, #32]

	bl		pmull_aes_encrypt_final

	cmp		r0, #0
	beq		.Ldec_final

	mov_l		ip, .Lpermute
	sub		r4, r4, #16
	add		r8, ip, r0
	add		ip, ip, #32
	add		r4, r4, r0
	sub		ip, ip, r0

	vld1.8		{e3}, [r8]		// permute vector for key stream
	vld1.8		{e2}, [ip]		// permute vector for ghash input

	vtbl.8		e3l, {e0}, e3l
	vtbl.8		e3h, {e0}, e3h

	vld1.8		{e0}, [r4]

	vtbl.8		T1_L, {e0}, e2l
	vtbl.8		T1_H, {e0}, e2h

	veor		e0, e0, e3
	vst1.8		{e0}, [r4]

	vld1.64		{XL}, [r1]
.Ldec_final:
	vld1.64		{SHASH}, [r3]
	vmov.i8		MASK, #0xe1
	veor		SHASH2_p64, SHASH_L, SHASH_H
	vshl.u64	MASK, MASK, #57
	mov		r0, #1
	bne		3f			// process head block first
	ghash_update	aggregate=0, head=0

	vrev64.8	XL, XL
	vext.8		XL, XL, XL, #8
	veor		XL, XL, e1

	mov_l		ip, .Lpermute
	ldrd		r2, r3, [sp, #40]	// otag and authsize
	vld1.8		{T1}, [r2]
	add		ip, ip, r3
	vceq.i8		T1, T1, XL		// compare tags
	vmvn		T1, T1			// 0 for eq, -1 for ne

	vld1.8		{e0}, [ip]
	vtbl.8		XL_L, {T1}, e0l		// keep authsize bytes only
	vtbl.8		XL_H, {T1}, e0h

	vpmin.s8	XL_L, XL_L, XL_H	// take the minimum s8 across the vector
	vpmin.s8	XL_L, XL_L, XL_L
	vmov.32		r0, XL_L[0]		// fail if != 0x0

	pop		{r4-r8, pc}
ENDPROC(pmull_gcm_dec_final)

	.section	".rodata", "a", %progbits
	.align		5
.Lpermute:
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
	.byte		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
