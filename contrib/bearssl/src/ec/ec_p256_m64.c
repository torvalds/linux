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

#if BR_INT128 || BR_UMUL128

#if BR_UMUL128
#include <intrin.h>
#endif

static const unsigned char P256_G[] = {
	0x04, 0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8,
	0xBC, 0xE6, 0xE5, 0x63, 0xA4, 0x40, 0xF2, 0x77, 0x03, 0x7D,
	0x81, 0x2D, 0xEB, 0x33, 0xA0, 0xF4, 0xA1, 0x39, 0x45, 0xD8,
	0x98, 0xC2, 0x96, 0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F,
	0x9B, 0x8E, 0xE7, 0xEB, 0x4A, 0x7C, 0x0F, 0x9E, 0x16, 0x2B,
	0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE, 0xCB, 0xB6, 0x40,
	0x68, 0x37, 0xBF, 0x51, 0xF5
};

static const unsigned char P256_N[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD,
	0xA7, 0x17, 0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63,
	0x25, 0x51
};

static const unsigned char *
api_generator(int curve, size_t *len)
{
	(void)curve;
	*len = sizeof P256_G;
	return P256_G;
}

static const unsigned char *
api_order(int curve, size_t *len)
{
	(void)curve;
	*len = sizeof P256_N;
	return P256_N;
}

static size_t
api_xoff(int curve, size_t *len)
{
	(void)curve;
	*len = 32;
	return 1;
}

/*
 * A field element is encoded as four 64-bit integers, in basis 2^64.
 * Values may reach up to 2^256-1. Montgomery multiplication is used.
 */

/* R = 2^256 mod p */
static const uint64_t F256_R[] = {
	0x0000000000000001, 0xFFFFFFFF00000000,
	0xFFFFFFFFFFFFFFFF, 0x00000000FFFFFFFE
};

/* Curve equation is y^2 = x^3 - 3*x + B. This constant is B*R mod p
   (Montgomery representation of B). */
static const uint64_t P256_B_MONTY[] = {
	0xD89CDF6229C4BDDF, 0xACF005CD78843090,
	0xE5A220ABF7212ED6, 0xDC30061D04874834
};

/*
 * Addition in the field.
 */
static inline void
f256_add(uint64_t *d, const uint64_t *a, const uint64_t *b)
{
#if BR_INT128
	unsigned __int128 w;
	uint64_t t;

	w = (unsigned __int128)a[0] + b[0];
	d[0] = (uint64_t)w;
	w = (unsigned __int128)a[1] + b[1] + (w >> 64);
	d[1] = (uint64_t)w;
	w = (unsigned __int128)a[2] + b[2] + (w >> 64);
	d[2] = (uint64_t)w;
	w = (unsigned __int128)a[3] + b[3] + (w >> 64);
	d[3] = (uint64_t)w;
	t = (uint64_t)(w >> 64);

	/*
	 * 2^256 = 2^224 - 2^192 - 2^96 + 1 in the field.
	 */
	w = (unsigned __int128)d[0] + t;
	d[0] = (uint64_t)w;
	w = (unsigned __int128)d[1] + (w >> 64) - (t << 32);
	d[1] = (uint64_t)w;
	/* Here, carry "w >> 64" can only be 0 or -1 */
	w = (unsigned __int128)d[2] - ((w >> 64) & 1);
	d[2] = (uint64_t)w;
	/* Again, carry is 0 or -1 */
	d[3] += (uint64_t)(w >> 64) + (t << 32) - t;

#elif BR_UMUL128

	unsigned char cc;
	uint64_t t;

	cc = _addcarry_u64(0, a[0], b[0], &d[0]);
	cc = _addcarry_u64(cc, a[1], b[1], &d[1]);
	cc = _addcarry_u64(cc, a[2], b[2], &d[2]);
	cc = _addcarry_u64(cc, a[3], b[3], &d[3]);

	/*
	 * If there is a carry, then we want to subtract p, which we
	 * do by adding 2^256 - p.
	 */
	t = cc;
	cc = _addcarry_u64(cc, d[0], 0, &d[0]);
	cc = _addcarry_u64(cc, d[1], -(t << 32), &d[1]);
	cc = _addcarry_u64(cc, d[2], -t, &d[2]);
	(void)_addcarry_u64(cc, d[3], (t << 32) - (t << 1), &d[3]);

#endif
}

/*
 * Subtraction in the field.
 */
static inline void
f256_sub(uint64_t *d, const uint64_t *a, const uint64_t *b)
{
#if BR_INT128

	unsigned __int128 w;
	uint64_t t;

	w = (unsigned __int128)a[0] - b[0];
	d[0] = (uint64_t)w;
	w = (unsigned __int128)a[1] - b[1] - ((w >> 64) & 1);
	d[1] = (uint64_t)w;
	w = (unsigned __int128)a[2] - b[2] - ((w >> 64) & 1);
	d[2] = (uint64_t)w;
	w = (unsigned __int128)a[3] - b[3] - ((w >> 64) & 1);
	d[3] = (uint64_t)w;
	t = (uint64_t)(w >> 64) & 1;

	/*
	 * p = 2^256 - 2^224 + 2^192 + 2^96 - 1.
	 */
	w = (unsigned __int128)d[0] - t;
	d[0] = (uint64_t)w;
	w = (unsigned __int128)d[1] + (t << 32) - ((w >> 64) & 1);
	d[1] = (uint64_t)w;
	/* Here, carry "w >> 64" can only be 0 or +1 */
	w = (unsigned __int128)d[2] + (w >> 64);
	d[2] = (uint64_t)w;
	/* Again, carry is 0 or +1 */
	d[3] += (uint64_t)(w >> 64) - (t << 32) + t;

#elif BR_UMUL128

	unsigned char cc;
	uint64_t t;

	cc = _subborrow_u64(0, a[0], b[0], &d[0]);
	cc = _subborrow_u64(cc, a[1], b[1], &d[1]);
	cc = _subborrow_u64(cc, a[2], b[2], &d[2]);
	cc = _subborrow_u64(cc, a[3], b[3], &d[3]);

	/*
	 * If there is a carry, then we need to add p.
	 */
	t = cc;
	cc = _addcarry_u64(0, d[0], -t, &d[0]);
	cc = _addcarry_u64(cc, d[1], (-t) >> 32, &d[1]);
	cc = _addcarry_u64(cc, d[2], 0, &d[2]);
	(void)_addcarry_u64(cc, d[3], t - (t << 32), &d[3]);

#endif
}

/*
 * Montgomery multiplication in the field.
 */
