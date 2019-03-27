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
 * Make a random integer of the provided size. The size is encoded.
 * The header word is untouched.
 */
static void
mkrand(const br_prng_class **rng, uint32_t *x, uint32_t esize)
{
	size_t u, len;
	unsigned m;

	len = (esize + 31) >> 5;
	(*rng)->generate(rng, x + 1, len * sizeof(uint32_t));
	for (u = 1; u < len; u ++) {
		x[u] &= 0x7FFFFFFF;
	}
	m = esize & 31;
	if (m == 0) {
		x[len] &= 0x7FFFFFFF;
	} else {
		x[len] &= 0x7FFFFFFF >> (31 - m);
	}
}

/*
 * This is the big-endian unsigned representation of the product of
 * all small primes from 13 to 1481.
 */
static const unsigned char SMALL_PRIMES[] = {
	0x2E, 0xAB, 0x92, 0xD1, 0x8B, 0x12, 0x47, 0x31, 0x54, 0x0A,
	0x99, 0x5D, 0x25, 0x5E, 0xE2, 0x14, 0x96, 0x29, 0x1E, 0xB7,
	0x78, 0x70, 0xCC, 0x1F, 0xA5, 0xAB, 0x8D, 0x72, 0x11, 0x37,
	0xFB, 0xD8, 0x1E, 0x3F, 0x5B, 0x34, 0x30, 0x17, 0x8B, 0xE5,
	0x26, 0x28, 0x23, 0xA1, 0x8A, 0xA4, 0x29, 0xEA, 0xFD, 0x9E,
	0x39, 0x60, 0x8A, 0xF3, 0xB5, 0xA6, 0xEB, 0x3F, 0x02, 0xB6,
	0x16, 0xC3, 0x96, 0x9D, 0x38, 0xB0, 0x7D, 0x82, 0x87, 0x0C,
	0xF7, 0xBE, 0x24, 0xE5, 0x5F, 0x41, 0x04, 0x79, 0x76, 0x40,
	0xE7, 0x00, 0x22, 0x7E, 0xB5, 0x85, 0x7F, 0x8D, 0x01, 0x50,
	0xE9, 0xD3, 0x29, 0x42, 0x08, 0xB3, 0x51, 0x40, 0x7B, 0xD7,
	0x8D, 0xCC, 0x10, 0x01, 0x64, 0x59, 0x28, 0xB6, 0x53, 0xF3,
	0x50, 0x4E, 0xB1, 0xF2, 0x58, 0xCD, 0x6E, 0xF5, 0x56, 0x3E,
	0x66, 0x2F, 0xD7, 0x07, 0x7F, 0x52, 0x4C, 0x13, 0x24, 0xDC,
	0x8E, 0x8D, 0xCC, 0xED, 0x77, 0xC4, 0x21, 0xD2, 0xFD, 0x08,
	0xEA, 0xD7, 0xC0, 0x5C, 0x13, 0x82, 0x81, 0x31, 0x2F, 0x2B,
	0x08, 0xE4, 0x80, 0x04, 0x7A, 0x0C, 0x8A, 0x3C, 0xDC, 0x22,
	0xE4, 0x5A, 0x7A, 0xB0, 0x12, 0x5E, 0x4A, 0x76, 0x94, 0x77,
	0xC2, 0x0E, 0x92, 0xBA, 0x8A, 0xA0, 0x1F, 0x14, 0x51, 0x1E,
	0x66, 0x6C, 0x38, 0x03, 0x6C, 0xC7, 0x4A, 0x4B, 0x70, 0x80,
	0xAF, 0xCA, 0x84, 0x51, 0xD8, 0xD2, 0x26, 0x49, 0xF5, 0xA8,
	0x5E, 0x35, 0x4B, 0xAC, 0xCE, 0x29, 0x92, 0x33, 0xB7, 0xA2,
	0x69, 0x7D, 0x0C, 0xE0, 0x9C, 0xDB, 0x04, 0xD6, 0xB4, 0xBC,
	0x39, 0xD7, 0x7F, 0x9E, 0x9D, 0x78, 0x38, 0x7F, 0x51, 0x54,
	0x50, 0x8B, 0x9E, 0x9C, 0x03, 0x6C, 0xF5, 0x9D, 0x2C, 0x74,
	0x57, 0xF0, 0x27, 0x2A, 0xC3, 0x47, 0xCA, 0xB9, 0xD7, 0x5C,
	0xFF, 0xC2, 0xAC, 0x65, 0x4E, 0xBD
};

