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
uint32_t
br_rsa_i62_oaep_decrypt(const br_hash_class *dig,
	const void *label, size_t label_len,
	const br_rsa_private_key *sk, void *data, size_t *len)
{
	uint32_t r;

	if (*len != ((sk->n_bitlen + 7) >> 3)) {
		return 0;
	}
	r = br_rsa_i62_private(data, sk);
	r &= br_rsa_oaep_unpad(dig, label, label_len, data, len);
	return r;
}

/* see bearssl_rsa.h */
br_rsa_oaep_decrypt
br_rsa_i62_oaep_decrypt_get(void)
{
	return &br_rsa_i62_oaep_decrypt;
}

#else

/* see bearssl_rsa.h */
br_rsa_oaep_decrypt
br_rsa_i62_oaep_decrypt_get(void)
{
	return 0;
}

#endif
