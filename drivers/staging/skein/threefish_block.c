// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include "threefish_api.h"

void threefish_encrypt_256(struct threefish_key *key_ctx, u64 *input,
			   u64 *output)
{
	u64 b0 = input[0], b1 = input[1],
	    b2 = input[2], b3 = input[3];
	u64 k0 = key_ctx->key[0], k1 = key_ctx->key[1],
	    k2 = key_ctx->key[2], k3 = key_ctx->key[3],
	    k4 = key_ctx->key[4];
	u64 t0 = key_ctx->tweak[0], t1 = key_ctx->tweak[1],
	    t2 = key_ctx->tweak[2];

	b1 += k1 + t0;
	b0 += b1 + k0;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k3;
	b2 += b3 + k2 + t1;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k2 + t1;
	b0 += b1 + k1;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k4 + 1;
	b2 += b3 + k3 + t2;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	b1 += k3 + t2;
	b0 += b1 + k2;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k0 + 2;
	b2 += b3 + k4 + t0;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k4 + t0;
	b0 += b1 + k3;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k1 + 3;
	b2 += b3 + k0 + t1;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	b1 += k0 + t1;
	b0 += b1 + k4;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k2 + 4;
	b2 += b3 + k1 + t2;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k1 + t2;
	b0 += b1 + k0;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k3 + 5;
	b2 += b3 + k2 + t0;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	b1 += k2 + t0;
	b0 += b1 + k1;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k4 + 6;
	b2 += b3 + k3 + t1;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k3 + t1;
	b0 += b1 + k2;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k0 + 7;
	b2 += b3 + k4 + t2;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	b1 += k4 + t2;
	b0 += b1 + k3;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k1 + 8;
	b2 += b3 + k0 + t0;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k0 + t0;
	b0 += b1 + k4;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k2 + 9;
	b2 += b3 + k1 + t1;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	b1 += k1 + t1;
	b0 += b1 + k0;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k3 + 10;
	b2 += b3 + k2 + t2;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k2 + t2;
	b0 += b1 + k1;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k4 + 11;
	b2 += b3 + k3 + t0;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	b1 += k3 + t0;
	b0 += b1 + k2;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k0 + 12;
	b2 += b3 + k4 + t1;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k4 + t1;
	b0 += b1 + k3;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k1 + 13;
	b2 += b3 + k0 + t2;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	b1 += k0 + t2;
	b0 += b1 + k4;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k2 + 14;
	b2 += b3 + k1 + t0;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k1 + t0;
	b0 += b1 + k0;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k3 + 15;
	b2 += b3 + k2 + t1;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	b1 += k2 + t1;
	b0 += b1 + k1;
	b1 = rol64(b1, 14) ^ b0;

	b3 += k4 + 16;
	b2 += b3 + k3 + t2;
	b3 = rol64(b3, 16) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 52) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 57) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 23) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 40) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 5) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 37) ^ b2;

	b1 += k3 + t2;
	b0 += b1 + k2;
	b1 = rol64(b1, 25) ^ b0;

	b3 += k0 + 17;
	b2 += b3 + k4 + t0;
	b3 = rol64(b3, 33) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 46) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 12) ^ b2;

	b0 += b1;
	b1 = rol64(b1, 58) ^ b0;

	b2 += b3;
	b3 = rol64(b3, 22) ^ b2;

	b0 += b3;
	b3 = rol64(b3, 32) ^ b0;

	b2 += b1;
	b1 = rol64(b1, 32) ^ b2;

	output[0] = b0 + k3;
	output[1] = b1 + k4 + t0;
	output[2] = b2 + k0 + t1;
	output[3] = b3 + k1 + 18;
}

void threefish_decrypt_256(struct threefish_key *key_ctx, u64 *input,
			   u64 *output)
{
	u64 b0 = input[0], b1 = input[1],
	    b2 = input[2], b3 = input[3];
	u64 k0 = key_ctx->key[0], k1 = key_ctx->key[1],
	    k2 = key_ctx->key[2], k3 = key_ctx->key[3],
	    k4 = key_ctx->key[4];
	u64 t0 = key_ctx->tweak[0], t1 = key_ctx->tweak[1],
	    t2 = key_ctx->tweak[2];

	u64 tmp;

	b0 -= k3;
	b1 -= k4 + t0;
	b2 -= k0 + t1;
	b3 -= k1 + 18;
	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k2;
	b1 -= k3 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k4 + t0;
	b3 -= k0 + 17;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k1;
	b1 -= k2 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k3 + t2;
	b3 -= k4 + 16;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k0;
	b1 -= k1 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k2 + t1;
	b3 -= k3 + 15;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k4;
	b1 -= k0 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k1 + t0;
	b3 -= k2 + 14;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k3;
	b1 -= k4 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k0 + t2;
	b3 -= k1 + 13;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k2;
	b1 -= k3 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k4 + t1;
	b3 -= k0 + 12;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k1;
	b1 -= k2 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k3 + t0;
	b3 -= k4 + 11;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k0;
	b1 -= k1 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k2 + t2;
	b3 -= k3 + 10;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k4;
	b1 -= k0 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k1 + t1;
	b3 -= k2 + 9;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k3;
	b1 -= k4 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k0 + t0;
	b3 -= k1 + 8;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k2;
	b1 -= k3 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k4 + t2;
	b3 -= k0 + 7;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k1;
	b1 -= k2 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k3 + t1;
	b3 -= k4 + 6;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k0;
	b1 -= k1 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k2 + t0;
	b3 -= k3 + 5;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k4;
	b1 -= k0 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k1 + t2;
	b3 -= k2 + 4;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k3;
	b1 -= k4 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k0 + t1;
	b3 -= k1 + 3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k2;
	b1 -= k3 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k4 + t0;
	b3 -= k0 + 2;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 32);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 32);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 58);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 22);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 46);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 12);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 25);
	b0 -= b1 + k1;
	b1 -= k2 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 33);
	b2 -= b3 + k3 + t2;
	b3 -= k4 + 1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 5);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 37);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 23);
	b0 -= b1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 40);
	b2 -= b3;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 52);
	b0 -= b3;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 57);
	b2 -= b1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 14);
	b0 -= b1 + k0;
	b1 -= k1 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 16);
	b2 -= b3 + k2 + t1;
	b3 -= k3;

	output[0] = b0;
	output[1] = b1;
	output[2] = b2;
	output[3] = b3;
}

