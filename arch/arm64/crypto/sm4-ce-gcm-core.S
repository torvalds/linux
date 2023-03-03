/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4-GCM AEAD Algorithm using ARMv8 Crypto Extensions
 * as specified in rfc8998
 * https://datatracker.ietf.org/doc/html/rfc8998
 *
 * Copyright (C) 2016 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>
#include <asm/assembler.h>
#include "sm4-ce-asm.h"

.arch	armv8-a+crypto

.irp b, 0, 1, 2, 3, 24, 25, 26, 27, 28, 29, 30, 31
	.set .Lv\b\().4s, \b
.endr

.macro sm4e, vd, vn
	.inst 0xcec08400 | (.L\vn << 5) | .L\vd
.endm

/* Register macros */

/* Used for both encryption and decryption */
#define	RHASH	v21
#define	RRCONST	v22
#define RZERO	v23

/* Helper macros. */

/*
 * input: m0, m1
 * output: r0:r1 (low 128-bits in r0, high in r1)
 */
#define PMUL_128x128(r0, r1, m0, m1, T0, T1)			\
		ext		T0.16b, m1.16b, m1.16b, #8;	\
		pmull		r0.1q, m0.1d, m1.1d;		\
		pmull		T1.1q, m0.1d, T0.1d;		\
		pmull2		T0.1q, m0.2d, T0.2d;		\
		pmull2		r1.1q, m0.2d, m1.2d;		\
		eor		T0.16b, T0.16b, T1.16b;		\
		ext		T1.16b, RZERO.16b, T0.16b, #8;	\
		ext		T0.16b, T0.16b, RZERO.16b, #8;	\
		eor		r0.16b, r0.16b, T1.16b;		\
		eor		r1.16b, r1.16b, T0.16b;

#define PMUL_128x128_4x(r0, r1, m0, m1, T0, T1,			\
			r2, r3, m2, m3, T2, T3,			\
			r4, r5, m4, m5, T4, T5,			\
			r6, r7, m6, m7, T6, T7)			\
		ext		T0.16b, m1.16b, m1.16b, #8;	\
		ext		T2.16b, m3.16b, m3.16b, #8;	\
		ext		T4.16b, m5.16b, m5.16b, #8;	\
		ext		T6.16b, m7.16b, m7.16b, #8;	\
		pmull		r0.1q, m0.1d, m1.1d;		\
		pmull		r2.1q, m2.1d, m3.1d;		\
		pmull		r4.1q, m4.1d, m5.1d;		\
		pmull		r6.1q, m6.1d, m7.1d;		\
		pmull		T1.1q, m0.1d, T0.1d;		\
		pmull		T3.1q, m2.1d, T2.1d;		\
		pmull		T5.1q, m4.1d, T4.1d;		\
		pmull		T7.1q, m6.1d, T6.1d;		\
		pmull2		T0.1q, m0.2d, T0.2d;		\
		pmull2		T2.1q, m2.2d, T2.2d;		\
		pmull2		T4.1q, m4.2d, T4.2d;		\
		pmull2		T6.1q, m6.2d, T6.2d;		\
		pmull2		r1.1q, m0.2d, m1.2d;		\
		pmull2		r3.1q, m2.2d, m3.2d;		\
		pmull2		r5.1q, m4.2d, m5.2d;		\
		pmull2		r7.1q, m6.2d, m7.2d;		\
		eor		T0.16b, T0.16b, T1.16b;		\
		eor		T2.16b, T2.16b, T3.16b;		\
		eor		T4.16b, T4.16b, T5.16b;		\
		eor		T6.16b, T6.16b, T7.16b;		\
		ext		T1.16b, RZERO.16b, T0.16b, #8;	\
		ext		T3.16b, RZERO.16b, T2.16b, #8;	\
		ext		T5.16b, RZERO.16b, T4.16b, #8;	\
		ext		T7.16b, RZERO.16b, T6.16b, #8;	\
		ext		T0.16b, T0.16b, RZERO.16b, #8;	\
		ext		T2.16b, T2.16b, RZERO.16b, #8;	\
		ext		T4.16b, T4.16b, RZERO.16b, #8;	\
		ext		T6.16b, T6.16b, RZERO.16b, #8;	\
		eor		r0.16b, r0.16b, T1.16b;		\
		eor		r2.16b, r2.16b, T3.16b; 	\
		eor		r4.16b, r4.16b, T5.16b; 	\
		eor		r6.16b, r6.16b, T7.16b; 	\
		eor		r1.16b, r1.16b, T0.16b; 	\
		eor		r3.16b, r3.16b, T2.16b; 	\
		eor		r5.16b, r5.16b, T4.16b; 	\
		eor		r7.16b, r7.16b, T6.16b;

