// SPDX-License-Identifier: GPL-2.0
/*
 * ARM64 NEON-accelerated implementation of Speck128-XTS and Speck64-XTS
 *
 * Copyright (c) 2018 Google, Inc
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */

#include <linux/linkage.h>

	.text

	// arguments
	ROUND_KEYS	.req	x0	// const {u64,u32} *round_keys
	NROUNDS		.req	w1	// int nrounds
	NROUNDS_X	.req	x1
	DST		.req	x2	// void *dst
	SRC		.req	x3	// const void *src
	NBYTES		.req	w4	// unsigned int nbytes
	TWEAK		.req	x5	// void *tweak

	// registers which hold the data being encrypted/decrypted
	// (underscores avoid a naming collision with ARM64 registers x0-x3)
	X_0		.req	v0
	Y_0		.req	v1
	X_1		.req	v2
	Y_1		.req	v3
	X_2		.req	v4
	Y_2		.req	v5
	X_3		.req	v6
	Y_3		.req	v7

	// the round key, duplicated in all lanes
	ROUND_KEY	.req	v8

	// index vector for tbl-based 8-bit rotates
	ROTATE_TABLE	.req	v9
	ROTATE_TABLE_Q	.req	q9

	// temporary registers
	TMP0		.req	v10
	TMP1		.req	v11
	TMP2		.req	v12
	TMP3		.req	v13

	// multiplication table for updating XTS tweaks
	GFMUL_TABLE	.req	v14
	GFMUL_TABLE_Q	.req	q14

	// next XTS tweak value(s)
	TWEAKV_NEXT	.req	v15

	// XTS tweaks for the blocks currently being encrypted/decrypted
	TWEAKV0		.req	v16
	TWEAKV1		.req	v17
	TWEAKV2		.req	v18
	TWEAKV3		.req	v19
	TWEAKV4		.req	v20
	TWEAKV5		.req	v21
	TWEAKV6		.req	v22
	TWEAKV7		.req	v23

	.align		4
.Lror64_8_table:
	.octa		0x080f0e0d0c0b0a090007060504030201
.Lror32_8_table:
	.octa		0x0c0f0e0d080b0a090407060500030201
.Lrol64_8_table:
	.octa		0x0e0d0c0b0a09080f0605040302010007
.Lrol32_8_table:
	.octa		0x0e0d0c0f0a09080b0605040702010003
.Lgf128mul_table:
	.octa		0x00000000000000870000000000000001
.Lgf64mul_table:
	.octa		0x0000000000000000000000002d361b00

/*
 * _speck_round_128bytes() - Speck encryption round on 128 bytes at a time
 *
 * Do one Speck encryption round on the 128 bytes (8 blocks for Speck128, 16 for
 * Speck64) stored in X0-X3 and Y0-Y3, using the round key stored in all lanes
 * of ROUND_KEY.  'n' is the lane size: 64 for Speck128, or 32 for Speck64.
 * 'lanes' is the lane specifier: "2d" for Speck128 or "4s" for Speck64.
 */
.macro _speck_round_128bytes	n, lanes

	// x = ror(x, 8)
	tbl		X_0.16b, {X_0.16b}, ROTATE_TABLE.16b
	tbl		X_1.16b, {X_1.16b}, ROTATE_TABLE.16b
	tbl		X_2.16b, {X_2.16b}, ROTATE_TABLE.16b
	tbl		X_3.16b, {X_3.16b}, ROTATE_TABLE.16b

	// x += y
	add		X_0.\lanes, X_0.\lanes, Y_0.\lanes
	add		X_1.\lanes, X_1.\lanes, Y_1.\lanes
	add		X_2.\lanes, X_2.\lanes, Y_2.\lanes
	add		X_3.\lanes, X_3.\lanes, Y_3.\lanes

	// x ^= k
	eor		X_0.16b, X_0.16b, ROUND_KEY.16b
	eor		X_1.16b, X_1.16b, ROUND_KEY.16b
	eor		X_2.16b, X_2.16b, ROUND_KEY.16b
	eor		X_3.16b, X_3.16b, ROUND_KEY.16b

	// y = rol(y, 3)
	shl		TMP0.\lanes, Y_0.\lanes, #3
	shl		TMP1.\lanes, Y_1.\lanes, #3
	shl		TMP2.\lanes, Y_2.\lanes, #3
	shl		TMP3.\lanes, Y_3.\lanes, #3
	sri		TMP0.\lanes, Y_0.\lanes, #(\n - 3)
	sri		TMP1.\lanes, Y_1.\lanes, #(\n - 3)
	sri		TMP2.\lanes, Y_2.\lanes, #(\n - 3)
	sri		TMP3.\lanes, Y_3.\lanes, #(\n - 3)

	// y ^= x
	eor		Y_0.16b, TMP0.16b, X_0.16b
	eor		Y_1.16b, TMP1.16b, X_1.16b
	eor		Y_2.16b, TMP2.16b, X_2.16b
	eor		Y_3.16b, TMP3.16b, X_3.16b