void threefish_encrypt_512(struct threefish_key *key_ctx, u64 *input,
			   u64 *output)
{
	u64 b0 = input[0], b1 = input[1],
	    b2 = input[2], b3 = input[3],
	    b4 = input[4], b5 = input[5],
	    b6 = input[6], b7 = input[7];
	u64 k0 = key_ctx->key[0], k1 = key_ctx->key[1],
	    k2 = key_ctx->key[2], k3 = key_ctx->key[3],
	    k4 = key_ctx->key[4], k5 = key_ctx->key[5],
	    k6 = key_ctx->key[6], k7 = key_ctx->key[7],
	    k8 = key_ctx->key[8];
	u64 t0 = key_ctx->tweak[0], t1 = key_ctx->tweak[1],
	    t2 = key_ctx->tweak[2];

	b1 += k1;
	b0 += b1 + k0;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k3;
	b2 += b3 + k2;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k5 + t0;
	b4 += b5 + k4;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k7;
	b6 += b7 + k6 + t1;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k2;
	b0 += b1 + k1;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k4;
	b2 += b3 + k3;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k6 + t1;
	b4 += b5 + k5;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k8 + 1;
	b6 += b7 + k7 + t2;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	b1 += k3;
	b0 += b1 + k2;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k5;
	b2 += b3 + k4;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k7 + t2;
	b4 += b5 + k6;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k0 + 2;
	b6 += b7 + k8 + t0;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k4;
	b0 += b1 + k3;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k6;
	b2 += b3 + k5;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k8 + t0;
	b4 += b5 + k7;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k1 + 3;
	b6 += b7 + k0 + t1;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	b1 += k5;
	b0 += b1 + k4;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k7;
	b2 += b3 + k6;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k0 + t1;
	b4 += b5 + k8;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k2 + 4;
	b6 += b7 + k1 + t2;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k6;
	b0 += b1 + k5;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k8;
	b2 += b3 + k7;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k1 + t2;
	b4 += b5 + k0;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k3 + 5;
	b6 += b7 + k2 + t0;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	b1 += k7;
	b0 += b1 + k6;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k0;
	b2 += b3 + k8;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k2 + t0;
	b4 += b5 + k1;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k4 + 6;
	b6 += b7 + k3 + t1;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k8;
	b0 += b1 + k7;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k1;
	b2 += b3 + k0;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k3 + t1;
	b4 += b5 + k2;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k5 + 7;
	b6 += b7 + k4 + t2;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	b1 += k0;
	b0 += b1 + k8;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k2;
	b2 += b3 + k1;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k4 + t2;
	b4 += b5 + k3;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k6 + 8;
	b6 += b7 + k5 + t0;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k1;
	b0 += b1 + k0;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k3;
	b2 += b3 + k2;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k5 + t0;
	b4 += b5 + k4;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k7 + 9;
	b6 += b7 + k6 + t1;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	b1 += k2;
	b0 += b1 + k1;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k4;
	b2 += b3 + k3;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k6 + t1;
	b4 += b5 + k5;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k8 + 10;
	b6 += b7 + k7 + t2;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k3;
	b0 += b1 + k2;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k5;
	b2 += b3 + k4;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k7 + t2;
	b4 += b5 + k6;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k0 + 11;
	b6 += b7 + k8 + t0;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	b1 += k4;
	b0 += b1 + k3;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k6;
	b2 += b3 + k5;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k8 + t0;
	b4 += b5 + k7;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k1 + 12;
	b6 += b7 + k0 + t1;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k5;
	b0 += b1 + k4;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k7;
	b2 += b3 + k6;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k0 + t1;
	b4 += b5 + k8;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k2 + 13;
	b6 += b7 + k1 + t2;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	b1 += k6;
	b0 += b1 + k5;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k8;
	b2 += b3 + k7;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k1 + t2;
	b4 += b5 + k0;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k3 + 14;
	b6 += b7 + k2 + t0;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k7;
	b0 += b1 + k6;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k0;
	b2 += b3 + k8;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k2 + t0;
	b4 += b5 + k1;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k4 + 15;
	b6 += b7 + k3 + t1;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	b1 += k8;
	b0 += b1 + k7;
	b1 = rol64(b1, 46) ^ b0;

	b3 += k1;
	b2 += b3 + k0;
	b3 = rol64(b3, 36) ^ b2;

	b5 += k3 + t1;
	b4 += b5 + k2;
	b5 = rol64(b5, 19) ^ b4;

	b7 += k5 + 16;
	b6 += b7 + k4 + t2;
	b7 = rol64(b7, 37) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 33) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 27) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 14) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 42) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 17) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 49) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 36) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 39) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 44) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 9) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 54) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 56) ^ b4;

	b1 += k0;
	b0 += b1 + k8;
	b1 = rol64(b1, 39) ^ b0;

	b3 += k2;
	b2 += b3 + k1;
	b3 = rol64(b3, 30) ^ b2;

	b5 += k4 + t2;
	b4 += b5 + k3;
	b5 = rol64(b5, 34) ^ b4;

	b7 += k6 + 17;
	b6 += b7 + k5 + t0;
	b7 = rol64(b7, 24) ^ b6;

	b2 += b1;
	b1 = rol64(b1, 13) ^ b2;

	b4 += b7;
	b7 = rol64(b7, 50) ^ b4;

	b6 += b5;
	b5 = rol64(b5, 10) ^ b6;

	b0 += b3;
	b3 = rol64(b3, 17) ^ b0;

	b4 += b1;
	b1 = rol64(b1, 25) ^ b4;

	b6 += b3;
	b3 = rol64(b3, 29) ^ b6;

	b0 += b5;
	b5 = rol64(b5, 39) ^ b0;

	b2 += b7;
	b7 = rol64(b7, 43) ^ b2;

	b6 += b1;
	b1 = rol64(b1, 8) ^ b6;

	b0 += b7;
	b7 = rol64(b7, 35) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 56) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 22) ^ b4;

	output[0] = b0 + k0;
	output[1] = b1 + k1;
	output[2] = b2 + k2;
	output[3] = b3 + k3;
	output[4] = b4 + k4;
	output[5] = b5 + k5 + t0;
	output[6] = b6 + k6 + t1;
	output[7] = b7 + k7 + 18;
}