/*
 * input: r0:r1 (low 128-bits in r0, high in r1)
 * output: a
 */
#define REDUCTION(a, r0, r1, rconst, T0, T1)			\
		pmull2		T0.1q, r1.2d, rconst.2d;	\
		ext		T1.16b, T0.16b, RZERO.16b, #8;	\
		ext		T0.16b, RZERO.16b, T0.16b, #8;	\
		eor		r1.16b, r1.16b, T1.16b;		\
		eor		r0.16b, r0.16b, T0.16b;		\
		pmull		T0.1q, r1.1d, rconst.1d;	\
		eor		a.16b, r0.16b, T0.16b;

#define SM4_CRYPT_PMUL_128x128_BLK(b0, r0, r1, m0, m1, T0, T1)	\
	rev32			b0.16b, b0.16b;			\
		ext		T0.16b, m1.16b, m1.16b, #8;	\
	sm4e			b0.4s, v24.4s;			\
		pmull		r0.1q, m0.1d, m1.1d;		\
	sm4e			b0.4s, v25.4s;			\
		pmull		T1.1q, m0.1d, T0.1d;		\
	sm4e			b0.4s, v26.4s;			\
		pmull2		T0.1q, m0.2d, T0.2d;		\
	sm4e			b0.4s, v27.4s;			\
		pmull2		r1.1q, m0.2d, m1.2d;		\
	sm4e			b0.4s, v28.4s;			\
		eor		T0.16b, T0.16b, T1.16b;		\
	sm4e			b0.4s, v29.4s;			\
		ext		T1.16b, RZERO.16b, T0.16b, #8;	\
	sm4e			b0.4s, v30.4s;			\
		ext		T0.16b, T0.16b, RZERO.16b, #8;	\
	sm4e			b0.4s, v31.4s;			\
		eor		r0.16b, r0.16b, T1.16b;		\
	rev64			b0.4s, b0.4s;			\
		eor		r1.16b, r1.16b, T0.16b;		\
	ext			b0.16b, b0.16b, b0.16b, #8;	\
	rev32			b0.16b, b0.16b;

