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

/* see inner.h */
uint32_t
br_rsa_pss_sig_pad(const br_prng_class **rng,
	const br_hash_class *hf_data, const br_hash_class *hf_mgf1,
	const unsigned char *hash, size_t salt_len,
	uint32_t n_bitlen, unsigned char *x)
{
	size_t xlen, hash_len;
	br_hash_compat_context hc;
	unsigned char *salt, *seed;

	hash_len = br_digest_size(hf_data);

	/*
	 * The padded string is one bit smaller than the modulus;
	 * notably, if the modulus length is equal to 1 modulo 8, then
	 * the padded string will be one _byte_ smaller, and the first
	 * byte will be set to 0. We apply these transformations here.
	 */
	n_bitlen --;
	if ((n_bitlen & 7) == 0) {
		*x ++ = 0;
	}
	xlen = (n_bitlen + 7) >> 3;

	/*
	 * Check that the modulus is large enough for the hash value
	 * length combined with the intended salt length.
	 */
	if (hash_len > xlen || salt_len > xlen
		|| (hash_len + salt_len + 2) > xlen)
	{
		return 0;
	}

	/*
	 * Produce a random salt.
	 */
	salt = x + xlen - hash_len - salt_len - 1;
	if (salt_len != 0) {
		(*rng)->generate(rng, salt, salt_len);
	}

	/*
	 * Compute the seed for MGF1.
	 */
	seed = x + xlen - hash_len - 1;
	hf_data->init(&hc.vtable);
	memset(seed, 0, 8);
	hf_data->update(&hc.vtable, seed, 8);
	hf_data->update(&hc.vtable, hash, hash_len);
	hf_data->update(&hc.vtable, salt, salt_len);
	hf_data->out(&hc.vtable, seed);

	/*
	 * Prepare string PS (padded salt). The salt is already at the
	 * right place.
	 */
	memset(x, 0, xlen - salt_len - hash_len - 2);
	x[xlen - salt_len - hash_len - 2] = 0x01;

	/*
	 * Generate the mask and XOR it into PS.
	 */
	br_mgf1_xor(x, xlen - hash_len - 1, hf_mgf1, seed, hash_len);

	/*
	 * Clear the top bits to ensure the value is lower than the
	 * modulus.
	 */
	x[0] &= 0xFF >> (((uint32_t)xlen << 3) - n_bitlen);

	/*
	 * The seed (H) is already in the right place. We just set the
	 * last byte.
	 */
	x[xlen - 1] = 0xBC;

	return 1;
}
