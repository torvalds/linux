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

#if BR_INT128 || BR_UMUL128

#if BR_INT128

/*
 * Compute x*y+v1+v2. Operands are 64-bit, and result is 128-bit, with
 * high word in "hi" and low word in "lo".
 */
#define FMA1(hi, lo, x, y, v1, v2)   do { \
		unsigned __int128 fmaz; \
		fmaz = (unsigned __int128)(x) * (unsigned __int128)(y) \
			+ (unsigned __int128)(v1) + (unsigned __int128)(v2); \
		(hi) = (uint64_t)(fmaz >> 64); \
		(lo) = (uint64_t)fmaz; \
	} while (0)

/*
 * Compute x1*y1+x2*y2+v1+v2. Operands are 64-bit, and result is 128-bit,
 * with high word in "hi" and low word in "lo".
 *
 * Callers should ensure that the two inner products, and the v1 and v2
 * operands, are multiple of 4 (this is not used by this specific definition
 * but may help other implementations).
 */
#define FMA2(hi, lo, x1, y1, x2, y2, v1, v2)   do { \
		unsigned __int128 fmaz; \
		fmaz = (unsigned __int128)(x1) * (unsigned __int128)(y1) \
			+ (unsigned __int128)(x2) * (unsigned __int128)(y2) \
			+ (unsigned __int128)(v1) + (unsigned __int128)(v2); \
		(hi) = (uint64_t)(fmaz >> 64); \
		(lo) = (uint64_t)fmaz; \
	} while (0)

#elif BR_UMUL128

#include <intrin.h>

#define FMA1(hi, lo, x, y, v1, v2)   do { \
		uint64_t fmahi, fmalo; \
		unsigned char fmacc; \
		fmalo = _umul128((x), (y), &fmahi); \
		fmacc = _addcarry_u64(0, fmalo, (v1), &fmalo); \
		_addcarry_u64(fmacc, fmahi, 0, &fmahi); \
		fmacc = _addcarry_u64(0, fmalo, (v2), &(lo)); \
		_addcarry_u64(fmacc, fmahi, 0, &(hi)); \
	} while (0)

/*
 * Normally we should use _addcarry_u64() for FMA2 too, but it makes
 * Visual Studio crash. Instead we use this version, which leverages
 * the fact that the vx operands, and the products, are multiple of 4.
 * This is unfortunately slower.
 */
#define FMA2(hi, lo, x1, y1, x2, y2, v1, v2)   do { \
		uint64_t fma1hi, fma1lo; \
		uint64_t fma2hi, fma2lo; \
		uint64_t fmatt; \
		fma1lo = _umul128((x1), (y1), &fma1hi); \
		fma2lo = _umul128((x2), (y2), &fma2hi); \
		fmatt = (fma1lo >> 2) + (fma2lo >> 2) \
			+ ((v1) >> 2) + ((v2) >> 2); \
		(lo) = fmatt << 2; \
		(hi) = fma1hi + fma2hi + (fmatt >> 62); \
	} while (0)

/*
 * The FMA2 macro definition we would prefer to use, but it triggers
 * an internal compiler error in Visual Studio 2015.
 *
#define FMA2(hi, lo, x1, y1, x2, y2, v1, v2)   do { \
		uint64_t fma1hi, fma1lo; \
		uint64_t fma2hi, fma2lo; \
		unsigned char fmacc; \
		fma1lo = _umul128((x1), (y1), &fma1hi); \
		fma2lo = _umul128((x2), (y2), &fma2hi); \
		fmacc = _addcarry_u64(0, fma1lo, (v1), &fma1lo); \
		_addcarry_u64(fmacc, fma1hi, 0, &fma1hi); \
		fmacc = _addcarry_u64(0, fma2lo, (v2), &fma2lo); \
		_addcarry_u64(fmacc, fma2hi, 0, &fma2hi); \
		fmacc = _addcarry_u64(0, fma1lo, fma2lo, &(lo)); \
		_addcarry_u64(fmacc, fma1hi, fma2hi, &(hi)); \
	} while (0)
 */

#endif

#define MASK62           ((uint64_t)0x3FFFFFFFFFFFFFFF)
#define MUL62_lo(x, y)   (((uint64_t)(x) * (uint64_t)(y)) & MASK62)

