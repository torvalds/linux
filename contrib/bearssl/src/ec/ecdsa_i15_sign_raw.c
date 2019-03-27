/*
 * Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
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

#define I15_LEN     ((BR_MAX_EC_SIZE + 29) / 15)
#define POINT_LEN   (1 + (((BR_MAX_EC_SIZE + 7) >> 3) << 1))
#define ORDER_LEN   ((BR_MAX_EC_SIZE + 7) >> 3)

/* see bearssl_ec.h */
size_t
br_ecdsa_i15_sign_raw(const br_ec_impl *impl,
	const br_hash_class *hf, const void *hash_value,
	const br_ec_private_key *sk, void *sig)
{
	/*
	 * IMPORTANT: this code is fit only for curves with a prime
	 * order. This is needed so that modular reduction of the X
	 * coordinate of a point can be done with a simple subtraction.
	 * We also rely on the last byte of the curve order to be distinct
	 * from 0 and 1.
	 */
	const br_ec_curve_def *cd;
	uint16_t n[I15_LEN], r[I15_LEN], s[I15_LEN], x[I15_LEN];
	uint16_t m[I15_LEN], k[I15_LEN], t1[I15_LEN], t2[I15_LEN];
	unsigned char tt[ORDER_LEN << 1];
	unsigned char eU[POINT_LEN];
	size_t hash_len, nlen, ulen;
	uint16_t n0i;
	uint32_t ctl;
	br_hmac_drbg_context drbg;

	/*
	 * If the curve is not supported, then exit with an error.
	 */
	if (((impl->supported_curves >> sk->curve) & 1) == 0) {
		return 0;
	}

	/*
	 * Get the curve parameters (generator and order).
	 */
	switch (sk->curve) {
	case BR_EC_secp256r1:
		cd = &br_secp256r1;
		break;
	case BR_EC_secp384r1:
		cd = &br_secp384r1;
		break;
	case BR_EC_secp521r1:
		cd = &br_secp521r1;
		break;
	default:
		return 0;
	}

	/*
	 * Get modulus.
	 */
	nlen = cd->order_len;
	br_i15_decode(n, cd->order, nlen);
	n0i = br_i15_ninv15(n[1]);

	/*
	 * Get private key as an i15 integer. This also checks that the
	 * private key is well-defined (not zero, and less than the
	 * curve order).
	 */
	if (!br_i15_decode_mod(x, sk->x, sk->xlen, n)) {
		return 0;
	}
	if (br_i15_iszero(x)) {
		return 0;
	}

	/*
	 * Get hash length.
	 */
	hash_len = (hf->desc >> BR_HASHDESC_OUT_OFF) & BR_HASHDESC_OUT_MASK;

	/*
	 * Truncate and reduce the hash value modulo the curve order.
	 */
	br_ecdsa_i15_bits2int(m, hash_value, hash_len, n[0]);
	br_i15_sub(m, n, br_i15_sub(m, n, 0) ^ 1);

	/*
	 * RFC 6979 generation of the "k" value.
	 *
	 * The process uses HMAC_DRBG (with the hash function used to
	 * process the message that is to be signed). The seed is the
	 * concatenation of the encodings of the private key and
	 * the hash value (after truncation and modular reduction).
	 */
	br_i15_encode(tt, nlen, x);
	br_i15_encode(tt + nlen, nlen, m);
	br_hmac_drbg_init(&drbg, hf, tt, nlen << 1);
	for (;;) {
		br_hmac_drbg_generate(&drbg, tt, nlen);
		br_ecdsa_i15_bits2int(k, tt, nlen, n[0]);
		if (br_i15_iszero(k)) {
			continue;
		}
		if (br_i15_sub(k, n, 0)) {
			break;
		}
	}

	/*
	 * Compute k*G and extract the X coordinate, then reduce it
	 * modulo the curve order. Since we support only curves with
	 * prime order, that reduction is only a matter of computing
	 * a subtraction.
	 */
	br_i15_encode(tt, nlen, k);
	ulen = impl->mulgen(eU, tt, nlen, sk->curve);
	br_i15_zero(r, n[0]);
	br_i15_decode(r, &eU[1], ulen >> 1);
	r[0] = n[0];
	br_i15_sub(r, n, br_i15_sub(r, n, 0) ^ 1);

	/*
	 * Compute 1/k in double-Montgomery representation. We do so by
	 * first converting _from_ Montgomery representation (twice),
	 * then using a modular exponentiation.
	 */
	br_i15_from_monty(k, n, n0i);
	br_i15_from_monty(k, n, n0i);
	memcpy(tt, cd->order, nlen);
	tt[nlen - 1] -= 2;
	br_i15_modpow(k, tt, nlen, n, n0i, t1, t2);

	/*
	 * Compute s = (m+xr)/k (mod n).
	 * The k[] array contains R^2/k (double-Montgomery representation);
	 * we thus can use direct Montgomery multiplications and conversions
	 * from Montgomery, avoiding any call to br_i15_to_monty() (which
	 * is slower).
	 */
	br_i15_from_monty(m, n, n0i);
	br_i15_montymul(t1, x, r, n, n0i);
	ctl = br_i15_add(t1, m, 1);
	ctl |= br_i15_sub(t1, n, 0) ^ 1;
	br_i15_sub(t1, n, ctl);
	br_i15_montymul(s, t1, k, n, n0i);

	/*
	 * Encode r and s in the signature.
	 */
	br_i15_encode(sig, nlen, r);
	br_i15_encode((unsigned char *)sig + nlen, nlen, s);
	return nlen << 1;
}