static void
f256_montymul(uint64_t *d, const uint64_t *a, const uint64_t *b)
{
#if BR_INT128

	uint64_t x, f, t0, t1, t2, t3, t4;
	unsigned __int128 z, ff;
	int i;

	/*
	 * When computing d <- d + a[u]*b, we also add f*p such
	 * that d + a[u]*b + f*p is a multiple of 2^64. Since
	 * p = -1 mod 2^64, we can compute f = d[0] + a[u]*b[0] mod 2^64.
	 */

	/*
	 * Step 1: t <- (a[0]*b + f*p) / 2^64
	 * We have f = a[0]*b[0] mod 2^64. Since p = -1 mod 2^64, this
	 * ensures that (a[0]*b + f*p) is a multiple of 2^64.
	 *
	 * We also have: f*p = f*2^256 - f*2^224 + f*2^192 + f*2^96 - f.
	 */
	x = a[0];
	z = (unsigned __int128)b[0] * x;
	f = (uint64_t)z;
	z = (unsigned __int128)b[1] * x + (z >> 64) + (uint64_t)(f << 32);
	t0 = (uint64_t)z;
	z = (unsigned __int128)b[2] * x + (z >> 64) + (uint64_t)(f >> 32);
	t1 = (uint64_t)z;
	z = (unsigned __int128)b[3] * x + (z >> 64) + f;
	t2 = (uint64_t)z;
	t3 = (uint64_t)(z >> 64);
	ff = ((unsigned __int128)f << 64) - ((unsigned __int128)f << 32);
	z = (unsigned __int128)t2 + (uint64_t)ff;
	t2 = (uint64_t)z;
	z = (unsigned __int128)t3 + (z >> 64) + (ff >> 64);
	t3 = (uint64_t)z;
	t4 = (uint64_t)(z >> 64);

	/*
	 * Steps 2 to 4: t <- (t + a[i]*b + f*p) / 2^64
	 */
	for (i = 1; i < 4; i ++) {
		x = a[i];

		/* t <- (t + x*b - f) / 2^64 */
		z = (unsigned __int128)b[0] * x + t0;
		f = (uint64_t)z;
		z = (unsigned __int128)b[1] * x + t1 + (z >> 64);
		t0 = (uint64_t)z;
		z = (unsigned __int128)b[2] * x + t2 + (z >> 64);
		t1 = (uint64_t)z;
		z = (unsigned __int128)b[3] * x + t3 + (z >> 64);
		t2 = (uint64_t)z;
		z = t4 + (z >> 64);
		t3 = (uint64_t)z;
		t4 = (uint64_t)(z >> 64);

		/* t <- t + f*2^32, carry in the upper half of z */
		z = (unsigned __int128)t0 + (uint64_t)(f << 32);
		t0 = (uint64_t)z;
		z = (z >> 64) + (unsigned __int128)t1 + (uint64_t)(f >> 32);
		t1 = (uint64_t)z;

		/* t <- t + f*2^192 - f*2^160 + f*2^128 */
		ff = ((unsigned __int128)f << 64) 
			- ((unsigned __int128)f << 32) + f;
		z = (z >> 64) + (unsigned __int128)t2 + (uint64_t)ff;
		t2 = (uint64_t)z;
		z = (unsigned __int128)t3 + (z >> 64) + (ff >> 64);
		t3 = (uint64_t)z;
		t4 += (uint64_t)(z >> 64);
	}

	/*
	 * At that point, we have computed t = (a*b + F*p) / 2^256, where
	 * F is a 256-bit integer whose limbs are the "f" coefficients
	 * in the steps above. We have:
	 *   a <= 2^256-1
	 *   b <= 2^256-1
	 *   F <= 2^256-1
	 * Hence:
	 *   a*b + F*p <= (2^256-1)*(2^256-1) + p*(2^256-1)
	 *   a*b + F*p <= 2^256*(2^256 - 2 + p) + 1 - p
	 * Therefore:
	 *   t < 2^256 + p - 2
	 * Since p < 2^256, it follows that:
	 *   t4 can be only 0 or 1
	 *   t - p < 2^256
	 * We can therefore subtract p from t, conditionally on t4, to
	 * get a nonnegative result that fits on 256 bits.
	 */
	z = (unsigned __int128)t0 + t4;
	t0 = (uint64_t)z;
	z = (unsigned __int128)t1 - (t4 << 32) + (z >> 64);
	t1 = (uint64_t)z;
	z = (unsigned __int128)t2 - (z >> 127);
	t2 = (uint64_t)z;
	t3 = t3 - (uint64_t)(z >> 127) - t4 + (t4 << 32);

	d[0] = t0;
	d[1] = t1;
	d[2] = t2;
	d[3] = t3;

#elif BR_UMUL128

	uint64_t x, f, t0, t1, t2, t3, t4;
	uint64_t zl, zh, ffl, ffh;
	unsigned char k, m;
	int i;

	/*
	 * When computing d <- d + a[u]*b, we also add f*p such
	 * that d + a[u]*b + f*p is a multiple of 2^64. Since
	 * p = -1 mod 2^64, we can compute f = d[0] + a[u]*b[0] mod 2^64.
	 */

	/*
	 * Step 1: t <- (a[0]*b + f*p) / 2^64
	 * We have f = a[0]*b[0] mod 2^64. Since p = -1 mod 2^64, this
	 * ensures that (a[0]*b + f*p) is a multiple of 2^64.
	 *
	 * We also have: f*p = f*2^256 - f*2^224 + f*2^192 + f*2^96 - f.
	 */
	x = a[0];

	zl = _umul128(b[0], x, &zh);
	f = zl;
	t0 = zh;

	zl = _umul128(b[1], x, &zh);
	k = _addcarry_u64(0, zl, t0, &zl);
	(void)_addcarry_u64(k, zh, 0, &zh);
	k = _addcarry_u64(0, zl, f << 32, &zl);
	(void)_addcarry_u64(k, zh, 0, &zh);
	t0 = zl;
	t1 = zh;

	zl = _umul128(b[2], x, &zh);
	k = _addcarry_u64(0, zl, t1, &zl);
	(void)_addcarry_u64(k, zh, 0, &zh);
	k = _addcarry_u64(0, zl, f >> 32, &zl);
	(void)_addcarry_u64(k, zh, 0, &zh);
	t1 = zl;
	t2 = zh;

	zl = _umul128(b[3], x, &zh);
	k = _addcarry_u64(0, zl, t2, &zl);
	(void)_addcarry_u64(k, zh, 0, &zh);
	k = _addcarry_u64(0, zl, f, &zl);
	(void)_addcarry_u64(k, zh, 0, &zh);
	t2 = zl;
	t3 = zh;

	t4 = _addcarry_u64(0, t3, f, &t3);
	k = _subborrow_u64(0, t2, f << 32, &t2);
	k = _subborrow_u64(k, t3, f >> 32, &t3);
	(void)_subborrow_u64(k, t4, 0, &t4);

	/*
	 * Steps 2 to 4: t <- (t + a[i]*b + f*p) / 2^64
	 */
	for (i = 1; i < 4; i ++) {
		x = a[i];
		/* f = t0 + x * b[0]; -- computed below */

		/* t <- (t + x*b - f) / 2^64 */
		zl = _umul128(b[0], x, &zh);
		k = _addcarry_u64(0, zl, t0, &f);
		(void)_addcarry_u64(k, zh, 0, &t0);

		zl = _umul128(b[1], x, &zh);
		k = _addcarry_u64(0, zl, t0, &zl);
		(void)_addcarry_u64(k, zh, 0, &zh);
		k = _addcarry_u64(0, zl, t1, &t0);
		(void)_addcarry_u64(k, zh, 0, &t1);

		zl = _umul128(b[2], x, &zh);
		k = _addcarry_u64(0, zl, t1, &zl);
		(void)_addcarry_u64(k, zh, 0, &zh);
		k = _addcarry_u64(0, zl, t2, &t1);
		(void)_addcarry_u64(k, zh, 0, &t2);

		zl = _umul128(b[3], x, &zh);
		k = _addcarry_u64(0, zl, t2, &zl);
		(void)_addcarry_u64(k, zh, 0, &zh);
		k = _addcarry_u64(0, zl, t3, &t2);
		(void)_addcarry_u64(k, zh, 0, &t3);

		t4 = _addcarry_u64(0, t3, t4, &t3);

		/* t <- t + f*2^32, carry in k */
		k = _addcarry_u64(0, t0, f << 32, &t0);
		k = _addcarry_u64(k, t1, f >> 32, &t1);

		/* t <- t + f*2^192 - f*2^160 + f*2^128 */
		m = _subborrow_u64(0, f, f << 32, &ffl);
		(void)_subborrow_u64(m, f, f >> 32, &ffh);
		k = _addcarry_u64(k, t2, ffl, &t2);
		k = _addcarry_u64(k, t3, ffh, &t3);
		(void)_addcarry_u64(k, t4, 0, &t4);
	}

	/*
	 * At that point, we have computed t = (a*b + F*p) / 2^256, where
	 * F is a 256-bit integer whose limbs are the "f" coefficients
	 * in the steps above. We have:
	 *   a <= 2^256-1
	 *   b <= 2^256-1
	 *   F <= 2^256-1
	 * Hence:
	 *   a*b + F*p <= (2^256-1)*(2^256-1) + p*(2^256-1)
	 *   a*b + F*p <= 2^256*(2^256 - 2 + p) + 1 - p
	 * Therefore:
	 *   t < 2^256 + p - 2
	 * Since p < 2^256, it follows that:
	 *   t4 can be only 0 or 1
	 *   t - p < 2^256
	 * We can therefore subtract p from t, conditionally on t4, to
	 * get a nonnegative result that fits on 256 bits.
	 */
	k = _addcarry_u64(0, t0, t4, &t0);
	k = _addcarry_u64(k, t1, -(t4 << 32), &t1);
	k = _addcarry_u64(k, t2, -t4, &t2);
	(void)_addcarry_u64(k, t3, (t4 << 32) - (t4 << 1), &t3);

	d[0] = t0;
	d[1] = t1;
	d[2] = t2;
	d[3] = t3;

#endif
}

