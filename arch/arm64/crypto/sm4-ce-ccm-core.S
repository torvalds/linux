/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4-CCM AEAD Algorithm using ARMv8 Crypto Extensions
 * as specified in rfc8998
 * https://datatracker.ietf.org/doc/html/rfc8998
 *
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>
#include "sm4-ce-asm.h"

.arch	armv8-a+crypto

.irp b, 0, 1, 8, 9, 10, 11, 12, 13, 14, 15, 16, 24, 25, 26, 27, 28, 29, 30, 31
	.set .Lv\b\().4s, \b
.endr

.macro sm4e, vd, vn
	.inst 0xcec08400 | (.L\vn << 5) | .L\vd
.endm

/* Register macros */

#define RMAC	v16

/* Helper macros. */

#define inc_le128(vctr)					\
		mov		vctr.d[1], x8;		\
		mov		vctr.d[0], x7;		\
		adds		x8, x8, #1;		\
		rev64		vctr.16b, vctr.16b;	\
		adc		x7, x7, xzr;


.align 3
SYM_FUNC_START(sm4_ce_cbcmac_update)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: mac
	 *   x2: src
	 *   w3: nblocks
	 */
	SM4_PREPARE(x0)

	ld1		{RMAC.16b}, [x1]

.Lcbcmac_loop_4x:
	cmp		w3, #4
	blt		.Lcbcmac_loop_1x

	sub		w3, w3, #4

	ld1		{v0.16b-v3.16b}, [x2], #64

	SM4_CRYPT_BLK(RMAC)
	eor		RMAC.16b, RMAC.16b, v0.16b
	SM4_CRYPT_BLK(RMAC)
	eor		RMAC.16b, RMAC.16b, v1.16b
	SM4_CRYPT_BLK(RMAC)
	eor		RMAC.16b, RMAC.16b, v2.16b
	SM4_CRYPT_BLK(RMAC)
	eor		RMAC.16b, RMAC.16b, v3.16b

	cbz		w3, .Lcbcmac_end
	b		.Lcbcmac_loop_4x

.Lcbcmac_loop_1x:
	sub		w3, w3, #1

	ld1		{v0.16b}, [x2], #16

	SM4_CRYPT_BLK(RMAC)
	eor		RMAC.16b, RMAC.16b, v0.16b

	cbnz		w3, .Lcbcmac_loop_1x

.Lcbcmac_end:
	st1		{RMAC.16b}, [x1]
	ret
SYM_FUNC_END(sm4_ce_cbcmac_update)

.align 3
SYM_FUNC_START(sm4_ce_ccm_final)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: ctr0 (big endian, 128 bit)
	 *   x2: mac
	 */
	SM4_PREPARE(x0)

	ld1		{RMAC.16b}, [x2]
	ld1		{v0.16b}, [x1]

	SM4_CRYPT_BLK2(RMAC, v0)

	/* en-/decrypt the mac with ctr0 */
	eor		RMAC.16b, RMAC.16b, v0.16b
	st1		{RMAC.16b}, [x2]

	ret
SYM_FUNC_END(sm4_ce_ccm_final)

.align 3
SYM_TYPED_FUNC_START(sm4_ce_ccm_enc)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: ctr (big endian, 128 bit)
	 *   w4: nbytes
	 *   x5: mac
	 */
	SM4_PREPARE(x0)

	ldp		x7, x8, [x3]
	rev		x7, x7
	rev		x8, x8

	ld1		{RMAC.16b}, [x5]

.Lccm_enc_loop_4x:
	cmp		w4, #(4 * 16)
	blt		.Lccm_enc_loop_1x

	sub		w4, w4, #(4 * 16)

	/* construct CTRs */
	inc_le128(v8)			/* +0 */
	inc_le128(v9)			/* +1 */
	inc_le128(v10)			/* +2 */
	inc_le128(v11)			/* +3 */

	ld1		{v0.16b-v3.16b}, [x2], #64

	SM4_CRYPT_BLK2(v8, RMAC)
	eor		v8.16b, v8.16b, v0.16b
	eor		RMAC.16b, RMAC.16b, v0.16b
	SM4_CRYPT_BLK2(v9, RMAC)
	eor		v9.16b, v9.16b, v1.16b
	eor		RMAC.16b, RMAC.16b, v1.16b
	SM4_CRYPT_BLK2(v10, RMAC)
	eor		v10.16b, v10.16b, v2.16b
	eor		RMAC.16b, RMAC.16b, v2.16b
	SM4_CRYPT_BLK2(v11, RMAC)
	eor		v11.16b, v11.16b, v3.16b
	eor		RMAC.16b, RMAC.16b, v3.16b

	st1		{v8.16b-v11.16b}, [x1], #64

	cbz		w4, .Lccm_enc_end
	b		.Lccm_enc_loop_4x

.Lccm_enc_loop_1x:
	cmp		w4, #16
	blt		.Lccm_enc_tail

	sub		w4, w4, #16

	/* construct CTRs */
	inc_le128(v8)

	ld1		{v0.16b}, [x2], #16

	SM4_CRYPT_BLK2(v8, RMAC)
	eor		v8.16b, v8.16b, v0.16b
	eor		RMAC.16b, RMAC.16b, v0.16b

	st1		{v8.16b}, [x1], #16

	cbz		w4, .Lccm_enc_end
	b		.Lccm_enc_loop_1x

