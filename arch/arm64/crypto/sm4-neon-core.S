/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4 Cipher Algorithm for ARMv8 NEON
 * as specified in
 * https://tools.ietf.org/id/draft-ribose-cfrg-sm4-10.html
 *
 * Copyright (C) 2022, Alibaba Group.
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/* Register macros */

#define RTMP0	v8
#define RTMP1	v9
#define RTMP2	v10
#define RTMP3	v11

#define RTMP4	v12
#define RTMP5	v13
#define RTMP6	v14
#define RTMP7	v15

#define RX0	v12
#define RX1	v13
#define RKEY	v14
#define RIV	v15

/* Helper macros. */

#define SM4_PREPARE()                                           \
	adr_l		x5, crypto_sm4_sbox;                    \
	ld1		{v16.16b-v19.16b}, [x5], #64;           \
	ld1		{v20.16b-v23.16b}, [x5], #64;           \
	ld1		{v24.16b-v27.16b}, [x5], #64;           \
	ld1		{v28.16b-v31.16b}, [x5];

#define transpose_4x4(s0, s1, s2, s3)                           \
	zip1		RTMP0.4s, s0.4s, s1.4s;                 \
	zip1		RTMP1.4s, s2.4s, s3.4s;                 \
	zip2		RTMP2.4s, s0.4s, s1.4s;                 \
	zip2		RTMP3.4s, s2.4s, s3.4s;                 \
	zip1		s0.2d, RTMP0.2d, RTMP1.2d;              \
	zip2		s1.2d, RTMP0.2d, RTMP1.2d;              \
	zip1		s2.2d, RTMP2.2d, RTMP3.2d;              \
	zip2		s3.2d, RTMP2.2d, RTMP3.2d;

#define transpose_4x4_2x(s0, s1, s2, s3, s4, s5, s6, s7)        \
	zip1		RTMP0.4s, s0.4s, s1.4s;                 \
	zip1		RTMP1.4s, s2.4s, s3.4s;                 \
	zip2		RTMP2.4s, s0.4s, s1.4s;                 \
	zip2		RTMP3.4s, s2.4s, s3.4s;                 \
	zip1		RTMP4.4s, s4.4s, s5.4s;                 \
	zip1		RTMP5.4s, s6.4s, s7.4s;                 \
	zip2		RTMP6.4s, s4.4s, s5.4s;                 \
	zip2		RTMP7.4s, s6.4s, s7.4s;                 \
	zip1		s0.2d, RTMP0.2d, RTMP1.2d;              \
	zip2		s1.2d, RTMP0.2d, RTMP1.2d;              \
	zip1		s2.2d, RTMP2.2d, RTMP3.2d;              \
	zip2		s3.2d, RTMP2.2d, RTMP3.2d;              \
	zip1		s4.2d, RTMP4.2d, RTMP5.2d;              \
	zip2		s5.2d, RTMP4.2d, RTMP5.2d;              \
	zip1		s6.2d, RTMP6.2d, RTMP7.2d;              \
	zip2		s7.2d, RTMP6.2d, RTMP7.2d;

#define rotate_clockwise_4x4(s0, s1, s2, s3)                    \
	zip1		RTMP0.4s, s1.4s, s0.4s;                 \
	zip2		RTMP1.4s, s1.4s, s0.4s;                 \
	zip1		RTMP2.4s, s3.4s, s2.4s;                 \
	zip2		RTMP3.4s, s3.4s, s2.4s;                 \
	zip1		s0.2d, RTMP2.2d, RTMP0.2d;              \
	zip2		s1.2d, RTMP2.2d, RTMP0.2d;              \
	zip1		s2.2d, RTMP3.2d, RTMP1.2d;              \
	zip2		s3.2d, RTMP3.2d, RTMP1.2d;

