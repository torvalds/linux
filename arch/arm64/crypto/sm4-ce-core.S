/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4 Cipher Algorithm for ARMv8 with Crypto Extensions
 * as specified in
 * https://tools.ietf.org/id/draft-ribose-cfrg-sm4-10.html
 *
 * Copyright (C) 2022, Alibaba Group.
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include "sm4-ce-asm.h"

.arch	armv8-a+crypto

.irp b, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, \
		20, 24, 25, 26, 27, 28, 29, 30, 31
	.set .Lv\b\().4s, \b
.endr

.macro sm4e, vd, vn
	.inst 0xcec08400 | (.L\vn << 5) | .L\vd
.endm

.macro sm4ekey, vd, vn, vm
	.inst 0xce60c800 | (.L\vm << 16) | (.L\vn << 5) | .L\vd
.endm

/* Register macros */

#define RTMP0	v16
#define RTMP1	v17
#define RTMP2	v18
#define RTMP3	v19

#define RIV	v20


.align 3
SYM_FUNC_START(sm4_ce_expand_key)
	/* input:
	 *   x0: 128-bit key
	 *   x1: rkey_enc
	 *   x2: rkey_dec
	 *   x3: fk array
	 *   x4: ck array
	 */
	ld1		{v0.16b}, [x0];
	rev32		v0.16b, v0.16b;
	ld1		{v1.16b}, [x3];
	/* load ck */
	ld1		{v24.16b-v27.16b}, [x4], #64;
	ld1		{v28.16b-v31.16b}, [x4];

	/* input ^ fk */
	eor		v0.16b, v0.16b, v1.16b;

	sm4ekey		v0.4s, v0.4s, v24.4s;
	sm4ekey		v1.4s, v0.4s, v25.4s;
	sm4ekey		v2.4s, v1.4s, v26.4s;
	sm4ekey		v3.4s, v2.4s, v27.4s;
	sm4ekey		v4.4s, v3.4s, v28.4s;
	sm4ekey		v5.4s, v4.4s, v29.4s;
	sm4ekey		v6.4s, v5.4s, v30.4s;
	sm4ekey		v7.4s, v6.4s, v31.4s;

	st1		{v0.16b-v3.16b}, [x1], #64;
	st1		{v4.16b-v7.16b}, [x1];
	rev64		v7.4s, v7.4s;
	rev64		v6.4s, v6.4s;
	rev64		v5.4s, v5.4s;
	rev64		v4.4s, v4.4s;
	rev64		v3.4s, v3.4s;
	rev64		v2.4s, v2.4s;
	rev64		v1.4s, v1.4s;
	rev64		v0.4s, v0.4s;
	ext		v7.16b, v7.16b, v7.16b, #8;
	ext		v6.16b, v6.16b, v6.16b, #8;
	ext		v5.16b, v5.16b, v5.16b, #8;
	ext		v4.16b, v4.16b, v4.16b, #8;
	ext		v3.16b, v3.16b, v3.16b, #8;
	ext		v2.16b, v2.16b, v2.16b, #8;
	ext		v1.16b, v1.16b, v1.16b, #8;
	ext		v0.16b, v0.16b, v0.16b, #8;
	st1		{v7.16b}, [x2], #16;
	st1		{v6.16b}, [x2], #16;
	st1		{v5.16b}, [x2], #16;
	st1		{v4.16b}, [x2], #16;
	st1		{v3.16b}, [x2], #16;
	st1		{v2.16b}, [x2], #16;
	st1		{v1.16b}, [x2], #16;
	st1		{v0.16b}, [x2];

	ret;
SYM_FUNC_END(sm4_ce_expand_key)

.align 3
SYM_FUNC_START(sm4_ce_crypt_block)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 */
	SM4_PREPARE(x0)

	ld1		{v0.16b}, [x2];
	SM4_CRYPT_BLK(v0);
	st1		{v0.16b}, [x1];

	ret;
SYM_FUNC_END(sm4_ce_crypt_block)

.align 3
SYM_FUNC_START(sm4_ce_crypt)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   w3: nblocks
	 */
	SM4_PREPARE(x0)

.Lcrypt_loop_blk:
	sub		w3, w3, #8;
	tbnz		w3, #31, .Lcrypt_tail8;

	ld1		{v0.16b-v3.16b}, [x2], #64;
	ld1		{v4.16b-v7.16b}, [x2], #64;

	SM4_CRYPT_BLK8(v0, v1, v2, v3, v4, v5, v6, v7);

	st1		{v0.16b-v3.16b}, [x1], #64;
	st1		{v4.16b-v7.16b}, [x1], #64;

	cbz		w3, .Lcrypt_end;
	b		.Lcrypt_loop_blk;