/*
 * We need temporary values for at least 7 integers of the same size
 * as a factor (including header word); more space helps with performance
 * (in modular exponentiations), but we much prefer to remain under
 * 2 kilobytes in total, to save stack space. The macro TEMPS below
 * exceeds 512 (which is a count in 32-bit words) when BR_MAX_RSA_SIZE
 * is greater than 4464 (default value is 4096, so the 2-kB limit is
 * maintained unless BR_MAX_RSA_SIZE was modified).
 */
#define MAX(x, y)   ((x) > (y) ? (x) : (y))
#define ROUND2(x)   ((((x) + 1) >> 1) << 1)

#define TEMPS   MAX(512, ROUND2(7 * ((((BR_MAX_RSA_SIZE + 1) >> 1) + 61) / 31)))

/*
 * Perform trial division on a candidate prime. This computes
 * y = SMALL_PRIMES mod x, then tries to compute y/y mod x. The
 * br_i31_moddiv() function will report an error if y is not invertible
 * modulo x. Returned value is 1 on success (none of the small primes
 * divides x), 0 on error (a non-trivial GCD is obtained).
 *
 * This function assumes that x is odd.
 */
static uint32_t
trial_divisions(const uint32_t *x, uint32_t *t)
{
	uint32_t *y;
	uint32_t x0i;

	y = t;
	t += 1 + ((x[0] + 31) >> 5);
	x0i = br_i31_ninv31(x[1]);
	br_i31_decode_reduce(y, SMALL_PRIMES, sizeof SMALL_PRIMES, x);
	return br_i31_moddiv(y, y, x, x0i, t);
}

/*
 * Perform n rounds of Miller-Rabin on the candidate prime x. This
 * function assumes that x = 3 mod 4.
 *
 * Returned value is 1 on success (all rounds completed successfully),
 * 0 otherwise.
 */