/*
 * Montgomery squaring in the field; currently a basic wrapper around
 * multiplication (inline, should be optimized away).
 * TODO: see if some extra speed can be gained here.
 */
static inline void
f256_montysquare(uint64_t *d, const uint64_t *a)
{
	f256_montymul(d, a, a);
}

/*
 * Convert to Montgomery representation.
 */
static void
f256_tomonty(uint64_t *d, const uint64_t *a)
{
	/*
	 * R2 = 2^512 mod p.
	 * If R = 2^256 mod p, then R2 = R^2 mod p; and the Montgomery
	 * multiplication of a by R2 is: a*R2/R = a*R mod p, i.e. the
	 * conversion to Montgomery representation.
	 */
	static const uint64_t R2[] = {
		0x0000000000000003,
		0xFFFFFFFBFFFFFFFF,
		0xFFFFFFFFFFFFFFFE,
		0x00000004FFFFFFFD
	};

	f256_montymul(d, a, R2);
}

/*
 * Convert from Montgomery representation.
 */
static void
f256_frommonty(uint64_t *d, const uint64_t *a)
{
	/*
	 * Montgomery multiplication by 1 is division by 2^256 modulo p.
	 */
	static const uint64_t one[] = { 1, 0, 0, 0 };

	f256_montymul(d, a, one);
}

/*
 * Inversion in the field. If the source value is 0 modulo p, then this
 * returns 0 or p. This function uses Montgomery representation.
 */
static void
f256_invert(uint64_t *d, const uint64_t *a)
{
	/*
	 * We compute a^(p-2) mod p. The exponent pattern (from high to
	 * low) is:
	 *  - 32 bits of value 1
	 *  - 31 bits of value 0
	 *  - 1 bit of value 1
	 *  - 96 bits of value 0
	 *  - 94 bits of value 1
	 *  - 1 bit of value 0
	 *  - 1 bit of value 1
	 * To speed up the square-and-multiply algorithm, we precompute
	 * a^(2^31-1).
	 */

	uint64_t r[4], t[4];
	int i;

	memcpy(t, a, sizeof t);
	for (i = 0; i < 30; i ++) {
		f256_montysquare(t, t);
		f256_montymul(t, t, a);
	}

	memcpy(r, t, sizeof t);
	for (i = 224; i >= 0; i --) {
		f256_montysquare(r, r);
		switch (i) {
		case 0:
		case 2:
		case 192:
		case 224:
			f256_montymul(r, r, a);
			break;
		case 3:
		case 34:
		case 65:
			f256_montymul(r, r, t);
			break;
		}
	}
	memcpy(d, r, sizeof r);
}

/*
 * Finalize reduction.
 * Input value fits on 256 bits. This function subtracts p if and only
 * if the input is greater than or equal to p.
 */
static inline void
f256_final_reduce(uint64_t *a)
{
#if BR_INT128

	uint64_t t0, t1, t2, t3, cc;
	unsigned __int128 z;

	/*
	 * We add 2^224 - 2^192 - 2^96 + 1 to a. If there is no carry,
	 * then a < p; otherwise, the addition result we computed is
	 * the value we must return.
	 */
	z = (unsigned __int128)a[0] + 1;
	t0 = (uint64_t)z;
	z = (unsigned __int128)a[1] + (z >> 64) - ((uint64_t)1 << 32);
	t1 = (uint64_t)z;
	z = (unsigned __int128)a[2] - (z >> 127);
	t2 = (uint64_t)z;
	z = (unsigned __int128)a[3] - (z >> 127) + 0xFFFFFFFF;
	t3 = (uint64_t)z;
	cc = -(uint64_t)(z >> 64);

	a[0] ^= cc & (a[0] ^ t0);
	a[1] ^= cc & (a[1] ^ t1);
	a[2] ^= cc & (a[2] ^ t2);
	a[3] ^= cc & (a[3] ^ t3);

#elif BR_UMUL128

	uint64_t t0, t1, t2, t3, m;
	unsigned char k;

	k = _addcarry_u64(0, a[0], (uint64_t)1, &t0);
	k = _addcarry_u64(k, a[1], -((uint64_t)1 << 32), &t1);
	k = _addcarry_u64(k, a[2], -(uint64_t)1, &t2);
	k = _addcarry_u64(k, a[3], ((uint64_t)1 << 32) - 2, &t3);
	m = -(uint64_t)k;

	a[0] ^= m & (a[0] ^ t0);
	a[1] ^= m & (a[1] ^ t1);
	a[2] ^= m & (a[2] ^ t2);
	a[3] ^= m & (a[3] ^ t3);

#endif
}

/*
 * Points in affine and Jacobian coordinates.
 *
 *  - In affine coordinates, the point-at-infinity cannot be encoded.
 *  - Jacobian coordinates (X,Y,Z) correspond to affine (X/Z^2,Y/Z^3);
 *    if Z = 0 then this is the point-at-infinity.
 */
typedef struct {
	uint64_t x[4];
	uint64_t y[4];
} p256_affine;

typedef struct {
	uint64_t x[4];
	uint64_t y[4];
	uint64_t z[4];
} p256_jacobian;

/*
 * Decode a point. The returned point is in Jacobian coordinates, but
 * with z = 1. If the encoding is invalid, or encodes a point which is
 * not on the curve, or encodes the point at infinity, then this function
 * returns 0. Otherwise, 1 is returned.
 *
 * The buffer is assumed to have length exactly 65 bytes.
 */
static uint32_t
point_decode(p256_jacobian *P, const unsigned char *buf)
{
	uint64_t x[4], y[4], t[4], x3[4], tt;
	uint32_t r;

	/*
	 * Header byte shall be 0x04.
	 */
	r = EQ(buf[0], 0x04);

	/*
	 * Decode X and Y coordinates, and convert them into
	 * Montgomery representation.
	 */
	x[3] = br_dec64be(buf +  1);
	x[2] = br_dec64be(buf +  9);
	x[1] = br_dec64be(buf + 17);
	x[0] = br_dec64be(buf + 25);
	y[3] = br_dec64be(buf + 33);
	y[2] = br_dec64be(buf + 41);
	y[1] = br_dec64be(buf + 49);
	y[0] = br_dec64be(buf + 57);
	f256_tomonty(x, x);
	f256_tomonty(y, y);

	/*
	 * Verify y^2 = x^3 + A*x + B. In curve P-256, A = -3.
	 * Note that the Montgomery representation of 0 is 0. We must
	 * take care to apply the final reduction to make sure we have
	 * 0 and not p.
	 */
	f256_montysquare(t, y);
	f256_montysquare(x3, x);
	f256_montymul(x3, x3, x);
	f256_sub(t, t, x3);
	f256_add(t, t, x);
	f256_add(t, t, x);
	f256_add(t, t, x);
	f256_sub(t, t, P256_B_MONTY);
	f256_final_reduce(t);
	tt = t[0] | t[1] | t[2] | t[3];
	r &= EQ((uint32_t)(tt | (tt >> 32)), 0);

	/*
	 * Return the point in Jacobian coordinates (and Montgomery
	 * representation).
	 */
	memcpy(P->x, x, sizeof x);
	memcpy(P->y, y, sizeof y);
	memcpy(P->z, F256_R, sizeof F256_R);
	return r;
}