.Lcrypt_tail8:
	add		w3, w3, #8;
	cmp		w3, #4;
	blt		.Lcrypt_tail4;

	sub		w3, w3, #4;

	ld1		{v0.16b-v3.16b}, [x2], #64;
	SM4_CRYPT_BLK4(v0, v1, v2, v3);
	st1		{v0.16b-v3.16b}, [x1], #64;

	cbz		w3, .Lcrypt_end;

.Lcrypt_tail4:
	sub		w3, w3, #1;

	ld1		{v0.16b}, [x2], #16;
	SM4_CRYPT_BLK(v0);
	st1		{v0.16b}, [x1], #16;

	cbnz		w3, .Lcrypt_tail4;

.Lcrypt_end:
	ret;
SYM_FUNC_END(sm4_ce_crypt)

.align 3
SYM_FUNC_START(sm4_ce_cbc_enc)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: iv (big endian, 128 bit)
	 *   w4: nblocks
	 */
	SM4_PREPARE(x0)

	ld1		{RIV.16b}, [x3]

.Lcbc_enc_loop_4x:
	cmp		w4, #4
	blt		.Lcbc_enc_loop_1x

	sub		w4, w4, #4

	ld1		{v0.16b-v3.16b}, [x2], #64

	eor		v0.16b, v0.16b, RIV.16b
	SM4_CRYPT_BLK(v0)
	eor		v1.16b, v1.16b, v0.16b
	SM4_CRYPT_BLK(v1)
	eor		v2.16b, v2.16b, v1.16b
	SM4_CRYPT_BLK(v2)
	eor		v3.16b, v3.16b, v2.16b
	SM4_CRYPT_BLK(v3)

	st1		{v0.16b-v3.16b}, [x1], #64
	mov		RIV.16b, v3.16b

	cbz		w4, .Lcbc_enc_end
	b		.Lcbc_enc_loop_4x

.Lcbc_enc_loop_1x:
	sub		w4, w4, #1

	ld1		{v0.16b}, [x2], #16

	eor		RIV.16b, RIV.16b, v0.16b
	SM4_CRYPT_BLK(RIV)

	st1		{RIV.16b}, [x1], #16

	cbnz		w4, .Lcbc_enc_loop_1x

.Lcbc_enc_end:
	/* store new IV */
	st1		{RIV.16b}, [x3]

	ret
SYM_FUNC_END(sm4_ce_cbc_enc)

.align 3
SYM_FUNC_START(sm4_ce_cbc_dec)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: iv (big endian, 128 bit)
	 *   w4: nblocks
	 */
	SM4_PREPARE(x0)

	ld1		{RIV.16b}, [x3]

.Lcbc_dec_loop_8x:
	sub		w4, w4, #8
	tbnz		w4, #31, .Lcbc_dec_4x

	ld1		{v0.16b-v3.16b}, [x2], #64
	ld1		{v4.16b-v7.16b}, [x2], #64

	rev32		v8.16b, v0.16b
	rev32		v9.16b, v1.16b
	rev32		v10.16b, v2.16b
	rev32		v11.16b, v3.16b
	rev32		v12.16b, v4.16b
	rev32		v13.16b, v5.16b
	rev32		v14.16b, v6.16b
	rev32		v15.16b, v7.16b

	SM4_CRYPT_BLK8_BE(v8, v9, v10, v11, v12, v13, v14, v15)

	eor		v8.16b, v8.16b, RIV.16b
	eor		v9.16b, v9.16b, v0.16b
	eor		v10.16b, v10.16b, v1.16b
	eor		v11.16b, v11.16b, v2.16b
	eor		v12.16b, v12.16b, v3.16b
	eor		v13.16b, v13.16b, v4.16b
	eor		v14.16b, v14.16b, v5.16b
	eor		v15.16b, v15.16b, v6.16b

	st1		{v8.16b-v11.16b}, [x1], #64
	st1		{v12.16b-v15.16b}, [x1], #64

	mov		RIV.16b, v7.16b

	cbz		w4, .Lcbc_dec_end
	b		.Lcbc_dec_loop_8x

.Lcbc_dec_4x:
	add		w4, w4, #8
	cmp		w4, #4
	blt		.Lcbc_dec_loop_1x

	sub		w4, w4, #4

	ld1		{v0.16b-v3.16b}, [x2], #64

	rev32		v8.16b, v0.16b
	rev32		v9.16b, v1.16b
	rev32		v10.16b, v2.16b
	rev32		v11.16b, v3.16b

	SM4_CRYPT_BLK4_BE(v8, v9, v10, v11)

	eor		v8.16b, v8.16b, RIV.16b
	eor		v9.16b, v9.16b, v0.16b
	eor		v10.16b, v10.16b, v1.16b
	eor		v11.16b, v11.16b, v2.16b

	st1		{v8.16b-v11.16b}, [x1], #64

	mov		RIV.16b, v3.16b

	cbz		w4, .Lcbc_dec_end

