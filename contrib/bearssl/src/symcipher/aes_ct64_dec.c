/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "inner.h"

/* see inner.h */
void
br_aes_ct64_bitslice_invSbox(uint64_t *q)
{
	/*
	 * See br_aes_ct_bitslice_invSbox(). This is the natural extension
	 * to 64-bit registers.
	 */
	uint64_t q0, q1, q2, q3, q4, q5, q6, q7;

	q0 = ~q[0];
	q1 = ~q[1];
	q2 = q[2];
	q3 = q[3];
	q4 = q[4];
	q5 = ~q[5];
	q6 = ~q[6];
	q7 = q[7];
	q[7] = q1 ^ q4 ^ q6;
	q[6] = q0 ^ q3 ^ q5;
	q[5] = q7 ^ q2 ^ q4;
	q[4] = q6 ^ q1 ^ q3;
	q[3] = q5 ^ q0 ^ q2;
	q[2] = q4 ^ q7 ^ q1;
	q[1] = q3 ^ q6 ^ q0;
	q[0] = q2 ^ q5 ^ q7;

	br_aes_ct64_bitslice_Sbox(q);

	q0 = ~q[0];
	q1 = ~q[1];
	q2 = q[2];
	q3 = q[3];
	q4 = q[4];
	q5 = ~q[5];
	q6 = ~q[6];
	q7 = q[7];
	q[7] = q1 ^ q4 ^ q6;
	q[6] = q0 ^ q3 ^ q5;
	q[5] = q7 ^ q2 ^ q4;
	q[4] = q6 ^ q1 ^ q3;
	q[3] = q5 ^ q0 ^ q2;
	q[2] = q4 ^ q7 ^ q1;
	q[1] = q3 ^ q6 ^ q0;
	q[0] = q2 ^ q5 ^ q7;
}

static void
add_round_key(uint64_t *q, const uint64_t *sk)
{
	int i;

	for (i = 0; i < 8; i ++) {
		q[i] ^= sk[i];
	}
}

static void
inv_shift_rows(uint64_t *q)
{
	int i;

	for (i = 0; i < 8; i ++) {
		uint64_t x;

		x = q[i];
		q[i] = (x & (uint64_t)0x000000000000FFFF)
			| ((x & (uint64_t)0x000000000FFF0000) << 4)
			| ((x & (uint64_t)0x00000000F0000000) >> 12)
			| ((x & (uint64_t)0x000000FF00000000) << 8)
			| ((x & (uint64_t)0x0000FF0000000000) >> 8)
			| ((x & (uint64_t)0x000F000000000000) << 12)
			| ((x & (uint64_t)0xFFF0000000000000) >> 4);
	}
}

static inline uint64_t
rotr32(uint64_t x)
{
	return (x << 32) | (x >> 32);
}

static void
inv_mix_columns(uint64_t *q)
{
	uint64_t q0, q1, q2, q3, q4, q5, q6, q7;
	uint64_t r0, r1, r2, r3, r4, r5, r6, r7;

	q0 = q[0];
	q1 = q[1];
	q2 = q[2];
	q3 = q[3];
	q4 = q[4];
	q5 = q[5];
	q6 = q[6];
	q7 = q[7];
	r0 = (q0 >> 16) | (q0 << 48);
	r1 = (q1 >> 16) | (q1 << 48);
	r2 = (q2 >> 16) | (q2 << 48);
	r3 = (q3 >> 16) | (q3 << 48);
	r4 = (q4 >> 16) | (q4 << 48);
	r5 = (q5 >> 16) | (q5 << 48);
	r6 = (q6 >> 16) | (q6 << 48);
	r7 = (q7 >> 16) | (q7 << 48);

	q[0] = q5 ^ q6 ^ q7 ^ r0 ^ r5 ^ r7 ^ rotr32(q0 ^ q5 ^ q6 ^ r0 ^ r5);
	q[1] = q0 ^ q5 ^ r0 ^ r1 ^ r5 ^ r6 ^ r7 ^ rotr32(q1 ^ q5 ^ q7 ^ r1 ^ r5 ^ r6);
	q[2] = q0 ^ q1 ^ q6 ^ r1 ^ r2 ^ r6 ^ r7 ^ rotr32(q0 ^ q2 ^ q6 ^ r2 ^ r6 ^ r7);
	q[3] = q0 ^ q1 ^ q2 ^ q5 ^ q6 ^ r0 ^ r2 ^ r3 ^ r5 ^ rotr32(q0 ^ q1 ^ q3 ^ q5 ^ q6 ^ q7 ^ r0 ^ r3 ^ r5 ^ r7);
	q[4] = q1 ^ q2 ^ q3 ^ q5 ^ r1 ^ r3 ^ r4 ^ r5 ^ r6 ^ r7 ^ rotr32(q1 ^ q2 ^ q4 ^ q5 ^ q7 ^ r1 ^ r4 ^ r5 ^ r6);
	q[5] = q2 ^ q3 ^ q4 ^ q6 ^ r2 ^ r4 ^ r5 ^ r6 ^ r7 ^ rotr32(q2 ^ q3 ^ q5 ^ q6 ^ r2 ^ r5 ^ r6 ^ r7);
	q[6] = q3 ^ q4 ^ q5 ^ q7 ^ r3 ^ r5 ^ r6 ^ r7 ^ rotr32(q3 ^ q4 ^ q6 ^ q7 ^ r3 ^ r6 ^ r7);
	q[7] = q4 ^ q5 ^ q6 ^ r4 ^ r6 ^ r7 ^ rotr32(q4 ^ q5 ^ q7 ^ r4 ^ r7);
}

/* see inner.h */
void
br_aes_ct64_bitslice_decrypt(unsigned num_rounds,
	const uint64_t *skey, uint64_t *q)
{
	unsigned u;

	add_round_key(q, skey + (num_rounds << 3));
	for (u = num_rounds - 1; u > 0; u --) {
		inv_shift_rows(q);
		br_aes_ct64_bitslice_invSbox(q);
		add_round_key(q, skey + (u << 3));
		inv_mix_columns(q);
	}
	inv_shift_rows(q);
	br_aes_ct64_bitslice_invSbox(q);
	add_round_key(q, skey);
}
