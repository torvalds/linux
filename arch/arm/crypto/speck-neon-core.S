// SPDX-License-Identifier: GPL-2.0
/*
 * NEON-accelerated implementation of Speck128-XTS and Speck64-XTS
 *
 * Copyright (c) 2018 Google, Inc
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */

#include <linux/linkage.h>

	.text
	.fpu		neon

	// arguments
	ROUND_KEYS	.req	r0	// const {u64,u32} *round_keys
	NROUNDS		.req	r1	// int nrounds
	DST		.req	r2	// void *dst
	SRC		.req	r3	// const void *src
	NBYTES		.req	r4	// unsigned int nbytes
	TWEAK		.req	r5	// void *tweak

	// registers which hold the data being encrypted/decrypted
	X0		.req	q0
	X0_L		.req	d0
	X0_H		.req	d1
	Y0		.req	q1
	Y0_H		.req	d3
	X1		.req	q2
	X1_L		.req	d4
	X1_H		.req	d5
	Y1		.req	q3
	Y1_H		.req	d7
	X2		.req	q4
	X2_L		.req	d8
	X2_H		.req	d9
	Y2		.req	q5
	Y2_H		.req	d11
	X3		.req	q6
	X3_L		.req	d12
	X3_H		.req	d13
	Y3		.req	q7
	Y3_H		.req	d15

	// the round key, duplicated in all lanes
	ROUND_KEY	.req	q8
	ROUND_KEY_L	.req	d16
	ROUND_KEY_H	.req	d17

	// index vector for vtbl-based 8-bit rotates
	ROTATE_TABLE	.req	d18

	// multiplication table for updating XTS tweaks
	GF128MUL_TABLE	.req	d19
	GF64MUL_TABLE	.req	d19

	// current XTS tweak value(s)
	TWEAKV		.req	q10
	TWEAKV_L	.req	d20
	TWEAKV_H	.req	d21

	TMP0		.req	q12
	TMP0_L		.req	d24
	TMP0_H		.req	d25
	TMP1		.req	q13
	TMP2		.req	q14
	TMP3		.req	q15

	.align		4
.Lror64_8_table:
	.byte		1, 2, 3, 4, 5, 6, 7, 0
.Lror32_8_table:
	.byte		1, 2, 3, 0, 5, 6, 7, 4
.Lrol64_8_table:
	.byte		7, 0, 1, 2, 3, 4, 5, 6
.Lrol32_8_table:
	.byte		3, 0, 1, 2, 7, 4, 5, 6
.Lgf128mul_table:
	.byte		0, 0x87
	.fill		14
.Lgf64mul_table:
	.byte		0, 0x1b, (0x1b << 1), (0x1b << 1) ^ 0x1b
	.fill		12

/*
 * _speck_round_128bytes() - Speck encryption round on 128 bytes at a time
 *
 * Do one Speck encryption round on the 128 bytes (8 blocks for Speck128, 16 for
 * Speck64) stored in X0-X3 and Y0-Y3, using the round key stored in all lanes
 * of ROUND_KEY.  'n' is the lane size: 64 for Speck128, or 32 for Speck64.
 *
 * The 8-bit rotates are implemented using vtbl instead of vshr + vsli because
 * the vtbl approach is faster on some processors and the same speed on others.
 */
.macro _speck_round_128bytes	n

	// x = ror(x, 8)
	vtbl.8		X0_L, {X0_L}, ROTATE_TABLE
	vtbl.8		X0_H, {X0_H}, ROTATE_TABLE
	vtbl.8		X1_L, {X1_L}, ROTATE_TABLE
	vtbl.8		X1_H, {X1_H}, ROTATE_TABLE
	vtbl.8		X2_L, {X2_L}, ROTATE_TABLE
	vtbl.8		X2_H, {X2_H}, ROTATE_TABLE
	vtbl.8		X3_L, {X3_L}, ROTATE_TABLE
	vtbl.8		X3_H, {X3_H}, ROTATE_TABLE

	// x += y
	vadd.u\n	X0, Y0
	vadd.u\n	X1, Y1
	vadd.u\n	X2, Y2
	vadd.u\n	X3, Y3

	// x ^= k
	veor		X0, ROUND_KEY
	veor		X1, ROUND_KEY
	veor		X2, ROUND_KEY
	veor		X3, ROUND_KEY

	// y = rol(y, 3)
	vshl.u\n	TMP0, Y0, #3
	vshl.u\n	TMP1, Y1, #3
	vshl.u\n	TMP2, Y2, #3
	vshl.u\n	TMP3, Y3, #3
	vsri.u\n	TMP0, Y0, #(\n - 3)
	vsri.u\n	TMP1, Y1, #(\n - 3)
	vsri.u\n	TMP2, Y2, #(\n - 3)
	vsri.u\n	TMP3, Y3, #(\n - 3)

	// y ^= x
	veor		Y0, TMP0, X0
	veor		Y1, TMP1, X1
	veor		Y2, TMP2, X2
	veor		Y3, TMP3, X3