/*
 * Final conversion for a point:
 *  - The point is converted back to affine coordinates.
 *  - Final reduction is performed.
 *  - The point is encoded into the provided buffer.
 *
 * If the point is the point-at-infinity, all operations are performed,
 * but the buffer contents are indeterminate, and 0 is returned. Otherwise,
 * the encoded point is written in the buffer, and 1 is returned.
 */
static uint32_t
point_encode(unsigned char *buf, const p256_jacobian *P)
{
	uint64_t t1[4], t2[4], z;

	/* Set t1 = 1/z^2 and t2 = 1/z^3. */
	f256_invert(t2, P->z);
	f256_montysquare(t1, t2);
	f256_montymul(t2, t2, t1);

	/* Compute affine coordinates x (in t1) and y (in t2). */
	f256_montymul(t1, P->x, t1);
	f256_montymul(t2, P->y, t2);

	/* Convert back from Montgomery representation, and finalize
	   reductions. */
	f256_frommonty(t1, t1);
	f256_frommonty(t2, t2);
	f256_final_reduce(t1);
	f256_final_reduce(t2);

	/* Encode. */
	buf[0] = 0x04;
	br_enc64be(buf +  1, t1[3]);
	br_enc64be(buf +  9, t1[2]);
	br_enc64be(buf + 17, t1[1]);
	br_enc64be(buf + 25, t1[0]);
	br_enc64be(buf + 33, t2[3]);
	br_enc64be(buf + 41, t2[2]);
	br_enc64be(buf + 49, t2[1]);
	br_enc64be(buf + 57, t2[0]);

	/* Return success if and only if P->z != 0. */
	z = P->z[0] | P->z[1] | P->z[2] | P->z[3];
	return NEQ((uint32_t)(z | z >> 32), 0);
}

/*
 * Point doubling in Jacobian coordinates: point P is doubled.
 * Note: if the source point is the point-at-infinity, then the result is
 * still the point-at-infinity, which is correct. Moreover, if the three
 * coordinates were zero, then they still are zero in the returned value.
 *
 * (Note: this is true even without the final reduction: if the three
 * coordinates are encoded as four words of value zero each, then the
 * result will also have all-zero coordinate encodings, not the alternate
 * encoding as the integer p.)
 */
static void
p256_double(p256_jacobian *P)
{
	/*
	 * Doubling formulas are:
	 *
	 *   s = 4*x*y^2
	 *   m = 3*(x + z^2)*(x - z^2)
	 *   x' = m^2 - 2*s
	 *   y' = m*(s - x') - 8*y^4
	 *   z' = 2*y*z
	 *
	 * These formulas work for all points, including points of order 2
	 * and points at infinity:
	 *   - If y = 0 then z' = 0. But there is no such point in P-256
	 *     anyway.
	 *   - If z = 0 then z' = 0.
	 */
	uint64_t t1[4], t2[4], t3[4], t4[4];

	/*
	 * Compute z^2 in t1.
	 */
	f256_montysquare(t1, P->z);

	/*
	 * Compute x-z^2 in t2 and x+z^2 in t1.
	 */
	f256_add(t2, P->x, t1);
	f256_sub(t1, P->x, t1);

	/*
	 * Compute 3*(x+z^2)*(x-z^2) in t1.
	 */
	f256_montymul(t3, t1, t2);
	f256_add(t1, t3, t3);
	f256_add(t1, t3, t1);

	/*
	 * Compute 4*x*y^2 (in t2) and 2*y^2 (in t3).
	 */
	f256_montysquare(t3, P->y);
	f256_add(t3, t3, t3);
	f256_montymul(t2, P->x, t3);
	f256_add(t2, t2, t2);

	/*
	 * Compute x' = m^2 - 2*s.
	 */
	f256_montysquare(P->x, t1);
	f256_sub(P->x, P->x, t2);
	f256_sub(P->x, P->x, t2);

	/*
	 * Compute z' = 2*y*z.
	 */
	f256_montymul(t4, P->y, P->z);
	f256_add(P->z, t4, t4);

	/*
	 * Compute y' = m*(s - x') - 8*y^4. Note that we already have
	 * 2*y^2 in t3.
	 */
	f256_sub(t2, t2, P->x);
	f256_montymul(P->y, t1, t2);
	f256_montysquare(t4, t3);
	f256_add(t4, t4, t4);
	f256_sub(P->y, P->y, t4);
}

/*
 * Point addition (Jacobian coordinates): P1 is replaced with P1+P2.
 * This function computes the wrong result in the following cases:
 *
 *   - If P1 == 0 but P2 != 0
 *   - If P1 != 0 but P2 == 0
 *   - If P1 == P2
 *
 * In all three cases, P1 is set to the point at infinity.
 *
 * Returned value is 0 if one of the following occurs:
 *
 *   - P1 and P2 have the same Y coordinate.
 *   - P1 == 0 and P2 == 0.
 *   - The Y coordinate of one of the points is 0 and the other point is
 *     the point at infinity.
 *
 * The third case cannot actually happen with valid points, since a point
 * with Y == 0 is a point of order 2, and there is no point of order 2 on
 * curve P-256.
 *
 * Therefore, assuming that P1 != 0 and P2 != 0 on input, then the caller
 * can apply the following:
 *
 *   - If the result is not the point at infinity, then it is correct.
 *   - Otherwise, if the returned value is 1, then this is a case of
 *     P1+P2 == 0, so the result is indeed the point at infinity.
 *   - Otherwise, P1 == P2, so a "double" operation should have been
 *     performed.
 *
 * Note that you can get a returned value of 0 with a correct result,
 * e.g. if P1 and P2 have the same Y coordinate, but distinct X coordinates.
 */
static uint32_t
p256_add(p256_jacobian *P1, const p256_jacobian *P2)
{
	/*
	 * Addtions formulas are:
	 *
	 *   u1 = x1 * z2^2
	 *   u2 = x2 * z1^2
	 *   s1 = y1 * z2^3
	 *   s2 = y2 * z1^3
	 *   h = u2 - u1
	 *   r = s2 - s1
	 *   x3 = r^2 - h^3 - 2 * u1 * h^2
	 *   y3 = r * (u1 * h^2 - x3) - s1 * h^3
	 *   z3 = h * z1 * z2
	 */
	uint64_t t1[4], t2[4], t3[4], t4[4], t5[4], t6[4], t7[4], tt;
	uint32_t ret;

	/*
	 * Compute u1 = x1*z2^2 (in t1) and s1 = y1*z2^3 (in t3).
	 */
	f256_montysquare(t3, P2->z);
	f256_montymul(t1, P1->x, t3);
	f256_montymul(t4, P2->z, t3);
	f256_montymul(t3, P1->y, t4);

	/*
	 * Compute u2 = x2*z1^2 (in t2) and s2 = y2*z1^3 (in t4).
	 */
	f256_montysquare(t4, P1->z);
	f256_montymul(t2, P2->x, t4);
	f256_montymul(t5, P1->z, t4);
	f256_montymul(t4, P2->y, t5);

	/*
	 * Compute h = h2 - u1 (in t2) and r = s2 - s1 (in t4).
	 * We need to test whether r is zero, so we will do some extra
	 * reduce.
	 */
	f256_sub(t2, t2, t1);
	f256_sub(t4, t4, t3);
	f256_final_reduce(t4);
	tt = t4[0] | t4[1] | t4[2] | t4[3];
	ret = (uint32_t)(tt | (tt >> 32));
	ret = (ret | -ret) >> 31;

	/*
	 * Compute u1*h^2 (in t6) and h^3 (in t5);
	 */
	f256_montysquare(t7, t2);
	f256_montymul(t6, t1, t7);
	f256_montymul(t5, t7, t2);

	/*
	 * Compute x3 = r^2 - h^3 - 2*u1*h^2.
	 */
	f256_montysquare(P1->x, t4);
	f256_sub(P1->x, P1->x, t5);
	f256_sub(P1->x, P1->x, t6);
	f256_sub(P1->x, P1->x, t6);

	/*
	 * Compute y3 = r*(u1*h^2 - x3) - s1*h^3.
	 */
	f256_sub(t6, t6, P1->x);
	f256_montymul(P1->y, t4, t6);
	f256_montymul(t1, t5, t3);
	f256_sub(P1->y, P1->y, t1);

	/*
	 * Compute z3 = h*z1*z2.
	 */
	f256_montymul(t1, P1->z, P2->z);
	f256_montymul(P1->z, t1, t2);

	return ret;
}

