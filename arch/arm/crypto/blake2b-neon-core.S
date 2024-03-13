/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * BLAKE2b digest algorithm, NEON accelerated
 *
 * Copyright 2020 Google LLC
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */

#include <linux/linkage.h>

	.text
	.fpu		neon

	// The arguments to blake2b_compress_neon()
	STATE		.req	r0
	BLOCK		.req	r1
	NBLOCKS		.req	r2
	INC		.req	r3

	// Pointers to the rotation tables
	ROR24_TABLE	.req	r4
	ROR16_TABLE	.req	r5

	// The original stack pointer
	ORIG_SP		.req	r6

	// NEON registers which contain the message words of the current block.
	// M_0-M_3 are occasionally used for other purposes too.
	M_0		.req	d16
	M_1		.req	d17
	M_2		.req	d18
	M_3		.req	d19
	M_4		.req	d20
	M_5		.req	d21
	M_6		.req	d22
	M_7		.req	d23
	M_8		.req	d24
	M_9		.req	d25
	M_10		.req	d26
	M_11		.req	d27
	M_12		.req	d28
	M_13		.req	d29
	M_14		.req	d30
	M_15		.req	d31

	.align		4
	// Tables for computing ror64(x, 24) and ror64(x, 16) using the vtbl.8
	// instruction.  This is the most efficient way to implement these
	// rotation amounts with NEON.  (On Cortex-A53 it's the same speed as
	// vshr.u64 + vsli.u64, while on Cortex-A7 it's faster.)
.Lror24_table:
	.byte		3, 4, 5, 6, 7, 0, 1, 2
.Lror16_table:
	.byte		2, 3, 4, 5, 6, 7, 0, 1
	// The BLAKE2b initialization vector
.Lblake2b_IV:
	.quad		0x6a09e667f3bcc908, 0xbb67ae8584caa73b
	.quad		0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1
	.quad		0x510e527fade682d1, 0x9b05688c2b3e6c1f
	.quad		0x1f83d9abfb41bd6b, 0x5be0cd19137e2179

// Execute one round of BLAKE2b by updating the state matrix v[0..15] in the
// NEON registers q0-q7.  The message block is in q8..q15 (M_0-M_15).  The stack
// pointer points to a 32-byte aligned buffer containing a copy of q8 and q9
// (M_0-M_3), so that they can be reloaded if they are used as temporary
// registers.  The macro arguments s0-s15 give the order in which the message
// words are used in this round.  'final' is 1 if this is the final round.
.macro	_blake2b_round	s0, s1, s2, s3, s4, s5, s6, s7, \
			s8, s9, s10, s11, s12, s13, s14, s15, final=0

	// Mix the columns:
	// (v[0], v[4], v[8], v[12]), (v[1], v[5], v[9], v[13]),
	// (v[2], v[6], v[10], v[14]), and (v[3], v[7], v[11], v[15]).

	// a += b + m[blake2b_sigma[r][2*i + 0]];
	vadd.u64	q0, q0, q2
	vadd.u64	q1, q1, q3
	vadd.u64	d0, d0, M_\s0
	vadd.u64	d1, d1, M_\s2
	vadd.u64	d2, d2, M_\s4
	vadd.u64	d3, d3, M_\s6

	// d = ror64(d ^ a, 32);
	veor		q6, q6, q0
	veor		q7, q7, q1
	vrev64.32	q6, q6
	vrev64.32	q7, q7

	// c += d;
	vadd.u64	q4, q4, q6
	vadd.u64	q5, q5, q7

	// b = ror64(b ^ c, 24);
	vld1.8		{M_0}, [ROR24_TABLE, :64]
	veor		q2, q2, q4
	veor		q3, q3, q5
	vtbl.8		d4, {d4}, M_0
	vtbl.8		d5, {d5}, M_0
	vtbl.8		d6, {d6}, M_0
	vtbl.8		d7, {d7}, M_0

	// a += b + m[blake2b_sigma[r][2*i + 1]];
	//
	// M_0 got clobbered above, so we have to reload it if any of the four
	// message words this step needs happens to be M_0.  Otherwise we don't
	// need to reload it here, as it will just get clobbered again below.
.if \s1 == 0 || \s3 == 0 || \s5 == 0 || \s7 == 0
	vld1.8		{M_0}, [sp, :64]