.endm

/*
 * _speck_unround_128bytes() - Speck decryption round on 128 bytes at a time
 *
 * This is the inverse of _speck_round_128bytes().
 */
.macro _speck_unround_128bytes	n

	// y ^= x
	veor		TMP0, Y0, X0
	veor		TMP1, Y1, X1
	veor		TMP2, Y2, X2
	veor		TMP3, Y3, X3

	// y = ror(y, 3)
	vshr.u\n	Y0, TMP0, #3
	vshr.u\n	Y1, TMP1, #3
	vshr.u\n	Y2, TMP2, #3
	vshr.u\n	Y3, TMP3, #3
	vsli.u\n	Y0, TMP0, #(\n - 3)
	vsli.u\n	Y1, TMP1, #(\n - 3)
	vsli.u\n	Y2, TMP2, #(\n - 3)
	vsli.u\n	Y3, TMP3, #(\n - 3)

	// x ^= k
	veor		X0, ROUND_KEY
	veor		X1, ROUND_KEY
	veor		X2, ROUND_KEY
	veor		X3, ROUND_KEY

	// x -= y
	vsub.u\n	X0, Y0
	vsub.u\n	X1, Y1
	vsub.u\n	X2, Y2
	vsub.u\n	X3, Y3

	// x = rol(x, 8);
	vtbl.8		X0_L, {X0_L}, ROTATE_TABLE
	vtbl.8		X0_H, {X0_H}, ROTATE_TABLE
	vtbl.8		X1_L, {X1_L}, ROTATE_TABLE
	vtbl.8		X1_H, {X1_H}, ROTATE_TABLE
	vtbl.8		X2_L, {X2_L}, ROTATE_TABLE
	vtbl.8		X2_H, {X2_H}, ROTATE_TABLE
	vtbl.8		X3_L, {X3_L}, ROTATE_TABLE
	vtbl.8		X3_H, {X3_H}, ROTATE_TABLE
.endm

.macro _xts128_precrypt_one	dst_reg, tweak_buf, tmp

	// Load the next source block
	vld1.8		{\dst_reg}, [SRC]!

	// Save the current tweak in the tweak buffer
	vst1.8		{TWEAKV}, [\tweak_buf:128]!

	// XOR the next source block with the current tweak
	veor		\dst_reg, TWEAKV

	/*
	 * Calculate the next tweak by multiplying the current one by x,
	 * modulo p(x) = x^128 + x^7 + x^2 + x + 1.
	 */
	vshr.u64	\tmp, TWEAKV, #63
	vshl.u64	TWEAKV, #1
	veor		TWEAKV_H, \tmp\()_L
	vtbl.8		\tmp\()_H, {GF128MUL_TABLE}, \tmp\()_H
	veor		TWEAKV_L, \tmp\()_H
.endm

.macro _xts64_precrypt_two	dst_reg, tweak_buf, tmp

	// Load the next two source blocks
	vld1.8		{\dst_reg}, [SRC]!

	// Save the current two tweaks in the tweak buffer
	vst1.8		{TWEAKV}, [\tweak_buf:128]!

	// XOR the next two source blocks with the current two tweaks
	veor		\dst_reg, TWEAKV

	/*
	 * Calculate the next two tweaks by multiplying the current ones by x^2,
	 * modulo p(x) = x^64 + x^4 + x^3 + x + 1.
	 */
	vshr.u64	\tmp, TWEAKV, #62
	vshl.u64	TWEAKV, #2
	vtbl.8		\tmp\()_L, {GF64MUL_TABLE}, \tmp\()_L
	vtbl.8		\tmp\()_H, {GF64MUL_TABLE}, \tmp\()_H
	veor		TWEAKV, \tmp