void threefish_decrypt_512(struct threefish_key *key_ctx, u64 *input,
			   u64 *output)
{
	u64 b0 = input[0], b1 = input[1],
	    b2 = input[2], b3 = input[3],
	    b4 = input[4], b5 = input[5],
	    b6 = input[6], b7 = input[7];
	u64 k0 = key_ctx->key[0], k1 = key_ctx->key[1],
	    k2 = key_ctx->key[2], k3 = key_ctx->key[3],
	    k4 = key_ctx->key[4], k5 = key_ctx->key[5],
	    k6 = key_ctx->key[6], k7 = key_ctx->key[7],
	    k8 = key_ctx->key[8];
	u64 t0 = key_ctx->tweak[0], t1 = key_ctx->tweak[1],
	    t2 = key_ctx->tweak[2];

	u64 tmp;

	b0 -= k0;
	b1 -= k1;
	b2 -= k2;
	b3 -= k3;
	b4 -= k4;
	b5 -= k5 + t0;
	b6 -= k6 + t1;
	b7 -= k7 + 18;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k5 + t0;
	b7 -= k6 + 17;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k3;
	b5 -= k4 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k1;
	b3 -= k2;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k8;
	b1 -= k0;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k4 + t2;
	b7 -= k5 + 16;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k2;
	b5 -= k3 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k0;
	b3 -= k1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k7;
	b1 -= k8;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k3 + t1;
	b7 -= k4 + 15;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k1;
	b5 -= k2 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k8;
	b3 -= k0;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k6;
	b1 -= k7;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k2 + t0;
	b7 -= k3 + 14;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k0;
	b5 -= k1 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k7;
	b3 -= k8;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k5;
	b1 -= k6;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k1 + t2;
	b7 -= k2 + 13;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k8;
	b5 -= k0 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k6;
	b3 -= k7;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k4;
	b1 -= k5;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k0 + t1;
	b7 -= k1 + 12;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k7;
	b5 -= k8 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k5;
	b3 -= k6;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k3;
	b1 -= k4;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k8 + t0;
	b7 -= k0 + 11;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k6;
	b5 -= k7 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k4;
	b3 -= k5;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k2;
	b1 -= k3;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k7 + t2;
	b7 -= k8 + 10;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k5;
	b5 -= k6 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k3;
	b3 -= k4;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k1;
	b1 -= k2;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k6 + t1;
	b7 -= k7 + 9;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k4;
	b5 -= k5 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k2;
	b3 -= k3;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k0;
	b1 -= k1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k5 + t0;
	b7 -= k6 + 8;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k3;
	b5 -= k4 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k1;
	b3 -= k2;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k8;
	b1 -= k0;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k4 + t2;
	b7 -= k5 + 7;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k2;
	b5 -= k3 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k0;
	b3 -= k1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k7;
	b1 -= k8;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k3 + t1;
	b7 -= k4 + 6;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k1;
	b5 -= k2 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k8;
	b3 -= k0;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k6;
	b1 -= k7;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k2 + t0;
	b7 -= k3 + 5;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k0;
	b5 -= k1 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k7;
	b3 -= k8;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k5;
	b1 -= k6;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k1 + t2;
	b7 -= k2 + 4;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k8;
	b5 -= k0 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k6;
	b3 -= k7;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k4;
	b1 -= k5;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k0 + t1;
	b7 -= k1 + 3;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k7;
	b5 -= k8 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k5;
	b3 -= k6;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k3;
	b1 -= k4;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k8 + t0;
	b7 -= k0 + 2;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k6;
	b5 -= k7 + t2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k4;
	b3 -= k5;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k2;
	b1 -= k3;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 22);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 56);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 35);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 8);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 43);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 39);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 29);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 25);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 17);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 10);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 50);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 13);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 24);
	b6 -= b7 + k7 + t2;
	b7 -= k8 + 1;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 34);
	b4 -= b5 + k5;
	b5 -= k6 + t1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 30);
	b2 -= b3 + k3;
	b3 -= k4;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 39);
	b0 -= b1 + k1;
	b1 -= k2;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 56);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 54);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 9);
	b0 -= b7;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 44);
	b6 -= b1;

	tmp = b7 ^ b2;
	b7 = ror64(tmp, 39);
	b2 -= b7;

	tmp = b5 ^ b0;
	b5 = ror64(tmp, 36);
	b0 -= b5;

	tmp = b3 ^ b6;
	b3 = ror64(tmp, 49);
	b6 -= b3;

	tmp = b1 ^ b4;
	b1 = ror64(tmp, 17);
	b4 -= b1;

	tmp = b3 ^ b0;
	b3 = ror64(tmp, 42);
	b0 -= b3;

	tmp = b5 ^ b6;
	b5 = ror64(tmp, 14);
	b6 -= b5;

	tmp = b7 ^ b4;
	b7 = ror64(tmp, 27);
	b4 -= b7;

	tmp = b1 ^ b2;
	b1 = ror64(tmp, 33);
	b2 -= b1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 37);
	b6 -= b7 + k6 + t1;
	b7 -= k7;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 19);
	b4 -= b5 + k4;
	b5 -= k5 + t0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 36);
	b2 -= b3 + k2;
	b3 -= k3;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 46);
	b0 -= b1 + k0;
	b1 -= k1;

	output[0] = b0;
	output[1] = b1;
	output[2] = b2;
	output[3] = b3;

	output[7] = b7;
	output[6] = b6;
	output[5] = b5;
	output[4] = b4;
}

