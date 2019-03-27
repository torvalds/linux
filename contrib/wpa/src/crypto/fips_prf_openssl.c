/*
 * FIPS 186-2 PRF for libcrypto
 * Copyright (c) 2004-2005, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <openssl/sha.h>

#include "common.h"
#include "crypto.h"


static void sha1_transform(u32 *state, const u8 data[64])
{
	SHA_CTX context;
	os_memset(&context, 0, sizeof(context));
#if defined(OPENSSL_IS_BORINGSSL) && !defined(ANDROID)
	context.h[0] = state[0];
	context.h[1] = state[1];
	context.h[2] = state[2];
	context.h[3] = state[3];
	context.h[4] = state[4];
	SHA1_Transform(&context, data);
	state[0] = context.h[0];
	state[1] = context.h[1];
	state[2] = context.h[2];
	state[3] = context.h[3];
	state[4] = context.h[4];
#else
	context.h0 = state[0];
	context.h1 = state[1];
	context.h2 = state[2];
	context.h3 = state[3];
	context.h4 = state[4];
	SHA1_Transform(&context, data);
	state[0] = context.h0;
	state[1] = context.h1;
	state[2] = context.h2;
	state[3] = context.h3;
	state[4] = context.h4;
#endif
}


int fips186_2_prf(const u8 *seed, size_t seed_len, u8 *x, size_t xlen)
{
	u8 xkey[64];
	u32 t[5], _t[5];
	int i, j, m, k;
	u8 *xpos = x;
	u32 carry;

	if (seed_len < sizeof(xkey))
		os_memset(xkey + seed_len, 0, sizeof(xkey) - seed_len);
	else
		seed_len = sizeof(xkey);

	/* FIPS 186-2 + change notice 1 */

	os_memcpy(xkey, seed, seed_len);
	t[0] = 0x67452301;
	t[1] = 0xEFCDAB89;
	t[2] = 0x98BADCFE;
	t[3] = 0x10325476;
	t[4] = 0xC3D2E1F0;

	m = xlen / 40;
	for (j = 0; j < m; j++) {
		/* XSEED_j = 0 */
		for (i = 0; i < 2; i++) {
			/* XVAL = (XKEY + XSEED_j) mod 2^b */

			/* w_i = G(t, XVAL) */
			os_memcpy(_t, t, 20);
			sha1_transform(_t, xkey);
			WPA_PUT_BE32(xpos, _t[0]);
			WPA_PUT_BE32(xpos + 4, _t[1]);
			WPA_PUT_BE32(xpos + 8, _t[2]);
			WPA_PUT_BE32(xpos + 12, _t[3]);
			WPA_PUT_BE32(xpos + 16, _t[4]);

			/* XKEY = (1 + XKEY + w_i) mod 2^b */
			carry = 1;
			for (k = 19; k >= 0; k--) {
				carry += xkey[k] + xpos[k];
				xkey[k] = carry & 0xff;
				carry >>= 8;
			}

			xpos += 20;
		}
		/* x_j = w_0|w_1 */
	}

	return 0;
}