#define SM4_CRYPT_PMUL_128x128_BLK3(b0, b1, b2,			\
				    r0, r1, m0, m1, T0, T1,	\
				    r2, r3, m2, m3, T2, T3,	\
				    r4, r5, m4, m5, T4, T5)	\
	rev32			b0.16b, b0.16b;			\
	rev32			b1.16b, b1.16b;			\
	rev32			b2.16b, b2.16b;			\
		ext		T0.16b, m1.16b, m1.16b, #8;	\
		ext		T2.16b, m3.16b, m3.16b, #8;	\
		ext		T4.16b, m5.16b, m5.16b, #8;	\
	sm4e			b0.4s, v24.4s;			\
	sm4e			b1.4s, v24.4s;			\
	sm4e			b2.4s, v24.4s;			\
		pmull		r0.1q, m0.1d, m1.1d;		\
		pmull		r2.1q, m2.1d, m3.1d;		\
		pmull		r4.1q, m4.1d, m5.1d;		\
	sm4e			b0.4s, v25.4s;			\
	sm4e			b1.4s, v25.4s;			\
	sm4e			b2.4s, v25.4s;			\
		pmull		T1.1q, m0.1d, T0.1d;		\
		pmull		T3.1q, m2.1d, T2.1d;		\
		pmull		T5.1q, m4.1d, T4.1d;		\
	sm4e			b0.4s, v26.4s;			\
	sm4e			b1.4s, v26.4s;			\
	sm4e			b2.4s, v26.4s;			\
		pmull2		T0.1q, m0.2d, T0.2d;		\
		pmull2		T2.1q, m2.2d, T2.2d;		\
		pmull2		T4.1q, m4.2d, T4.2d;		\
	sm4e			b0.4s, v27.4s;			\
	sm4e			b1.4s, v27.4s;			\
	sm4e			b2.4s, v27.4s;			\
		pmull2		r1.1q, m0.2d, m1.2d;		\
		pmull2		r3.1q, m2.2d, m3.2d;		\
		pmull2		r5.1q, m4.2d, m5.2d;		\
	sm4e			b0.4s, v28.4s;			\
	sm4e			b1.4s, v28.4s;			\
	sm4e			b2.4s, v28.4s;			\
		eor		T0.16b, T0.16b, T1.16b;		\
		eor		T2.16b, T2.16b, T3.16b;		\
		eor		T4.16b, T4.16b, T5.16b;		\
	sm4e			b0.4s, v29.4s;			\
	sm4e			b1.4s, v29.4s;			\
	sm4e			b2.4s, v29.4s;			\
		ext		T1.16b, RZERO.16b, T0.16b, #8;	\
		ext		T3.16b, RZERO.16b, T2.16b, #8;	\
		ext		T5.16b, RZERO.16b, T4.16b, #8;	\
	sm4e			b0.4s, v30.4s;			\
	sm4e			b1.4s, v30.4s;			\
	sm4e			b2.4s, v30.4s;			\
		ext		T0.16b, T0.16b, RZERO.16b, #8;	\
		ext		T2.16b, T2.16b, RZERO.16b, #8;	\
		ext		T4.16b, T4.16b, RZERO.16b, #8;	\
	sm4e			b0.4s, v31.4s;			\
	sm4e			b1.4s, v31.4s;			\
	sm4e			b2.4s, v31.4s;			\
		eor		r0.16b, r0.16b, T1.16b;		\
		eor		r2.16b, r2.16b, T3.16b;		\
		eor		r4.16b, r4.16b, T5.16b;		\
	rev64			b0.4s, b0.4s;			\
	rev64			b1.4s, b1.4s;			\
	rev64			b2.4s, b2.4s;			\
		eor		r1.16b, r1.16b, T0.16b;		\
		eor		r3.16b, r3.16b, T2.16b;		\
		eor		r5.16b, r5.16b, T4.16b;		\
	ext			b0.16b, b0.16b, b0.16b, #8;	\
	ext			b1.16b, b1.16b, b1.16b, #8;	\
	ext			b2.16b, b2.16b, b2.16b, #8;	\
		eor		r0.16b, r0.16b, r2.16b;		\
		eor		r1.16b, r1.16b, r3.16b;		\
	rev32			b0.16b, b0.16b;			\
	rev32			b1.16b, b1.16b;			\
	rev32			b2.16b, b2.16b;			\
		eor		r0.16b, r0.16b, r4.16b;		\
		eor		r1.16b, r1.16b, r5.16b;

#define inc32_le128(vctr)					\
		mov		vctr.d[1], x9;			\
		add		w6, w9, #1;			\
		mov		vctr.d[0], x8;			\
		bfi		x9, x6, #0, #32;		\
		rev64		vctr.16b, vctr.16b;

#define GTAG_HASH_LENGTHS(vctr0, vlen)					\
		ld1		{vlen.16b}, [x7];			\
		/* construct CTR0 */					\
		/* the lower 32-bits of initial IV is always be32(1) */	\
		mov		x6, #0x1;				\
		bfi		x9, x6, #0, #32;			\
		mov		vctr0.d[0], x8;				\
		mov		vctr0.d[1], x9;				\
		rbit		vlen.16b, vlen.16b;			\
		rev64		vctr0.16b, vctr0.16b;			\
		/* authtag = GCTR(CTR0, GHASH) */			\
		eor		RHASH.16b, RHASH.16b, vlen.16b;		\
		SM4_CRYPT_PMUL_128x128_BLK(vctr0, RR0, RR1, RHASH, RH1,	\
					   RTMP0, RTMP1);		\
		REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP2, RTMP3);	\
		rbit		RHASH.16b, RHASH.16b;			\
		eor		RHASH.16b, RHASH.16b, vctr0.16b;


