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
br_rsa_pss_sig_unpad(const br_hash_class *hf_data,
	const br_hash_class *hf_mgf1,
	const unsigned char *hash, size_t salt_len,
	const br_rsa_public_key *pk, unsigned char *x)
{
	size_t u, xlen, hash_len;
	br_hash_compat_context hc;
	unsigned char *seed, *salt;
	unsigned char tmp[64];
	uint32_t r, n_bitlen;

	hash_len = br_digest_size(hf_data);

	/*
	 * Value r will be set to a non-zero value is any test fails.
	 */
	r = 0;

	/*
	 * The value bit length (as an integer) must be strictly less than
	 * that of the modulus.
	 */
	for (u = 0; u < pk->nlen; u ++) {
		if (pk->n[u] != 0) {
			break;
		}
	}
	if (u == pk->nlen) {
		return 0;
	}
	n_bitlen = BIT_LENGTH(pk->n[u]) + ((uint32_t)(pk->nlen - u - 1) << 3);
	n_bitlen --;
	if ((n_bitlen & 7) == 0) {
		r |= *x ++;
	} else {
		r |= x[0] & (0xFF << (n_bitlen & 7));
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
	 * Check value of rightmost byte.
	 */
	r |= x[xlen - 1] ^ 0xBC;

	/*
	 * Generate the mask and XOR it into the first bytes to reveal PS;
	 * we must also mask out the leading bits.
	 */
	seed = x + xlen - hash_len - 1;
	br_mgf1_xor(x, xlen - hash_len - 1, hf_mgf1, seed, hash_len);
	if ((n_bitlen & 7) != 0) {
		x[0] &= 0xFF >> (8 - (n_bitlen & 7));
	}

	/*
	 * Check that all padding bytes have the expected value.
	 */
	for (u = 0; u < (xlen - hash_len - salt_len - 2); u ++) {
		r |= x[u];
	}
	r |= x[xlen - hash_len - salt_len - 2] ^ 0x01;

	/*
	 * Recompute H.
	 */
	salt = x + xlen - hash_len - salt_len - 1;
	hf_data->init(&hc.vtable);
	memset(tmp, 0, 8);
	hf_data->update(&hc.vtable, tmp, 8);
	hf_data->update(&hc.vtable, hash, hash_len);
	hf_data->update(&hc.vtable, salt, salt_len);
	hf_data->out(&hc.vtable, tmp);

	/*
	 * Check that the recomputed H value matches the one appearing
	 * in the string.
	 */
	for (u = 0; u < hash_len; u ++) {
		r |= tmp[u] ^ x[(xlen - salt_len - 1) + u];
	}

	return EQ0(r);
}