.endif
	vadd.u64	q0, q0, q2
	vadd.u64	q1, q1, q3
	vadd.u64	d0, d0, M_\s1
	vadd.u64	d1, d1, M_\s3
	vadd.u64	d2, d2, M_\s5
	vadd.u64	d3, d3, M_\s7

	// d = ror64(d ^ a, 16);
	vld1.8		{M_0}, [ROR16_TABLE, :64]
	veor		q6, q6, q0
	veor		q7, q7, q1
	vtbl.8		d12, {d12}, M_0
	vtbl.8		d13, {d13}, M_0
	vtbl.8		d14, {d14}, M_0
	vtbl.8		d15, {d15}, M_0

	// c += d;
	vadd.u64	q4, q4, q6
	vadd.u64	q5, q5, q7

	// b = ror64(b ^ c, 63);
	//
	// This rotation amount isn't a multiple of 8, so it has to be
	// implemented using a pair of shifts, which requires temporary
	// registers.  Use q8-q9 (M_0-M_3) for this, and reload them afterwards.
	veor		q8, q2, q4
	veor		q9, q3, q5
	vshr.u64	q2, q8, #63
	vshr.u64	q3, q9, #63
	vsli.u64	q2, q8, #1
	vsli.u64	q3, q9, #1
	vld1.8		{q8-q9}, [sp, :256]

	// Mix the diagonals:
	// (v[0], v[5], v[10], v[15]), (v[1], v[6], v[11], v[12]),
	// (v[2], v[7], v[8], v[13]), and (v[3], v[4], v[9], v[14]).
	//
	// There are two possible ways to do this: use 'vext' instructions to
	// shift the rows of the matrix so that the diagonals become columns,
	// and undo it afterwards; or just use 64-bit operations on 'd'
	// registers instead of 128-bit operations on 'q' registers.  We use the
	// latter approach, as it performs much better on Cortex-A7.

	// a += b + m[blake2b_sigma[r][2*i + 0]];
	vadd.u64	d0, d0, d5
	vadd.u64	d1, d1, d6
	vadd.u64	d2, d2, d7
	vadd.u64	d3, d3, d4
	vadd.u64	d0, d0, M_\s8
	vadd.u64	d1, d1, M_\s10
	vadd.u64	d2, d2, M_\s12
	vadd.u64	d3, d3, M_\s14

	// d = ror64(d ^ a, 32);
	veor		d15, d15, d0
	veor		d12, d12, d1
	veor		d13, d13, d2
	veor		d14, d14, d3
	vrev64.32	d15, d15
	vrev64.32	d12, d12
	vrev64.32	d13, d13
	vrev64.32	d14, d14

	// c += d;
	vadd.u64	d10, d10, d15
	vadd.u64	d11, d11, d12
	vadd.u64	d8, d8, d13
	vadd.u64	d9, d9, d14

	// b = ror64(b ^ c, 24);
	vld1.8		{M_0}, [ROR24_TABLE, :64]
	veor		d5, d5, d10
	veor		d6, d6, d11
	veor		d7, d7, d8
	veor		d4, d4, d9
	vtbl.8		d5, {d5}, M_0
	vtbl.8		d6, {d6}, M_0
	vtbl.8		d7, {d7}, M_0
	vtbl.8		d4, {d4}, M_0

	// a += b + m[blake2b_sigma[r][2*i + 1]];
.if \s9 == 0 || \s11 == 0 || \s13 == 0 || \s15 == 0
	vld1.8		{M_0}, [sp, :64]
.endif
	vadd.u64	d0, d0, d5
	vadd.u64	d1, d1, d6
	vadd.u64	d2, d2, d7
	vadd.u64	d3, d3, d4
	vadd.u64	d0, d0, M_\s9
	vadd.u64	d1, d1, M_\s11
	vadd.u64	d2, d2, M_\s13
	vadd.u64	d3, d3, M_\s15

	// d = ror64(d ^ a, 16);
	vld1.8		{M_0}, [ROR16_TABLE, :64]
	veor		d15, d15, d0
	veor		d12, d12, d1
	veor		d13, d13, d2
	veor		d14, d14, d3
	vtbl.8		d12, {d12}, M_0
	vtbl.8		d13, {d13}, M_0
	vtbl.8		d14, {d14}, M_0
	vtbl.8		d15, {d15}, M_0

	// c += d;
	vadd.u64	d10, d10, d15
	vadd.u64	d11, d11, d12
	vadd.u64	d8, d8, d13
	vadd.u64	d9, d9, d14

	// b = ror64(b ^ c, 63);
	veor		d16, d4, d9
	veor		d17, d5, d10
	veor		d18, d6, d11
	veor		d19, d7, d8
	vshr.u64	q2, q8, #63
	vshr.u64	q3, q9, #63
	vsli.u64	q2, q8, #1
	vsli.u64	q3, q9, #1
	// Reloading q8-q9 can be skipped on the final round.
.if ! \final
	vld1.8		{q8-q9}, [sp, :256]
.endif
.endm

//
// void blake2b_compress_neon(struct blake2b_state *state,
//			      const u8 *block, size_t nblocks, u32 inc);
//
// Only the first three fields of struct blake2b_state are used:
//	u64 h[8];	(inout)
//	u64 t[2];	(inout)
//	u64 f[2];	(in)
//
	.align		5
