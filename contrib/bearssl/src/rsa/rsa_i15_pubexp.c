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

/*
 * Recompute public exponent, based on factor p and reduced private
 * exponent dp.
 */
static uint32_t
get_pubexp(const unsigned char *pbuf, size_t plen,
	const unsigned char *dpbuf, size_t dplen)
{
	/*
	 * dp is the inverse of e modulo p-1. If p = 3 mod 4, then
	 * p-1 = 2*((p-1)/2). Taken modulo 2, e is odd and has inverse 1;
	 * thus, dp must be odd.
	 *
	 * We compute the inverse of dp modulo (p-1)/2. This requires
	 * first reducing dp modulo (p-1)/2 (this can be done with a
	 * conditional subtract, no need to use the generic modular
	 * reduction function); then, we use moddiv.
	 */

	uint16_t tmp[6 * ((BR_MAX_RSA_FACTOR + 29) / 15)];
	uint16_t *p, *dp, *x;
	size_t len;
	uint32_t e;

	/*
	 * Compute actual factor length (in bytes) and check that it fits
	 * under our size constraints.
	 */
	while (plen > 0 && *pbuf == 0) {
		pbuf ++;
		plen --;
	}
	if (plen == 0 || plen < 5 || plen > (BR_MAX_RSA_FACTOR / 8)) {
		return 0;
	}

	/*
	 * Compute actual reduced exponent length (in bytes) and check that
	 * it is not longer than p.
	 */
	while (dplen > 0 && *dpbuf == 0) {
		dpbuf ++;
		dplen --;
	}
	if (dplen > plen || dplen == 0
		|| (dplen == plen && dpbuf[0] > pbuf[0]))
	{
		return 0;
	}

	/*
	 * Verify that p = 3 mod 4 and that dp is odd.
	 */
	if ((pbuf[plen - 1] & 3) != 3 || (dpbuf[dplen - 1] & 1) != 1) {
		return 0;
	}

	/*
	 * Decode p and compute (p-1)/2.
	 */
	p = tmp;
	br_i15_decode(p, pbuf, plen);
	len = (p[0] + 31) >> 4;
	br_i15_rshift(p, 1);

	/*
	 * Decode dp and make sure its announced bit length matches that of
	 * p (we already know that the size of dp, in bits, does not exceed
	 * the size of p, so we just have to copy the header word).
	 */
	dp = p + len;
	memset(dp, 0, len * sizeof *dp);
	br_i15_decode(dp, dpbuf, dplen);
	dp[0] = p[0];

	/*
	 * Subtract (p-1)/2 from dp if necessary.
	 */
	br_i15_sub(dp, p, NOT(br_i15_sub(dp, p, 0)));

	/*
	 * If another subtraction is needed, then this means that the
	 * value was invalid. We don't care to leak information about
	 * invalid keys.
	 */
	if (br_i15_sub(dp, p, 0) == 0) {
		return 0;
	}

	/*
	 * Invert dp modulo (p-1)/2. If the inversion fails, then the
	 * key value was invalid.
	 */
	x = dp + len;
	br_i15_zero(x, p[0]);
	x[1] = 1;
	if (br_i15_moddiv(x, dp, p, br_i15_ninv15(p[1]), x + len) == 0) {
		return 0;
	}

	/*
	 * We now have an inverse. We must set it to zero (error) if its
	 * length is greater than 32 bits and/or if it is an even integer.
	 * Take care that the bit_length function returns an encoded
	 * bit length.
	 */
	e = (uint32_t)x[1] | ((uint32_t)x[2] << 15) | ((uint32_t)x[3] << 30);
	e &= -LT(br_i15_bit_length(x + 1, len - 1), 35);
	e &= -(e & 1);
	return e;
}

/* see bearssl_rsa.h */
uint32_t
br_rsa_i15_compute_pubexp(const br_rsa_private_key *sk)
{
	/*
	 * Get the public exponent from both p and q. This is the right
	 * exponent if we get twice the same value.
	 */
	uint32_t ep, eq;

	ep = get_pubexp(sk->p, sk->plen, sk->dp, sk->dplen);
	eq = get_pubexp(sk->q, sk->qlen, sk->dq, sk->dqlen);
	return ep & -EQ(ep, eq);
}