static uint32_t
miller_rabin(const br_prng_class **rng, const uint32_t *x, int n,
	uint32_t *t, size_t tlen, br_i31_modpow_opt_type mp31)
{
	/*
	 * Since x = 3 mod 4, the Miller-Rabin test is simple:
	 *  - get a random base a (such that 1 < a < x-1)
	 *  - compute z = a^((x-1)/2) mod x
	 *  - if z != 1 and z != x-1, the number x is composite
	 *
	 * We generate bases 'a' randomly with a size which is
	 * one bit less than x, which ensures that a < x-1. It
	 * is not useful to verify that a > 1 because the probability
	 * that we get a value a equal to 0 or 1 is much smaller
	 * than the probability of our Miller-Rabin tests not to
	 * detect a composite, which is already quite smaller than the
	 * probability of the hardware misbehaving and return a
	 * composite integer because of some glitch (e.g. bad RAM
	 * or ill-timed cosmic ray).
	 */
	unsigned char *xm1d2;
	size_t xlen, xm1d2_len, xm1d2_len_u32, u;
	uint32_t asize;
	unsigned cc;
	uint32_t x0i;

	/*
	 * Compute (x-1)/2 (encoded).
	 */
	xm1d2 = (unsigned char *)t;
	xm1d2_len = ((x[0] - (x[0] >> 5)) + 7) >> 3;
	br_i31_encode(xm1d2, xm1d2_len, x);
	cc = 0;
	for (u = 0; u < xm1d2_len; u ++) {
		unsigned w;

		w = xm1d2[u];
		xm1d2[u] = (unsigned char)((w >> 1) | cc);
		cc = w << 7;
	}

	/*
	 * We used some words of the provided buffer for (x-1)/2.
	 */
	xm1d2_len_u32 = (xm1d2_len + 3) >> 2;
	t += xm1d2_len_u32;
	tlen -= xm1d2_len_u32;

	xlen = (x[0] + 31) >> 5;
	asize = x[0] - 1 - EQ0(x[0] & 31);
	x0i = br_i31_ninv31(x[1]);
	while (n -- > 0) {
		uint32_t *a, *t2;
		uint32_t eq1, eqm1;
		size_t t2len;

		/*
		 * Generate a random base. We don't need the base to be
		 * really uniform modulo x, so we just get a random
		 * number which is one bit shorter than x.
		 */
		a = t;
		a[0] = x[0];
		a[xlen] = 0;
		mkrand(rng, a, asize);

		/*
		 * Compute a^((x-1)/2) mod x. We assume here that the
		 * function will not fail (the temporary array is large
		 * enough).
		 */
		t2 = t + 1 + xlen;
		t2len = tlen - 1 - xlen;
		if ((t2len & 1) != 0) {
			/*
			 * Since the source array is 64-bit aligned and
			 * has an even number of elements (TEMPS), we
			 * can use the parity of the remaining length to
			 * detect and adjust alignment.
			 */
			t2 ++;
			t2len --;
		}
		mp31(a, xm1d2, xm1d2_len, x, x0i, t2, t2len);

		/*
		 * We must obtain either 1 or x-1. Note that x is odd,
		 * hence x-1 differs from x only in its low word (no
		 * carry).
		 */
		eq1 = a[1] ^ 1;
		eqm1 = a[1] ^ (x[1] - 1);
		for (u = 2; u <= xlen; u ++) {
			eq1 |= a[u];
			eqm1 |= a[u] ^ x[u];
		}

		if ((EQ0(eq1) | EQ0(eqm1)) == 0) {
			return 0;
		}
	}
	return 1;
}

/*
 * Create a random prime of the provided size. 'size' is the _encoded_
 * bit length. The two top bits and the two bottom bits are set to 1.
 */
