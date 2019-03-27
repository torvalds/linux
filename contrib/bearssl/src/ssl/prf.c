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
br_tls_phash(void *dst, size_t len,
	const br_hash_class *dig,
	const void *secret, size_t secret_len, const char *label,
	size_t seed_num, const br_tls_prf_seed_chunk *seed)
{
	unsigned char *buf;
	unsigned char tmp[64], a[64];
	br_hmac_key_context kc;
	br_hmac_context hc;
	size_t label_len, hlen, u;

	if (len == 0) {
		return;
	}
	buf = dst;
	for (label_len = 0; label[label_len]; label_len ++);
	hlen = br_digest_size(dig);
	br_hmac_key_init(&kc, dig, secret, secret_len);
	br_hmac_init(&hc, &kc, 0);
	br_hmac_update(&hc, label, label_len);
	for (u = 0; u < seed_num; u ++) {
		br_hmac_update(&hc, seed[u].data, seed[u].len);
	}
	br_hmac_out(&hc, a);
	for (;;) {
		br_hmac_init(&hc, &kc, 0);
		br_hmac_update(&hc, a, hlen);
		br_hmac_update(&hc, label, label_len);
		for (u = 0; u < seed_num; u ++) {
			br_hmac_update(&hc, seed[u].data, seed[u].len);
		}
		br_hmac_out(&hc, tmp);
		for (u = 0; u < hlen && u < len; u ++) {
			buf[u] ^= tmp[u];
		}
		buf += u;
		len -= u;
		if (len == 0) {
			return;
		}
		br_hmac_init(&hc, &kc, 0);
		br_hmac_update(&hc, a, hlen);
		br_hmac_out(&hc, a);
	}
}