.Lcbc_dec_loop_1x:
	sub		w4, w4, #1

	ld1		{v0.16b}, [x2], #16

	rev32		v8.16b, v0.16b

	SM4_CRYPT_BLK_BE(v8)

	eor		v8.16b, v8.16b, RIV.16b
	st1		{v8.16b}, [x1], #16

	mov		RIV.16b, v0.16b

	cbnz		w4, .Lcbc_dec_loop_1x

.Lcbc_dec_end:
	/* store new IV */
	st1		{RIV.16b}, [x3]

	ret
SYM_FUNC_END(sm4_ce_cbc_dec)

.align 3
SYM_FUNC_START(sm4_ce_cfb_enc)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: iv (big endian, 128 bit)
	 *   w4: nblocks
	 */
	SM4_PREPARE(x0)

	ld1		{RIV.16b}, [x3]

.Lcfb_enc_loop_4x:
	cmp		w4, #4
	blt		.Lcfb_enc_loop_1x

	sub		w4, w4, #4

	ld1		{v0.16b-v3.16b}, [x2], #64

	rev32		v8.16b, RIV.16b
	SM4_CRYPT_BLK_BE(v8)
	eor		v0.16b, v0.16b, v8.16b

	rev32		v8.16b, v0.16b
	SM4_CRYPT_BLK_BE(v8)
	eor		v1.16b, v1.16b, v8.16b

	rev32		v8.16b, v1.16b
	SM4_CRYPT_BLK_BE(v8)
	eor		v2.16b, v2.16b, v8.16b

	rev32		v8.16b, v2.16b
	SM4_CRYPT_BLK_BE(v8)
	eor		v3.16b, v3.16b, v8.16b

	st1		{v0.16b-v3.16b}, [x1], #64
	mov		RIV.16b, v3.16b

	cbz		w4, .Lcfb_enc_end
	b		.Lcfb_enc_loop_4x

.Lcfb_enc_loop_1x:
	sub		w4, w4, #1

	ld1		{v0.16b}, [x2], #16

	SM4_CRYPT_BLK(RIV)
	eor		RIV.16b, RIV.16b, v0.16b

	st1		{RIV.16b}, [x1], #16

	cbnz		w4, .Lcfb_enc_loop_1x

.Lcfb_enc_end:
	/* store new IV */
	st1		{RIV.16b}, [x3]

	ret
SYM_FUNC_END(sm4_ce_cfb_enc)

.align 3
SYM_FUNC_START(sm4_ce_cfb_dec)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: iv (big endian, 128 bit)
	 *   w4: nblocks
	 */
	SM4_PREPARE(x0)

	ld1		{RIV.16b}, [x3]

.Lcfb_dec_loop_8x:
	sub		w4, w4, #8
	tbnz		w4, #31, .Lcfb_dec_4x

	ld1		{v0.16b-v3.16b}, [x2], #64
	ld1		{v4.16b-v7.16b}, [x2], #64

	rev32		v8.16b, RIV.16b
	rev32		v9.16b, v0.16b
	rev32		v10.16b, v1.16b
	rev32		v11.16b, v2.16b
	rev32		v12.16b, v3.16b
	rev32		v13.16b, v4.16b
	rev32		v14.16b, v5.16b
	rev32		v15.16b, v6.16b

	SM4_CRYPT_BLK8_BE(v8, v9, v10, v11, v12, v13, v14, v15)

	mov		RIV.16b, v7.16b

	eor		v0.16b, v0.16b, v8.16b
	eor		v1.16b, v1.16b, v9.16b
	eor		v2.16b, v2.16b, v10.16b
	eor		v3.16b, v3.16b, v11.16b
	eor		v4.16b, v4.16b, v12.16b
	eor		v5.16b, v5.16b, v13.16b
	eor		v6.16b, v6.16b, v14.16b
	eor		v7.16b, v7.16b, v15.16b

	st1		{v0.16b-v3.16b}, [x1], #64
	st1		{v4.16b-v7.16b}, [x1], #64

	cbz		w4, .Lcfb_dec_end
	b		.Lcfb_dec_loop_8x