/* Register macros for encrypt and ghash */

/* can be the same as input v0-v3 */
#define	RR1	v0
#define	RR3	v1
#define	RR5	v2
#define	RR7	v3

#define	RR0	v4
#define	RR2	v5
#define	RR4	v6
#define	RR6	v7

#define RTMP0	v8
#define RTMP1	v9
#define RTMP2	v10
#define RTMP3	v11
#define RTMP4	v12
#define RTMP5	v13
#define RTMP6	v14
#define RTMP7	v15

#define	RH1	v16
#define	RH2	v17
#define	RH3	v18
#define	RH4	v19

.align 3
SYM_FUNC_START(sm4_ce_pmull_ghash_setup)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: ghash table
	 */
	SM4_PREPARE(x0)

	adr_l		x2, .Lghash_rconst
	ld1r		{RRCONST.2d}, [x2]

	eor		RZERO.16b, RZERO.16b, RZERO.16b

	/* H = E(K, 0^128) */
	rev32		v0.16b, RZERO.16b
	SM4_CRYPT_BLK_BE(v0)

	/* H ^ 1 */
	rbit		RH1.16b, v0.16b

	/* H ^ 2 */
	PMUL_128x128(RR0, RR1, RH1, RH1, RTMP0, RTMP1)
	REDUCTION(RH2, RR0, RR1, RRCONST, RTMP2, RTMP3)

	/* H ^ 3 */
	PMUL_128x128(RR0, RR1, RH2, RH1, RTMP0, RTMP1)
	REDUCTION(RH3, RR0, RR1, RRCONST, RTMP2, RTMP3)

	/* H ^ 4 */
	PMUL_128x128(RR0, RR1, RH2, RH2, RTMP0, RTMP1)
	REDUCTION(RH4, RR0, RR1, RRCONST, RTMP2, RTMP3)

	st1		{RH1.16b-RH4.16b}, [x1]

	ret
SYM_FUNC_END(sm4_ce_pmull_ghash_setup)

.align 3
SYM_FUNC_START(pmull_ghash_update)
	/* input:
	 *   x0: ghash table
	 *   x1: ghash result
	 *   x2: src
	 *   w3: nblocks
	 */
	ld1		{RH1.16b-RH4.16b}, [x0]

	ld1		{RHASH.16b}, [x1]
	rbit		RHASH.16b, RHASH.16b

	adr_l		x4, .Lghash_rconst
	ld1r		{RRCONST.2d}, [x4]

	eor		RZERO.16b, RZERO.16b, RZERO.16b

.Lghash_loop_4x:
	cmp		w3, #4
	blt		.Lghash_loop_1x

	sub		w3, w3, #4

	ld1		{v0.16b-v3.16b}, [x2], #64

	rbit		v0.16b, v0.16b
	rbit		v1.16b, v1.16b
	rbit		v2.16b, v2.16b
	rbit		v3.16b, v3.16b

	/*
	 * (in0 ^ HASH) * H^4 => rr0:rr1
	 * (in1)        * H^3 => rr2:rr3
	 * (in2)        * H^2 => rr4:rr5
	 * (in3)        * H^1 => rr6:rr7
	 */
	eor		RHASH.16b, RHASH.16b, v0.16b

	PMUL_128x128_4x(RR0, RR1, RHASH, RH4, RTMP0, RTMP1,
			RR2, RR3, v1, RH3, RTMP2, RTMP3,
			RR4, RR5, v2, RH2, RTMP4, RTMP5,
			RR6, RR7, v3, RH1, RTMP6, RTMP7)

	eor		RR0.16b, RR0.16b, RR2.16b
	eor		RR1.16b, RR1.16b, RR3.16b
	eor		RR0.16b, RR0.16b, RR4.16b
	eor		RR1.16b, RR1.16b, RR5.16b
	eor		RR0.16b, RR0.16b, RR6.16b
	eor		RR1.16b, RR1.16b, RR7.16b

	REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP0, RTMP1)

	cbz		w3, .Lghash_end
	b		.Lghash_loop_4x

