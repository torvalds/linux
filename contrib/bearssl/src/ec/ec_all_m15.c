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

static const unsigned char *
api_generator(int curve, size_t *len)
{
	switch (curve) {
	case BR_EC_secp256r1:
		return br_ec_p256_m15.generator(curve, len);
	case BR_EC_curve25519:
		return br_ec_c25519_m15.generator(curve, len);
	default:
		return br_ec_prime_i15.generator(curve, len);
	}
}

static const unsigned char *
api_order(int curve, size_t *len)
{
	switch (curve) {
	case BR_EC_secp256r1:
		return br_ec_p256_m15.order(curve, len);
	case BR_EC_curve25519:
		return br_ec_c25519_m15.order(curve, len);
	default:
		return br_ec_prime_i15.order(curve, len);
	}
}

static size_t
api_xoff(int curve, size_t *len)
{
	switch (curve) {
	case BR_EC_secp256r1:
		return br_ec_p256_m15.xoff(curve, len);
	case BR_EC_curve25519:
		return br_ec_c25519_m15.xoff(curve, len);
	default:
		return br_ec_prime_i15.xoff(curve, len);
	}
}

static uint32_t
api_mul(unsigned char *G, size_t Glen,
	const unsigned char *kb, size_t kblen, int curve)
{
	switch (curve) {
	case BR_EC_secp256r1:
		return br_ec_p256_m15.mul(G, Glen, kb, kblen, curve);
	case BR_EC_curve25519:
		return br_ec_c25519_m15.mul(G, Glen, kb, kblen, curve);
	default:
		return br_ec_prime_i15.mul(G, Glen, kb, kblen, curve);
	}
}

static size_t
api_mulgen(unsigned char *R,
	const unsigned char *x, size_t xlen, int curve)
{
	switch (curve) {
	case BR_EC_secp256r1:
		return br_ec_p256_m15.mulgen(R, x, xlen, curve);
	case BR_EC_curve25519:
		return br_ec_c25519_m15.mulgen(R, x, xlen, curve);
	default:
		return br_ec_prime_i15.mulgen(R, x, xlen, curve);
	}
}

static uint32_t
api_muladd(unsigned char *A, const unsigned char *B, size_t len,
	const unsigned char *x, size_t xlen,
	const unsigned char *y, size_t ylen, int curve)
{
	switch (curve) {
	case BR_EC_secp256r1:
		return br_ec_p256_m15.muladd(A, B, len,
			x, xlen, y, ylen, curve);
	case BR_EC_curve25519:
		return br_ec_c25519_m15.muladd(A, B, len,
			x, xlen, y, ylen, curve);
	default:
		return br_ec_prime_i15.muladd(A, B, len,
			x, xlen, y, ylen, curve);
	}
}

/* see bearssl_ec.h */
const br_ec_impl br_ec_all_m15 = {
	(uint32_t)0x23800000,
	&api_generator,
	&api_order,
	&api_xoff,
	&api_mul,
	&api_mulgen,
	&api_muladd
};