#define rotate_clockwise_4x4_2x(s0, s1, s2, s3, s4, s5, s6, s7) \
	zip1		RTMP0.4s, s1.4s, s0.4s;                 \
	zip1		RTMP2.4s, s3.4s, s2.4s;                 \
	zip2		RTMP1.4s, s1.4s, s0.4s;                 \
	zip2		RTMP3.4s, s3.4s, s2.4s;                 \
	zip1		RTMP4.4s, s5.4s, s4.4s;                 \
	zip1		RTMP6.4s, s7.4s, s6.4s;                 \
	zip2		RTMP5.4s, s5.4s, s4.4s;                 \
	zip2		RTMP7.4s, s7.4s, s6.4s;                 \
	zip1		s0.2d, RTMP2.2d, RTMP0.2d;              \
	zip2		s1.2d, RTMP2.2d, RTMP0.2d;              \
	zip1		s2.2d, RTMP3.2d, RTMP1.2d;              \
	zip2		s3.2d, RTMP3.2d, RTMP1.2d;              \
	zip1		s4.2d, RTMP6.2d, RTMP4.2d;              \
	zip2		s5.2d, RTMP6.2d, RTMP4.2d;              \
	zip1		s6.2d, RTMP7.2d, RTMP5.2d;              \
	zip2		s7.2d, RTMP7.2d, RTMP5.2d;

#define ROUND4(round, s0, s1, s2, s3)                           \
	dup		RX0.4s, RKEY.s[round];                  \
	/* rk ^ s1 ^ s2 ^ s3 */                                 \
	eor		RTMP1.16b, s2.16b, s3.16b;              \
	eor		RX0.16b, RX0.16b, s1.16b;               \
	eor		RX0.16b, RX0.16b, RTMP1.16b;            \
                                                                \
	/* sbox, non-linear part */                             \
	movi		RTMP3.16b, #64;  /* sizeof(sbox) / 4 */ \
	tbl		RTMP0.16b, {v16.16b-v19.16b}, RX0.16b;  \
	sub		RX0.16b, RX0.16b, RTMP3.16b;            \
	tbx		RTMP0.16b, {v20.16b-v23.16b}, RX0.16b;  \
	sub		RX0.16b, RX0.16b, RTMP3.16b;            \
	tbx		RTMP0.16b, {v24.16b-v27.16b}, RX0.16b;  \
	sub		RX0.16b, RX0.16b, RTMP3.16b;            \
	tbx		RTMP0.16b, {v28.16b-v31.16b}, RX0.16b;  \
                                                                \
	/* linear part */                                       \
	shl		RTMP1.4s, RTMP0.4s, #8;                 \
	shl		RTMP2.4s, RTMP0.4s, #16;                \
	shl		RTMP3.4s, RTMP0.4s, #24;                \
	sri		RTMP1.4s, RTMP0.4s, #(32-8);            \
	sri		RTMP2.4s, RTMP0.4s, #(32-16);           \
	sri		RTMP3.4s, RTMP0.4s, #(32-24);           \
	/* RTMP1 = x ^ rol32(x, 8) ^ rol32(x, 16) */            \
	eor		RTMP1.16b, RTMP1.16b, RTMP0.16b;        \
	eor		RTMP1.16b, RTMP1.16b, RTMP2.16b;        \
	/* RTMP3 = x ^ rol32(x, 24) ^ rol32(RTMP1, 2) */        \
	eor		RTMP3.16b, RTMP3.16b, RTMP0.16b;        \
	shl		RTMP2.4s, RTMP1.4s, 2;                  \
	sri		RTMP2.4s, RTMP1.4s, #(32-2);            \
	eor		RTMP3.16b, RTMP3.16b, RTMP2.16b;        \
	/* s0 ^= RTMP3 */                                       \
	eor		s0.16b, s0.16b, RTMP3.16b;

#define SM4_CRYPT_BLK4_BE(b0, b1, b2, b3)                       \
	mov		x6, 8;                                  \
4:                                                              \
	ld1		{RKEY.4s}, [x0], #16;                   \
	subs		x6, x6, #1;                             \
                                                                \
	ROUND4(0, b0, b1, b2, b3);                              \
	ROUND4(1, b1, b2, b3, b0);                              \
	ROUND4(2, b2, b3, b0, b1);                              \
	ROUND4(3, b3, b0, b1, b2);                              \
                                                                \
	bne		4b;                                     \
                                                                \
	rev32		b0.16b, b0.16b;                         \
	rev32		b1.16b, b1.16b;                         \
	rev32		b2.16b, b2.16b;                         \
	rev32		b3.16b, b3.16b;                         \
                                                                \
	rotate_clockwise_4x4(b0, b1, b2, b3);                   \
                                                                \
	/* repoint to rkey */                                   \
	sub		x0, x0, #128;