static void
mkprime(const br_prng_class **rng, uint32_t *x, uint32_t esize,
	uint32_t pubexp, uint32_t *t, size_t tlen, br_i31_modpow_opt_type mp31)
{
	size_t len;

	x[0] = esize;
	len = (esize + 31) >> 5;
	for (;;) {
		size_t u;
		uint32_t m3, m5, m7, m11;
		int rounds, s7, s11;

		/*
		 * Generate random bits. We force the two top bits and the
		 * two bottom bits to 1.
		 */
		mkrand(rng, x, esize);
		if ((esize & 31) == 0) {
			x[len] |= 0x60000000;
		} else if ((esize & 31) == 1) {
			x[len] |= 0x00000001;
			x[len - 1] |= 0x40000000;
		} else {
			x[len] |= 0x00000003 << ((esize & 31) - 2);
		}
		x[1] |= 0x00000003;

		/*
		 * Trial division with low primes (3, 5, 7 and 11). We
		 * use the following properties:
		 *
		 *   2^2 = 1 mod 3
		 *   2^4 = 1 mod 5
		 *   2^3 = 1 mod 7
		 *   2^10 = 1 mod 11
		 */
		m3 = 0;
		m5 = 0;
		m7 = 0;
		m11 = 0;
		s7 = 0;
		s11 = 0;
		for (u = 0; u < len; u ++) {
			uint32_t w, w3, w5, w7, w11;

			w = x[1 + u];
			w3 = (w & 0xFFFF) + (w >> 16);     /* max: 98302 */
			w5 = (w & 0xFFFF) + (w >> 16);     /* max: 98302 */
			w7 = (w & 0x7FFF) + (w >> 15);     /* max: 98302 */
			w11 = (w & 0xFFFFF) + (w >> 20);   /* max: 1050622 */

			m3 += w3 << (u & 1);
			m3 = (m3 & 0xFF) + (m3 >> 8);      /* max: 1025 */

			m5 += w5 << ((4 - u) & 3);
			m5 = (m5 & 0xFFF) + (m5 >> 12);    /* max: 4479 */

			m7 += w7 << s7;
			m7 = (m7 & 0x1FF) + (m7 >> 9);     /* max: 1280 */
			if (++ s7 == 3) {
				s7 = 0;
			}

			m11 += w11 << s11;
			if (++ s11 == 10) {
				s11 = 0;
			}
			m11 = (m11 & 0x3FF) + (m11 >> 10); /* max: 526847 */
		}

		m3 = (m3 & 0x3F) + (m3 >> 6);      /* max: 78 */
		m3 = (m3 & 0x0F) + (m3 >> 4);      /* max: 18 */
		m3 = ((m3 * 43) >> 5) & 3;

		m5 = (m5 & 0xFF) + (m5 >> 8);      /* max: 271 */
		m5 = (m5 & 0x0F) + (m5 >> 4);      /* max: 31 */
		m5 -= 20 & -GT(m5, 19);
		m5 -= 10 & -GT(m5, 9);
		m5 -= 5 & -GT(m5, 4);

		m7 = (m7 & 0x3F) + (m7 >> 6);      /* max: 82 */
		m7 = (m7 & 0x07) + (m7 >> 3);      /* max: 16 */
		m7 = ((m7 * 147) >> 7) & 7;

		/*
		 * 2^5 = 32 = -1 mod 11.
		 */
		m11 = (m11 & 0x3FF) + (m11 >> 10);      /* max: 1536 */
		m11 = (m11 & 0x3FF) + (m11 >> 10);      /* max: 1023 */
		m11 = (m11 & 0x1F) + 33 - (m11 >> 5);   /* max: 64 */
		m11 -= 44 & -GT(m11, 43);
		m11 -= 22 & -GT(m11, 21);
		m11 -= 11 & -GT(m11, 10);

		/*
		 * If any of these modulo is 0, then the candidate is
		 * not prime. Also, if pubexp is 3, 5, 7 or 11, and the
		 * corresponding modulus is 1, then the candidate must
		 * be rejected, because we need e to be invertible
		 * modulo p-1. We can use simple comparisons here
		 * because they won't leak information on a candidate
		 * that we keep, only on one that we reject (and is thus
		 * not secret).
		 */
		if (m3 == 0 || m5 == 0 || m7 == 0 || m11 == 0) {
			continue;
		}
		if ((pubexp == 3 && m3 == 1)
			|| (pubexp == 5 && m5 == 5)
			|| (pubexp == 7 && m5 == 7)
			|| (pubexp == 11 && m5 == 11))
		{
			continue;
		}

		/*
		 * More trial divisions.
		 */
		if (!trial_divisions(x, t)) {
			continue;
		}

		/*
		 * Miller-Rabin algorithm. Since we selected a random
		 * integer, not a maliciously crafted integer, we can use
		 * relatively few rounds to lower the risk of a false
		 * positive (i.e. declaring prime a non-prime) under
		 * 2^(-80). It is not useful to lower the probability much
		 * below that, since that would be substantially below
		 * the probability of the hardware misbehaving. Sufficient
		 * numbers of rounds are extracted from the Handbook of
		 * Applied Cryptography, note 4.49 (page 149).
		 *
		 * Since we work on the encoded size (esize), we need to
		 * compare with encoded thresholds.
		 */
		if (esize < 309) {
			rounds = 12;
		} else if (esize < 464) {
			rounds = 9;
		} else if (esize < 670) {
			rounds = 6;
		} else if (esize < 877) {
			rounds = 4;
		} else if (esize < 1341) {
			rounds = 3;
		} else {
			rounds = 2;
		}

		if (miller_rabin(rng, x, rounds, t, tlen, mp31)) {
			return;
		}
	}
}

