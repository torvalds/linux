// SPDX-License-Identifier: GPL-2.0

#include <linux/linkage.h>
#include <asm/cache.h>
#include <asm/assembler.h>

	.text

#define state0		v0
#define state1		v1
#define state2		v2
#define state3		v3
#define copy0		v4
#define copy0_q		q4
#define copy1		v5
#define copy2		v6
#define copy3		v7
#define copy3_d		d7
#define one_d		d16
#define one_q		q16
#define one_v		v16
#define tmp		v17
#define rot8		v18

/*
 * ARM64 ChaCha20 implementation meant for vDSO.  Produces a given positive
 * number of blocks of output with nonce 0, taking an input key and 8-bytes
 * counter.  Importantly does not spill to the stack.
 *
 * This implementation avoids d8-d15 because they are callee-save in user
 * space.
 *
 * void __arch_chacha20_blocks_nostack(uint8_t *dst_bytes,
 *				       const uint8_t *key,
 * 				       uint32_t *counter,
 *				       size_t nblocks)
 *
 * 	x0: output bytes
 *	x1: 32-byte key input
 *	x2: 8-byte counter input/output
 *	x3: number of 64-byte block to write to output
 */
SYM_FUNC_START(__arch_chacha20_blocks_nostack)

	/* copy0 = "expand 32-byte k" */
	mov_q		x8, 0x3320646e61707865
	mov_q		x9, 0x6b20657479622d32
	mov		copy0.d[0], x8
	mov		copy0.d[1], x9

	/* copy1,copy2 = key */
	ld1		{ copy1.4s, copy2.4s }, [x1]
	/* copy3 = counter || zero nonce  */
	ld1		{ copy3.2s }, [x2]

	movi		one_v.2s, #1
	uzp1		one_v.4s, one_v.4s, one_v.4s

.Lblock:
	/* copy state to auxiliary vectors for the final add after the permute.  */
	mov		state0.16b, copy0.16b
	mov		state1.16b, copy1.16b
	mov		state2.16b, copy2.16b
	mov		state3.16b, copy3.16b

	mov		w4, 20
.Lpermute:
	/*
	 * Permute one 64-byte block where the state matrix is stored in the four NEON
	 * registers state0-state3.  It performs matrix operations on four words in parallel,
	 * but requires shuffling to rearrange the words after each round.
	 */

.Ldoubleround:
	/* state0 += state1, state3 = rotl32(state3 ^ state0, 16) */
	add		state0.4s, state0.4s, state1.4s
	eor		state3.16b, state3.16b, state0.16b
	rev32		state3.8h, state3.8h

	/* state2 += state3, state1 = rotl32(state1 ^ state2, 12) */
	add		state2.4s, state2.4s, state3.4s
	eor		tmp.16b, state1.16b, state2.16b
	shl		state1.4s, tmp.4s, #12
	sri		state1.4s, tmp.4s, #20

	/* state0 += state1, state3 = rotl32(state3 ^ state0, 8) */
	add		state0.4s, state0.4s, state1.4s
	eor		tmp.16b, state3.16b, state0.16b
	shl		state3.4s, tmp.4s, #8
	sri		state3.4s, tmp.4s, #24

	/* state2 += state3, state1 = rotl32(state1 ^ state2, 7) */
	add		state2.4s, state2.4s, state3.4s
	eor		tmp.16b, state1.16b, state2.16b
	shl		state1.4s, tmp.4s, #7
	sri		state1.4s, tmp.4s, #25

	/* state1[0,1,2,3] = state1[1,2,3,0] */
	ext		state1.16b, state1.16b, state1.16b, #4
	/* state2[0,1,2,3] = state2[2,3,0,1] */
	ext		state2.16b, state2.16b, state2.16b, #8
	/* state3[0,1,2,3] = state3[1,2,3,0] */
	ext		state3.16b, state3.16b, state3.16b, #12

	/* state0 += state1, state3 = rotl32(state3 ^ state0, 16) */
	add		state0.4s, state0.4s, state1.4s
	eor		state3.16b, state3.16b, state0.16b
	rev32		state3.8h, state3.8h

	/* state2 += state3, state1 = rotl32(state1 ^ state2, 12) */
	add		state2.4s, state2.4s, state3.4s
	eor		tmp.16b, state1.16b, state2.16b
	shl		state1.4s, tmp.4s, #12
	sri		state1.4s, tmp.4s, #20

	/* state0 += state1, state3 = rotl32(state3 ^ state0, 8) */
	add		state0.4s, state0.4s, state1.4s
	eor		tmp.16b, state3.16b, state0.16b
	shl		state3.4s, tmp.4s, #8
	sri		state3.4s, tmp.4s, #24

	/* state2 += state3, state1 = rotl32(state1 ^ state2, 7) */
	add		state2.4s, state2.4s, state3.4s
	eor		tmp.16b, state1.16b, state2.16b
	shl		state1.4s, tmp.4s, #7
	sri		state1.4s, tmp.4s, #25

	/* state1[0,1,2,3] = state1[3,0,1,2] */
	ext		state1.16b, state1.16b, state1.16b, #12
	/* state2[0,1,2,3] = state2[2,3,0,1] */
	ext		state2.16b, state2.16b, state2.16b, #8
	/* state3[0,1,2,3] = state3[1,2,3,0] */
	ext		state3.16b, state3.16b, state3.16b, #4

	subs		w4, w4, #2
	b.ne		.Ldoubleround

	/* output0 = state0 + state0 */
	add		state0.4s, state0.4s, copy0.4s
	/* output1 = state1 + state1 */
	add		state1.4s, state1.4s, copy1.4s
	/* output2 = state2 + state2 */
	add		state2.4s, state2.4s, copy2.4s
	/* output2 = state3 + state3 */
	add		state3.4s, state3.4s, copy3.4s
	st1		{ state0.16b - state3.16b }, [x0]

	/*
	 * ++copy3.counter, the 'add' clears the upper half of the SIMD register
	 * which is the expected behaviour here.
	 */
	add		copy3_d, copy3_d, one_d

	/* output += 64, --nblocks */
	add		x0, x0, 64
	subs		x3, x3, #1
	b.ne		.Lblock

	/* counter = copy3.counter */
	st1		{ copy3.2s }, [x2]

	/* Zero out the potentially sensitive regs, in case nothing uses these again. */
	movi		state0.16b, #0
	movi		state1.16b, #0
	movi		state2.16b, #0
	movi		state3.16b, #0
	movi		copy1.16b, #0
	movi		copy2.16b, #0
	ret
SYM_FUNC_END(__arch_chacha20_blocks_nostack)

emit_aarch64_feature_1_and