#define SM4_CRYPT_BLK4(b0, b1, b2, b3)                          \
	rev32		b0.16b, b0.16b;                         \
	rev32		b1.16b, b1.16b;                         \
	rev32		b2.16b, b2.16b;                         \
	rev32		b3.16b, b3.16b;                         \
	SM4_CRYPT_BLK4_BE(b0, b1, b2, b3);

#define ROUND8(round, s0, s1, s2, s3, t0, t1, t2, t3)           \
	/* rk ^ s1 ^ s2 ^ s3 */                                 \
	dup		RX0.4s, RKEY.s[round];                  \
	eor		RTMP0.16b, s2.16b, s3.16b;              \
	mov		RX1.16b, RX0.16b;                       \
	eor		RTMP1.16b, t2.16b, t3.16b;              \
	eor		RX0.16b, RX0.16b, s1.16b;               \
	eor		RX1.16b, RX1.16b, t1.16b;               \
	eor		RX0.16b, RX0.16b, RTMP0.16b;            \
	eor		RX1.16b, RX1.16b, RTMP1.16b;            \
                                                                \
	/* sbox, non-linear part */                             \
	movi		RTMP3.16b, #64;  /* sizeof(sbox) / 4 */ \
	tbl		RTMP0.16b, {v16.16b-v19.16b}, RX0.16b;  \
	tbl		RTMP1.16b, {v16.16b-v19.16b}, RX1.16b;  \
	sub		RX0.16b, RX0.16b, RTMP3.16b;            \
	sub		RX1.16b, RX1.16b, RTMP3.16b;            \
	tbx		RTMP0.16b, {v20.16b-v23.16b}, RX0.16b;  \
	tbx		RTMP1.16b, {v20.16b-v23.16b}, RX1.16b;  \
	sub		RX0.16b, RX0.16b, RTMP3.16b;            \
	sub		RX1.16b, RX1.16b, RTMP3.16b;            \
	tbx		RTMP0.16b, {v24.16b-v27.16b}, RX0.16b;  \
	tbx		RTMP1.16b, {v24.16b-v27.16b}, RX1.16b;  \
	sub		RX0.16b, RX0.16b, RTMP3.16b;            \
	sub		RX1.16b, RX1.16b, RTMP3.16b;            \
	tbx		RTMP0.16b, {v28.16b-v31.16b}, RX0.16b;  \
	tbx		RTMP1.16b, {v28.16b-v31.16b}, RX1.16b;  \
                                                                \
	/* linear part */                                       \
	shl		RX0.4s, RTMP0.4s, #8;                   \
	shl		RX1.4s, RTMP1.4s, #8;                   \
	shl		RTMP2.4s, RTMP0.4s, #16;                \
	shl		RTMP3.4s, RTMP1.4s, #16;                \
	sri		RX0.4s, RTMP0.4s, #(32 - 8);            \
	sri		RX1.4s, RTMP1.4s, #(32 - 8);            \
	sri		RTMP2.4s, RTMP0.4s, #(32 - 16);         \
	sri		RTMP3.4s, RTMP1.4s, #(32 - 16);         \
	/* RX = x ^ rol32(x, 8) ^ rol32(x, 16) */               \
	eor		RX0.16b, RX0.16b, RTMP0.16b;            \
	eor		RX1.16b, RX1.16b, RTMP1.16b;            \
	eor		RX0.16b, RX0.16b, RTMP2.16b;            \
	eor		RX1.16b, RX1.16b, RTMP3.16b;            \
	/* RTMP0/1 ^= x ^ rol32(x, 24) ^ rol32(RX, 2) */        \
	shl		RTMP2.4s, RTMP0.4s, #24;                \
	shl		RTMP3.4s, RTMP1.4s, #24;                \
	sri		RTMP2.4s, RTMP0.4s, #(32 - 24);         \
	sri		RTMP3.4s, RTMP1.4s, #(32 - 24);         \
	eor		RTMP0.16b, RTMP0.16b, RTMP2.16b;        \
	eor		RTMP1.16b, RTMP1.16b, RTMP3.16b;        \
	shl		RTMP2.4s, RX0.4s, #2;                   \
	shl		RTMP3.4s, RX1.4s, #2;                   \
	sri		RTMP2.4s, RX0.4s, #(32 - 2);            \
	sri		RTMP3.4s, RX1.4s, #(32 - 2);            \
	eor		RTMP0.16b, RTMP0.16b, RTMP2.16b;        \
	eor		RTMP1.16b, RTMP1.16b, RTMP3.16b;        \
	/* s0/t0 ^= RTMP0/1 */                                  \
	eor		s0.16b, s0.16b, RTMP0.16b;              \
	eor		t0.16b, t0.16b, RTMP1.16b;