/*
 * Let p be a prime (p > 2^33, p = 3 mod 4). Let m = (p-1)/2, provided
 * as parameter (with announced bit length equal to that of p). This
 * function computes d = 1/e mod p-1 (for an odd integer e). Returned
 * value is 1 on success, 0 on error (an error is reported if e is not
 * invertible modulo p-1).
 *
 * The temporary buffer (t) must have room for at least 4 integers of
 * the size of p.
 */
static uint32_t
invert_pubexp(uint32_t *d, const uint32_t *m, uint32_t e, uint32_t *t)
{
	uint32_t *f;
	uint32_t r;

	f = t;
	t += 1 + ((m[0] + 31) >> 5);

	/*
	 * Compute d = 1/e mod m. Since p = 3 mod 4, m is odd.
	 */
	br_i31_zero(d, m[0]);
	d[1] = 1;
	br_i31_zero(f, m[0]);
	f[1] = e & 0x7FFFFFFF;
	f[2] = e >> 31;
	r = br_i31_moddiv(d, f, m, br_i31_ninv31(m[1]), t);

	/*
	 * We really want d = 1/e mod p-1, with p = 2m. By the CRT,
	 * the result is either the d we got, or d + m.
	 *
	 * Let's write e*d = 1 + k*m, for some integer k. Integers e
	 * and m are odd. If d is odd, then e*d is odd, which implies
	 * that k must be even; in that case, e*d = 1 + (k/2)*2m, and
	 * thus d is already fine. Conversely, if d is even, then k
	 * is odd, and we must add m to d in order to get the correct
	 * result.
	 */
	br_i31_add(d, m, (uint32_t)(1 - (d[1] & 1)));

	return r;
}

/*
 * Swap two buffers in RAM. They must be disjoint.
 */
static void
bufswap(void *b1, void *b2, size_t len)
{
	size_t u;
	unsigned char *buf1, *buf2;

	buf1 = b1;
	buf2 = b2;
	for (u = 0; u < len; u ++) {
		unsigned w;

		w = buf1[u];
		buf1[u] = buf2[u];
		buf2[u] = w;
	}
}

