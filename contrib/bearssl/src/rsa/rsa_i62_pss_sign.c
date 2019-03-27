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
br_rsa_i62_pss_sign(const br_prng_class **rng,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1,
	const unsigned char *hash, size_t salt_len,
	const br_rsa_private_key *sk, unsigned char *x)
{
	if (!br_rsa_pss_sig_pad(rng, hf_data, hf_mgf1, hash,
		salt_len, sk->n_bitlen, x))
	{
		return 0;
	}
	return br_rsa_i62_private(x, sk);
}

/* see bearssl_rsa.h */
br_rsa_pss_sign
br_rsa_i62_pss_sign_get(void)
{
	return &br_rsa_i62_pss_sign;
}

#else

/* see bearssl_rsa.h */
br_rsa_pss_sign
br_rsa_i62_pss_sign_get(void)
{
	return 0;
}

#endif
