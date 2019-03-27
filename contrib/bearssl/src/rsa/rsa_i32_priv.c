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

#define U   (1 + (BR_MAX_RSA_FACTOR >> 5))

/* see bearssl_rsa.h */
uint32_t
br_rsa_i32_private(unsigned char *x, const br_rsa_private_key *sk)
{
	const unsigned char *p, *q;
	size_t plen, qlen;
	uint32_t tmp[6 * U];
	uint32_t *mp, *mq, *s1, *s2, *t1, *t2, *t3;
	uint32_t p0i, q0i;
	size_t xlen, u;
	uint32_t r;

	/*
	 * All our temporary buffers are from the tmp[] array.
	 *
	 * The mp, mq, s1, s2, t1 and t2 buffers are large enough to
	 * contain a RSA factor. The t3 buffer can contain a complete
	 * RSA modulus. t3 shares its storage space with s2, s1 and t1,
	 * in that order (this is important, see below).
	 */
	mq = tmp;
	mp = tmp + U;
	t2 = tmp + 2 * U;
	s2 = tmp + 3 * U;
	s1 = tmp + 4 * U;
	t1 = tmp + 5 * U;
	t3 = s2;

	/*
	 * Compute the actual lengths (in bytes) of p and q, and check
	 * that they fit within our stack buffers.
	 */
	p = sk->p;
	plen = sk->plen;
	while (plen > 0 && *p == 0) {
		p ++;
		plen --;
	}
	q = sk->q;
	qlen = sk->qlen;
	while (qlen > 0 && *q == 0) {
		q ++;
		qlen --;
	}
	if (plen > (BR_MAX_RSA_FACTOR >> 3)
		|| qlen > (BR_MAX_RSA_FACTOR >> 3))
	{
		return 0;
	}

	/*
	 * Decode p and q.
	 */
	br_i32_decode(mp, p, plen);
	br_i32_decode(mq, q, qlen);

	/*
	 * Recompute modulus, to compare with the source value.
	 */
	br_i32_zero(t2, mp[0]);
	br_i32_mulacc(t2, mp, mq);
	xlen = (sk->n_bitlen + 7) >> 3;
	br_i32_encode(t2 + 2 * U, xlen, t2);
	u = xlen;
	r = 0;
	while (u > 0) {
		uint32_t wn, wx;

		u --;
		wn = ((unsigned char *)(t2 + 2 * U))[u];
		wx = x[u];
		r = ((wx - (wn + r)) >> 8) & 1;
	}

	/*
	 * Compute s1 = x^dp mod p.
	 */
	p0i = br_i32_ninv32(mp[1]);
	br_i32_decode_reduce(s1, x, xlen, mp);
	br_i32_modpow(s1, sk->dp, sk->dplen, mp, p0i, t1, t2);

	/*
	 * Compute s2 = x^dq mod q.
	 */
	q0i = br_i32_ninv32(mq[1]);
	br_i32_decode_reduce(s2, x, xlen, mq);
	br_i32_modpow(s2, sk->dq, sk->dqlen, mq, q0i, t1, t2);

	/*
	 * Compute:
	 *   h = (s1 - s2)*(1/q) mod p
	 * s1 is an integer modulo p, but s2 is modulo q. PKCS#1 is
	 * unclear about whether p may be lower than q (some existing,
	 * widely deployed implementations of RSA don't tolerate p < q),
	 * but we want to support that occurrence, so we need to use the
	 * reduction function.
	 *
	 * Since we use br_i32_decode_reduce() for iq (purportedly, the
	 * inverse of q modulo p), we also tolerate improperly large
	 * values for this parameter.
	 */
	br_i32_reduce(t2, s2, mp);
	br_i32_add(s1, mp, br_i32_sub(s1, t2, 1));
	br_i32_to_monty(s1, mp);
	br_i32_decode_reduce(t1, sk->iq, sk->iqlen, mp);
	br_i32_montymul(t2, s1, t1, mp, p0i);

	/*
	 * h is now in t2. We compute the final result:
	 *   s = s2 + q*h
	 * All these operations are non-modular.
	 *
	 * We need mq, s2 and t2. We use the t3 buffer as destination.
	 * The buffers mp, s1 and t1 are no longer needed. Moreover,
	 * the first step is to copy s2 into the destination buffer t3.
	 * We thus arranged for t3 to actually share space with s2, and
	 * to be followed by the space formerly used by s1 and t1.
	 */
	br_i32_mulacc(t3, mq, t2);

	/*
	 * Encode the result. Since we already checked the value of xlen,
	 * we can just use it right away.
	 */
	br_i32_encode(x, xlen, t3);

	/*
	 * The only error conditions remaining at that point are invalid
	 * values for p and q (even integers).
	 */
	return p0i & q0i & r;
}