#define SM4_CRYPT_BLK8_norotate(b0, b1, b2, b3, b4, b5, b6, b7) \
	rev32		b0.16b, b0.16b;                         \
	rev32		b1.16b, b1.16b;                         \
	rev32		b2.16b, b2.16b;                         \
	rev32		b3.16b, b3.16b;                         \
	rev32		b4.16b, b4.16b;                         \
	rev32		b5.16b, b5.16b;                         \
	rev32		b6.16b, b6.16b;                         \
	rev32		b7.16b, b7.16b;                         \
                                                                \
	mov		x6, 8;                                  \
8:                                                              \
	ld1		{RKEY.4s}, [x0], #16;                   \
	subs		x6, x6, #1;                             \
                                                                \
	ROUND8(0, b0, b1, b2, b3, b4, b5, b6, b7);              \
	ROUND8(1, b1, b2, b3, b0, b5, b6, b7, b4);              \
	ROUND8(2, b2, b3, b0, b1, b6, b7, b4, b5);              \
	ROUND8(3, b3, b0, b1, b2, b7, b4, b5, b6);              \
                                                                \
	bne		8b;                                     \
                                                                \
	rev32		b0.16b, b0.16b;                         \
	rev32		b1.16b, b1.16b;                         \
	rev32		b2.16b, b2.16b;                         \
	rev32		b3.16b, b3.16b;                         \
	rev32		b4.16b, b4.16b;                         \
	rev32		b5.16b, b5.16b;                         \
	rev32		b6.16b, b6.16b;                         \
	rev32		b7.16b, b7.16b;                         \
                                                                \
	/* repoint to rkey */                                   \
	sub		x0, x0, #128;

#define SM4_CRYPT_BLK8(b0, b1, b2, b3, b4, b5, b6, b7)			\
	SM4_CRYPT_BLK8_norotate(b0, b1, b2, b3, b4, b5, b6, b7);	\
	rotate_clockwise_4x4_2x(b0, b1, b2, b3, b4, b5, b6, b7);	\


.align 3
SYM_FUNC_START(sm4_neon_crypt)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   w3: nblocks
	 */
	SM4_PREPARE()

.Lcrypt_loop_8x:
	sub		w3, w3, #8
	tbnz		w3, #31, .Lcrypt_4x

	ld4		{v0.4s-v3.4s}, [x2], #64
	ld4		{v4.4s-v7.4s}, [x2], #64

	SM4_CRYPT_BLK8(v0, v1, v2, v3, v4, v5, v6, v7)

	st1		{v0.16b-v3.16b}, [x1], #64
	st1		{v4.16b-v7.16b}, [x1], #64

	cbz		w3, .Lcrypt_end
	b		.Lcrypt_loop_8x

.Lcrypt_4x:
	add		w3, w3, #8
	cmp		w3, #4
	blt		.Lcrypt_tail

	sub		w3, w3, #4

	ld4		{v0.4s-v3.4s}, [x2], #64

	SM4_CRYPT_BLK4(v0, v1, v2, v3)

	st1		{v0.16b-v3.16b}, [x1], #64

	cbz		w3, .Lcrypt_end

.Lcrypt_tail:
	cmp		w3, #2
	ld1		{v0.16b}, [x2], #16
	blt		.Lcrypt_tail_load_done
	ld1		{v1.16b}, [x2], #16
	beq		.Lcrypt_tail_load_done
	ld1		{v2.16b}, [x2], #16

