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

/* see bearssl_ec.h */
uint32_t
br_ecdsa_i15_vrfy_raw(const br_ec_impl *impl,
	const void *hash, size_t hash_len,
	const br_ec_public_key *pk,
	const void *sig, size_t sig_len)
{
	/*
	 * IMPORTANT: this code is fit only for curves with a prime
	 * order. This is needed so that modular reduction of the X
	 * coordinate of a point can be done with a simple subtraction.
	 */
	const br_ec_curve_def *cd;
	uint16_t n[I15_LEN], r[I15_LEN], s[I15_LEN], t1[I15_LEN], t2[I15_LEN];
	unsigned char tx[(BR_MAX_EC_SIZE + 7) >> 3];
	unsigned char ty[(BR_MAX_EC_SIZE + 7) >> 3];
	unsigned char eU[POINT_LEN];
	size_t nlen, rlen, ulen;
	uint16_t n0i;
	uint32_t res;

	/*
	 * If the curve is not supported, then report an error.
	 */
	if (((impl->supported_curves >> pk->curve) & 1) == 0) {
		return 0;
	}

	/*
	 * Get the curve parameters (generator and order).
	 */
	switch (pk->curve) {
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
	 * Signature length must be even.
	 */
	if (sig_len & 1) {
		return 0;
	}
	rlen = sig_len >> 1;

	/*
	 * Public key point must have the proper size for this curve.
	 */
	if (pk->qlen != cd->generator_len) {
		return 0;
	}

	/*
	 * Get modulus; then decode the r and s values. They must be
	 * lower than the modulus, and s must not be null.
	 */
	nlen = cd->order_len;
	br_i15_decode(n, cd->order, nlen);
	n0i = br_i15_ninv15(n[1]);
	if (!br_i15_decode_mod(r, sig, rlen, n)) {
		return 0;
	}
	if (!br_i15_decode_mod(s, (const unsigned char *)sig + rlen, rlen, n)) {
		return 0;
	}
	if (br_i15_iszero(s)) {
		return 0;
	}

	/*
	 * Invert s. We do that with a modular exponentiation; we use
	 * the fact that for all the curves we support, the least
	 * significant byte is not 0 or 1, so we can subtract 2 without
	 * any carry to process.
	 * We also want 1/s in Montgomery representation, which can be
	 * done by converting _from_ Montgomery representation before
	 * the inversion (because (1/s)*R = 1/(s/R)).
	 */
	br_i15_from_monty(s, n, n0i);
	memcpy(tx, cd->order, nlen);
	tx[nlen - 1] -= 2;
	br_i15_modpow(s, tx, nlen, n, n0i, t1, t2);

	/*
	 * Truncate the hash to the modulus length (in bits) and reduce
	 * it modulo the curve order. The modular reduction can be done
	 * with a subtraction since the truncation already reduced the
	 * value to the modulus bit length.
	 */
	br_ecdsa_i15_bits2int(t1, hash, hash_len, n[0]);
	br_i15_sub(t1, n, br_i15_sub(t1, n, 0) ^ 1);

	/*
	 * Multiply the (truncated, reduced) hash value with 1/s, result in
	 * t2, encoded in ty.
	 */
	br_i15_montymul(t2, t1, s, n, n0i);
	br_i15_encode(ty, nlen, t2);

	/*
	 * Multiply r with 1/s, result in t1, encoded in tx.
	 */
	br_i15_montymul(t1, r, s, n, n0i);
	br_i15_encode(tx, nlen, t1);

	/*
	 * Compute the point x*Q + y*G.
	 */
	ulen = cd->generator_len;
	memcpy(eU, pk->q, ulen);
	res = impl->muladd(eU, NULL, ulen,
		tx, nlen, ty, nlen, cd->curve);

	/*
	 * Get the X coordinate, reduce modulo the curve order, and
	 * compare with the 'r' value.
	 *
	 * The modular reduction can be done with subtractions because
	 * we work with curves of prime order, so the curve order is
	 * close to the field order (Hasse's theorem).
	 */
	br_i15_zero(t1, n[0]);
	br_i15_decode(t1, &eU[1], ulen >> 1);
	t1[0] = n[0];
	br_i15_sub(t1, n, br_i15_sub(t1, n, 0) ^ 1);
	res &= ~br_i15_sub(t1, r, 1);
	res &= br_i15_iszero(t1);
	return res;
}