.endm

/*
 * _speck_unround_128bytes() - Speck decryption round on 128 bytes at a time
 *
 * This is the inverse of _speck_round_128bytes().
 */
.macro _speck_unround_128bytes	n, lanes

	// y ^= x
	eor		TMP0.16b, Y_0.16b, X_0.16b
	eor		TMP1.16b, Y_1.16b, X_1.16b
	eor		TMP2.16b, Y_2.16b, X_2.16b
	eor		TMP3.16b, Y_3.16b, X_3.16b

	// y = ror(y, 3)
	ushr		Y_0.\lanes, TMP0.\lanes, #3
	ushr		Y_1.\lanes, TMP1.\lanes, #3
	ushr		Y_2.\lanes, TMP2.\lanes, #3
	ushr		Y_3.\lanes, TMP3.\lanes, #3
	sli		Y_0.\lanes, TMP0.\lanes, #(\n - 3)
	sli		Y_1.\lanes, TMP1.\lanes, #(\n - 3)
	sli		Y_2.\lanes, TMP2.\lanes, #(\n - 3)
	sli		Y_3.\lanes, TMP3.\lanes, #(\n - 3)

	// x ^= k
	eor		X_0.16b, X_0.16b, ROUND_KEY.16b
	eor		X_1.16b, X_1.16b, ROUND_KEY.16b
	eor		X_2.16b, X_2.16b, ROUND_KEY.16b
	eor		X_3.16b, X_3.16b, ROUND_KEY.16b

	// x -= y
	sub		X_0.\lanes, X_0.\lanes, Y_0.\lanes
	sub		X_1.\lanes, X_1.\lanes, Y_1.\lanes
	sub		X_2.\lanes, X_2.\lanes, Y_2.\lanes
	sub		X_3.\lanes, X_3.\lanes, Y_3.\lanes

	// x = rol(x, 8)
	tbl		X_0.16b, {X_0.16b}, ROTATE_TABLE.16b
	tbl		X_1.16b, {X_1.16b}, ROTATE_TABLE.16b
	tbl		X_2.16b, {X_2.16b}, ROTATE_TABLE.16b
	tbl		X_3.16b, {X_3.16b}, ROTATE_TABLE.16b
.endm

.macro _next_xts_tweak	next, cur, tmp, n
.if \n == 64
	/*
	 * Calculate the next tweak by multiplying the current one by x,
	 * modulo p(x) = x^128 + x^7 + x^2 + x + 1.
	 */
	sshr		\tmp\().2d, \cur\().2d, #63
	and		\tmp\().16b, \tmp\().16b, GFMUL_TABLE.16b
	shl		\next\().2d, \cur\().2d, #1
	ext		\tmp\().16b, \tmp\().16b, \tmp\().16b, #8
	eor		\next\().16b, \next\().16b, \tmp\().16b
.else
	/*
	 * Calculate the next two tweaks by multiplying the current ones by x^2,
	 * modulo p(x) = x^64 + x^4 + x^3 + x + 1.
	 */
	ushr		\tmp\().2d, \cur\().2d, #62
	shl		\next\().2d, \cur\().2d, #2
	tbl		\tmp\().16b, {GFMUL_TABLE.16b}, \tmp\().16b
	eor		\next\().16b, \next\().16b, \tmp\().16b