/*
 * Point addition (mixed coordinates): P1 is replaced with P1+P2.
 * This is a specialised function for the case when P2 is a non-zero point
 * in affine coordinates.
 *
 * This function computes the wrong result in the following cases:
 *
 *   - If P1 == 0
 *   - If P1 == P2
 *
 * In both cases, P1 is set to the point at infinity.
 *
 * Returned value is 0 if one of the following occurs:
 *
 *   - P1 and P2 have the same Y (affine) coordinate.
 *   - The Y coordinate of P2 is 0 and P1 is the point at infinity.
 *
 * The second case cannot actually happen with valid points, since a point
 * with Y == 0 is a point of order 2, and there is no point of order 2 on
 * curve P-256.
 *
 * Therefore, assuming that P1 != 0 on input, then the caller
 * can apply the following:
 *
 *   - If the result is not the point at infinity, then it is correct.
 *   - Otherwise, if the returned value is 1, then this is a case of
 *     P1+P2 == 0, so the result is indeed the point at infinity.
 *   - Otherwise, P1 == P2, so a "double" operation should have been
 *     performed.
 *
 * Again, a value of 0 may be returned in some cases where the addition
 * result is correct.
 */
static uint32_t
p256_add_mixed(p256_jacobian *P1, const p256_affine *P2)
{
	/*
	 * Addtions formulas are:
	 *
	 *   u1 = x1
	 *   u2 = x2 * z1^2
	 *   s1 = y1
	 *   s2 = y2 * z1^3
	 *   h = u2 - u1
	 *   r = s2 - s1
	 *   x3 = r^2 - h^3 - 2 * u1 * h^2
	 *   y3 = r * (u1 * h^2 - x3) - s1 * h^3
	 *   z3 = h * z1
	 */
	uint64_t t1[4], t2[4], t3[4], t4[4], t5[4], t6[4], t7[4], tt;
	uint32_t ret;

	/*
	 * Compute u1 = x1 (in t1) and s1 = y1 (in t3).
	 */
	memcpy(t1, P1->x, sizeof t1);
	memcpy(t3, P1->y, sizeof t3);

	/*
	 * Compute u2 = x2*z1^2 (in t2) and s2 = y2*z1^3 (in t4).
	 */
	f256_montysquare(t4, P1->z);
	f256_montymul(t2, P2->x, t4);
	f256_montymul(t5, P1->z, t4);
	f256_montymul(t4, P2->y, t5);

	/*
	 * Compute h = h2 - u1 (in t2) and r = s2 - s1 (in t4).
	 * We need to test whether r is zero, so we will do some extra
	 * reduce.
	 */
	f256_sub(t2, t2, t1);
	f256_sub(t4, t4, t3);
	f256_final_reduce(t4);
	tt = t4[0] | t4[1] | t4[2] | t4[3];
	ret = (uint32_t)(tt | (tt >> 32));
	ret = (ret | -ret) >> 31;

	/*
	 * Compute u1*h^2 (in t6) and h^3 (in t5);
	 */
	f256_montysquare(t7, t2);
	f256_montymul(t6, t1, t7);
	f256_montymul(t5, t7, t2);

	/*
	 * Compute x3 = r^2 - h^3 - 2*u1*h^2.
	 */
	f256_montysquare(P1->x, t4);
	f256_sub(P1->x, P1->x, t5);
	f256_sub(P1->x, P1->x, t6);
	f256_sub(P1->x, P1->x, t6);

	/*
	 * Compute y3 = r*(u1*h^2 - x3) - s1*h^3.
	 */
	f256_sub(t6, t6, P1->x);
	f256_montymul(P1->y, t4, t6);
	f256_montymul(t1, t5, t3);
	f256_sub(P1->y, P1->y, t1);

	/*
	 * Compute z3 = h*z1*z2.
	 */
	f256_montymul(P1->z, P1->z, t2);

	return ret;
}

#if 0
/* unused */
/*
 * Point addition (mixed coordinates, complete): P1 is replaced with P1+P2.
 * This is a specialised function for the case when P2 is a non-zero point
 * in affine coordinates.
 *
 * This function returns the correct result in all cases.
 */
