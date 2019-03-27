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

static const unsigned char POINT_LEN[] = {
	  0,   /* 0: not a valid curve ID */
	 43,   /* sect163k1 */
	 43,   /* sect163r1 */
	 43,   /* sect163r2 */
	 51,   /* sect193r1 */
	 51,   /* sect193r2 */
	 61,   /* sect233k1 */
	 61,   /* sect233r1 */
	 61,   /* sect239k1 */
	 73,   /* sect283k1 */
	 73,   /* sect283r1 */
	105,   /* sect409k1 */
	105,   /* sect409r1 */
	145,   /* sect571k1 */
	145,   /* sect571r1 */
	 41,   /* secp160k1 */
	 41,   /* secp160r1 */
	 41,   /* secp160r2 */
	 49,   /* secp192k1 */
	 49,   /* secp192r1 */
	 57,   /* secp224k1 */
	 57,   /* secp224r1 */
	 65,   /* secp256k1 */
	 65,   /* secp256r1 */
	 97,   /* secp384r1 */
	133,   /* secp521r1 */
	 65,   /* brainpoolP256r1 */
	 97,   /* brainpoolP384r1 */
	129,   /* brainpoolP512r1 */
	 32,   /* curve25519 */
	 56,   /* curve448 */
};

/* see bearssl_ec.h */
size_t
br_ec_compute_pub(const br_ec_impl *impl, br_ec_public_key *pk,
	void *kbuf, const br_ec_private_key *sk)
{
	int curve;
	size_t len;

	curve = sk->curve;
	if (curve < 0 || curve >= 32 || curve >= (int)(sizeof POINT_LEN)
		|| ((impl->supported_curves >> curve) & 1) == 0)
	{
		return 0;
	}
	if (kbuf == NULL) {
		return POINT_LEN[curve];
	}
	len = impl->mulgen(kbuf, sk->x, sk->xlen, curve);
	if (pk != NULL) {
		pk->curve = curve;
		pk->q = kbuf;
		pk->qlen = len;
	}
	return len;
}
