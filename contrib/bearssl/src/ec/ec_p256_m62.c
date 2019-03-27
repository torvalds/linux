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
 * A field element is encoded as five 64-bit integers, in basis 2^52.
 * Limbs may occasionally exceed 2^52.
 *
 * A _partially reduced_ value is such that the following hold:
 *   - top limb is less than 2^48 + 2^30
 *   - the other limbs fit on 53 bits each
 * In particular, such a value is less than twice the modulus p.
 */

#define BIT(n)   ((uint64_t)1 << (n))
#define MASK48   (BIT(48) - BIT(0))
#define MASK52   (BIT(52) - BIT(0))

/* R = 2^260 mod p */
static const uint64_t F256_R[] = {
	0x0000000000010, 0xF000000000000, 0xFFFFFFFFFFFFF,
	0xFFEFFFFFFFFFF, 0x00000000FFFFF
};

/* Curve equation is y^2 = x^3 - 3*x + B. This constant is B*R mod p
   (Montgomery representation of B). */
static const uint64_t P256_B_MONTY[] = {
	0xDF6229C4BDDFD, 0xCA8843090D89C, 0x212ED6ACF005C,
	0x83415A220ABF7, 0x0C30061DD4874
};

/*
 * Addition in the field. Carry propagation is not performed.
 * On input, limbs may be up to 63 bits each; on output, they will
 * be up to one bit more than on input.
 */
static inline void
f256_add(uint64_t *d, const uint64_t *a, const uint64_t *b)
{
	d[0] = a[0] + b[0];
	d[1] = a[1] + b[1];
	d[2] = a[2] + b[2];
	d[3] = a[3] + b[3];
	d[4] = a[4] + b[4];
}

/*
 * Partially reduce the provided value.
 * Input: limbs can go up to 61 bits each.
 * Output: partially reduced.
 */
static inline void
f256_partial_reduce(uint64_t *a)
{
	uint64_t w, cc, s;

	/*
	 * Propagate carries.
	 */
	w = a[0];
	a[0] = w & MASK52;
	cc = w >> 52;
	w = a[1] + cc;
	a[1] = w & MASK52;
	cc = w >> 52;
	w = a[2] + cc;
	a[2] = w & MASK52;
	cc = w >> 52;
	w = a[3] + cc;
	a[3] = w & MASK52;
	cc = w >> 52;
	a[4] += cc;

	s = a[4] >> 48;             /* s < 2^14 */
	a[0] += s;                  /* a[0] < 2^52 + 2^14 */
	w = a[1] - (s << 44);
	a[1] = w & MASK52;          /* a[1] < 2^52 */
	cc = -(w >> 52) & 0xFFF;    /* cc < 16 */
	w = a[2] - cc;
	a[2] = w & MASK52;          /* a[2] < 2^52 */
	cc = w >> 63;               /* cc = 0 or 1 */
	w = a[3] - cc - (s << 36);
	a[3] = w & MASK52;          /* a[3] < 2^52 */
	cc = w >> 63;               /* cc = 0 or 1 */
	w = a[4] & MASK48;
	a[4] = w + (s << 16) - cc;  /* a[4] < 2^48 + 2^30 */
}

/*
 * Subtraction in the field.
 * Input: limbs must fit on 60 bits each; in particular, the complete
 * integer will be less than 2^268 + 2^217.
 * Output: partially reduced.
 */