.Lccm_enc_tail:
	/* construct CTRs */
	inc_le128(v8)

	SM4_CRYPT_BLK2(RMAC, v8)

	/* store new MAC */
	st1		{RMAC.16b}, [x5]

.Lccm_enc_tail_loop:
	ldrb		w0, [x2], #1		/* get 1 byte from input */
	umov		w9, v8.b[0]		/* get top crypted CTR byte */
	umov		w6, RMAC.b[0]		/* get top MAC byte */

	eor		w9, w9, w0		/* w9 = CTR ^ input */
	eor		w6, w6, w0		/* w6 = MAC ^ input */

	strb		w9, [x1], #1		/* store out byte */
	strb		w6, [x5], #1		/* store MAC byte */

	subs		w4, w4, #1
	beq		.Lccm_enc_ret

	/* shift out one byte */
	ext		RMAC.16b, RMAC.16b, RMAC.16b, #1
	ext		v8.16b, v8.16b, v8.16b, #1

	b		.Lccm_enc_tail_loop

.Lccm_enc_end:
	/* store new MAC */
	st1		{RMAC.16b}, [x5]

	/* store new CTR */
	rev		x7, x7
	rev		x8, x8
	stp		x7, x8, [x3]

.Lccm_enc_ret:
	ret
SYM_FUNC_END(sm4_ce_ccm_enc)

.align 3
SYM_TYPED_FUNC_START(sm4_ce_ccm_dec)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: ctr (big endian, 128 bit)
	 *   w4: nbytes
	 *   x5: mac
	 */
	SM4_PREPARE(x0)

	ldp		x7, x8, [x3]
	rev		x7, x7
	rev		x8, x8

	ld1		{RMAC.16b}, [x5]

.Lccm_dec_loop_4x:
	cmp		w4, #(4 * 16)
	blt		.Lccm_dec_loop_1x

	sub		w4, w4, #(4 * 16)

	/* construct CTRs */
	inc_le128(v8)			/* +0 */
	inc_le128(v9)			/* +1 */
	inc_le128(v10)			/* +2 */
	inc_le128(v11)			/* +3 */

	ld1		{v0.16b-v3.16b}, [x2], #64

	SM4_CRYPT_BLK2(v8, RMAC)
	eor		v8.16b, v8.16b, v0.16b
	eor		RMAC.16b, RMAC.16b, v8.16b
	SM4_CRYPT_BLK2(v9, RMAC)
	eor		v9.16b, v9.16b, v1.16b
	eor		RMAC.16b, RMAC.16b, v9.16b
	SM4_CRYPT_BLK2(v10, RMAC)
	eor		v10.16b, v10.16b, v2.16b
	eor		RMAC.16b, RMAC.16b, v10.16b
	SM4_CRYPT_BLK2(v11, RMAC)
	eor		v11.16b, v11.16b, v3.16b
	eor		RMAC.16b, RMAC.16b, v11.16b

	st1		{v8.16b-v11.16b}, [x1], #64

	cbz		w4, .Lccm_dec_end
	b		.Lccm_dec_loop_4x

.Lccm_dec_loop_1x:
	cmp		w4, #16
	blt		.Lccm_dec_tail

	sub		w4, w4, #16

	/* construct CTRs */
	inc_le128(v8)

	ld1		{v0.16b}, [x2], #16

	SM4_CRYPT_BLK2(v8, RMAC)
	eor		v8.16b, v8.16b, v0.16b
	eor		RMAC.16b, RMAC.16b, v8.16b

	st1		{v8.16b}, [x1], #16

	cbz		w4, .Lccm_dec_end
	b		.Lccm_dec_loop_1x

.Lccm_dec_tail:
	/* construct CTRs */
	inc_le128(v8)

	SM4_CRYPT_BLK2(RMAC, v8)

	/* store new MAC */
	st1		{RMAC.16b}, [x5]

.Lccm_dec_tail_loop:
	ldrb		w0, [x2], #1		/* get 1 byte from input */
	umov		w9, v8.b[0]		/* get top crypted CTR byte */
	umov		w6, RMAC.b[0]		/* get top MAC byte */

	eor		w9, w9, w0		/* w9 = CTR ^ input */
	eor		w6, w6, w9		/* w6 = MAC ^ output */

	strb		w9, [x1], #1		/* store out byte */
	strb		w6, [x5], #1		/* store MAC byte */

	subs		w4, w4, #1
	beq		.Lccm_dec_ret

	/* shift out one byte */
	ext		RMAC.16b, RMAC.16b, RMAC.16b, #1
	ext		v8.16b, v8.16b, v8.16b, #1

	b		.Lccm_dec_tail_loop

.Lccm_dec_end:
	/* store new MAC */
	st1		{RMAC.16b}, [x5]

	/* store new CTR */
	rev		x7, x7
	rev		x8, x8
	stp		x7, x8, [x3]

.Lccm_dec_ret:
	ret
SYM_FUNC_END(sm4_ce_ccm_dec)