.Lghash_loop_1x:
	sub		w3, w3, #1

	ld1		{v0.16b}, [x2], #16
	rbit		v0.16b, v0.16b
	eor		RHASH.16b, RHASH.16b, v0.16b

	PMUL_128x128(RR0, RR1, RHASH, RH1, RTMP0, RTMP1)
	REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP2, RTMP3)

	cbnz		w3, .Lghash_loop_1x

.Lghash_end:
	rbit		RHASH.16b, RHASH.16b
	st1		{RHASH.2d}, [x1]

	ret
SYM_FUNC_END(pmull_ghash_update)

.align 3
SYM_TYPED_FUNC_START(sm4_ce_pmull_gcm_enc)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: ctr (big endian, 128 bit)
	 *   w4: nbytes
	 *   x5: ghash result
	 *   x6: ghash table
	 *   x7: lengths (only for last block)
	 */
	SM4_PREPARE(x0)

	ldp		x8, x9, [x3]
	rev		x8, x8
	rev		x9, x9

	ld1		{RH1.16b-RH4.16b}, [x6]

	ld1		{RHASH.16b}, [x5]
	rbit		RHASH.16b, RHASH.16b

	adr_l		x6, .Lghash_rconst
	ld1r		{RRCONST.2d}, [x6]

	eor		RZERO.16b, RZERO.16b, RZERO.16b

	cbz		w4, .Lgcm_enc_hash_len

.Lgcm_enc_loop_4x:
	cmp		w4, #(4 * 16)
	blt		.Lgcm_enc_loop_1x

	sub		w4, w4, #(4 * 16)

	/* construct CTRs */
	inc32_le128(v0)			/* +0 */
	inc32_le128(v1)			/* +1 */
	inc32_le128(v2)			/* +2 */
	inc32_le128(v3)			/* +3 */

	ld1		{RTMP0.16b-RTMP3.16b}, [x2], #64

	SM4_CRYPT_BLK4(v0, v1, v2, v3)

	eor		v0.16b, v0.16b, RTMP0.16b
	eor		v1.16b, v1.16b, RTMP1.16b
	eor		v2.16b, v2.16b, RTMP2.16b
	eor		v3.16b, v3.16b, RTMP3.16b
	st1		{v0.16b-v3.16b}, [x1], #64

	/* ghash update */

	rbit		v0.16b, v0.16b
	rbit		v1.16b, v1.16b
	rbit		v2.16b, v2.16b
	rbit		v3.16b, v3.16b

	/*
	 * (in0 ^ HASH) * H^4 => rr0:rr1
	 * (in1)        * H^3 => rr2:rr3
	 * (in2)        * H^2 => rr4:rr5
	 * (in3)        * H^1 => rr6:rr7
	 */
	eor		RHASH.16b, RHASH.16b, v0.16b

	PMUL_128x128_4x(RR0, RR1, RHASH, RH4, RTMP0, RTMP1,
			RR2, RR3, v1, RH3, RTMP2, RTMP3,
			RR4, RR5, v2, RH2, RTMP4, RTMP5,
			RR6, RR7, v3, RH1, RTMP6, RTMP7)

	eor		RR0.16b, RR0.16b, RR2.16b
	eor		RR1.16b, RR1.16b, RR3.16b
	eor		RR0.16b, RR0.16b, RR4.16b
	eor		RR1.16b, RR1.16b, RR5.16b
	eor		RR0.16b, RR0.16b, RR6.16b
	eor		RR1.16b, RR1.16b, RR7.16b

	REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP0, RTMP1)

	cbz		w4, .Lgcm_enc_hash_len
	b		.Lgcm_enc_loop_4x

.Lgcm_enc_loop_1x:
	cmp		w4, #16
	blt		.Lgcm_enc_tail

	sub		w4, w4, #16

	/* construct CTRs */
	inc32_le128(v0)

	ld1		{RTMP0.16b}, [x2], #16

	SM4_CRYPT_BLK(v0)

	eor		v0.16b, v0.16b, RTMP0.16b
	st1		{v0.16b}, [x1], #16

	/* ghash update */
	rbit		v0.16b, v0.16b
	eor		RHASH.16b, RHASH.16b, v0.16b
	PMUL_128x128(RR0, RR1, RHASH, RH1, RTMP0, RTMP1)
	REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP2, RTMP3)

	cbz		w4, .Lgcm_enc_hash_len
	b		.Lgcm_enc_loop_1x