.Lcfb_dec_4x:
	add		w4, w4, #8
	cmp		w4, #4
	blt		.Lcfb_dec_loop_1x

	sub		w4, w4, #4

	ld1		{v0.16b-v3.16b}, [x2], #64

	rev32		v8.16b, RIV.16b
	rev32		v9.16b, v0.16b
	rev32		v10.16b, v1.16b
	rev32		v11.16b, v2.16b

	SM4_CRYPT_BLK4_BE(v8, v9, v10, v11)

	mov		RIV.16b, v3.16b

	eor		v0.16b, v0.16b, v8.16b
	eor		v1.16b, v1.16b, v9.16b
	eor		v2.16b, v2.16b, v10.16b
	eor		v3.16b, v3.16b, v11.16b

	st1		{v0.16b-v3.16b}, [x1], #64

	cbz		w4, .Lcfb_dec_end

.Lcfb_dec_loop_1x:
	sub		w4, w4, #1

	ld1		{v0.16b}, [x2], #16

	SM4_CRYPT_BLK(RIV)

	eor		RIV.16b, RIV.16b, v0.16b
	st1		{RIV.16b}, [x1], #16

	mov		RIV.16b, v0.16b

	cbnz		w4, .Lcfb_dec_loop_1x

.Lcfb_dec_end:
	/* store new IV */
	st1		{RIV.16b}, [x3]

	ret
SYM_FUNC_END(sm4_ce_cfb_dec)

.align 3
SYM_FUNC_START(sm4_ce_ctr_enc)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: ctr (big endian, 128 bit)
	 *   w4: nblocks
	 */
	SM4_PREPARE(x0)

	ldp		x7, x8, [x3]
	rev		x7, x7
	rev		x8, x8

.Lctr_loop_8x:
	sub		w4, w4, #8
	tbnz		w4, #31, .Lctr_4x

#define inc_le128(vctr)					\
		mov		vctr.d[1], x8;		\
		mov		vctr.d[0], x7;		\
		adds		x8, x8, #1;		\
		rev64		vctr.16b, vctr.16b;	\
		adc		x7, x7, xzr;

	/* construct CTRs */
	inc_le128(v0)			/* +0 */
	inc_le128(v1)			/* +1 */
	inc_le128(v2)			/* +2 */
	inc_le128(v3)			/* +3 */
	inc_le128(v4)			/* +4 */
	inc_le128(v5)			/* +5 */
	inc_le128(v6)			/* +6 */
	inc_le128(v7)			/* +7 */

	ld1		{v8.16b-v11.16b}, [x2], #64
	ld1		{v12.16b-v15.16b}, [x2], #64

	SM4_CRYPT_BLK8(v0, v1, v2, v3, v4, v5, v6, v7)

	eor		v0.16b, v0.16b, v8.16b
	eor		v1.16b, v1.16b, v9.16b
	eor		v2.16b, v2.16b, v10.16b
	eor		v3.16b, v3.16b, v11.16b
	eor		v4.16b, v4.16b, v12.16b
	eor		v5.16b, v5.16b, v13.16b
	eor		v6.16b, v6.16b, v14.16b
	eor		v7.16b, v7.16b, v15.16b

	st1		{v0.16b-v3.16b}, [x1], #64
	st1		{v4.16b-v7.16b}, [x1], #64

	cbz		w4, .Lctr_end
	b		.Lctr_loop_8x

.Lctr_4x:
	add		w4, w4, #8
	cmp		w4, #4
	blt		.Lctr_loop_1x

	sub		w4, w4, #4

	/* construct CTRs */
	inc_le128(v0)			/* +0 */
	inc_le128(v1)			/* +1 */
	inc_le128(v2)			/* +2 */
	inc_le128(v3)			/* +3 */

	ld1		{v8.16b-v11.16b}, [x2], #64

	SM4_CRYPT_BLK4(v0, v1, v2, v3)

	eor		v0.16b, v0.16b, v8.16b
	eor		v1.16b, v1.16b, v9.16b
	eor		v2.16b, v2.16b, v10.16b
	eor		v3.16b, v3.16b, v11.16b

	st1		{v0.16b-v3.16b}, [x1], #64

	cbz		w4, .Lctr_end

.Lctr_loop_1x:
	sub		w4, w4, #1

	/* construct CTRs */
	inc_le128(v0)

	ld1		{v8.16b}, [x2], #16

	SM4_CRYPT_BLK(v0)

	eor		v0.16b, v0.16b, v8.16b
	st1		{v0.16b}, [x1], #16

	cbnz		w4, .Lctr_loop_1x

.Lctr_end:
	/* store new CTR */
	rev		x7, x7
	rev		x8, x8
	stp		x7, x8, [x3]

	ret
SYM_FUNC_END(sm4_ce_ctr_enc)