.endif
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
.macro _speck_xts_crypt	n, lanes, decrypting

	/*
	 * If decrypting, modify the ROUND_KEYS parameter to point to the last
	 * round key rather than the first, since for decryption the round keys
	 * are used in reverse order.
	 */
.if \decrypting
	mov		NROUNDS, NROUNDS	/* zero the high 32 bits */
.if \n == 64
	add		ROUND_KEYS, ROUND_KEYS, NROUNDS_X, lsl #3
	sub		ROUND_KEYS, ROUND_KEYS, #8
.else
	add		ROUND_KEYS, ROUND_KEYS, NROUNDS_X, lsl #2
	sub		ROUND_KEYS, ROUND_KEYS, #4
.endif
.endif

	// Load the index vector for tbl-based 8-bit rotates
.if \decrypting
	ldr		ROTATE_TABLE_Q, .Lrol\n\()_8_table
.else
	ldr		ROTATE_TABLE_Q, .Lror\n\()_8_table
.endif

	// One-time XTS preparation
.if \n == 64
	// Load first tweak
	ld1		{TWEAKV0.16b}, [TWEAK]

	// Load GF(2^128) multiplication table
	ldr		GFMUL_TABLE_Q, .Lgf128mul_table
.else
	// Load first tweak
	ld1		{TWEAKV0.8b}, [TWEAK]

	// Load GF(2^64) multiplication table
	ldr		GFMUL_TABLE_Q, .Lgf64mul_table

	// Calculate second tweak, packing it together with the first
	ushr		TMP0.2d, TWEAKV0.2d, #63
	shl		TMP1.2d, TWEAKV0.2d, #1
	tbl		TMP0.8b, {GFMUL_TABLE.16b}, TMP0.8b
	eor		TMP0.8b, TMP0.8b, TMP1.8b
	mov		TWEAKV0.d[1], TMP0.d[0]
.endif

.Lnext_128bytes_\@:

	// Calculate XTS tweaks for next 128 bytes
	_next_xts_tweak	TWEAKV1, TWEAKV0, TMP0, \n
	_next_xts_tweak	TWEAKV2, TWEAKV1, TMP0, \n
	_next_xts_tweak	TWEAKV3, TWEAKV2, TMP0, \n
	_next_xts_tweak	TWEAKV4, TWEAKV3, TMP0, \n
	_next_xts_tweak	TWEAKV5, TWEAKV4, TMP0, \n
	_next_xts_tweak	TWEAKV6, TWEAKV5, TMP0, \n
	_next_xts_tweak	TWEAKV7, TWEAKV6, TMP0, \n
	_next_xts_tweak	TWEAKV_NEXT, TWEAKV7, TMP0, \n

	// Load the next source blocks into {X,Y}[0-3]
	ld1		{X_0.16b-Y_1.16b}, [SRC], #64
	ld1		{X_2.16b-Y_3.16b}, [SRC], #64

	// XOR the source blocks with their XTS tweaks
	eor		TMP0.16b, X_0.16b, TWEAKV0.16b
	eor		Y_0.16b,  Y_0.16b, TWEAKV1.16b
	eor		TMP1.16b, X_1.16b, TWEAKV2.16b
	eor		Y_1.16b,  Y_1.16b, TWEAKV3.16b
	eor		TMP2.16b, X_2.16b, TWEAKV4.16b
	eor		Y_2.16b,  Y_2.16b, TWEAKV5.16b
	eor		TMP3.16b, X_3.16b, TWEAKV6.16b
	eor		Y_3.16b,  Y_3.16b, TWEAKV7.16b

	/*
	 * De-interleave the 'x' and 'y' elements of each block, i.e. make it so
	 * that the X[0-3] registers contain only the second halves of blocks,
	 * and the Y[0-3] registers contain only the first halves of blocks.
	 * (Speck uses the order (y, x) rather than the more intuitive (x, y).)
	 */
	uzp2		X_0.\lanes, TMP0.\lanes, Y_0.\lanes
	uzp1		Y_0.\lanes, TMP0.\lanes, Y_0.\lanes
	uzp2		X_1.\lanes, TMP1.\lanes, Y_1.\lanes
	uzp1		Y_1.\lanes, TMP1.\lanes, Y_1.\lanes
	uzp2		X_2.\lanes, TMP2.\lanes, Y_2.\lanes
	uzp1		Y_2.\lanes, TMP2.\lanes, Y_2.\lanes
	uzp2		X_3.\lanes, TMP3.\lanes, Y_3.\lanes
	uzp1		Y_3.\lanes, TMP3.\lanes, Y_3.\lanes

	// Do the cipher rounds
	mov		x6, ROUND_KEYS
	mov		w7, NROUNDS