.Lcrypt_tail_load_done:
	transpose_4x4(v0, v1, v2, v3)

	SM4_CRYPT_BLK4(v0, v1, v2, v3)

	cmp		w3, #2
	st1		{v0.16b}, [x1], #16
	blt		.Lcrypt_end
	st1		{v1.16b}, [x1], #16
	beq		.Lcrypt_end
	st1		{v2.16b}, [x1], #16

.Lcrypt_end:
	ret
SYM_FUNC_END(sm4_neon_crypt)

.align 3
SYM_FUNC_START(sm4_neon_cbc_dec)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: iv (big endian, 128 bit)
	 *   w4: nblocks
	 */
	SM4_PREPARE()

	ld1		{RIV.16b}, [x3]

.Lcbc_dec_loop_8x:
	sub		w4, w4, #8
	tbnz		w4, #31, .Lcbc_dec_4x

	ld4		{v0.4s-v3.4s}, [x2], #64
	ld4		{v4.4s-v7.4s}, [x2]

	SM4_CRYPT_BLK8_norotate(v0, v1, v2, v3, v4, v5, v6, v7)

	/* Avoid overwriting the RIV register */
	rotate_clockwise_4x4(v0, v1, v2, v3)
	rotate_clockwise_4x4(v4, v5, v6, v7)

	sub		x2, x2, #64

	eor		v0.16b, v0.16b, RIV.16b

	ld1		{RTMP0.16b-RTMP3.16b}, [x2], #64
	ld1		{RTMP4.16b-RTMP7.16b}, [x2], #64

	eor		v1.16b, v1.16b, RTMP0.16b
	eor		v2.16b, v2.16b, RTMP1.16b
	eor		v3.16b, v3.16b, RTMP2.16b
	eor		v4.16b, v4.16b, RTMP3.16b
	eor		v5.16b, v5.16b, RTMP4.16b
	eor		v6.16b, v6.16b, RTMP5.16b
	eor		v7.16b, v7.16b, RTMP6.16b

	mov		RIV.16b, RTMP7.16b

	st1		{v0.16b-v3.16b}, [x1], #64
	st1		{v4.16b-v7.16b}, [x1], #64

	cbz		w4, .Lcbc_dec_end
	b		.Lcbc_dec_loop_8x

.Lcbc_dec_4x:
	add		w4, w4, #8
	cmp		w4, #4
	blt		.Lcbc_dec_tail

	sub		w4, w4, #4

	ld1		{v0.16b-v3.16b}, [x2], #64

	rev32		v4.16b, v0.16b
	rev32		v5.16b, v1.16b
	rev32		v6.16b, v2.16b
	rev32		v7.16b, v3.16b

	transpose_4x4(v4, v5, v6, v7)

	SM4_CRYPT_BLK4_BE(v4, v5, v6, v7)

	eor		v4.16b, v4.16b, RIV.16b
	eor		v5.16b, v5.16b, v0.16b
	eor		v6.16b, v6.16b, v1.16b
	eor		v7.16b, v7.16b, v2.16b

	mov		RIV.16b, v3.16b

	st1		{v4.16b-v7.16b}, [x1], #64

	cbz		w4, .Lcbc_dec_end

.Lcbc_dec_tail:
	cmp		w4, #2
	ld1		{v0.16b}, [x2], #16
	blt		.Lcbc_dec_tail_load_done
	ld1		{v1.16b}, [x2], #16
	beq		.Lcbc_dec_tail_load_done
	ld1		{v2.16b}, [x2], #16

.Lcbc_dec_tail_load_done:
	rev32		v4.16b, v0.16b
	rev32		v5.16b, v1.16b
	rev32		v6.16b, v2.16b

	transpose_4x4(v4, v5, v6, v7)

	SM4_CRYPT_BLK4_BE(v4, v5, v6, v7)

	cmp		w4, #2
	eor		v4.16b, v4.16b, RIV.16b
	mov		RIV.16b, v0.16b
	st1		{v4.16b}, [x1], #16
	blt		.Lcbc_dec_end

	eor		v5.16b, v5.16b, v0.16b
	mov		RIV.16b, v1.16b
	st1		{v5.16b}, [x1], #16
	beq		.Lcbc_dec_end

	eor		v6.16b, v6.16b, v1.16b
	mov		RIV.16b, v2.16b
	st1		{v6.16b}, [x1], #16

