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

#if BR_INT128 || BR_UMUL128

/*
 * As a strict minimum, we need four buffers that can hold a
 * modular integer. But TLEN is expressed in 64-bit words.
 */
#define TLEN   (2 * (2 + ((BR_MAX_RSA_SIZE + 30) / 31)))

/* see bearssl_rsa.h */
uint32_t
br_rsa_i62_public(unsigned char *x, size_t xlen,
	const br_rsa_public_key *pk)
{
	const unsigned char *n;
	size_t nlen;
	uint64_t tmp[TLEN];
	uint32_t *m, *a;
	size_t fwlen;
	long z;
	uint32_t m0i, r;

	/*
	 * Get the actual length of the modulus, and see if it fits within
	 * our stack buffer. We also check that the length of x[] is valid.
	 */
	n = pk->n;
	nlen = pk->nlen;
	while (nlen > 0 && *n == 0) {
		n ++;
		nlen --;
	}
	if (nlen == 0 || nlen > (BR_MAX_RSA_SIZE >> 3) || xlen != nlen) {
		return 0;
	}
	z = (long)nlen << 3;
	fwlen = 1;
	while (z > 0) {
		z -= 31;
		fwlen ++;
	}
	/*
	 * Convert fwlen to a count in 62-bit words.
	 */
	fwlen = (fwlen + 1) >> 1;

	/*
	 * The modulus gets decoded into m[].
	 * The value to exponentiate goes into a[].
	 */
	m = (uint32_t *)tmp;
	a = (uint32_t *)(tmp + fwlen);

	/*
	 * Decode the modulus.
	 */
	br_i31_decode(m, n, nlen);
	m0i = br_i31_ninv31(m[1]);

	/*
	 * Note: if m[] is even, then m0i == 0. Otherwise, m0i must be
	 * an odd integer.
	 */
	r = m0i & 1;

	/*
	 * Decode x[] into a[]; we also check that its value is proper.
	 */
	r &= br_i31_decode_mod(a, x, xlen, m);

	/*
	 * Compute the modular exponentiation.
	 */
	br_i62_modpow_opt(a, pk->e, pk->elen, m, m0i,
		tmp + 2 * fwlen, TLEN - 2 * fwlen);

	/*
	 * Encode the result.
	 */
	br_i31_encode(x, xlen, a);
	return r;
}

/* see bearssl_rsa.h */
br_rsa_public
br_rsa_i62_public_get(void)
{
	return &br_rsa_i62_public;
}

#else

/* see bearssl_rsa.h */
br_rsa_public
br_rsa_i62_public_get(void)
{
	return 0;
}

#endif