void threefish_encrypt_1024(struct threefish_key *key_ctx, u64 *input,
			    u64 *output)
{
	u64 b0 = input[0], b1 = input[1],
	    b2 = input[2], b3 = input[3],
	    b4 = input[4], b5 = input[5],
	    b6 = input[6], b7 = input[7],
	    b8 = input[8], b9 = input[9],
	    b10 = input[10], b11 = input[11],
	    b12 = input[12], b13 = input[13],
	    b14 = input[14], b15 = input[15];
	u64 k0 = key_ctx->key[0], k1 = key_ctx->key[1],
	    k2 = key_ctx->key[2], k3 = key_ctx->key[3],
	    k4 = key_ctx->key[4], k5 = key_ctx->key[5],
	    k6 = key_ctx->key[6], k7 = key_ctx->key[7],
	    k8 = key_ctx->key[8], k9 = key_ctx->key[9],
	    k10 = key_ctx->key[10], k11 = key_ctx->key[11],
	    k12 = key_ctx->key[12], k13 = key_ctx->key[13],
	    k14 = key_ctx->key[14], k15 = key_ctx->key[15],
	    k16 = key_ctx->key[16];
	u64 t0 = key_ctx->tweak[0], t1 = key_ctx->tweak[1],
	    t2 = key_ctx->tweak[2];

	b1 += k1;
	b0 += b1 + k0;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k3;
	b2 += b3 + k2;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k5;
	b4 += b5 + k4;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k7;
	b6 += b7 + k6;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k9;
	b8 += b9 + k8;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k11;
	b10 += b11 + k10;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k13 + t0;
	b12 += b13 + k12;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k15;
	b14 += b15 + k14 + t1;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k2;
	b0 += b1 + k1;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k4;
	b2 += b3 + k3;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k6;
	b4 += b5 + k5;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k8;
	b6 += b7 + k7;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k10;
	b8 += b9 + k9;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k12;
	b10 += b11 + k11;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k14 + t1;
	b12 += b13 + k13;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k16 + 1;
	b14 += b15 + k15 + t2;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k3;
	b0 += b1 + k2;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k5;
	b2 += b3 + k4;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k7;
	b4 += b5 + k6;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k9;
	b6 += b7 + k8;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k11;
	b8 += b9 + k10;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k13;
	b10 += b11 + k12;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k15 + t2;
	b12 += b13 + k14;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k0 + 2;
	b14 += b15 + k16 + t0;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k4;
	b0 += b1 + k3;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k6;
	b2 += b3 + k5;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k8;
	b4 += b5 + k7;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k10;
	b6 += b7 + k9;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k12;
	b8 += b9 + k11;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k14;
	b10 += b11 + k13;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k16 + t0;
	b12 += b13 + k15;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k1 + 3;
	b14 += b15 + k0 + t1;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k5;
	b0 += b1 + k4;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k7;
	b2 += b3 + k6;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k9;
	b4 += b5 + k8;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k11;
	b6 += b7 + k10;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k13;
	b8 += b9 + k12;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k15;
	b10 += b11 + k14;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k0 + t1;
	b12 += b13 + k16;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k2 + 4;
	b14 += b15 + k1 + t2;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k6;
	b0 += b1 + k5;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k8;
	b2 += b3 + k7;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k10;
	b4 += b5 + k9;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k12;
	b6 += b7 + k11;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k14;
	b8 += b9 + k13;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k16;
	b10 += b11 + k15;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k1 + t2;
	b12 += b13 + k0;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k3 + 5;
	b14 += b15 + k2 + t0;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k7;
	b0 += b1 + k6;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k9;
	b2 += b3 + k8;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k11;
	b4 += b5 + k10;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k13;
	b6 += b7 + k12;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k15;
	b8 += b9 + k14;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k0;
	b10 += b11 + k16;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k2 + t0;
	b12 += b13 + k1;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k4 + 6;
	b14 += b15 + k3 + t1;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k8;
	b0 += b1 + k7;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k10;
	b2 += b3 + k9;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k12;
	b4 += b5 + k11;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k14;
	b6 += b7 + k13;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k16;
	b8 += b9 + k15;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k1;
	b10 += b11 + k0;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k3 + t1;
	b12 += b13 + k2;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k5 + 7;
	b14 += b15 + k4 + t2;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k9;
	b0 += b1 + k8;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k11;
	b2 += b3 + k10;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k13;
	b4 += b5 + k12;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k15;
	b6 += b7 + k14;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k0;
	b8 += b9 + k16;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k2;
	b10 += b11 + k1;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k4 + t2;
	b12 += b13 + k3;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k6 + 8;
	b14 += b15 + k5 + t0;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k10;
	b0 += b1 + k9;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k12;
	b2 += b3 + k11;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k14;
	b4 += b5 + k13;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k16;
	b6 += b7 + k15;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k1;
	b8 += b9 + k0;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k3;
	b10 += b11 + k2;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k5 + t0;
	b12 += b13 + k4;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k7 + 9;
	b14 += b15 + k6 + t1;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k11;
	b0 += b1 + k10;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k13;
	b2 += b3 + k12;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k15;
	b4 += b5 + k14;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k0;
	b6 += b7 + k16;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k2;
	b8 += b9 + k1;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k4;
	b10 += b11 + k3;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k6 + t1;
	b12 += b13 + k5;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k8 + 10;
	b14 += b15 + k7 + t2;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k12;
	b0 += b1 + k11;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k14;
	b2 += b3 + k13;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k16;
	b4 += b5 + k15;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k1;
	b6 += b7 + k0;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k3;
	b8 += b9 + k2;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k5;
	b10 += b11 + k4;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k7 + t2;
	b12 += b13 + k6;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k9 + 11;
	b14 += b15 + k8 + t0;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k13;
	b0 += b1 + k12;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k15;
	b2 += b3 + k14;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k0;
	b4 += b5 + k16;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k2;
	b6 += b7 + k1;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k4;
	b8 += b9 + k3;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k6;
	b10 += b11 + k5;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k8 + t0;
	b12 += b13 + k7;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k10 + 12;
	b14 += b15 + k9 + t1;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k14;
	b0 += b1 + k13;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k16;
	b2 += b3 + k15;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k1;
	b4 += b5 + k0;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k3;
	b6 += b7 + k2;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k5;
	b8 += b9 + k4;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k7;
	b10 += b11 + k6;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k9 + t1;
	b12 += b13 + k8;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k11 + 13;
	b14 += b15 + k10 + t2;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k15;
	b0 += b1 + k14;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k0;
	b2 += b3 + k16;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k2;
	b4 += b5 + k1;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k4;
	b6 += b7 + k3;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k6;
	b8 += b9 + k5;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k8;
	b10 += b11 + k7;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k10 + t2;
	b12 += b13 + k9;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k12 + 14;
	b14 += b15 + k11 + t0;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k16;
	b0 += b1 + k15;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k1;
	b2 += b3 + k0;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k3;
	b4 += b5 + k2;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k5;
	b6 += b7 + k4;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k7;
	b8 += b9 + k6;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k9;
	b10 += b11 + k8;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k11 + t0;
	b12 += b13 + k10;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k13 + 15;
	b14 += b15 + k12 + t1;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k0;
	b0 += b1 + k16;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k2;
	b2 += b3 + k1;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k4;
	b4 += b5 + k3;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k6;
	b6 += b7 + k5;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k8;
	b8 += b9 + k7;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k10;
	b10 += b11 + k9;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k12 + t1;
	b12 += b13 + k11;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k14 + 16;
	b14 += b15 + k13 + t2;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k1;
	b0 += b1 + k0;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k3;
	b2 += b3 + k2;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k5;
	b4 += b5 + k4;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k7;
	b6 += b7 + k6;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k9;
	b8 += b9 + k8;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k11;
	b10 += b11 + k10;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k13 + t2;
	b12 += b13 + k12;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k15 + 17;
	b14 += b15 + k14 + t0;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	b1 += k2;
	b0 += b1 + k1;
	b1 = rol64(b1, 24) ^ b0;

	b3 += k4;
	b2 += b3 + k3;
	b3 = rol64(b3, 13) ^ b2;

	b5 += k6;
	b4 += b5 + k5;
	b5 = rol64(b5, 8) ^ b4;

	b7 += k8;
	b6 += b7 + k7;
	b7 = rol64(b7, 47) ^ b6;

	b9 += k10;
	b8 += b9 + k9;
	b9 = rol64(b9, 8) ^ b8;

	b11 += k12;
	b10 += b11 + k11;
	b11 = rol64(b11, 17) ^ b10;

	b13 += k14 + t0;
	b12 += b13 + k13;
	b13 = rol64(b13, 22) ^ b12;

	b15 += k16 + 18;
	b14 += b15 + k15 + t1;
	b15 = rol64(b15, 37) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 38) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 19) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 10) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 55) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 49) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 18) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 23) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 52) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 33) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 4) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 51) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 13) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 34) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 41) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 59) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 17) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 5) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 20) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 48) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 41) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 47) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 28) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 16) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 25) ^ b12;

	b1 += k3;
	b0 += b1 + k2;
	b1 = rol64(b1, 41) ^ b0;

	b3 += k5;
	b2 += b3 + k4;
	b3 = rol64(b3, 9) ^ b2;

	b5 += k7;
	b4 += b5 + k6;
	b5 = rol64(b5, 37) ^ b4;

	b7 += k9;
	b6 += b7 + k8;
	b7 = rol64(b7, 31) ^ b6;

	b9 += k11;
	b8 += b9 + k10;
	b9 = rol64(b9, 12) ^ b8;

	b11 += k13;
	b10 += b11 + k12;
	b11 = rol64(b11, 47) ^ b10;

	b13 += k15 + t1;
	b12 += b13 + k14;
	b13 = rol64(b13, 44) ^ b12;

	b15 += k0 + 19;
	b14 += b15 + k16 + t2;
	b15 = rol64(b15, 30) ^ b14;

	b0 += b9;
	b9 = rol64(b9, 16) ^ b0;

	b2 += b13;
	b13 = rol64(b13, 34) ^ b2;

	b6 += b11;
	b11 = rol64(b11, 56) ^ b6;

	b4 += b15;
	b15 = rol64(b15, 51) ^ b4;

	b10 += b7;
	b7 = rol64(b7, 4) ^ b10;

	b12 += b3;
	b3 = rol64(b3, 53) ^ b12;

	b14 += b5;
	b5 = rol64(b5, 42) ^ b14;

	b8 += b1;
	b1 = rol64(b1, 41) ^ b8;

	b0 += b7;
	b7 = rol64(b7, 31) ^ b0;

	b2 += b5;
	b5 = rol64(b5, 44) ^ b2;

	b4 += b3;
	b3 = rol64(b3, 47) ^ b4;

	b6 += b1;
	b1 = rol64(b1, 46) ^ b6;

	b12 += b15;
	b15 = rol64(b15, 19) ^ b12;

	b14 += b13;
	b13 = rol64(b13, 42) ^ b14;

	b8 += b11;
	b11 = rol64(b11, 44) ^ b8;

	b10 += b9;
	b9 = rol64(b9, 25) ^ b10;

	b0 += b15;
	b15 = rol64(b15, 9) ^ b0;

	b2 += b11;
	b11 = rol64(b11, 48) ^ b2;

	b6 += b13;
	b13 = rol64(b13, 35) ^ b6;

	b4 += b9;
	b9 = rol64(b9, 52) ^ b4;

	b14 += b1;
	b1 = rol64(b1, 23) ^ b14;

	b8 += b5;
	b5 = rol64(b5, 31) ^ b8;

	b10 += b3;
	b3 = rol64(b3, 37) ^ b10;

	b12 += b7;
	b7 = rol64(b7, 20) ^ b12;

	output[0] = b0 + k3;
	output[1] = b1 + k4;
	output[2] = b2 + k5;
	output[3] = b3 + k6;
	output[4] = b4 + k7;
	output[5] = b5 + k8;
	output[6] = b6 + k9;
	output[7] = b7 + k10;
	output[8] = b8 + k11;
	output[9] = b9 + k12;
	output[10] = b10 + k13;
	output[11] = b11 + k14;
	output[12] = b12 + k15;
	output[13] = b13 + k16 + t2;
	output[14] = b14 + k0 + t0;
	output[15] = b15 + k1 + 20;
}