static uint32_t
p256_add_complete_mixed(p256_jacobian *P1, const p256_affine *P2)
{
	/*
	 * Addtions formulas, in the general case, are:
	 *
	 *   u1 = x1
	 *   u2 = x2 * z1^2
	 *   s1 = y1
	 *   s2 = y2 * z1^3
	 *   h = u2 - u1
	 *   r = s2 - s1
	 *   x3 = r^2 - h^3 - 2 * u1 * h^2
	 *   y3 = r * (u1 * h^2 - x3) - s1 * h^3
	 *   z3 = h * z1
	 *
	 * These formulas mishandle the two following cases:
	 *
	 *  - If P1 is the point-at-infinity (z1 = 0), then z3 is
	 *    incorrectly set to 0.
	 *
	 *  - If P1 = P2, then u1 = u2 and s1 = s2, and x3, y3 and z3
	 *    are all set to 0.
	 *
	 * However, if P1 + P2 = 0, then u1 = u2 but s1 != s2, and then
	 * we correctly get z3 = 0 (the point-at-infinity).
	 *
	 * To fix the case P1 = 0, we perform at the end a copy of P2
	 * over P1, conditional to z1 = 0.
	 *
	 * For P1 = P2: in that case, both h and r are set to 0, and
	 * we get x3, y3 and z3 equal to 0. We can test for that
	 * occurrence to make a mask which will be all-one if P1 = P2,
	 * or all-zero otherwise; then we can compute the double of P2
	 * and add it, combined with the mask, to (x3,y3,z3).
	 *
	 * Using the doubling formulas in p256_double() on (x2,y2),
	 * simplifying since P2 is affine (i.e. z2 = 1, implicitly),
	 * we get:
	 *   s = 4*x2*y2^2
	 *   m = 3*(x2 + 1)*(x2 - 1)
	 *   x' = m^2 - 2*s
	 *   y' = m*(s - x') - 8*y2^4
	 *   z' = 2*y2
	 * which requires only 6 multiplications. Added to the 11
	 * multiplications of the normal mixed addition in Jacobian
	 * coordinates, we get a cost of 17 multiplications in total.
	 */
	uint64_t t1[4], t2[4], t3[4], t4[4], t5[4], t6[4], t7[4], tt, zz;
	int i;

	/*
	 * Set zz to -1 if P1 is the point at infinity, 0 otherwise.
	 */
	zz = P1->z[0] | P1->z[1] | P1->z[2] | P1->z[3];
	zz = ((zz | -zz) >> 63) - (uint64_t)1;

	/*
	 * Compute u1 = x1 (in t1) and s1 = y1 (in t3).
	 */
	memcpy(t1, P1->x, sizeof t1);
	memcpy(t3, P1->y, sizeof t3);

	/*
	 * Compute u2 = x2*z1^2 (in t2) and s2 = y2*z1^3 (in t4).
	 */
	f256_montysquare(t4, P1->z);
	f256_montymul(t2, P2->x, t4);
	f256_montymul(t5, P1->z, t4);
	f256_montymul(t4, P2->y, t5);

	/*
	 * Compute h = h2 - u1 (in t2) and r = s2 - s1 (in t4).
	 * reduce.
	 */
	f256_sub(t2, t2, t1);
	f256_sub(t4, t4, t3);

	/*
	 * If both h = 0 and r = 0, then P1 = P2, and we want to set
	 * the mask tt to -1; otherwise, the mask will be 0.
	 */
	f256_final_reduce(t2);
	f256_final_reduce(t4);
	tt = t2[0] | t2[1] | t2[2] | t2[3] | t4[0] | t4[1] | t4[2] | t4[3];
	tt = ((tt | -tt) >> 63) - (uint64_t)1;

	/*
	 * Compute u1*h^2 (in t6) and h^3 (in t5);
	 */
	f256_montysquare(t7, t2);
	f256_montymul(t6, t1, t7);
	f256_montymul(t5, t7, t2);

	/*
	 * Compute x3 = r^2 - h^3 - 2*u1*h^2.
	 */
	f256_montysquare(P1->x, t4);
	f256_sub(P1->x, P1->x, t5);
	f256_sub(P1->x, P1->x, t6);
	f256_sub(P1->x, P1->x, t6);

	/*
	 * Compute y3 = r*(u1*h^2 - x3) - s1*h^3.
	 */
	f256_sub(t6, t6, P1->x);
	f256_montymul(P1->y, t4, t6);
	f256_montymul(t1, t5, t3);
	f256_sub(P1->y, P1->y, t1);

	/*
	 * Compute z3 = h*z1.
	 */
	f256_montymul(P1->z, P1->z, t2);

	/*
	 * The "double" result, in case P1 = P2.
	 */

	/*
	 * Compute z' = 2*y2 (in t1).
	 */
	f256_add(t1, P2->y, P2->y);

	/*
	 * Compute 2*(y2^2) (in t2) and s = 4*x2*(y2^2) (in t3).
	 */
	f256_montysquare(t2, P2->y);
	f256_add(t2, t2, t2);
	f256_add(t3, t2, t2);
	f256_montymul(t3, P2->x, t3);

	/*
	 * Compute m = 3*(x2^2 - 1) (in t4).
	 */
	f256_montysquare(t4, P2->x);
	f256_sub(t4, t4, F256_R);
	f256_add(t5, t4, t4);
	f256_add(t4, t4, t5);

	/*
	 * Compute x' = m^2 - 2*s (in t5).
	 */
	f256_montysquare(t5, t4);
	f256_sub(t5, t3);
	f256_sub(t5, t3);

	/*
	 * Compute y' = m*(s - x') - 8*y2^4 (in t6).
	 */
	f256_sub(t6, t3, t5);
	f256_montymul(t6, t6, t4);
	f256_montysquare(t7, t2);
	f256_sub(t6, t6, t7);
	f256_sub(t6, t6, t7);

	/*
	 * We now have the alternate (doubling) coordinates in (t5,t6,t1).
	 * We combine them with (x3,y3,z3).
	 */
	for (i = 0; i < 4; i ++) {
		P1->x[i] |= tt & t5[i];
		P1->y[i] |= tt & t6[i];
		P1->z[i] |= tt & t1[i];
	}

	/*
	 * If P1 = 0, then we get z3 = 0 (which is invalid); if z1 is 0,
	 * then we want to replace the result with a copy of P2. The
	 * test on z1 was done at the start, in the zz mask.
	 */
	for (i = 0; i < 4; i ++) {
		P1->x[i] ^= zz & (P1->x[i] ^ P2->x[i]);
		P1->y[i] ^= zz & (P1->y[i] ^ P2->y[i]);
		P1->z[i] ^= zz & (P1->z[i] ^ F256_R[i]);
	}
}
#endif

/*
 * Inner function for computing a point multiplication. A window is
 * provided, with points 1*P to 15*P in affine coordinates.
 *
 * Assumptions:
 *  - All provided points are valid points on the curve.
 *  - Multiplier is non-zero, and smaller than the curve order.
 *  - Everything is in Montgomery representation.
 */
static void
point_mul_inner(p256_jacobian *R, const p256_affine *W,
	const unsigned char *k, size_t klen)
{
	p256_jacobian Q;
	uint32_t qz;

	memset(&Q, 0, sizeof Q);
	qz = 1;
	while (klen -- > 0) {
		int i;
		unsigned bk;

		bk = *k ++;
		for (i = 0; i < 2; i ++) {
			uint32_t bits;
			uint32_t bnz;
			p256_affine T;
			p256_jacobian U;
			uint32_t n;
			int j;
			uint64_t m;

			p256_double(&Q);
			p256_double(&Q);
			p256_double(&Q);
			p256_double(&Q);
			bits = (bk >> 4) & 0x0F;
			bnz = NEQ(bits, 0);

			/*
			 * Lookup point in window. If the bits are 0,
			 * we get something invalid, which is not a
			 * problem because we will use it only if the
			 * bits are non-zero.
			 */
			memset(&T, 0, sizeof T);
			for (n = 0; n < 15; n ++) {
				m = -(uint64_t)EQ(bits, n + 1);
				T.x[0] |= m & W[n].x[0];
				T.x[1] |= m & W[n].x[1];
				T.x[2] |= m & W[n].x[2];
				T.x[3] |= m & W[n].x[3];
				T.y[0] |= m & W[n].y[0];
				T.y[1] |= m & W[n].y[1];
				T.y[2] |= m & W[n].y[2];
				T.y[3] |= m & W[n].y[3];
			}

			U = Q;
			p256_add_mixed(&U, &T);

			/*
			 * If qz is still 1, then Q was all-zeros, and this
			 * is conserved through p256_double().
			 */
			m = -(uint64_t)(bnz & qz);
			for (j = 0; j < 4; j ++) {
				Q.x[j] |= m & T.x[j];
				Q.y[j] |= m & T.y[j];
				Q.z[j] |= m & F256_R[j];
			}
			CCOPY(bnz & ~qz, &Q, &U, sizeof Q);
			qz &= ~bnz;
			bk <<= 4;
		}
	}
	*R = Q;
}

/*
 * Convert a window from Jacobian to affine coordinates. A single
 * field inversion is used. This function works for windows up to
 * 32 elements.
 *
 * The destination array (aff[]) and the source array (jac[]) may
 * overlap, provided that the start of aff[] is not after the start of
 * jac[]. Even if the arrays do _not_ overlap, the source array is
 * modified.
 */
