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
br_rsa_i15_compute_privexp(void *d,
	const br_rsa_private_key *sk, uint32_t e)
{
	/*
	 * We want to invert e modulo phi = (p-1)(q-1). This first
	 * requires computing phi, which is easy since we have the factors
	 * p and q in the private key structure.
	 *
	 * Since p = 3 mod 4 and q = 3 mod 4, phi/4 is an odd integer.
	 * We could invert e modulo phi/4 then patch the result to
	 * modulo phi, but this would involve assembling three modulus-wide
	 * values (phi/4, 1 and e) and calling moddiv, that requires
	 * three more temporaries, for a total of six big integers, or
	 * slightly more than 3 kB of stack space for RSA-4096. This
	 * exceeds our stack requirements.
	 *
	 * Instead, we first use one step of the extended GCD:
	 *
	 *   - We compute phi = k*e + r  (Euclidean division of phi by e).
	 *     If public exponent e is correct, then r != 0 (e must be
	 *     invertible modulo phi). We also have k != 0 since we
	 *     enforce non-ridiculously-small factors.
	 *
	 *   - We find small u, v such that u*e - v*r = 1  (using a
	 *     binary GCD; we can arrange for u < r and v < e, i.e. all
	 *     values fit on 32 bits).
	 *
	 *   - Solution is: d = u + v*k
	 *     This last computation is exact: since u < r and v < e,
	 *     the above implies d < r + e*((phi-r)/e) = phi
	 */

	uint16_t tmp[4 * ((BR_MAX_RSA_FACTOR + 14) / 15) + 12];
	uint16_t *p, *q, *k, *m, *z, *phi;
	const unsigned char *pbuf, *qbuf;
	size_t plen, qlen, u, len, dlen;
	uint32_t r, a, b, u0, v0, u1, v1, he, hr;
	int i;

	/*
	 * Check that e is correct.
	 */
	if (e < 3 || (e & 1) == 0) {
		return 0;
	}

	/*
	 * Check lengths of p and q, and that they are both odd.
	 */
	pbuf = sk->p;
	plen = sk->plen;
	while (plen > 0 && *pbuf == 0) {
		pbuf ++;
		plen --;
	}
	if (plen < 5 || plen > (BR_MAX_RSA_FACTOR / 8)
		|| (pbuf[plen - 1] & 1) != 1)
	{
		return 0;
	}
	qbuf = sk->q;
	qlen = sk->qlen;
	while (qlen > 0 && *qbuf == 0) {
		qbuf ++;
		qlen --;
	}
	if (qlen < 5 || qlen > (BR_MAX_RSA_FACTOR / 8)
		|| (qbuf[qlen - 1] & 1) != 1)
	{
		return 0;
	}

	/*
	 * Output length is that of the modulus.
	 */
	dlen = (sk->n_bitlen + 7) >> 3;
	if (d == NULL) {
		return dlen;
	}

	p = tmp;
	br_i15_decode(p, pbuf, plen);
	plen = (p[0] + 15) >> 4;
	q = p + 1 + plen;
	br_i15_decode(q, qbuf, qlen);
	qlen = (q[0] + 15) >> 4;

	/*
	 * Compute phi = (p-1)*(q-1), then move it over p-1 and q-1 (that
	 * we do not need anymore). The mulacc function sets the announced
	 * bit length of t to be the sum of the announced bit lengths of
	 * p-1 and q-1, which is usually exact but may overshoot by one 1
	 * bit in some cases; we readjust it to its true length.
	 */
	p[1] --;
	q[1] --;
	phi = q + 1 + qlen;
	br_i15_zero(phi, p[0]);
	br_i15_mulacc(phi, p, q);
	len = (phi[0] + 15) >> 4;
	memmove(tmp, phi, (1 + len) * sizeof *phi);
	phi = tmp;
	phi[0] = br_i15_bit_length(phi + 1, len);
	len = (phi[0] + 15) >> 4;

	/*
	 * Divide phi by public exponent e. The final remainder r must be
	 * non-zero (otherwise, the key is invalid). The quotient is k,
	 * which we write over phi, since we don't need phi after that.
	 */
	r = 0;
	for (u = len; u >= 1; u --) {
		/*
		 * Upon entry, r < e, and phi[u] < 2^15; hence,
		 * hi:lo < e*2^15. Thus, the produced word k[u]
		 * must be lower than 2^15, and the new remainder r
		 * is lower than e.
		 */
		uint32_t hi, lo;

		hi = r >> 17;
		lo = (r << 15) + phi[u];
		phi[u] = br_divrem(hi, lo, e, &r);
	}
	if (r == 0) {
		return 0;
	}
	k = phi;

	/*
	 * Compute u and v such that u*e - v*r = GCD(e,r). We use
	 * a binary GCD algorithm, with 6 extra integers a, b,
	 * u0, u1, v0 and v1. Initial values are:
	 *   a = e    u0 = 1   v0 = 0
	 *   b = r    u1 = r   v1 = e-1
	 * The following invariants are maintained:
	 *   a = u0*e - v0*r
	 *   b = u1*e - v1*r
	 *   0 < a <= e
	 *   0 < b <= r
	 *   0 <= u0 <= r
	 *   0 <= v0 <= e
	 *   0 <= u1 <= r
	 *   0 <= v1 <= e
	 *
	 * At each iteration, we reduce either a or b by one bit, and
	 * adjust u0, u1, v0 and v1 to maintain the invariants:
	 *  - if a is even, then a <- a/2
	 *  - otherwise, if b is even, then b <- b/2
	 *  - otherwise, if a > b, then a <- (a-b)/2
	 *  - otherwise, if b > a, then b <- (b-a)/2
	 * Algorithm stops when a = b. At that point, the common value
	 * is the GCD of e and r; it must be 1 (otherwise, the private
	 * key or public exponent is not valid). The (u0,v0) or (u1,v1)
	 * pairs are the solution we are looking for.
	 *
	 * Since either a or b is reduced by at least 1 bit at each
	 * iteration, 62 iterations are enough to reach the end
	 * condition.
	 *
	 * To maintain the invariants, we must compute the same operations
	 * on the u* and v* values that we do on a and b:
	 *  - When a is divided by 2, u0 and v0 must be divided by 2.
	 *  - When b is divided by 2, u1 and v1 must be divided by 2.
	 *  - When b is subtracted from a, u1 and v1 are subtracted from
	 *    u0 and v0, respectively.
	 *  - When a is subtracted from b, u0 and v0 are subtracted from
	 *    u1 and v1, respectively.
	 *
	 * However, we want to keep the u* and v* values in their proper
	 * ranges. The following remarks apply:
	 *
	 *  - When a is divided by 2, then a is even. Therefore:
	 *
	 *     * If r is odd, then u0 and v0 must have the same parity;
	 *       if they are both odd, then adding r to u0 and e to v0
	 *       makes them both even, and the division by 2 brings them
	 *       back to the proper range.
	 *
	 *     * If r is even, then u0 must be even; if v0 is odd, then
	 *       adding r to u0 and e to v0 makes them both even, and the
	 *       division by 2 brings them back to the proper range.
	 *
	 *    Thus, all we need to do is to look at the parity of v0,
	 *    and add (r,e) to (u0,v0) when v0 is odd. In order to avoid
	 *    a 32-bit overflow, we can add ((r+1)/2,(e/2)+1) after the
	 *    division (r+1 does not overflow since r < e; and (e/2)+1
	 *    is equal to (e+1)/2 since e is odd).
	 *
	 *  - When we subtract b from a, three cases may occur:
	 *
	 *     * u1 <= u0 and v1 <= v0: just do the subtractions
	 *
	 *     * u1 > u0 and v1 > v0: compute:
	 *         (u0, v0) <- (u0 + r - u1, v0 + e - v1)
	 *
	 *     * u1 <= u0 and v1 > v0: compute:
	 *         (u0, v0) <- (u0 + r - u1, v0 + e - v1)
	 *
	 *    The fourth case (u1 > u0 and v1 <= v0) is not possible
	 *    because it would contradict "b < a" (which is the reason
	 *    why we subtract b from a).
	 *
	 *    The tricky case is the third one: from the equations, it
	 *    seems that u0 may go out of range. However, the invariants
	 *    and ranges of other values imply that, in that case, the
	 *    new u0 does not actually exceed the range.
	 *
	 *    We can thus handle the subtraction by adding (r,e) based
	 *    solely on the comparison between v0 and v1.
	 */
	a = e;
	b = r;
	u0 = 1;
	v0 = 0;
	u1 = r;
	v1 = e - 1;
	hr = (r + 1) >> 1;
	he = (e >> 1) + 1;
	for (i = 0; i < 62; i ++) {
		uint32_t oa, ob, agtb, bgta;
		uint32_t sab, sba, da, db;
		uint32_t ctl;

		oa = a & 1;                  /* 1 if a is odd */
		ob = b & 1;                  /* 1 if b is odd */
		agtb = GT(a, b);             /* 1 if a > b */
		bgta = GT(b, a);             /* 1 if b > a */

		sab = oa & ob & agtb;        /* 1 if a <- a-b */
		sba = oa & ob & bgta;        /* 1 if b <- b-a */

		/* a <- a-b, u0 <- u0-u1, v0 <- v0-v1 */
		ctl = GT(v1, v0);
		a -= b & -sab;
		u0 -= (u1 - (r & -ctl)) & -sab;
		v0 -= (v1 - (e & -ctl)) & -sab;

		/* b <- b-a, u1 <- u1-u0 mod r, v1 <- v1-v0 mod e */
		ctl = GT(v0, v1);
		b -= a & -sba;
		u1 -= (u0 - (r & -ctl)) & -sba;
		v1 -= (v0 - (e & -ctl)) & -sba;

		da = NOT(oa) | sab;          /* 1 if a <- a/2 */
		db = (oa & NOT(ob)) | sba;   /* 1 if b <- b/2 */

		/* a <- a/2, u0 <- u0/2, v0 <- v0/2 */
		ctl = v0 & 1;
		a ^= (a ^ (a >> 1)) & -da;
		u0 ^= (u0 ^ ((u0 >> 1) + (hr & -ctl))) & -da;
		v0 ^= (v0 ^ ((v0 >> 1) + (he & -ctl))) & -da;

		/* b <- b/2, u1 <- u1/2 mod r, v1 <- v1/2 mod e */
		ctl = v1 & 1;
		b ^= (b ^ (b >> 1)) & -db;
		u1 ^= (u1 ^ ((u1 >> 1) + (hr & -ctl))) & -db;
		v1 ^= (v1 ^ ((v1 >> 1) + (he & -ctl))) & -db;
	}

	/*
	 * Check that the GCD is indeed 1. If not, then the key is invalid
	 * (and there's no harm in leaking that piece of information).
	 */
	if (a != 1) {
		return 0;
	}

	/*
	 * Now we have u0*e - v0*r = 1. Let's compute the result as:
	 *   d = u0 + v0*k
	 * We still have k in the tmp[] array, and its announced bit
	 * length is that of phi.
	 */
	m = k + 1 + len;
	m[0] = (2 << 4) + 2;  /* bit length is 32 bits, encoded */
	m[1] = v0 & 0x7FFF;
	m[2] = (v0 >> 15) & 0x7FFF;
	m[3] = v0 >> 30;
	z = m + 4;
	br_i15_zero(z, k[0]);
	z[1] = u0 & 0x7FFF;
	z[2] = (u0 >> 15) & 0x7FFF;
	z[3] = u0 >> 30;
	br_i15_mulacc(z, k, m);

	/*
	 * Encode the result.
	 */
	br_i15_encode(d, dlen, z);
	return dlen;
}