.Lgcm_enc_tail:
	/* construct CTRs */
	inc32_le128(v0)
	SM4_CRYPT_BLK(v0)

	/* load permute table */
	adr_l		x0, .Lcts_permute_table
	add		x0, x0, #32
	sub		x0, x0, w4, uxtw
	ld1		{v3.16b}, [x0]

.Lgcm_enc_tail_loop:
	/* do encrypt */
	ldrb		w0, [x2], #1	/* get 1 byte from input */
	umov		w6, v0.b[0]	/* get top crypted byte */
	eor		w6, w6, w0	/* w6 = CTR ^ input */
	strb		w6, [x1], #1	/* store out byte */

	/* shift right out one byte */
	ext		v0.16b, v0.16b, v0.16b, #1
	/* the last ciphertext is placed in high bytes */
	ins		v0.b[15], w6

	subs		w4, w4, #1
	bne		.Lgcm_enc_tail_loop

	/* padding last block with zeros */
	tbl		v0.16b, {v0.16b}, v3.16b

	/* ghash update */
	rbit		v0.16b, v0.16b
	eor		RHASH.16b, RHASH.16b, v0.16b
	PMUL_128x128(RR0, RR1, RHASH, RH1, RTMP0, RTMP1)
	REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP2, RTMP3)

.Lgcm_enc_hash_len:
	cbz		x7, .Lgcm_enc_end

	GTAG_HASH_LENGTHS(v1, v3)

	b		.Lgcm_enc_ret

.Lgcm_enc_end:
	/* store new CTR */
	rev		x8, x8
	rev		x9, x9
	stp		x8, x9, [x3]

	rbit		RHASH.16b, RHASH.16b

.Lgcm_enc_ret:
	/* store new MAC */
	st1		{RHASH.2d}, [x5]

	ret
SYM_FUNC_END(sm4_ce_pmull_gcm_enc)

#undef	RR1
#undef	RR3
#undef	RR5
#undef	RR7
#undef	RR0
#undef	RR2
#undef	RR4
#undef	RR6
#undef RTMP0
#undef RTMP1
#undef RTMP2
#undef RTMP3
#undef RTMP4
#undef RTMP5
#undef RTMP6
#undef RTMP7
#undef	RH1
#undef	RH2
#undef	RH3
#undef	RH4


/* Register macros for decrypt */

/* v0-v2 for building CTRs, v3-v5 for saving inputs */

#define	RR1	v6
#define	RR3	v7
#define	RR5	v8

#define	RR0	v9
#define	RR2	v10
#define	RR4	v11

#define RTMP0	v12
#define RTMP1	v13
#define RTMP2	v14
#define RTMP3	v15
#define RTMP4	v16
#define RTMP5	v17

#define	RH1	v18
#define	RH2	v19
#define	RH3	v20

.align 3
SYM_TYPED_FUNC_START(sm4_ce_pmull_gcm_dec)
	/* input:
	 *   x0: round key array, CTX
	 *   x1: dst
	 *   x2: src
	 *   x3: ctr (big endian, 128 bit)
	 *   w4: nbytes
	 *   x5: ghash result
	 *   x6: ghash table
	 *   x7: lengths (only for last block)
	 */
	SM4_PREPARE(x0)

	ldp		x8, x9, [x3]
	rev		x8, x8
	rev		x9, x9

	ld1		{RH1.16b-RH3.16b}, [x6]

	ld1		{RHASH.16b}, [x5]
	rbit		RHASH.16b, RHASH.16b

	adr_l		x6, .Lghash_rconst
	ld1r		{RRCONST.2d}, [x6]

	eor		RZERO.16b, RZERO.16b, RZERO.16b

	cbz		w4, .Lgcm_dec_hash_len