/*
 * Subtract b from a, and return the final carry. If 'ctl32' is 0, then
 * a[] is kept unmodified, but the final carry is still computed and
 * returned.
 */
static uint32_t
i62_sub(uint64_t *a, const uint64_t *b, size_t num, uint32_t ctl32)
{
	uint64_t cc, mask;
	size_t u;

	cc = 0;
	ctl32 = -ctl32;
	mask = (uint64_t)ctl32 | ((uint64_t)ctl32 << 32);
	for (u = 0; u < num; u ++) {
		uint64_t aw, bw, dw;

		aw = a[u];
		bw = b[u];
		dw = aw - bw - cc;
		cc = dw >> 63;
		dw &= MASK62;
		a[u] = aw ^ (mask & (dw ^ aw));
	}
	return (uint32_t)cc;
}

/*
 * Montgomery multiplication, over arrays of 62-bit values. The
 * destination array (d) must be distinct from the other operands
 * (x, y and m). All arrays are in little-endian format (least
 * significant word comes first) over 'num' words.
 */
static void
montymul(uint64_t *d, const uint64_t *x, const uint64_t *y,
	const uint64_t *m, size_t num, uint64_t m0i)
{
	uint64_t dh;
	size_t u, num4;

	num4 = 1 + ((num - 1) & ~(size_t)3);
	memset(d, 0, num * sizeof *d);
	dh = 0;
	for (u = 0; u < num; u ++) {
		size_t v;
		uint64_t f, xu;
		uint64_t r, zh;
		uint64_t hi, lo;

		xu = x[u] << 2;
		f = MUL62_lo(d[0] + MUL62_lo(x[u], y[0]), m0i) << 2;

		FMA2(hi, lo, xu, y[0], f, m[0], d[0] << 2, 0);
		r = hi;

		for (v = 1; v < num4; v += 4) {
			FMA2(hi, lo, xu, y[v + 0],
				f, m[v + 0], d[v + 0] << 2, r << 2);
			r = hi + (r >> 62);
			d[v - 1] = lo >> 2;
			FMA2(hi, lo, xu, y[v + 1],
				f, m[v + 1], d[v + 1] << 2, r << 2);
			r = hi + (r >> 62);
			d[v + 0] = lo >> 2;
			FMA2(hi, lo, xu, y[v + 2],
				f, m[v + 2], d[v + 2] << 2, r << 2);
			r = hi + (r >> 62);
			d[v + 1] = lo >> 2;
			FMA2(hi, lo, xu, y[v + 3],
				f, m[v + 3], d[v + 3] << 2, r << 2);
			r = hi + (r >> 62);
			d[v + 2] = lo >> 2;
		}
		for (; v < num; v ++) {
			FMA2(hi, lo, xu, y[v], f, m[v], d[v] << 2, r << 2);
			r = hi + (r >> 62);
			d[v - 1] = lo >> 2;
		}

		zh = dh + r;
		d[num - 1] = zh & MASK62;
		dh = zh >> 62;
	}
	i62_sub(d, m, num, (uint32_t)dh | NOT(i62_sub(d, m, num, 0)));
}

/*
 * Conversion back from Montgomery representation.
 */
static void
frommonty(uint64_t *x, const uint64_t *m, size_t num, uint64_t m0i)
{
	size_t u, v;

	for (u = 0; u < num; u ++) {
		uint64_t f, cc;

		f = MUL62_lo(x[0], m0i) << 2;
		cc = 0;
		for (v = 0; v < num; v ++) {
			uint64_t hi, lo;

			FMA1(hi, lo, f, m[v], x[v] << 2, cc);
			cc = hi << 2;
			if (v != 0) {
				x[v - 1] = lo >> 2;
			}
		}
		x[num - 1] = cc >> 2;
	}
	i62_sub(x, m, num, NOT(i62_sub(x, m, num, 0)));
}