.Lnext_round_\@:
.if \decrypting
	ld1r		{ROUND_KEY.\lanes}, [x6]
	sub		x6, x6, #( \n / 8 )
	_speck_unround_128bytes	\n, \lanes
.else
	ld1r		{ROUND_KEY.\lanes}, [x6], #( \n / 8 )
	_speck_round_128bytes	\n, \lanes
.endif
	subs		w7, w7, #1
	bne		.Lnext_round_\@

	// Re-interleave the 'x' and 'y' elements of each block
	zip1		TMP0.\lanes, Y_0.\lanes, X_0.\lanes
	zip2		Y_0.\lanes,  Y_0.\lanes, X_0.\lanes
	zip1		TMP1.\lanes, Y_1.\lanes, X_1.\lanes
	zip2		Y_1.\lanes,  Y_1.\lanes, X_1.\lanes
	zip1		TMP2.\lanes, Y_2.\lanes, X_2.\lanes
	zip2		Y_2.\lanes,  Y_2.\lanes, X_2.\lanes
	zip1		TMP3.\lanes, Y_3.\lanes, X_3.\lanes
	zip2		Y_3.\lanes,  Y_3.\lanes, X_3.\lanes

	// XOR the encrypted/decrypted blocks with the tweaks calculated earlier
	eor		X_0.16b, TMP0.16b, TWEAKV0.16b
	eor		Y_0.16b, Y_0.16b,  TWEAKV1.16b
	eor		X_1.16b, TMP1.16b, TWEAKV2.16b
	eor		Y_1.16b, Y_1.16b,  TWEAKV3.16b
	eor		X_2.16b, TMP2.16b, TWEAKV4.16b
	eor		Y_2.16b, Y_2.16b,  TWEAKV5.16b
	eor		X_3.16b, TMP3.16b, TWEAKV6.16b
	eor		Y_3.16b, Y_3.16b,  TWEAKV7.16b
	mov		TWEAKV0.16b, TWEAKV_NEXT.16b

	// Store the ciphertext in the destination buffer
	st1		{X_0.16b-Y_1.16b}, [DST], #64
	st1		{X_2.16b-Y_3.16b}, [DST], #64

	// Continue if there are more 128-byte chunks remaining
	subs		NBYTES, NBYTES, #128
	bne		.Lnext_128bytes_\@

	// Store the next tweak and return
.if \n == 64
	st1		{TWEAKV_NEXT.16b}, [TWEAK]
.else
	st1		{TWEAKV_NEXT.8b}, [TWEAK]
.endif
	ret
.endm

ENTRY(speck128_xts_encrypt_neon)
	_speck_xts_crypt	n=64, lanes=2d, decrypting=0
ENDPROC(speck128_xts_encrypt_neon)

ENTRY(speck128_xts_decrypt_neon)
	_speck_xts_crypt	n=64, lanes=2d, decrypting=1
ENDPROC(speck128_xts_decrypt_neon)

ENTRY(speck64_xts_encrypt_neon)
	_speck_xts_crypt	n=32, lanes=4s, decrypting=0
ENDPROC(speck64_xts_encrypt_neon)

ENTRY(speck64_xts_decrypt_neon)
	_speck_xts_crypt	n=32, lanes=4s, decrypting=1
ENDPROC(speck64_xts_decrypt_neon)
