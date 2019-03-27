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

#define S   br_aes_S

static void
add_round_key(unsigned *state, const uint32_t *skeys)
{
	int i;

	for (i = 0; i < 16; i += 4) {
		uint32_t k;

		k = *skeys ++;
		state[i + 0] ^= (unsigned)(k >> 24);
		state[i + 1] ^= (unsigned)(k >> 16) & 0xFF;
		state[i + 2] ^= (unsigned)(k >> 8) & 0xFF;
		state[i + 3] ^= (unsigned)k & 0xFF;
	}
}

static void
sub_bytes(unsigned *state)
{
	int i;

	for (i = 0; i < 16; i ++) {
		state[i] = S[state[i]];
	}
}

static void
shift_rows(unsigned *state)
{
	unsigned tmp;

	tmp = state[1];
	state[1] = state[5];
	state[5] = state[9];
	state[9] = state[13];
	state[13] = tmp;

	tmp = state[2];
	state[2] = state[10];
	state[10] = tmp;
	tmp = state[6];
	state[6] = state[14];
	state[14] = tmp;

	tmp = state[15];
	state[15] = state[11];
	state[11] = state[7];
	state[7] = state[3];
	state[3] = tmp;
}

static void
mix_columns(unsigned *state)
{
	int i;

	for (i = 0; i < 16; i += 4) {
		unsigned s0, s1, s2, s3;
		unsigned t0, t1, t2, t3;

		s0 = state[i + 0];
		s1 = state[i + 1];
		s2 = state[i + 2];
		s3 = state[i + 3];
		t0 = (s0 << 1) ^ s1 ^ (s1 << 1) ^ s2 ^ s3;
		t1 = s0 ^ (s1 << 1) ^ s2 ^ (s2 << 1) ^ s3;
		t2 = s0 ^ s1 ^ (s2 << 1) ^ s3 ^ (s3 << 1);
		t3 = s0 ^ (s0 << 1) ^ s1 ^ s2 ^ (s3 << 1);
		state[i + 0] = t0 ^ ((unsigned)(-(int)(t0 >> 8)) & 0x11B);
		state[i + 1] = t1 ^ ((unsigned)(-(int)(t1 >> 8)) & 0x11B);
		state[i + 2] = t2 ^ ((unsigned)(-(int)(t2 >> 8)) & 0x11B);
		state[i + 3] = t3 ^ ((unsigned)(-(int)(t3 >> 8)) & 0x11B);
	}
}

/* see inner.h */
void
br_aes_small_encrypt(unsigned num_rounds, const uint32_t *skey, void *data)
{
	unsigned char *buf;
	unsigned state[16];
	unsigned u;

	buf = data;
	for (u = 0; u < 16; u ++) {
		state[u] = buf[u];
	}
	add_round_key(state, skey);
	for (u = 1; u < num_rounds; u ++) {
		sub_bytes(state);
		shift_rows(state);
		mix_columns(state);
		add_round_key(state, skey + (u << 2));
	}
	sub_bytes(state);
	shift_rows(state);
	add_round_key(state, skey + (num_rounds << 2));
	for (u = 0; u < 16; u ++) {
		buf[u] = state[u];
	}
}