/* see inner.h */
uint32_t
br_i62_modpow_opt(uint32_t *x31, const unsigned char *e, size_t elen,
	const uint32_t *m31, uint32_t m0i31, uint64_t *tmp, size_t twlen)
{
	size_t u, mw31num, mw62num;
	uint64_t *x, *m, *t1, *t2;
	uint64_t m0i;
	uint32_t acc;
	int win_len, acc_len;

	/*
	 * Get modulus size, in words.
	 */
	mw31num = (m31[0] + 31) >> 5;
	mw62num = (mw31num + 1) >> 1;

	/*
	 * In order to apply this function, we must have enough room to
	 * copy the operand and modulus into the temporary array, along
	 * with at least two temporaries. If there is not enough room,
	 * switch to br_i31_modpow(). We also use br_i31_modpow() if the
	 * modulus length is not at least four words (94 bits or more).
	 */
	if (mw31num < 4 || (mw62num << 2) > twlen) {
		/*
		 * We assume here that we can split an aligned uint64_t
		 * into two properly aligned uint32_t. Since both types
		 * are supposed to have an exact width with no padding,
		 * then this property must hold.
		 */
		size_t txlen;

		txlen = mw31num + 1;
		if (twlen < txlen) {
			return 0;
		}
		br_i31_modpow(x31, e, elen, m31, m0i31,
			(uint32_t *)tmp, (uint32_t *)tmp + txlen);
		return 1;
	}

	/*
	 * Convert x to Montgomery representation: this means that
	 * we replace x with x*2^z mod m, where z is the smallest multiple
	 * of the word size such that 2^z >= m. We want to reuse the 31-bit
	 * functions here (for constant-time operation), but we need z
	 * for a 62-bit word size.
	 */
	for (u = 0; u < mw62num; u ++) {
		br_i31_muladd_small(x31, 0, m31);
		br_i31_muladd_small(x31, 0, m31);
	}

	/*
	 * Assemble operands into arrays of 62-bit words. Note that
	 * all the arrays of 62-bit words that we will handle here
	 * are without any leading size word.
	 *
	 * We also adjust tmp and twlen to account for the words used
	 * for these extra arrays.
	 */
	m = tmp;
	x = tmp + mw62num;
	tmp += (mw62num << 1);
	twlen -= (mw62num << 1);
	for (u = 0; u < mw31num; u += 2) {
		size_t v;

		v = u >> 1;
		if ((u + 1) == mw31num) {
			m[v] = (uint64_t)m31[u + 1];
			x[v] = (uint64_t)x31[u + 1];
		} else {
			m[v] = (uint64_t)m31[u + 1]
				+ ((uint64_t)m31[u + 2] << 31);
			x[v] = (uint64_t)x31[u + 1]
				+ ((uint64_t)x31[u + 2] << 31);
		}
	}

	/*
	 * Compute window size. We support windows up to 5 bits; for a
	 * window of size k bits, we need 2^k+1 temporaries (for k = 1,
	 * we use special code that uses only 2 temporaries).
	 */
	for (win_len = 5; win_len > 1; win_len --) {
		if ((((uint32_t)1 << win_len) + 1) * mw62num <= twlen) {
			break;
		}
	}

	t1 = tmp;
	t2 = tmp + mw62num;

	/*
	 * Compute m0i, which is equal to -(1/m0) mod 2^62. We were
	 * provided with m0i31, which already fulfills this property
	 * modulo 2^31; the single expression below is then sufficient.
	 */
	m0i = (uint64_t)m0i31;
	m0i = MUL62_lo(m0i, (uint64_t)2 + MUL62_lo(m0i, m[0]));

	/*
	 * Compute window contents. If the window has size one bit only,
	 * then t2 is set to x; otherwise, t2[0] is left untouched, and
	 * t2[k] is set to x^k (for k >= 1).
	 */
	if (win_len == 1) {
		memcpy(t2, x, mw62num * sizeof *x);
	} else {
		uint64_t *base;

		memcpy(t2 + mw62num, x, mw62num * sizeof *x);
		base = t2 + mw62num;
		for (u = 2; u < ((unsigned)1 << win_len); u ++) {
			montymul(base + mw62num, base, x, m, mw62num, m0i);
			base += mw62num;
		}
	}

	/*
	 * Set x to 1, in Montgomery representation. We again use the
	 * 31-bit code.
	 */
	br_i31_zero(x31, m31[0]);
	x31[(m31[0] + 31) >> 5] = 1;
	br_i31_muladd_small(x31, 0, m31);
	if (mw31num & 1) {
		br_i31_muladd_small(x31, 0, m31);
	}
	for (u = 0; u < mw31num; u += 2) {
		size_t v;

		v = u >> 1;
		if ((u + 1) == mw31num) {
			x[v] = (uint64_t)x31[u + 1];
		} else {
			x[v] = (uint64_t)x31[u + 1]
				+ ((uint64_t)x31[u + 2] << 31);
		}
	}

	/*
	 * We process bits from most to least significant. At each
	 * loop iteration, we have acc_len bits in acc.
	 */
	acc = 0;
	acc_len = 0;
	while (acc_len > 0 || elen > 0) {
		int i, k;
		uint32_t bits;
		uint64_t mask1, mask2;

		/*
		 * Get the next bits.
		 */
		k = win_len;
		if (acc_len < win_len) {
			if (elen > 0) {
				acc = (acc << 8) | *e ++;
				elen --;
				acc_len += 8;
			} else {
				k = acc_len;
			}
		}
		bits = (acc >> (acc_len - k)) & (((uint32_t)1 << k) - 1);
		acc_len -= k;

		/*
		 * We could get exactly k bits. Compute k squarings.
		 */
		for (i = 0; i < k; i ++) {
			montymul(t1, x, x, m, mw62num, m0i);
			memcpy(x, t1, mw62num * sizeof *x);
		}

		/*
		 * Window lookup: we want to set t2 to the window
		 * lookup value, assuming the bits are non-zero. If
		 * the window length is 1 bit only, then t2 is
		 * already set; otherwise, we do a constant-time lookup.
		 */
		if (win_len > 1) {
			uint64_t *base;

			memset(t2, 0, mw62num * sizeof *t2);
			base = t2 + mw62num;
			for (u = 1; u < ((uint32_t)1 << k); u ++) {
				uint64_t mask;
				size_t v;

				mask = -(uint64_t)EQ(u, bits);
				for (v = 0; v < mw62num; v ++) {
					t2[v] |= mask & base[v];
				}
				base += mw62num;
			}
		}

		/*
		 * Multiply with the looked-up value. We keep the product
		 * only if the exponent bits are not all-zero.
		 */
		montymul(t1, x, t2, m, mw62num, m0i);
		mask1 = -(uint64_t)EQ(bits, 0);
		mask2 = ~mask1;
		for (u = 0; u < mw62num; u ++) {
			x[u] = (mask1 & x[u]) | (mask2 & t1[u]);
		}
	}

	/*
	 * Convert back from Montgomery representation.
	 */
	frommonty(x, m, mw62num, m0i);

	/*
	 * Convert result into 31-bit words.
	 */
	for (u = 0; u < mw31num; u += 2) {
		uint64_t zw;

		zw = x[u >> 1];
		x31[u + 1] = (uint32_t)zw & 0x7FFFFFFF;
		if ((u + 1) < mw31num) {
			x31[u + 2] = (uint32_t)(zw >> 31);
		}
	}
	return 1;
}

#else

/* see inner.h */
uint32_t
br_i62_modpow_opt(uint32_t *x31, const unsigned char *e, size_t elen,
	const uint32_t *m31, uint32_t m0i31, uint64_t *tmp, size_t twlen)
{
	size_t mwlen;

	mwlen = (m31[0] + 63) >> 5;
	if (twlen < mwlen) {
		return 0;
	}
	return br_i31_modpow_opt(x31, e, elen, m31, m0i31,
		(uint32_t *)tmp, twlen << 1);
}

#endif

/* see inner.h */
uint32_t
br_i62_modpow_opt_as_i31(uint32_t *x31, const unsigned char *e, size_t elen,
	const uint32_t *m31, uint32_t m0i31, uint32_t *tmp, size_t twlen)
{
	/*
	 * As documented, this function expects the 'tmp' argument to be
	 * 64-bit aligned. This is OK since this function is internal (it
	 * is not part of BearSSL's public API).
	 */
	return br_i62_modpow_opt(x31, e, elen, m31, m0i31,
		(uint64_t *)tmp, twlen >> 1);
}