static void
window_to_affine(p256_affine *aff, p256_jacobian *jac, int num)
{
	/*
	 * Convert the window points to affine coordinates. We use the
	 * following trick to mutualize the inversion computation: if
	 * we have z1, z2, z3, and z4, and want to inverse all of them,
	 * we compute u = 1/(z1*z2*z3*z4), and then we have:
	 *   1/z1 = u*z2*z3*z4
	 *   1/z2 = u*z1*z3*z4
	 *   1/z3 = u*z1*z2*z4
	 *   1/z4 = u*z1*z2*z3
	 *
	 * The partial products are computed recursively:
	 *
	 *  - on input (z_1,z_2), return (z_2,z_1) and z_1*z_2
	 *  - on input (z_1,z_2,... z_n):
	 *       recurse on (z_1,z_2,... z_(n/2)) -> r1 and m1
	 *       recurse on (z_(n/2+1),z_(n/2+2)... z_n) -> r2 and m2
	 *       multiply elements of r1 by m2 -> s1
	 *       multiply elements of r2 by m1 -> s2
	 *       return r1||r2 and m1*m2
	 *
	 * In the example below, we suppose that we have 14 elements.
	 * Let z1, z2,... zE be the 14 values to invert (index noted in
	 * hexadecimal, starting at 1).
	 *
	 *  - Depth 1:
	 *      swap(z1, z2); z12 = z1*z2
	 *      swap(z3, z4); z34 = z3*z4
	 *      swap(z5, z6); z56 = z5*z6
	 *      swap(z7, z8); z78 = z7*z8
	 *      swap(z9, zA); z9A = z9*zA
	 *      swap(zB, zC); zBC = zB*zC
	 *      swap(zD, zE); zDE = zD*zE
	 *
	 *  - Depth 2:
	 *      z1 <- z1*z34, z2 <- z2*z34, z3 <- z3*z12, z4 <- z4*z12
	 *      z1234 = z12*z34
	 *      z5 <- z5*z78, z6 <- z6*z78, z7 <- z7*z56, z8 <- z8*z56
	 *      z5678 = z56*z78
	 *      z9 <- z9*zBC, zA <- zA*zBC, zB <- zB*z9A, zC <- zC*z9A
	 *      z9ABC = z9A*zBC
	 *
	 *  - Depth 3:
	 *      z1 <- z1*z5678, z2 <- z2*z5678, z3 <- z3*z5678, z4 <- z4*z5678
	 *      z5 <- z5*z1234, z6 <- z6*z1234, z7 <- z7*z1234, z8 <- z8*z1234
	 *      z12345678 = z1234*z5678
	 *      z9 <- z9*zDE, zA <- zA*zDE, zB <- zB*zDE, zC <- zC*zDE
	 *      zD <- zD*z9ABC, zE*z9ABC
	 *      z9ABCDE = z9ABC*zDE
	 *
	 *  - Depth 4:
	 *      multiply z1..z8 by z9ABCDE
	 *      multiply z9..zE by z12345678
	 *      final z = z12345678*z9ABCDE
	 */

	uint64_t z[16][4];
	int i, k, s;
#define zt   (z[15])
#define zu   (z[14])
#define zv   (z[13])

	/*
	 * First recursion step (pairwise swapping and multiplication).
	 * If there is an odd number of elements, then we "invent" an
	 * extra one with coordinate Z = 1 (in Montgomery representation).
	 */
	for (i = 0; (i + 1) < num; i += 2) {
		memcpy(zt, jac[i].z, sizeof zt);
		memcpy(jac[i].z, jac[i + 1].z, sizeof zt);
		memcpy(jac[i + 1].z, zt, sizeof zt);
		f256_montymul(z[i >> 1], jac[i].z, jac[i + 1].z);
	}
	if ((num & 1) != 0) {
		memcpy(z[num >> 1], jac[num - 1].z, sizeof zt);
		memcpy(jac[num - 1].z, F256_R, sizeof F256_R);
	}

	/*
	 * Perform further recursion steps. At the entry of each step,
	 * the process has been done for groups of 's' points. The
	 * integer k is the log2 of s.
	 */
	for (k = 1, s = 2; s < num; k ++, s <<= 1) {
		int n;

		for (i = 0; i < num; i ++) {
			f256_montymul(jac[i].z, jac[i].z, z[(i >> k) ^ 1]);
		}
		n = (num + s - 1) >> k;
		for (i = 0; i < (n >> 1); i ++) {
			f256_montymul(z[i], z[i << 1], z[(i << 1) + 1]);
		}
		if ((n & 1) != 0) {
			memmove(z[n >> 1], z[n], sizeof zt);
		}
	}

	/*
	 * Invert the final result, and convert all points.
	 */
	f256_invert(zt, z[0]);
	for (i = 0; i < num; i ++) {
		f256_montymul(zv, jac[i].z, zt);
		f256_montysquare(zu, zv);
		f256_montymul(zv, zv, zu);
		f256_montymul(aff[i].x, jac[i].x, zu);
		f256_montymul(aff[i].y, jac[i].y, zv);
	}
}

/*
 * Multiply the provided point by an integer.
 * Assumptions:
 *  - Source point is a valid curve point.
 *  - Source point is not the point-at-infinity.
 *  - Integer is not 0, and is lower than the curve order.
 * If these conditions are not met, then the result is indeterminate
 * (but the process is still constant-time).
 */
static void
p256_mul(p256_jacobian *P, const unsigned char *k, size_t klen)
{
	union {
		p256_affine aff[15];
		p256_jacobian jac[15];
	} window;
	int i;

	/*
	 * Compute window, in Jacobian coordinates.
	 */
	window.jac[0] = *P;
	for (i = 2; i < 16; i ++) {
		window.jac[i - 1] = window.jac[(i >> 1) - 1];
		if ((i & 1) == 0) {
			p256_double(&window.jac[i - 1]);
		} else {
			p256_add(&window.jac[i - 1], &window.jac[i >> 1]);
		}
	}

	/*
	 * Convert the window points to affine coordinates. Point
	 * window[0] is the source point, already in affine coordinates.
	 */
	window_to_affine(window.aff, window.jac, 15);

	/*
	 * Perform point multiplication.
	 */
	point_mul_inner(P, window.aff, k, klen);
}

/*
 * Precomputed window for the conventional generator: P256_Gwin[n]
 * contains (n+1)*G (affine coordinates, in Montgomery representation).
 */