void threefish_decrypt_1024(struct threefish_key *key_ctx, u64 *input,
			    u64 *output)
{
	u64 b0 = input[0], b1 = input[1],
	    b2 = input[2], b3 = input[3],
	    b4 = input[4], b5 = input[5],
	    b6 = input[6], b7 = input[7],
	    b8 = input[8], b9 = input[9],
	    b10 = input[10], b11 = input[11],
	    b12 = input[12], b13 = input[13],
	    b14 = input[14], b15 = input[15];
	u64 k0 = key_ctx->key[0], k1 = key_ctx->key[1],
	    k2 = key_ctx->key[2], k3 = key_ctx->key[3],
	    k4 = key_ctx->key[4], k5 = key_ctx->key[5],
	    k6 = key_ctx->key[6], k7 = key_ctx->key[7],
	    k8 = key_ctx->key[8], k9 = key_ctx->key[9],
	    k10 = key_ctx->key[10], k11 = key_ctx->key[11],
	    k12 = key_ctx->key[12], k13 = key_ctx->key[13],
	    k14 = key_ctx->key[14], k15 = key_ctx->key[15],
	    k16 = key_ctx->key[16];
	u64 t0 = key_ctx->tweak[0], t1 = key_ctx->tweak[1],
	    t2 = key_ctx->tweak[2];
	u64 tmp;

	b0 -= k3;
	b1 -= k4;
	b2 -= k5;
	b3 -= k6;
	b4 -= k7;
	b5 -= k8;
	b6 -= k9;
	b7 -= k10;
	b8 -= k11;
	b9 -= k12;
	b10 -= k13;
	b11 -= k14;
	b12 -= k15;
	b13 -= k16 + t2;
	b14 -= k0 + t0;
	b15 -= k1 + 20;
	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k16 + t2;
	b15 -= k0 + 19;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k14;
	b13 -= k15 + t1;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k12;
	b11 -= k13;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k10;
	b9 -= k11;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k8;
	b7 -= k9;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k6;
	b5 -= k7;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k4;
	b3 -= k5;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k2;
	b1 -= k3;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k15 + t1;
	b15 -= k16 + 18;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k13;
	b13 -= k14 + t0;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k11;
	b11 -= k12;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k9;
	b9 -= k10;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k7;
	b7 -= k8;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k5;
	b5 -= k6;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k3;
	b3 -= k4;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k1;
	b1 -= k2;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k14 + t0;
	b15 -= k15 + 17;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k12;
	b13 -= k13 + t2;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k10;
	b11 -= k11;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k8;
	b9 -= k9;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k6;
	b7 -= k7;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k4;
	b5 -= k5;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k2;
	b3 -= k3;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k0;
	b1 -= k1;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k13 + t2;
	b15 -= k14 + 16;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k11;
	b13 -= k12 + t1;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k9;
	b11 -= k10;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k7;
	b9 -= k8;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k5;
	b7 -= k6;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k3;
	b5 -= k4;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k1;
	b3 -= k2;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k16;
	b1 -= k0;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k12 + t1;
	b15 -= k13 + 15;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k10;
	b13 -= k11 + t0;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k8;
	b11 -= k9;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k6;
	b9 -= k7;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k4;
	b7 -= k5;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k2;
	b5 -= k3;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k0;
	b3 -= k1;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k15;
	b1 -= k16;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k11 + t0;
	b15 -= k12 + 14;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k9;
	b13 -= k10 + t2;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k7;
	b11 -= k8;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k5;
	b9 -= k6;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k3;
	b7 -= k4;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k1;
	b5 -= k2;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k16;
	b3 -= k0;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k14;
	b1 -= k15;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k10 + t2;
	b15 -= k11 + 13;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k8;
	b13 -= k9 + t1;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k6;
	b11 -= k7;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k4;
	b9 -= k5;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k2;
	b7 -= k3;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k0;
	b5 -= k1;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k15;
	b3 -= k16;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k13;
	b1 -= k14;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k9 + t1;
	b15 -= k10 + 12;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k7;
	b13 -= k8 + t0;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k5;
	b11 -= k6;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k3;
	b9 -= k4;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k1;
	b7 -= k2;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k16;
	b5 -= k0;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k14;
	b3 -= k15;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k12;
	b1 -= k13;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k8 + t0;
	b15 -= k9 + 11;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k6;
	b13 -= k7 + t2;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k4;
	b11 -= k5;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k2;
	b9 -= k3;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k0;
	b7 -= k1;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k15;
	b5 -= k16;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k13;
	b3 -= k14;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k11;
	b1 -= k12;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k7 + t2;
	b15 -= k8 + 10;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k5;
	b13 -= k6 + t1;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k3;
	b11 -= k4;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k1;
	b9 -= k2;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k16;
	b7 -= k0;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k14;
	b5 -= k15;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k12;
	b3 -= k13;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k10;
	b1 -= k11;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k6 + t1;
	b15 -= k7 + 9;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k4;
	b13 -= k5 + t0;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k2;
	b11 -= k3;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k0;
	b9 -= k1;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k15;
	b7 -= k16;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k13;
	b5 -= k14;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k11;
	b3 -= k12;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k9;
	b1 -= k10;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k5 + t0;
	b15 -= k6 + 8;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k3;
	b13 -= k4 + t2;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k1;
	b11 -= k2;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k16;
	b9 -= k0;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k14;
	b7 -= k15;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k12;
	b5 -= k13;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k10;
	b3 -= k11;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k8;
	b1 -= k9;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k4 + t2;
	b15 -= k5 + 7;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k2;
	b13 -= k3 + t1;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k0;
	b11 -= k1;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k15;
	b9 -= k16;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k13;
	b7 -= k14;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k11;
	b5 -= k12;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k9;
	b3 -= k10;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k7;
	b1 -= k8;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k3 + t1;
	b15 -= k4 + 6;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k1;
	b13 -= k2 + t0;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k16;
	b11 -= k0;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k14;
	b9 -= k15;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k12;
	b7 -= k13;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k10;
	b5 -= k11;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k8;
	b3 -= k9;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k6;
	b1 -= k7;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k2 + t0;
	b15 -= k3 + 5;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k0;
	b13 -= k1 + t2;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k15;
	b11 -= k16;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k13;
	b9 -= k14;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k11;
	b7 -= k12;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k9;
	b5 -= k10;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k7;
	b3 -= k8;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k5;
	b1 -= k6;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k1 + t2;
	b15 -= k2 + 4;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k16;
	b13 -= k0 + t1;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k14;
	b11 -= k15;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k12;
	b9 -= k13;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k10;
	b7 -= k11;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k8;
	b5 -= k9;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k6;
	b3 -= k7;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k4;
	b1 -= k5;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k0 + t1;
	b15 -= k1 + 3;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k15;
	b13 -= k16 + t0;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k13;
	b11 -= k14;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k11;
	b9 -= k12;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k9;
	b7 -= k10;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k7;
	b5 -= k8;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k5;
	b3 -= k6;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k3;
	b1 -= k4;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k16 + t0;
	b15 -= k0 + 2;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k14;
	b13 -= k15 + t2;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k12;
	b11 -= k13;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k10;
	b9 -= k11;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k8;
	b7 -= k9;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k6;
	b5 -= k7;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k4;
	b3 -= k5;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k2;
	b1 -= k3;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 20);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 37);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 31);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 23);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 52);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 35);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 48);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 9);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 25);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 44);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 42);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 19);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 46);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 47);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 44);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 31);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 41);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 42);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 53);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 4);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 51);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 56);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 34);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 16);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 30);
	b14 -= b15 + k15 + t2;
	b15 -= k16 + 1;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 44);
	b12 -= b13 + k13;
	b13 -= k14 + t1;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 47);
	b10 -= b11 + k11;
	b11 -= k12;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 12);
	b8 -= b9 + k9;
	b9 -= k10;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 31);
	b6 -= b7 + k7;
	b7 -= k8;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 37);
	b4 -= b5 + k5;
	b5 -= k6;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 9);
	b2 -= b3 + k3;
	b3 -= k4;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 41);
	b0 -= b1 + k1;
	b1 -= k2;

	tmp = b7 ^ b12;
	b7 = ror64(tmp, 25);
	b12 -= b7;

	tmp = b3 ^ b10;
	b3 = ror64(tmp, 16);
	b10 -= b3;

	tmp = b5 ^ b8;
	b5 = ror64(tmp, 28);
	b8 -= b5;

	tmp = b1 ^ b14;
	b1 = ror64(tmp, 47);
	b14 -= b1;

	tmp = b9 ^ b4;
	b9 = ror64(tmp, 41);
	b4 -= b9;

	tmp = b13 ^ b6;
	b13 = ror64(tmp, 48);
	b6 -= b13;

	tmp = b11 ^ b2;
	b11 = ror64(tmp, 20);
	b2 -= b11;

	tmp = b15 ^ b0;
	b15 = ror64(tmp, 5);
	b0 -= b15;

	tmp = b9 ^ b10;
	b9 = ror64(tmp, 17);
	b10 -= b9;

	tmp = b11 ^ b8;
	b11 = ror64(tmp, 59);
	b8 -= b11;

	tmp = b13 ^ b14;
	b13 = ror64(tmp, 41);
	b14 -= b13;

	tmp = b15 ^ b12;
	b15 = ror64(tmp, 34);
	b12 -= b15;

	tmp = b1 ^ b6;
	b1 = ror64(tmp, 13);
	b6 -= b1;

	tmp = b3 ^ b4;
	b3 = ror64(tmp, 51);
	b4 -= b3;

	tmp = b5 ^ b2;
	b5 = ror64(tmp, 4);
	b2 -= b5;

	tmp = b7 ^ b0;
	b7 = ror64(tmp, 33);
	b0 -= b7;

	tmp = b1 ^ b8;
	b1 = ror64(tmp, 52);
	b8 -= b1;

	tmp = b5 ^ b14;
	b5 = ror64(tmp, 23);
	b14 -= b5;

	tmp = b3 ^ b12;
	b3 = ror64(tmp, 18);
	b12 -= b3;

	tmp = b7 ^ b10;
	b7 = ror64(tmp, 49);
	b10 -= b7;

	tmp = b15 ^ b4;
	b15 = ror64(tmp, 55);
	b4 -= b15;

	tmp = b11 ^ b6;
	b11 = ror64(tmp, 10);
	b6 -= b11;

	tmp = b13 ^ b2;
	b13 = ror64(tmp, 19);
	b2 -= b13;

	tmp = b9 ^ b0;
	b9 = ror64(tmp, 38);
	b0 -= b9;

	tmp = b15 ^ b14;
	b15 = ror64(tmp, 37);
	b14 -= b15 + k14 + t1;
	b15 -= k15;

	tmp = b13 ^ b12;
	b13 = ror64(tmp, 22);
	b12 -= b13 + k12;
	b13 -= k13 + t0;

	tmp = b11 ^ b10;
	b11 = ror64(tmp, 17);
	b10 -= b11 + k10;
	b11 -= k11;

	tmp = b9 ^ b8;
	b9 = ror64(tmp, 8);
	b8 -= b9 + k8;
	b9 -= k9;

	tmp = b7 ^ b6;
	b7 = ror64(tmp, 47);
	b6 -= b7 + k6;
	b7 -= k7;

	tmp = b5 ^ b4;
	b5 = ror64(tmp, 8);
	b4 -= b5 + k4;
	b5 -= k5;

	tmp = b3 ^ b2;
	b3 = ror64(tmp, 13);
	b2 -= b3 + k2;
	b3 -= k3;

	tmp = b1 ^ b0;
	b1 = ror64(tmp, 24);
	b0 -= b1 + k0;
	b1 -= k1;

	output[15] = b15;
	output[14] = b14;
	output[13] = b13;
	output[12] = b12;
	output[11] = b11;
	output[10] = b10;
	output[9] = b9;
	output[8] = b8;
	output[7] = b7;
	output[6] = b6;
	output[5] = b5;
	output[4] = b4;
	output[3] = b3;
	output[2] = b2;
	output[1] = b1;
	output[0] = b0;
}