static inline void
f256_sub(uint64_t *d, const uint64_t *a, const uint64_t *b)
{
	uint64_t t[5], w, s, cc;

	/*
	 * We compute d = 2^13*p + a - b; this ensures a positive
	 * intermediate value.
	 *
	 * Each individual addition/subtraction may yield a positive or
	 * negative result; thus, we need to handle a signed carry, thus
	 * with sign extension. We prefer not to use signed types (int64_t)
	 * because conversion from unsigned to signed is cumbersome (a
	 * direct cast with the top bit set is undefined behavior; instead,
	 * we have to use pointer aliasing, using the guaranteed properties
	 * of exact-width types, but this requires the compiler to optimize
	 * away the writes and reads from RAM), and right-shifting a
	 * signed negative value is implementation-defined. Therefore,
	 * we use a custom sign extension.
	 */

	w = a[0] - b[0] - BIT(13);
	t[0] = w & MASK52;
	cc = w >> 52;
	cc |= -(cc & BIT(11));
	w = a[1] - b[1] + cc;
	t[1] = w & MASK52;
	cc = w >> 52;
	cc |= -(cc & BIT(11));
	w = a[2] - b[2] + cc;
	t[2] = (w & MASK52) + BIT(5);
	cc = w >> 52;
	cc |= -(cc & BIT(11));
	w = a[3] - b[3] + cc;
	t[3] = (w & MASK52) + BIT(49);
	cc = w >> 52;
	cc |= -(cc & BIT(11));
	t[4] = (BIT(61) - BIT(29)) + a[4] - b[4] + cc;

	/*
	 * Perform partial reduction. Rule is:
	 *  2^256 = 2^224 - 2^192 - 2^96 + 1 mod p
	 *
	 * At that point:
	 *    0 <= t[0] <= 2^52 - 1
	 *    0 <= t[1] <= 2^52 - 1
	 *    2^5 <= t[2] <= 2^52 + 2^5 - 1
	 *    2^49 <= t[3] <= 2^52 + 2^49 - 1
	 *    2^59 < t[4] <= 2^61 + 2^60 - 2^29
	 *
	 * Thus, the value 's' (t[4] / 2^48) will be necessarily
	 * greater than 2048, and less than 12288.
	 */
	s = t[4] >> 48;

	d[0] = t[0] + s;             /* d[0] <= 2^52 + 12287 */
	w = t[1] - (s << 44);
	d[1] = w & MASK52;           /* d[1] <= 2^52 - 1 */
	cc = -(w >> 52) & 0xFFF;     /* cc <= 48 */
	w = t[2] - cc;
	cc = w >> 63;                /* cc = 0 or 1 */
	d[2] = w + (cc << 52);       /* d[2] <= 2^52 + 31 */
	w = t[3] - cc - (s << 36);
	cc = w >> 63;                /* cc = 0 or 1 */
	d[3] = w + (cc << 52);       /* t[3] <= 2^52 + 2^49 - 1 */
	d[4] = (t[4] & MASK48) + (s << 16) - cc;  /* d[4] < 2^48 + 2^30 */

	/*
	 * If s = 0, then none of the limbs is modified, and there cannot
	 * be an overflow; if s != 0, then (s << 16) > cc, and there is
	 * no overflow either.
	 */
}

/*
 * Montgomery multiplication in the field.
 * Input: limbs must fit on 56 bits each.
 * Output: partially reduced.
 */