static const p256_affine P256_Gwin[] = {
	{
		{ 0x79E730D418A9143C, 0x75BA95FC5FEDB601,
		  0x79FB732B77622510, 0x18905F76A53755C6 },
		{ 0xDDF25357CE95560A, 0x8B4AB8E4BA19E45C,
		  0xD2E88688DD21F325, 0x8571FF1825885D85 }
	},
	{
		{ 0x850046D410DDD64D, 0xAA6AE3C1A433827D,
		  0x732205038D1490D9, 0xF6BB32E43DCF3A3B },
		{ 0x2F3648D361BEE1A5, 0x152CD7CBEB236FF8,
		  0x19A8FB0E92042DBE, 0x78C577510A5B8A3B }
	},
	{
		{ 0xFFAC3F904EEBC127, 0xB027F84A087D81FB,
		  0x66AD77DD87CBBC98, 0x26936A3FB6FF747E },
		{ 0xB04C5C1FC983A7EB, 0x583E47AD0861FE1A,
		  0x788208311A2EE98E, 0xD5F06A29E587CC07 }
	},
	{
		{ 0x74B0B50D46918DCC, 0x4650A6EDC623C173,
		  0x0CDAACACE8100AF2, 0x577362F541B0176B },
		{ 0x2D96F24CE4CBABA6, 0x17628471FAD6F447,
		  0x6B6C36DEE5DDD22E, 0x84B14C394C5AB863 }
	},
	{
		{ 0xBE1B8AAEC45C61F5, 0x90EC649A94B9537D,
		  0x941CB5AAD076C20C, 0xC9079605890523C8 },
		{ 0xEB309B4AE7BA4F10, 0x73C568EFE5EB882B,
		  0x3540A9877E7A1F68, 0x73A076BB2DD1E916 }
	},
	{
		{ 0x403947373E77664A, 0x55AE744F346CEE3E,
		  0xD50A961A5B17A3AD, 0x13074B5954213673 },
		{ 0x93D36220D377E44B, 0x299C2B53ADFF14B5,
		  0xF424D44CEF639F11, 0xA4C9916D4A07F75F }
	},
	{
		{ 0x0746354EA0173B4F, 0x2BD20213D23C00F7,
		  0xF43EAAB50C23BB08, 0x13BA5119C3123E03 },
		{ 0x2847D0303F5B9D4D, 0x6742F2F25DA67BDD,
		  0xEF933BDC77C94195, 0xEAEDD9156E240867 }
	},
	{
		{ 0x27F14CD19499A78F, 0x462AB5C56F9B3455,
		  0x8F90F02AF02CFC6B, 0xB763891EB265230D },
		{ 0xF59DA3A9532D4977, 0x21E3327DCF9EBA15,
		  0x123C7B84BE60BBF0, 0x56EC12F27706DF76 }
	},
	{
		{ 0x75C96E8F264E20E8, 0xABE6BFED59A7A841,
		  0x2CC09C0444C8EB00, 0xE05B3080F0C4E16B },
		{ 0x1EB7777AA45F3314, 0x56AF7BEDCE5D45E3,
		  0x2B6E019A88B12F1A, 0x086659CDFD835F9B }
	},
	{
		{ 0x2C18DBD19DC21EC8, 0x98F9868A0FCF8139,
		  0x737D2CD648250B49, 0xCC61C94724B3428F },
		{ 0x0C2B407880DD9E76, 0xC43A8991383FBE08,
		  0x5F7D2D65779BE5D2, 0x78719A54EB3B4AB5 }
	},
	{
		{ 0xEA7D260A6245E404, 0x9DE407956E7FDFE0,
		  0x1FF3A4158DAC1AB5, 0x3E7090F1649C9073 },
		{ 0x1A7685612B944E88, 0x250F939EE57F61C8,
		  0x0C0DAA891EAD643D, 0x68930023E125B88E }
	},
	{
		{ 0x04B71AA7D2697768, 0xABDEDEF5CA345A33,
		  0x2409D29DEE37385E, 0x4EE1DF77CB83E156 },
		{ 0x0CAC12D91CBB5B43, 0x170ED2F6CA895637,
		  0x28228CFA8ADE6D66, 0x7FF57C9553238ACA }
	},
	{
		{ 0xCCC425634B2ED709, 0x0E356769856FD30D,
		  0xBCBCD43F559E9811, 0x738477AC5395B759 },
		{ 0x35752B90C00EE17F, 0x68748390742ED2E3,
		  0x7CD06422BD1F5BC1, 0xFBC08769C9E7B797 }
	},
	{
		{ 0xA242A35BB0CF664A, 0x126E48F77F9707E3,
		  0x1717BF54C6832660, 0xFAAE7332FD12C72E },
		{ 0x27B52DB7995D586B, 0xBE29569E832237C2,
		  0xE8E4193E2A65E7DB, 0x152706DC2EAA1BBB }
	},
	{
		{ 0x72BCD8B7BC60055B, 0x03CC23EE56E27E4B,
		  0xEE337424E4819370, 0xE2AA0E430AD3DA09 },
		{ 0x40B8524F6383C45D, 0xD766355442A41B25,
		  0x64EFA6DE778A4797, 0x2042170A7079ADF4 }
	}
};

/*
 * Multiply the conventional generator of the curve by the provided
 * integer. Return is written in *P.
 *
 * Assumptions:
 *  - Integer is not 0, and is lower than the curve order.
 * If this conditions is not met, then the result is indeterminate
 * (but the process is still constant-time).
 */
static void
p256_mulgen(p256_jacobian *P, const unsigned char *k, size_t klen)
{
	point_mul_inner(P, P256_Gwin, k, klen);
}

/*
 * Return 1 if all of the following hold:
 *  - klen <= 32
 *  - k != 0
 *  - k is lower than the curve order
 * Otherwise, return 0.
 *
 * Constant-time behaviour: only klen may be observable.
 */
static uint32_t
check_scalar(const unsigned char *k, size_t klen)
{
	uint32_t z;
	int32_t c;
	size_t u;

	if (klen > 32) {
		return 0;
	}
	z = 0;
	for (u = 0; u < klen; u ++) {
		z |= k[u];
	}
	if (klen == 32) {
		c = 0;
		for (u = 0; u < klen; u ++) {
			c |= -(int32_t)EQ0(c) & CMP(k[u], P256_N[u]);
		}
	} else {
		c = -1;
	}
	return NEQ(z, 0) & LT0(c);
}

static uint32_t
api_mul(unsigned char *G, size_t Glen,
	const unsigned char *k, size_t klen, int curve)
{
	uint32_t r;
	p256_jacobian P;

	(void)curve;
	if (Glen != 65) {
		return 0;
	}
	r = check_scalar(k, klen);
	r &= point_decode(&P, G);
	p256_mul(&P, k, klen);
	r &= point_encode(G, &P);
	return r;
}

static size_t
api_mulgen(unsigned char *R,
	const unsigned char *k, size_t klen, int curve)
{
	p256_jacobian P;

	(void)curve;
	p256_mulgen(&P, k, klen);
	point_encode(R, &P);
	return 65;
}

static uint32_t
api_muladd(unsigned char *A, const unsigned char *B, size_t len,
	const unsigned char *x, size_t xlen,
	const unsigned char *y, size_t ylen, int curve)
{
	/*
	 * We might want to use Shamir's trick here: make a composite
	 * window of u*P+v*Q points, to merge the two doubling-ladders
	 * into one. This, however, has some complications:
	 *
	 *  - During the computation, we may hit the point-at-infinity.
	 *    Thus, we would need p256_add_complete_mixed() (complete
	 *    formulas for point addition), with a higher cost (17 muls
	 *    instead of 11).
	 *
	 *  - A 4-bit window would be too large, since it would involve
	 *    16*16-1 = 255 points. For the same window size as in the
	 *    p256_mul() case, we would need to reduce the window size
	 *    to 2 bits, and thus perform twice as many non-doubling
	 *    point additions.
	 *
	 *  - The window may itself contain the point-at-infinity, and
	 *    thus cannot be in all generality be made of affine points.
	 *    Instead, we would need to make it a window of points in
	 *    Jacobian coordinates. Even p256_add_complete_mixed() would
	 *    be inappropriate.
	 *
	 * For these reasons, the code below performs two separate
	 * point multiplications, then computes the final point addition
	 * (which is both a "normal" addition, and a doubling, to handle
	 * all cases).
	 */

	p256_jacobian P, Q;
	uint32_t r, t, s;
	uint64_t z;

	(void)curve;
	if (len != 65) {
		return 0;
	}
	r = point_decode(&P, A);
	p256_mul(&P, x, xlen);
	if (B == NULL) {
		p256_mulgen(&Q, y, ylen);
	} else {
		r &= point_decode(&Q, B);
		p256_mul(&Q, y, ylen);
	}

	/*
	 * The final addition may fail in case both points are equal.
	 */
	t = p256_add(&P, &Q);
	f256_final_reduce(P.z);
	z = P.z[0] | P.z[1] | P.z[2] | P.z[3];
	s = EQ((uint32_t)(z | (z >> 32)), 0);
	p256_double(&Q);

	/*
	 * If s is 1 then either P+Q = 0 (t = 1) or P = Q (t = 0). So we
	 * have the following:
	 *
	 *   s = 0, t = 0   return P (normal addition)
	 *   s = 0, t = 1   return P (normal addition)
	 *   s = 1, t = 0   return Q (a 'double' case)
	 *   s = 1, t = 1   report an error (P+Q = 0)
	 */
	CCOPY(s & ~t, &P, &Q, sizeof Q);
	point_encode(A, &P);
	r &= ~(s & t);
	return r;
}

/* see bearssl_ec.h */
const br_ec_impl br_ec_p256_m64 = {
	(uint32_t)0x00800000,
	&api_generator,
	&api_order,
	&api_xoff,
	&api_mul,
	&api_mulgen,
	&api_muladd
};

/* see bearssl_ec.h */
const br_ec_impl *
br_ec_p256_m64_get(void)
{
	return &br_ec_p256_m64;
}

#else

/* see bearssl_ec.h */
const br_ec_impl *
br_ec_p256_m64_get(void)
{
	return 0;
}

#endif