.endm

/*
 * _speck_xts_crypt() - Speck-XTS encryption/decryption
 *
 * Encrypt or decrypt NBYTES bytes of data from the SRC buffer to the DST buffer
 * using Speck-XTS, specifically the variant with a block size of '2n' and round
 * count given by NROUNDS.  The expanded round keys are given in ROUND_KEYS, and
 * the current XTS tweak value is given in TWEAK.  It's assumed that NBYTES is a
 * nonzero multiple of 128.
 */
.macro _speck_xts_crypt	n, decrypting
	push		{r4-r7}
	mov		r7, sp

	/*
	 * The first four parameters were passed in registers r0-r3.  Load the
	 * additional parameters, which were passed on the stack.
	 */
	ldr		NBYTES, [sp, #16]
	ldr		TWEAK, [sp, #20]

	/*
	 * If decrypting, modify the ROUND_KEYS parameter to point to the last
	 * round key rather than the first, since for decryption the round keys
	 * are used in reverse order.
	 */
.if \decrypting
.if \n == 64
	add		ROUND_KEYS, ROUND_KEYS, NROUNDS, lsl #3
	sub		ROUND_KEYS, #8
.else
	add		ROUND_KEYS, ROUND_KEYS, NROUNDS, lsl #2
	sub		ROUND_KEYS, #4
.endif
.endif

	// Load the index vector for vtbl-based 8-bit rotates
.if \decrypting
	ldr		r12, =.Lrol\n\()_8_table
.else
	ldr		r12, =.Lror\n\()_8_table
.endif
	vld1.8		{ROTATE_TABLE}, [r12:64]

	// One-time XTS preparation

	/*
	 * Allocate stack space to store 128 bytes worth of tweaks.  For
	 * performance, this space is aligned to a 16-byte boundary so that we
	 * can use the load/store instructions that declare 16-byte alignment.
	 */
	sub		sp, #128
	bic		sp, #0xf

.if \n == 64
	// Load first tweak
	vld1.8		{TWEAKV}, [TWEAK]

	// Load GF(2^128) multiplication table
	ldr		r12, =.Lgf128mul_table
	vld1.8		{GF128MUL_TABLE}, [r12:64]
.else
	// Load first tweak
	vld1.8		{TWEAKV_L}, [TWEAK]

	// Load GF(2^64) multiplication table
	ldr		r12, =.Lgf64mul_table
	vld1.8		{GF64MUL_TABLE}, [r12:64]

	// Calculate second tweak, packing it together with the first
	vshr.u64	TMP0_L, TWEAKV_L, #63
	vtbl.u8		TMP0_L, {GF64MUL_TABLE}, TMP0_L
	vshl.u64	TWEAKV_H, TWEAKV_L, #1
	veor		TWEAKV_H, TMP0_L
.endif

.Lnext_128bytes_\@:

	/*
	 * Load the source blocks into {X,Y}[0-3], XOR them with their XTS tweak
	 * values, and save the tweaks on the stack for later.  Then
	 * de-interleave the 'x' and 'y' elements of each block, i.e. make it so
	 * that the X[0-3] registers contain only the second halves of blocks,
	 * and the Y[0-3] registers contain only the first halves of blocks.
	 * (Speck uses the order (y, x) rather than the more intuitive (x, y).)
	 */
	mov		r12, sp
.if \n == 64
	_xts128_precrypt_one	X0, r12, TMP0
	_xts128_precrypt_one	Y0, r12, TMP0
	_xts128_precrypt_one	X1, r12, TMP0
	_xts128_precrypt_one	Y1, r12, TMP0
	_xts128_precrypt_one	X2, r12, TMP0
	_xts128_precrypt_one	Y2, r12, TMP0
	_xts128_precrypt_one	X3, r12, TMP0
	_xts128_precrypt_one	Y3, r12, TMP0
	vswp		X0_L, Y0_H
	vswp		X1_L, Y1_H
	vswp		X2_L, Y2_H
	vswp		X3_L, Y3_H
.else
	_xts64_precrypt_two	X0, r12, TMP0
	_xts64_precrypt_two	Y0, r12, TMP0
	_xts64_precrypt_two	X1, r12, TMP0
	_xts64_precrypt_two	Y1, r12, TMP0
	_xts64_precrypt_two	X2, r12, TMP0
	_xts64_precrypt_two	Y2, r12, TMP0
	_xts64_precrypt_two	X3, r12, TMP0
	_xts64_precrypt_two	Y3, r12, TMP0
	vuzp.32		Y0, X0
	vuzp.32		Y1, X1
	vuzp.32		Y2, X2
	vuzp.32		Y3, X3
.endif

	// Do the cipher rounds

	mov		r12, ROUND_KEYS
	mov		r6, NROUNDS

.Lnext_round_\@:
.if \decrypting
.if \n == 64
	vld1.64		ROUND_KEY_L, [r12]
	sub		r12, #8
	vmov		ROUND_KEY_H, ROUND_KEY_L
.else
	vld1.32		{ROUND_KEY_L[],ROUND_KEY_H[]}, [r12]
	sub		r12, #4
.endif
	_speck_unround_128bytes	\n
.else
.if \n == 64
	vld1.64		ROUND_KEY_L, [r12]!
	vmov		ROUND_KEY_H, ROUND_KEY_L
.else
	vld1.32		{ROUND_KEY_L[],ROUND_KEY_H[]}, [r12]!
.endif
	_speck_round_128bytes	\n
.endif
	subs		r6, r6, #1
	bne		.Lnext_round_\@

	// Re-interleave the 'x' and 'y' elements of each block
.if \n == 64
	vswp		X0_L, Y0_H
	vswp		X1_L, Y1_H
	vswp		X2_L, Y2_H
	vswp		X3_L, Y3_H
.else
	vzip.32		Y0, X0
	vzip.32		Y1, X1
	vzip.32		Y2, X2
	vzip.32		Y3, X3
.endif

	// XOR the encrypted/decrypted blocks with the tweaks we saved earlier
	mov		r12, sp
	vld1.8		{TMP0, TMP1}, [r12:128]!
	vld1.8		{TMP2, TMP3}, [r12:128]!
	veor		X0, TMP0
	veor		Y0, TMP1
	veor		X1, TMP2
	veor		Y1, TMP3
	vld1.8		{TMP0, TMP1}, [r12:128]!
	vld1.8		{TMP2, TMP3}, [r12:128]!
	veor		X2, TMP0
	veor		Y2, TMP1
	veor		X3, TMP2
	veor		Y3, TMP3

	// Store the ciphertext in the destination buffer
	vst1.8		{X0, Y0}, [DST]!
	vst1.8		{X1, Y1}, [DST]!
	vst1.8		{X2, Y2}, [DST]!
	vst1.8		{X3, Y3}, [DST]!

	// Continue if there are more 128-byte chunks remaining, else return
	subs		NBYTES, #128
	bne		.Lnext_128bytes_\@

	// Store the next tweak
.if \n == 64
	vst1.8		{TWEAKV}, [TWEAK]
.else
	vst1.8		{TWEAKV_L}, [TWEAK]
.endif

	mov		sp, r7
	pop		{r4-r7}
	bx		lr
.endm

ENTRY(speck128_xts_encrypt_neon)
	_speck_xts_crypt	n=64, decrypting=0
ENDPROC(speck128_xts_encrypt_neon)

ENTRY(speck128_xts_decrypt_neon)
	_speck_xts_crypt	n=64, decrypting=1
ENDPROC(speck128_xts_decrypt_neon)

ENTRY(speck64_xts_encrypt_neon)
	_speck_xts_crypt	n=32, decrypting=0
ENDPROC(speck64_xts_encrypt_neon)

ENTRY(speck64_xts_decrypt_neon)
	_speck_xts_crypt	n=32, decrypting=1
ENDPROC(speck64_xts_decrypt_neon)