ENTRY(blake2b_compress_neon)
	push		{r4-r10}

	// Allocate a 32-byte stack buffer that is 32-byte aligned.
	mov		ORIG_SP, sp
	sub		ip, sp, #32
	bic		ip, ip, #31
	mov		sp, ip

	adr		ROR24_TABLE, .Lror24_table
	adr		ROR16_TABLE, .Lror16_table

	mov		ip, STATE
	vld1.64		{q0-q1}, [ip]!		// Load h[0..3]
	vld1.64		{q2-q3}, [ip]!		// Load h[4..7]
.Lnext_block:
	  adr		r10, .Lblake2b_IV
	vld1.64		{q14-q15}, [ip]		// Load t[0..1] and f[0..1]
	vld1.64		{q4-q5}, [r10]!		// Load IV[0..3]
	  vmov		r7, r8, d28		// Copy t[0] to (r7, r8)
	vld1.64		{q6-q7}, [r10]		// Load IV[4..7]
	  adds		r7, r7, INC		// Increment counter
	bcs		.Lslow_inc_ctr
	vmov.i32	d28[0], r7
	vst1.64		{d28}, [ip]		// Update t[0]
.Linc_ctr_done:

	// Load the next message block and finish initializing the state matrix
	// 'v'.  Fortunately, there are exactly enough NEON registers to fit the
	// entire state matrix in q0-q7 and the entire message block in q8-15.
	//
	// However, _blake2b_round also needs some extra registers for rotates,
	// so we have to spill some registers.  It's better to spill the message
	// registers than the state registers, as the message doesn't change.
	// Therefore we store a copy of the first 32 bytes of the message block
	// (q8-q9) in an aligned buffer on the stack so that they can be
	// reloaded when needed.  (We could just reload directly from the
	// message buffer, but it's faster to use aligned loads.)
	vld1.8		{q8-q9}, [BLOCK]!
	  veor		q6, q6, q14	// v[12..13] = IV[4..5] ^ t[0..1]
	vld1.8		{q10-q11}, [BLOCK]!
	  veor		q7, q7, q15	// v[14..15] = IV[6..7] ^ f[0..1]
	vld1.8		{q12-q13}, [BLOCK]!
	vst1.8		{q8-q9}, [sp, :256]
	  mov		ip, STATE
	vld1.8		{q14-q15}, [BLOCK]!

	// Execute the rounds.  Each round is provided the order in which it
	// needs to use the message words.
	_blake2b_round	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	_blake2b_round	14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3
	_blake2b_round	11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4
	_blake2b_round	7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8
	_blake2b_round	9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13
	_blake2b_round	2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9
	_blake2b_round	12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11
	_blake2b_round	13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10
	_blake2b_round	6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5
	_blake2b_round	10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0
	_blake2b_round	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	_blake2b_round	14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 \
			final=1

	// Fold the final state matrix into the hash chaining value:
	//
	//	for (i = 0; i < 8; i++)
	//		h[i] ^= v[i] ^ v[i + 8];
	//
	  vld1.64	{q8-q9}, [ip]!		// Load old h[0..3]
	veor		q0, q0, q4		// v[0..1] ^= v[8..9]
	veor		q1, q1, q5		// v[2..3] ^= v[10..11]
	  vld1.64	{q10-q11}, [ip]		// Load old h[4..7]
	veor		q2, q2, q6		// v[4..5] ^= v[12..13]
	veor		q3, q3, q7		// v[6..7] ^= v[14..15]
	veor		q0, q0, q8		// v[0..1] ^= h[0..1]
	veor		q1, q1, q9		// v[2..3] ^= h[2..3]
	  mov		ip, STATE
	  subs		NBLOCKS, NBLOCKS, #1	// nblocks--
	  vst1.64	{q0-q1}, [ip]!		// Store new h[0..3]
	veor		q2, q2, q10		// v[4..5] ^= h[4..5]
	veor		q3, q3, q11		// v[6..7] ^= h[6..7]
	  vst1.64	{q2-q3}, [ip]!		// Store new h[4..7]

	// Advance to the next block, if there is one.
	bne		.Lnext_block		// nblocks != 0?

	mov		sp, ORIG_SP
	pop		{r4-r10}
	mov		pc, lr

.Lslow_inc_ctr:
	// Handle the case where the counter overflowed its low 32 bits, by
	// carrying the overflow bit into the full 128-bit counter.
	vmov		r9, r10, d29
	adcs		r8, r8, #0
	adcs		r9, r9, #0
	adc		r10, r10, #0
	vmov		d28, r7, r8
	vmov		d29, r9, r10
	vst1.64		{q14}, [ip]		// Update t[0] and t[1]
	b		.Linc_ctr_done
ENDPROC(blake2b_compress_neon)
