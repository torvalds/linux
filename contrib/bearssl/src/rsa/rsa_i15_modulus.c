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

/* see bearssl_rsa.h */
size_t
br_rsa_i15_compute_modulus(void *n, const br_rsa_private_key *sk)
{
	uint16_t tmp[2 * ((BR_MAX_RSA_SIZE + 14) / 15) + 5];
	uint16_t *t, *p, *q;
	const unsigned char *pbuf, *qbuf;
	size_t nlen, plen, qlen, tlen;

	/*
	 * Compute actual byte and lengths for p and q.
	 */
	pbuf = sk->p;
	plen = sk->plen;
	while (plen > 0 && *pbuf == 0) {
		pbuf ++;
		plen --;
	}
	qbuf = sk->q;
	qlen = sk->qlen;
	while (qlen > 0 && *qbuf == 0) {
		qbuf ++;
		qlen --;
	}

	t = tmp;
	tlen = (sizeof tmp) / (sizeof tmp[0]);

	/*
	 * Decode p.
	 */
	if ((15 * tlen) < (plen << 3) + 15) {
		return 0;
	}
	br_i15_decode(t, pbuf, plen);
	p = t;
	plen = (p[0] + 31) >> 4;
	t += plen;
	tlen -= plen;

	/*
	 * Decode q.
	 */
	if ((15 * tlen) < (qlen << 3) + 15) {
		return 0;
	}
	br_i15_decode(t, qbuf, qlen);
	q = t;
	qlen = (q[0] + 31) >> 4;
	t += qlen;
	tlen -= qlen;

	/*
	 * Computation can proceed only if we have enough room for the
	 * modulus.
	 */
	if (tlen < (plen + qlen + 1)) {
		return 0;
	}

	/*
	 * Private key already contains the modulus bit length, from which
	 * we can infer the output length. Even if n is NULL, we still had
	 * to decode p and q to make sure that the product can be computed.
	 */
	nlen = (sk->n_bitlen + 7) >> 3;
	if (n != NULL) {
		br_i15_zero(t, p[0]);
		br_i15_mulacc(t, p, q);
		br_i15_encode(n, nlen, t);
	}
	return nlen;
}