.Lgcm_dec_loop_3x:
	cmp		w4, #(3 * 16)
	blt		.Lgcm_dec_loop_1x

	sub		w4, w4, #(3 * 16)

	ld1		{v3.16b-v5.16b}, [x2], #(3 * 16)

	/* construct CTRs */
	inc32_le128(v0)			/* +0 */
	rbit		v6.16b, v3.16b
	inc32_le128(v1)			/* +1 */
	rbit		v7.16b, v4.16b
	inc32_le128(v2)			/* +2 */
	rbit		v8.16b, v5.16b

	eor		RHASH.16b, RHASH.16b, v6.16b

	/* decrypt & ghash update */
	SM4_CRYPT_PMUL_128x128_BLK3(v0, v1, v2,
				    RR0, RR1, RHASH, RH3, RTMP0, RTMP1,
				    RR2, RR3, v7, RH2, RTMP2, RTMP3,
				    RR4, RR5, v8, RH1, RTMP4, RTMP5)

	eor		v0.16b, v0.16b, v3.16b
	eor		v1.16b, v1.16b, v4.16b
	eor		v2.16b, v2.16b, v5.16b

	REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP0, RTMP1)

	st1		{v0.16b-v2.16b}, [x1], #(3 * 16)

	cbz		w4, .Lgcm_dec_hash_len
	b		.Lgcm_dec_loop_3x

.Lgcm_dec_loop_1x:
	cmp		w4, #16
	blt		.Lgcm_dec_tail

	sub		w4, w4, #16

	ld1		{v3.16b}, [x2], #16

	/* construct CTRs */
	inc32_le128(v0)
	rbit		v6.16b, v3.16b

	eor		RHASH.16b, RHASH.16b, v6.16b

	SM4_CRYPT_PMUL_128x128_BLK(v0, RR0, RR1, RHASH, RH1, RTMP0, RTMP1)

	eor		v0.16b, v0.16b, v3.16b

	REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP2, RTMP3)

	st1		{v0.16b}, [x1], #16

	cbz		w4, .Lgcm_dec_hash_len
	b		.Lgcm_dec_loop_1x

.Lgcm_dec_tail:
	/* construct CTRs */
	inc32_le128(v0)
	SM4_CRYPT_BLK(v0)

	/* load permute table */
	adr_l		x0, .Lcts_permute_table
	add		x0, x0, #32
	sub		x0, x0, w4, uxtw
	ld1		{v3.16b}, [x0]

.Lgcm_dec_tail_loop:
	/* do decrypt */
	ldrb		w0, [x2], #1	/* get 1 byte from input */
	umov		w6, v0.b[0]	/* get top crypted byte */
	eor		w6, w6, w0	/* w6 = CTR ^ input */
	strb		w6, [x1], #1	/* store out byte */

	/* shift right out one byte */
	ext		v0.16b, v0.16b, v0.16b, #1
	/* the last ciphertext is placed in high bytes */
	ins		v0.b[15], w0

	subs		w4, w4, #1
	bne		.Lgcm_dec_tail_loop

	/* padding last block with zeros */
	tbl		v0.16b, {v0.16b}, v3.16b

	/* ghash update */
	rbit		v0.16b, v0.16b
	eor		RHASH.16b, RHASH.16b, v0.16b
	PMUL_128x128(RR0, RR1, RHASH, RH1, RTMP0, RTMP1)
	REDUCTION(RHASH, RR0, RR1, RRCONST, RTMP2, RTMP3)

.Lgcm_dec_hash_len:
	cbz		x7, .Lgcm_dec_end

	GTAG_HASH_LENGTHS(v1, v3)

	b		.Lgcm_dec_ret

.Lgcm_dec_end:
	/* store new CTR */
	rev		x8, x8
	rev		x9, x9
	stp		x8, x9, [x3]

	rbit		RHASH.16b, RHASH.16b

.Lgcm_dec_ret:
	/* store new MAC */
	st1		{RHASH.2d}, [x5]

	ret
SYM_FUNC_END(sm4_ce_pmull_gcm_dec)

	.section	".rodata", "a"
	.align 4
.Lcts_permute_table:
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
	.byte		 0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	.byte		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff

.Lghash_rconst:
	.quad		0x87