.Lcbc_dec_end:
	/* store new IV */
	st1		{RIV.16b}, [x3]

	ret
SYM_FUNC_END(sm4_neon_cbc_dec)

.align 3
SYM_FUNC_START(sm4_neon_ctr_crypt)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: ctr (big endian, 128 bit)
	 *   w4: nblocks
	 */
	SM4_PREPARE()

	ldp		x7, x8, [x3]
	rev		x7, x7
	rev		x8, x8

.Lctr_crypt_loop_8x:
	sub		w4, w4, #8
	tbnz		w4, #31, .Lctr_crypt_4x

#define inc_le128(vctr)                             \
		mov		vctr.d[1], x8;      \
		mov		vctr.d[0], x7;      \
		adds		x8, x8, #1;         \
		rev64		vctr.16b, vctr.16b; \
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

	transpose_4x4_2x(v0, v1, v2, v3, v4, v5, v6, v7)

	SM4_CRYPT_BLK8(v0, v1, v2, v3, v4, v5, v6, v7)

	ld1		{RTMP0.16b-RTMP3.16b}, [x2], #64
	ld1		{RTMP4.16b-RTMP7.16b}, [x2], #64

	eor		v0.16b, v0.16b, RTMP0.16b
	eor		v1.16b, v1.16b, RTMP1.16b
	eor		v2.16b, v2.16b, RTMP2.16b
	eor		v3.16b, v3.16b, RTMP3.16b
	eor		v4.16b, v4.16b, RTMP4.16b
	eor		v5.16b, v5.16b, RTMP5.16b
	eor		v6.16b, v6.16b, RTMP6.16b
	eor		v7.16b, v7.16b, RTMP7.16b

	st1		{v0.16b-v3.16b}, [x1], #64
	st1		{v4.16b-v7.16b}, [x1], #64

	cbz		w4, .Lctr_crypt_end
	b		.Lctr_crypt_loop_8x

.Lctr_crypt_4x:
	add		w4, w4, #8
	cmp		w4, #4
	blt		.Lctr_crypt_tail

	sub		w4, w4, #4

	/* construct CTRs */
	inc_le128(v0)			/* +0 */
	inc_le128(v1)			/* +1 */
	inc_le128(v2)			/* +2 */
	inc_le128(v3)			/* +3 */

	ld1		{v4.16b-v7.16b}, [x2], #64

	transpose_4x4(v0, v1, v2, v3)

	SM4_CRYPT_BLK4(v0, v1, v2, v3)

	eor		v0.16b, v0.16b, v4.16b
	eor		v1.16b, v1.16b, v5.16b
	eor		v2.16b, v2.16b, v6.16b
	eor		v3.16b, v3.16b, v7.16b

	st1		{v0.16b-v3.16b}, [x1], #64

	cbz		w4, .Lctr_crypt_end

.Lctr_crypt_tail:
	/* inc_le128 will change the sign bit */
	ld1		{v4.16b}, [x2], #16
	inc_le128(v0)
	cmp		w4, #2
	blt		.Lctr_crypt_tail_load_done

	ld1		{v5.16b}, [x2], #16
	inc_le128(v1)
	cmp		w4, #2
	beq		.Lctr_crypt_tail_load_done

	ld1		{v6.16b}, [x2], #16
	inc_le128(v2)

.Lctr_crypt_tail_load_done:
	transpose_4x4(v0, v1, v2, v3)

	SM4_CRYPT_BLK4(v0, v1, v2, v3)

	cmp		w4, #2

	eor		v0.16b, v0.16b, v4.16b
	st1		{v0.16b}, [x1], #16
	blt		.Lctr_crypt_end

	eor		v1.16b, v1.16b, v5.16b
	st1		{v1.16b}, [x1], #16
	beq		.Lctr_crypt_end

	eor		v2.16b, v2.16b, v6.16b
	st1		{v2.16b}, [x1], #16

.Lctr_crypt_end:
	/* store new CTR */
	rev		x7, x7
	rev		x8, x8
	stp		x7, x8, [x3]

	ret
SYM_FUNC_END(sm4_neon_ctr_crypt)