/* see inner.h */
uint32_t
br_rsa_i31_keygen_inner(const br_prng_class **rng,
	br_rsa_private_key *sk, void *kbuf_priv,
	br_rsa_public_key *pk, void *kbuf_pub,
	unsigned size, uint32_t pubexp, br_i31_modpow_opt_type mp31)
{
	uint32_t esize_p, esize_q;
	size_t plen, qlen, tlen;
	uint32_t *p, *q, *t;
	union {
		uint32_t t32[TEMPS];
		uint64_t t64[TEMPS >> 1];  /* for 64-bit alignment */
	} tmp;
	uint32_t r;

	if (size < BR_MIN_RSA_SIZE || size > BR_MAX_RSA_SIZE) {
		return 0;
	}
	if (pubexp == 0) {
		pubexp = 3;
	} else if (pubexp == 1 || (pubexp & 1) == 0) {
		return 0;
	}

	esize_p = (size + 1) >> 1;
	esize_q = size - esize_p;
	sk->n_bitlen = size;
	sk->p = kbuf_priv;
	sk->plen = (esize_p + 7) >> 3;
	sk->q = sk->p + sk->plen;
	sk->qlen = (esize_q + 7) >> 3;
	sk->dp = sk->q + sk->qlen;
	sk->dplen = sk->plen;
	sk->dq = sk->dp + sk->dplen;
	sk->dqlen = sk->qlen;
	sk->iq = sk->dq + sk->dqlen;
	sk->iqlen = sk->plen;

	if (pk != NULL) {
		pk->n = kbuf_pub;
		pk->nlen = (size + 7) >> 3;
		pk->e = pk->n + pk->nlen;
		pk->elen = 4;
		br_enc32be(pk->e, pubexp);
		while (*pk->e == 0) {
			pk->e ++;
			pk->elen --;
		}
	}

	/*
	 * We now switch to encoded sizes.
	 *
	 * floor((x * 16913) / (2^19)) is equal to floor(x/31) for all
	 * integers x from 0 to 34966; the intermediate product fits on
	 * 30 bits, thus we can use MUL31().
	 */
	esize_p += MUL31(esize_p, 16913) >> 19;
	esize_q += MUL31(esize_q, 16913) >> 19;
	plen = (esize_p + 31) >> 5;
	qlen = (esize_q + 31) >> 5;
	p = tmp.t32;
	q = p + 1 + plen;
	t = q + 1 + qlen;
	tlen = ((sizeof tmp.t32) / sizeof(uint32_t)) - (2 + plen + qlen);

	/*
	 * When looking for primes p and q, we temporarily divide
	 * candidates by 2, in order to compute the inverse of the
	 * public exponent.
	 */

	for (;;) {
		mkprime(rng, p, esize_p, pubexp, t, tlen, mp31);
		br_i31_rshift(p, 1);
		if (invert_pubexp(t, p, pubexp, t + 1 + plen)) {
			br_i31_add(p, p, 1);
			p[1] |= 1;
			br_i31_encode(sk->p, sk->plen, p);
			br_i31_encode(sk->dp, sk->dplen, t);
			break;
		}
	}

	for (;;) {
		mkprime(rng, q, esize_q, pubexp, t, tlen, mp31);
		br_i31_rshift(q, 1);
		if (invert_pubexp(t, q, pubexp, t + 1 + qlen)) {
			br_i31_add(q, q, 1);
			q[1] |= 1;
			br_i31_encode(sk->q, sk->qlen, q);
			br_i31_encode(sk->dq, sk->dqlen, t);
			break;
		}
	}

	/*
	 * If p and q have the same size, then it is possible that q > p
	 * (when the target modulus size is odd, we generate p with a
	 * greater bit length than q). If q > p, we want to swap p and q
	 * (and also dp and dq) for two reasons:
	 *  - The final step below (inversion of q modulo p) is easier if
	 *    p > q.
	 *  - While BearSSL's RSA code is perfectly happy with RSA keys such
	 *    that p < q, some other implementations have restrictions and
	 *    require p > q.
	 *
	 * Note that we can do a simple non-constant-time swap here,
	 * because the only information we leak here is that we insist on
	 * returning p and q such that p > q, which is not a secret.
	 */
	if (esize_p == esize_q && br_i31_sub(p, q, 0) == 1) {
		bufswap(p, q, (1 + plen) * sizeof *p);
		bufswap(sk->p, sk->q, sk->plen);
		bufswap(sk->dp, sk->dq, sk->dplen);
	}

	/*
	 * We have produced p, q, dp and dq. We can now compute iq = 1/d mod p.
	 *
	 * We ensured that p >= q, so this is just a matter of updating the
	 * header word for q (and possibly adding an extra word).
	 *
	 * Theoretically, the call below may fail, in case we were
	 * extraordinarily unlucky, and p = q. Another failure case is if
	 * Miller-Rabin failed us _twice_, and p and q are non-prime and
	 * have a factor is common. We report the error mostly because it
	 * is cheap and we can, but in practice this never happens (or, at
	 * least, it happens way less often than hardware glitches).
	 */
	q[0] = p[0];
	if (plen > qlen) {
		q[plen] = 0;
		t ++;
		tlen --;
	}
	br_i31_zero(t, p[0]);
	t[1] = 1;
	r = br_i31_moddiv(t, q, p, br_i31_ninv31(p[1]), t + 1 + plen);
	br_i31_encode(sk->iq, sk->iqlen, t);

	/*
	 * Compute the public modulus too, if required.
	 */
	if (pk != NULL) {
		br_i31_zero(t, p[0]);
		br_i31_mulacc(t, p, q);
		br_i31_encode(pk->n, pk->nlen, t);
	}

	return r;
}
