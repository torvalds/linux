/*
 * Copyright (c) 2018 Thomas Pornin <pornin@bolet.org>
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

#if BR_INT128 || BR_UMUL128

/* see bearssl_rsa.h */
size_t
br_rsa_i62_oaep_encrypt(
	const br_prng_class **rnd, const br_hash_class *dig,
	const void *label, size_t label_len,
	const br_rsa_public_key *pk,
	void *dst, size_t dst_max_len,
	const void *src, size_t src_len)
{
	size_t dlen;

	dlen = br_rsa_oaep_pad(rnd, dig, label, label_len,
		pk, dst, dst_max_len, src, src_len);
	if (dlen == 0) {
		return 0;
	}
	return dlen & -(size_t)br_rsa_i62_public(dst, dlen, pk);
}

/* see bearssl_rsa.h */
br_rsa_oaep_encrypt
br_rsa_i62_oaep_encrypt_get(void)
{
	return &br_rsa_i62_oaep_encrypt;
}

#else

/* see bearssl_rsa.h */
br_rsa_oaep_encrypt
br_rsa_i62_oaep_encrypt_get(void)
{
	return 0;
}

#endif