static void
f256_montymul(uint64_t *d, const uint64_t *a, const uint64_t *b)
{
#if BR_INT128

	int i;
	uint64_t t[5];

	t[0] = 0;
	t[1] = 0;
	t[2] = 0;
	t[3] = 0;
	t[4] = 0;
	for (i = 0; i < 5; i ++) {
		uint64_t x, f, cc, w, s;
		unsigned __int128 z;

		/*
		 * Since limbs of a[] and b[] fit on 56 bits each,
		 * each individual product fits on 112 bits. Also,
		 * the factor f fits on 52 bits, so f<<48 fits on
		 * 112 bits too. This guarantees that carries (cc)
		 * will fit on 62 bits, thus no overflow.
		 *
		 * The operations below compute:
		 *   t <- (t + x*b + f*p) / 2^64
		 */
		x = a[i];
		z = (unsigned __int128)b[0] * (unsigned __int128)x
			+ (unsigned __int128)t[0];
		f = (uint64_t)z & MASK52;
		cc = (uint64_t)(z >> 52);
		z = (unsigned __int128)b[1] * (unsigned __int128)x
			+ (unsigned __int128)t[1] + cc
			+ ((unsigned __int128)f << 44);
		t[0] = (uint64_t)z & MASK52;
		cc = (uint64_t)(z >> 52);
		z = (unsigned __int128)b[2] * (unsigned __int128)x
			+ (unsigned __int128)t[2] + cc;
		t[1] = (uint64_t)z & MASK52;
		cc = (uint64_t)(z >> 52);
		z = (unsigned __int128)b[3] * (unsigned __int128)x
			+ (unsigned __int128)t[3] + cc
			+ ((unsigned __int128)f << 36);
		t[2] = (uint64_t)z & MASK52;
		cc = (uint64_t)(z >> 52);
		z = (unsigned __int128)b[4] * (unsigned __int128)x
			+ (unsigned __int128)t[4] + cc
			+ ((unsigned __int128)f << 48)
			- ((unsigned __int128)f << 16);
		t[3] = (uint64_t)z & MASK52;
		t[4] = (uint64_t)(z >> 52);

		/*
		 * t[4] may be up to 62 bits here; we need to do a
		 * partial reduction. Note that limbs t[0] to t[3]
		 * fit on 52 bits each.
		 */
		s = t[4] >> 48;             /* s < 2^14 */
		t[0] += s;                  /* t[0] < 2^52 + 2^14 */
		w = t[1] - (s << 44);
		t[1] = w & MASK52;          /* t[1] < 2^52 */
		cc = -(w >> 52) & 0xFFF;    /* cc < 16 */
		w = t[2] - cc;
		t[2] = w & MASK52;          /* t[2] < 2^52 */
		cc = w >> 63;               /* cc = 0 or 1 */
		w = t[3] - cc - (s << 36);
		t[3] = w & MASK52;          /* t[3] < 2^52 */
		cc = w >> 63;               /* cc = 0 or 1 */
		w = t[4] & MASK48;
		t[4] = w + (s << 16) - cc;  /* t[4] < 2^48 + 2^30 */

		/*
		 * The final t[4] cannot overflow because cc is 0 or 1,
		 * and cc can be 1 only if s != 0.
		 */
	}

	d[0] = t[0];
	d[1] = t[1];
	d[2] = t[2];
	d[3] = t[3];
	d[4] = t[4];

#elif BR_UMUL128

	int i;
	uint64_t t[5];

	t[0] = 0;
	t[1] = 0;
	t[2] = 0;
	t[3] = 0;
	t[4] = 0;
	for (i = 0; i < 5; i ++) {
		uint64_t x, f, cc, w, s, zh, zl;
		unsigned char k;

		/*
		 * Since limbs of a[] and b[] fit on 56 bits each,
		 * each individual product fits on 112 bits. Also,
		 * the factor f fits on 52 bits, so f<<48 fits on
		 * 112 bits too. This guarantees that carries (cc)
		 * will fit on 62 bits, thus no overflow.
		 *
		 * The operations below compute:
		 *   t <- (t + x*b + f*p) / 2^64
		 */
		x = a[i];
		zl = _umul128(b[0], x, &zh);
		k = _addcarry_u64(0, t[0], zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		f = zl & MASK52;
		cc = (zl >> 52) | (zh << 12);

		zl = _umul128(b[1], x, &zh);
		k = _addcarry_u64(0, t[1], zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		k = _addcarry_u64(0, cc, zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		k = _addcarry_u64(0, f << 44, zl, &zl);
		(void)_addcarry_u64(k, f >> 20, zh, &zh);
		t[0] = zl & MASK52;
		cc = (zl >> 52) | (zh << 12);

		zl = _umul128(b[2], x, &zh);
		k = _addcarry_u64(0, t[2], zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		k = _addcarry_u64(0, cc, zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		t[1] = zl & MASK52;
		cc = (zl >> 52) | (zh << 12);

		zl = _umul128(b[3], x, &zh);
		k = _addcarry_u64(0, t[3], zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		k = _addcarry_u64(0, cc, zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		k = _addcarry_u64(0, f << 36, zl, &zl);
		(void)_addcarry_u64(k, f >> 28, zh, &zh);
		t[2] = zl & MASK52;
		cc = (zl >> 52) | (zh << 12);

		zl = _umul128(b[4], x, &zh);
		k = _addcarry_u64(0, t[4], zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		k = _addcarry_u64(0, cc, zl, &zl);
		(void)_addcarry_u64(k, 0, zh, &zh);
		k = _addcarry_u64(0, f << 48, zl, &zl);
		(void)_addcarry_u64(k, f >> 16, zh, &zh);
		k = _subborrow_u64(0, zl, f << 16, &zl);
		(void)_subborrow_u64(k, zh, f >> 48, &zh);
		t[3] = zl & MASK52;
		t[4] = (zl >> 52) | (zh << 12);

		/*
		 * t[4] may be up to 62 bits here; we need to do a
		 * partial reduction. Note that limbs t[0] to t[3]
		 * fit on 52 bits each.
		 */
		s = t[4] >> 48;             /* s < 2^14 */
		t[0] += s;                  /* t[0] < 2^52 + 2^14 */
		w = t[1] - (s << 44);
		t[1] = w & MASK52;          /* t[1] < 2^52 */
		cc = -(w >> 52) & 0xFFF;    /* cc < 16 */
		w = t[2] - cc;
		t[2] = w & MASK52;          /* t[2] < 2^52 */
		cc = w >> 63;               /* cc = 0 or 1 */
		w = t[3] - cc - (s << 36);
		t[3] = w & MASK52;          /* t[3] < 2^52 */
		cc = w >> 63;               /* cc = 0 or 1 */
		w = t[4] & MASK48;
		t[4] = w + (s << 16) - cc;  /* t[4] < 2^48 + 2^30 */

		/*
		 * The final t[4] cannot overflow because cc is 0 or 1,
		 * and cc can be 1 only if s != 0.
		 */
	}

	d[0] = t[0];
	d[1] = t[1];
	d[2] = t[2];
	d[3] = t[3];
	d[4] = t[4];

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
	 * R2 = 2^520 mod p.
	 * If R = 2^260 mod p, then R2 = R^2 mod p; and the Montgomery
	 * multiplication of a by R2 is: a*R2/R = a*R mod p, i.e. the
	 * conversion to Montgomery representation.
	 */
	static const uint64_t R2[] = {
		0x0000000000300, 0xFFFFFFFF00000, 0xFFFFEFFFFFFFB,
		0xFDFFFFFFFFFFF, 0x0000004FFFFFF
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
	 * Montgomery multiplication by 1 is division by 2^260 modulo p.
	 */
	static const uint64_t one[] = { 1, 0, 0, 0, 0 };

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

	uint64_t r[5], t[5];
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
 * Input value should be partially reduced.
 * On output, limbs a[0] to a[3] fit on 52 bits each, limb a[4] fits
 * on 48 bits, and the integer is less than p.
 */
static inline void
f256_final_reduce(uint64_t *a)
{
	uint64_t r[5], t[5], w, cc;
	int i;

	/*
	 * Propagate carries to ensure that limbs 0 to 3 fit on 52 bits.
	 */
	cc = 0;
	for (i = 0; i < 5; i ++) {
		w = a[i] + cc;
		r[i] = w & MASK52;
		cc = w >> 52;
	}

	/*
	 * We compute t = r + (2^256 - p) = r + 2^224 - 2^192 - 2^96 + 1.
	 * If t < 2^256, then r < p, and we return r. Otherwise, we
	 * want to return r - p = t - 2^256.
	 */

	/*
	 * Add 2^224 + 1, and propagate carries to ensure that limbs
	 * t[0] to t[3] fit in 52 bits each.
	 */
	w = r[0] + 1;
	t[0] = w & MASK52;
	cc = w >> 52;
	w = r[1] + cc;
	t[1] = w & MASK52;
	cc = w >> 52;
	w = r[2] + cc;
	t[2] = w & MASK52;
	cc = w >> 52;
	w = r[3] + cc;
	t[3] = w & MASK52;
	cc = w >> 52;
	t[4] = r[4] + cc + BIT(16);

	/*
	 * Subtract 2^192 + 2^96. Since we just added 2^224 + 1, the
	 * result cannot be negative.
	 */
	w = t[1] - BIT(44);
	t[1] = w & MASK52;
	cc = w >> 63;
	w = t[2] - cc;
	t[2] = w & MASK52;
	cc = w >> 63;
	w = t[3] - BIT(36);
	t[3] = w & MASK52;
	cc = w >> 63;
	t[4] -= cc;

	/*
	 * If the top limb t[4] fits on 48 bits, then r[] is already
	 * in the proper range. Otherwise, t[] is the value to return
	 * (truncated to 256 bits).
	 */
	cc = -(t[4] >> 48);
	t[4] &= MASK48;
	for (i = 0; i < 5; i ++) {
		a[i] = r[i] ^ (cc & (r[i] ^ t[i]));
	}
}

/*
 * Points in affine and Jacobian coordinates.
 *
 *  - In affine coordinates, the point-at-infinity cannot be encoded.
 *  - Jacobian coordinates (X,Y,Z) correspond to affine (X/Z^2,Y/Z^3);
 *    if Z = 0 then this is the point-at-infinity.
 */
typedef struct {
	uint64_t x[5];
	uint64_t y[5];
} p256_affine;

typedef struct {
	uint64_t x[5];
	uint64_t y[5];
	uint64_t z[5];
} p256_jacobian;

/*
 * Decode a field element (unsigned big endian notation).
 */
static void
f256_decode(uint64_t *a, const unsigned char *buf)
{
	uint64_t w0, w1, w2, w3;

	w3 = br_dec64be(buf +  0);
	w2 = br_dec64be(buf +  8);
	w1 = br_dec64be(buf + 16);
	w0 = br_dec64be(buf + 24);
	a[0] = w0 & MASK52;
	a[1] = ((w0 >> 52) | (w1 << 12)) & MASK52;
	a[2] = ((w1 >> 40) | (w2 << 24)) & MASK52;
	a[3] = ((w2 >> 28) | (w3 << 36)) & MASK52;
	a[4] = w3 >> 16;
}

/*
 * Encode a field element (unsigned big endian notation). The field
 * element MUST be fully reduced.
 */
static void
f256_encode(unsigned char *buf, const uint64_t *a)
{
	uint64_t w0, w1, w2, w3;

	w0 = a[0] | (a[1] << 52);
	w1 = (a[1] >> 12) | (a[2] << 40);
	w2 = (a[2] >> 24) | (a[3] << 28);
	w3 = (a[3] >> 36) | (a[4] << 16);
	br_enc64be(buf +  0, w3);
	br_enc64be(buf +  8, w2);
	br_enc64be(buf + 16, w1);
	br_enc64be(buf + 24, w0);
}

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
	uint64_t x[5], y[5], t[5], x3[5], tt;
	uint32_t r;

	/*
	 * Header byte shall be 0x04.
	 */
	r = EQ(buf[0], 0x04);

	/*
	 * Decode X and Y coordinates, and convert them into
	 * Montgomery representation.
	 */
	f256_decode(x, buf +  1);
	f256_decode(y, buf + 33);
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
	tt = t[0] | t[1] | t[2] | t[3] | t[4];
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
	uint64_t t1[5], t2[5], z;

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
	f256_encode(buf +  1, t1);
	f256_encode(buf + 33, t2);

	/* Return success if and only if P->z != 0. */
	z = P->z[0] | P->z[1] | P->z[2] | P->z[3] | P->z[4];
	return NEQ((uint32_t)(z | z >> 32), 0);
}

/*
 * Point doubling in Jacobian coordinates: point P is doubled.
 * Note: if the source point is the point-at-infinity, then the result is
 * still the point-at-infinity, which is correct. Moreover, if the three
 * coordinates were zero, then they still are zero in the returned value.
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
	uint64_t t1[5], t2[5], t3[5], t4[5];

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
	f256_partial_reduce(P->z);

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
	uint64_t t1[5], t2[5], t3[5], t4[5], t5[5], t6[5], t7[5], tt;
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
	tt = t4[0] | t4[1] | t4[2] | t4[3] | t4[4];
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
	uint64_t t1[5], t2[5], t3[5], t4[5], t5[5], t6[5], t7[5], tt;
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
	tt = t4[0] | t4[1] | t4[2] | t4[3] | t4[4];
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
	uint64_t t1[5], t2[5], t3[5], t4[5], t5[5], t6[5], t7[5], tt, zz;
	int i;

	/*
	 * Set zz to -1 if P1 is the point at infinity, 0 otherwise.
	 */
	zz = P1->z[0] | P1->z[1] | P1->z[2] | P1->z[3] | P1->z[4];
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
	tt = t2[0] | t2[1] | t2[2] | t2[3] | t2[4]
		| t4[0] | t4[1] | t4[2] | t4[3] | t4[4];
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
	f256_partial_reduce(t1);

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
	for (i = 0; i < 5; i ++) {
		P1->x[i] |= tt & t5[i];
		P1->y[i] |= tt & t6[i];
		P1->z[i] |= tt & t1[i];
	}

	/*
	 * If P1 = 0, then we get z3 = 0 (which is invalid); if z1 is 0,
	 * then we want to replace the result with a copy of P2. The
	 * test on z1 was done at the start, in the zz mask.
	 */
	for (i = 0; i < 5; i ++) {
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
				T.x[4] |= m & W[n].x[4];
				T.y[0] |= m & W[n].y[0];
				T.y[1] |= m & W[n].y[1];
				T.y[2] |= m & W[n].y[2];
				T.y[3] |= m & W[n].y[3];
				T.y[4] |= m & W[n].y[4];
			}

			U = Q;
			p256_add_mixed(&U, &T);

			/*
			 * If qz is still 1, then Q was all-zeros, and this
			 * is conserved through p256_double().
			 */
			m = -(uint64_t)(bnz & qz);
			for (j = 0; j < 5; j ++) {
				Q.x[j] ^= m & (Q.x[j] ^ T.x[j]);
				Q.y[j] ^= m & (Q.y[j] ^ T.y[j]);
				Q.z[j] ^= m & (Q.z[j] ^ F256_R[j]);
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
	 * we have z1, z2, z3, and z4, and want to invert all of them,
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

	uint64_t z[16][5];
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
		{ 0x30D418A9143C1, 0xC4FEDB60179E7, 0x62251075BA95F,
		  0x5C669FB732B77, 0x08905F76B5375 },
		{ 0x5357CE95560A8, 0x43A19E45CDDF2, 0x21F3258B4AB8E,
		  0xD8552E88688DD, 0x0571FF18A5885 }
	},
	{
		{ 0x46D410DDD64DF, 0x0B433827D8500, 0x1490D9AA6AE3C,
		  0xA3A832205038D, 0x06BB32E52DCF3 },
		{ 0x48D361BEE1A57, 0xB7B236FF82F36, 0x042DBE152CD7C,
		  0xA3AA9A8FB0E92, 0x08C577517A5B8 }
	},
	{
		{ 0x3F904EEBC1272, 0x9E87D81FBFFAC, 0xCBBC98B027F84,
		  0x47E46AD77DD87, 0x06936A3FD6FF7 },
		{ 0x5C1FC983A7EBD, 0xC3861FE1AB04C, 0x2EE98E583E47A,
		  0xC06A88208311A, 0x05F06A2AB587C }
	},
	{
		{ 0xB50D46918DCC5, 0xD7623C17374B0, 0x100AF24650A6E,
		  0x76ABCDAACACE8, 0x077362F591B01 },
		{ 0xF24CE4CBABA68, 0x17AD6F4472D96, 0xDDD22E1762847,
		  0x862EB6C36DEE5, 0x04B14C39CC5AB }
	},
	{
		{ 0x8AAEC45C61F5C, 0x9D4B9537DBE1B, 0x76C20C90EC649,
		  0x3C7D41CB5AAD0, 0x0907960649052 },
		{ 0x9B4AE7BA4F107, 0xF75EB882BEB30, 0x7A1F6873C568E,
		  0x915C540A9877E, 0x03A076BB9DD1E }
	},
	{
		{ 0x47373E77664A1, 0xF246CEE3E4039, 0x17A3AD55AE744,
		  0x673C50A961A5B, 0x03074B5964213 },
		{ 0x6220D377E44BA, 0x30DFF14B593D3, 0x639F11299C2B5,
		  0x75F5424D44CEF, 0x04C9916DEA07F }
	},
	{
		{ 0x354EA0173B4F1, 0x3C23C00F70746, 0x23BB082BD2021,
		  0xE03E43EAAB50C, 0x03BA5119D3123 },
		{ 0xD0303F5B9D4DE, 0x17DA67BDD2847, 0xC941956742F2F,
		  0x8670F933BDC77, 0x0AEDD9164E240 }
	},
	{
		{ 0x4CD19499A78FB, 0x4BF9B345527F1, 0x2CFC6B462AB5C,
		  0x30CDF90F02AF0, 0x0763891F62652 },
		{ 0xA3A9532D49775, 0xD7F9EBA15F59D, 0x60BBF021E3327,
		  0xF75C23C7B84BE, 0x06EC12F2C706D }
	},
	{
		{ 0x6E8F264E20E8E, 0xC79A7A84175C9, 0xC8EB00ABE6BFE,
		  0x16A4CC09C0444, 0x005B3081D0C4E },
		{ 0x777AA45F33140, 0xDCE5D45E31EB7, 0xB12F1A56AF7BE,
		  0xF9B2B6E019A88, 0x086659CDFD835 }
	},
	{
		{ 0xDBD19DC21EC8C, 0x94FCF81392C18, 0x250B4998F9868,
		  0x28EB37D2CD648, 0x0C61C947E4B34 },
		{ 0x407880DD9E767, 0x0C83FBE080C2B, 0x9BE5D2C43A899,
		  0xAB4EF7D2D6577, 0x08719A555B3B4 }
	},
	{
		{ 0x260A6245E4043, 0x53E7FDFE0EA7D, 0xAC1AB59DE4079,
		  0x072EFF3A4158D, 0x0E7090F1949C9 },
		{ 0x85612B944E886, 0xE857F61C81A76, 0xAD643D250F939,
		  0x88DAC0DAA891E, 0x089300244125B }
	},
	{
		{ 0x1AA7D26977684, 0x58A345A3304B7, 0x37385EABDEDEF,
		  0x155E409D29DEE, 0x0EE1DF780B83E },
		{ 0x12D91CBB5B437, 0x65A8956370CAC, 0xDE6D66170ED2F,
		  0xAC9B8228CFA8A, 0x0FF57C95C3238 }
	},
	{
		{ 0x25634B2ED7097, 0x9156FD30DCCC4, 0x9E98110E35676,
		  0x7594CBCD43F55, 0x038477ACC395B },
		{ 0x2B90C00EE17FF, 0xF842ED2E33575, 0x1F5BC16874838,
		  0x7968CD06422BD, 0x0BC0876AB9E7B }
	},
	{
		{ 0xA35BB0CF664AF, 0x68F9707E3A242, 0x832660126E48F,
		  0x72D2717BF54C6, 0x0AAE7333ED12C },
		{ 0x2DB7995D586B1, 0xE732237C227B5, 0x65E7DBBE29569,
		  0xBBBD8E4193E2A, 0x052706DC3EAA1 }
	},
	{
		{ 0xD8B7BC60055BE, 0xD76E27E4B72BC, 0x81937003CC23E,
		  0xA090E337424E4, 0x02AA0E43EAD3D },
		{ 0x524F6383C45D2, 0x422A41B2540B8, 0x8A4797D766355,
		  0xDF444EFA6DE77, 0x0042170A9079A }
	},
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
	z = P.z[0] | P.z[1] | P.z[2] | P.z[3] | P.z[4];
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
const br_ec_impl br_ec_p256_m62 = {
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
br_ec_p256_m62_get(void)
{
	return &br_ec_p256_m62;
}

#else

/* see bearssl_ec.h */
const br_ec_impl *
br_ec_p256_m62_get(void)
{
	return 0;
}

#endif
