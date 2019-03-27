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

/*
 * If BR_NO_ARITH_SHIFT is undefined, or defined to 0, then we _assume_
 * that right-shifting a signed negative integer copies the sign bit
 * (arithmetic right-shift). This is "implementation-defined behaviour",
 * i.e. it is not undefined, but it may differ between compilers. Each
 * compiler is supposed to document its behaviour in that respect. GCC
 * explicitly defines that an arithmetic right shift is used. We expect
 * all other compilers to do the same, because underlying CPU offer an
 * arithmetic right shift opcode that could not be used otherwise.
 */
#if BR_NO_ARITH_SHIFT
#define ARSH(x, n)   (((uint32_t)(x) >> (n)) \
                    | ((-((uint32_t)(x) >> 31)) << (32 - (n))))
#else
#define ARSH(x, n)   ((*(int32_t *)&(x)) >> (n))
#endif

/*
 * Convert an integer from unsigned big-endian encoding to a sequence of
 * 13-bit words in little-endian order. The final "partial" word is
 * returned.
 */
static uint32_t
be8_to_le13(uint32_t *dst, const unsigned char *src, size_t len)
{
	uint32_t acc;
	int acc_len;

	acc = 0;
	acc_len = 0;
	while (len -- > 0) {
		acc |= (uint32_t)src[len] << acc_len;
		acc_len += 8;
		if (acc_len >= 13) {
			*dst ++ = acc & 0x1FFF;
			acc >>= 13;
			acc_len -= 13;
		}
	}
	return acc;
}

/*
 * Convert an integer (13-bit words, little-endian) to unsigned
 * big-endian encoding. The total encoding length is provided; all
 * the destination bytes will be filled.
 */
static void
le13_to_be8(unsigned char *dst, size_t len, const uint32_t *src)
{
	uint32_t acc;
	int acc_len;

	acc = 0;
	acc_len = 0;
	while (len -- > 0) {
		if (acc_len < 8) {
			acc |= (*src ++) << acc_len;
			acc_len += 13;
		}
		dst[len] = (unsigned char)acc;
		acc >>= 8;
		acc_len -= 8;
	}
}

/*
 * Normalise an array of words to a strict 13 bits per word. Returned
 * value is the resulting carry. The source (w) and destination (d)
 * arrays may be identical, but shall not overlap partially.
 */
static inline uint32_t
norm13(uint32_t *d, const uint32_t *w, size_t len)
{
	size_t u;
	uint32_t cc;

	cc = 0;
	for (u = 0; u < len; u ++) {
		int32_t z;

		z = w[u] + cc;
		d[u] = z & 0x1FFF;
		cc = ARSH(z, 13);
	}
	return cc;
}

/*
 * mul20() multiplies two 260-bit integers together. Each word must fit
 * on 13 bits; source operands use 20 words, destination operand
 * receives 40 words. All overlaps allowed.
 *
 * square20() computes the square of a 260-bit integer. Each word must
 * fit on 13 bits; source operand uses 20 words, destination operand
 * receives 40 words. All overlaps allowed.
 */

#if BR_SLOW_MUL15

static void
mul20(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	/*
	 * Two-level Karatsuba: turns a 20x20 multiplication into
	 * nine 5x5 multiplications. We use 13-bit words but do not
	 * propagate carries immediately, so words may expand:
	 *
	 *  - First Karatsuba decomposition turns the 20x20 mul on
	 *    13-bit words into three 10x10 muls, two on 13-bit words
	 *    and one on 14-bit words.
	 *
	 *  - Second Karatsuba decomposition further splits these into:
	 *
	 *     * four 5x5 muls on 13-bit words
	 *     * four 5x5 muls on 14-bit words
	 *     * one 5x5 mul on 15-bit words
	 *
	 * Highest word value is 8191, 16382 or 32764, for 13-bit, 14-bit
	 * or 15-bit words, respectively.
	 */
	uint32_t u[45], v[45], w[90];
	uint32_t cc;
	int i;

#define ZADD(dw, d_off, s1w, s1_off, s2w, s2_off)   do { \
		(dw)[5 * (d_off) + 0] = (s1w)[5 * (s1_off) + 0] \
			+ (s2w)[5 * (s2_off) + 0]; \
		(dw)[5 * (d_off) + 1] = (s1w)[5 * (s1_off) + 1] \
			+ (s2w)[5 * (s2_off) + 1]; \
		(dw)[5 * (d_off) + 2] = (s1w)[5 * (s1_off) + 2] \
			+ (s2w)[5 * (s2_off) + 2]; \
		(dw)[5 * (d_off) + 3] = (s1w)[5 * (s1_off) + 3] \
			+ (s2w)[5 * (s2_off) + 3]; \
		(dw)[5 * (d_off) + 4] = (s1w)[5 * (s1_off) + 4] \
			+ (s2w)[5 * (s2_off) + 4]; \
	} while (0)

#define ZADDT(dw, d_off, sw, s_off)   do { \
		(dw)[5 * (d_off) + 0] += (sw)[5 * (s_off) + 0]; \
		(dw)[5 * (d_off) + 1] += (sw)[5 * (s_off) + 1]; \
		(dw)[5 * (d_off) + 2] += (sw)[5 * (s_off) + 2]; \
		(dw)[5 * (d_off) + 3] += (sw)[5 * (s_off) + 3]; \
		(dw)[5 * (d_off) + 4] += (sw)[5 * (s_off) + 4]; \
	} while (0)

#define ZSUB2F(dw, d_off, s1w, s1_off, s2w, s2_off)   do { \
		(dw)[5 * (d_off) + 0] -= (s1w)[5 * (s1_off) + 0] \
			+ (s2w)[5 * (s2_off) + 0]; \
		(dw)[5 * (d_off) + 1] -= (s1w)[5 * (s1_off) + 1] \
			+ (s2w)[5 * (s2_off) + 1]; \
		(dw)[5 * (d_off) + 2] -= (s1w)[5 * (s1_off) + 2] \
			+ (s2w)[5 * (s2_off) + 2]; \
		(dw)[5 * (d_off) + 3] -= (s1w)[5 * (s1_off) + 3] \
			+ (s2w)[5 * (s2_off) + 3]; \
		(dw)[5 * (d_off) + 4] -= (s1w)[5 * (s1_off) + 4] \
			+ (s2w)[5 * (s2_off) + 4]; \
	} while (0)

#define CPR1(w, cprcc)   do { \
		uint32_t cprz = (w) + cprcc; \
		(w) = cprz & 0x1FFF; \
		cprcc = cprz >> 13; \
	} while (0)

#define CPR(dw, d_off)   do { \
		uint32_t cprcc; \
		cprcc = 0; \
		CPR1((dw)[(d_off) + 0], cprcc); \
		CPR1((dw)[(d_off) + 1], cprcc); \
		CPR1((dw)[(d_off) + 2], cprcc); \
		CPR1((dw)[(d_off) + 3], cprcc); \
		CPR1((dw)[(d_off) + 4], cprcc); \
		CPR1((dw)[(d_off) + 5], cprcc); \
		CPR1((dw)[(d_off) + 6], cprcc); \
		CPR1((dw)[(d_off) + 7], cprcc); \
		CPR1((dw)[(d_off) + 8], cprcc); \
		(dw)[(d_off) + 9] = cprcc; \
	} while (0)

	memcpy(u, a, 20 * sizeof *a);
	ZADD(u, 4, a, 0, a, 1);
	ZADD(u, 5, a, 2, a, 3);
	ZADD(u, 6, a, 0, a, 2);
	ZADD(u, 7, a, 1, a, 3);
	ZADD(u, 8, u, 6, u, 7);

	memcpy(v, b, 20 * sizeof *b);
	ZADD(v, 4, b, 0, b, 1);
	ZADD(v, 5, b, 2, b, 3);
	ZADD(v, 6, b, 0, b, 2);
	ZADD(v, 7, b, 1, b, 3);
	ZADD(v, 8, v, 6, v, 7);

	/*
	 * Do the eight first 8x8 muls. Source words are at most 16382
	 * each, so we can add product results together "as is" in 32-bit
	 * words.
	 */
	for (i = 0; i < 40; i += 5) {
		w[(i << 1) + 0] = MUL15(u[i + 0], v[i + 0]);
		w[(i << 1) + 1] = MUL15(u[i + 0], v[i + 1])
			+ MUL15(u[i + 1], v[i + 0]);
		w[(i << 1) + 2] = MUL15(u[i + 0], v[i + 2])
			+ MUL15(u[i + 1], v[i + 1])
			+ MUL15(u[i + 2], v[i + 0]);
		w[(i << 1) + 3] = MUL15(u[i + 0], v[i + 3])
			+ MUL15(u[i + 1], v[i + 2])
			+ MUL15(u[i + 2], v[i + 1])
			+ MUL15(u[i + 3], v[i + 0]);
		w[(i << 1) + 4] = MUL15(u[i + 0], v[i + 4])
			+ MUL15(u[i + 1], v[i + 3])
			+ MUL15(u[i + 2], v[i + 2])
			+ MUL15(u[i + 3], v[i + 1])
			+ MUL15(u[i + 4], v[i + 0]);
		w[(i << 1) + 5] = MUL15(u[i + 1], v[i + 4])
			+ MUL15(u[i + 2], v[i + 3])
			+ MUL15(u[i + 3], v[i + 2])
			+ MUL15(u[i + 4], v[i + 1]);
		w[(i << 1) + 6] = MUL15(u[i + 2], v[i + 4])
			+ MUL15(u[i + 3], v[i + 3])
			+ MUL15(u[i + 4], v[i + 2]);
		w[(i << 1) + 7] = MUL15(u[i + 3], v[i + 4])
			+ MUL15(u[i + 4], v[i + 3]);
		w[(i << 1) + 8] = MUL15(u[i + 4], v[i + 4]);
		w[(i << 1) + 9] = 0;
	}

	/*
	 * For the 9th multiplication, source words are up to 32764,
	 * so we must do some carry propagation. If we add up to
	 * 4 products and the carry is no more than 524224, then the
	 * result fits in 32 bits, and the next carry will be no more
	 * than 524224 (because 4*(32764^2)+524224 < 8192*524225).
	 *
	 * We thus just skip one of the products in the middle word,
	 * then do a carry propagation (this reduces words to 13 bits
	 * each, except possibly the last, which may use up to 17 bits
	 * or so), then add the missing product.
	 */
	w[80 + 0] = MUL15(u[40 + 0], v[40 + 0]);
	w[80 + 1] = MUL15(u[40 + 0], v[40 + 1])
		+ MUL15(u[40 + 1], v[40 + 0]);
	w[80 + 2] = MUL15(u[40 + 0], v[40 + 2])
		+ MUL15(u[40 + 1], v[40 + 1])
		+ MUL15(u[40 + 2], v[40 + 0]);
	w[80 + 3] = MUL15(u[40 + 0], v[40 + 3])
		+ MUL15(u[40 + 1], v[40 + 2])
		+ MUL15(u[40 + 2], v[40 + 1])
		+ MUL15(u[40 + 3], v[40 + 0]);
	w[80 + 4] = MUL15(u[40 + 0], v[40 + 4])
		+ MUL15(u[40 + 1], v[40 + 3])
		+ MUL15(u[40 + 2], v[40 + 2])
		+ MUL15(u[40 + 3], v[40 + 1]);
		/* + MUL15(u[40 + 4], v[40 + 0]) */
	w[80 + 5] = MUL15(u[40 + 1], v[40 + 4])
		+ MUL15(u[40 + 2], v[40 + 3])
		+ MUL15(u[40 + 3], v[40 + 2])
		+ MUL15(u[40 + 4], v[40 + 1]);
	w[80 + 6] = MUL15(u[40 + 2], v[40 + 4])
		+ MUL15(u[40 + 3], v[40 + 3])
		+ MUL15(u[40 + 4], v[40 + 2]);
	w[80 + 7] = MUL15(u[40 + 3], v[40 + 4])
		+ MUL15(u[40 + 4], v[40 + 3]);
	w[80 + 8] = MUL15(u[40 + 4], v[40 + 4]);

	CPR(w, 80);

	w[80 + 4] += MUL15(u[40 + 4], v[40 + 0]);

	/*
	 * The products on 14-bit words in slots 6 and 7 yield values
	 * up to 5*(16382^2) each, and we need to subtract two such
	 * values from the higher word. We need the subtraction to fit
	 * in a _signed_ 32-bit integer, i.e. 31 bits + a sign bit.
	 * However, 10*(16382^2) does not fit. So we must perform a
	 * bit of reduction here.
	 */
	CPR(w, 60);
	CPR(w, 70);

	/*
	 * Recompose results.
	 */

	/* 0..1*0..1 into 0..3 */
	ZSUB2F(w, 8, w, 0, w, 2);
	ZSUB2F(w, 9, w, 1, w, 3);
	ZADDT(w, 1, w, 8);
	ZADDT(w, 2, w, 9);

	/* 2..3*2..3 into 4..7 */
	ZSUB2F(w, 10, w, 4, w, 6);
	ZSUB2F(w, 11, w, 5, w, 7);
	ZADDT(w, 5, w, 10);
	ZADDT(w, 6, w, 11);

	/* (0..1+2..3)*(0..1+2..3) into 12..15 */
	ZSUB2F(w, 16, w, 12, w, 14);
	ZSUB2F(w, 17, w, 13, w, 15);
	ZADDT(w, 13, w, 16);
	ZADDT(w, 14, w, 17);

	/* first-level recomposition */
	ZSUB2F(w, 12, w, 0, w, 4);
	ZSUB2F(w, 13, w, 1, w, 5);
	ZSUB2F(w, 14, w, 2, w, 6);
	ZSUB2F(w, 15, w, 3, w, 7);
	ZADDT(w, 2, w, 12);
	ZADDT(w, 3, w, 13);
	ZADDT(w, 4, w, 14);
	ZADDT(w, 5, w, 15);

	/*
	 * Perform carry propagation to bring all words down to 13 bits.
	 */
	cc = norm13(d, w, 40);
	d[39] += (cc << 13);

#undef ZADD
#undef ZADDT
#undef ZSUB2F
#undef CPR1
#undef CPR
}

static inline void
square20(uint32_t *d, const uint32_t *a)
{
	mul20(d, a, a);
}

#else

static void
mul20(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	uint32_t t[39];

	t[ 0] = MUL15(a[ 0], b[ 0]);
	t[ 1] = MUL15(a[ 0], b[ 1])
		+ MUL15(a[ 1], b[ 0]);
	t[ 2] = MUL15(a[ 0], b[ 2])
		+ MUL15(a[ 1], b[ 1])
		+ MUL15(a[ 2], b[ 0]);
	t[ 3] = MUL15(a[ 0], b[ 3])
		+ MUL15(a[ 1], b[ 2])
		+ MUL15(a[ 2], b[ 1])
		+ MUL15(a[ 3], b[ 0]);
	t[ 4] = MUL15(a[ 0], b[ 4])
		+ MUL15(a[ 1], b[ 3])
		+ MUL15(a[ 2], b[ 2])
		+ MUL15(a[ 3], b[ 1])
		+ MUL15(a[ 4], b[ 0]);
	t[ 5] = MUL15(a[ 0], b[ 5])
		+ MUL15(a[ 1], b[ 4])
		+ MUL15(a[ 2], b[ 3])
		+ MUL15(a[ 3], b[ 2])
		+ MUL15(a[ 4], b[ 1])
		+ MUL15(a[ 5], b[ 0]);
	t[ 6] = MUL15(a[ 0], b[ 6])
		+ MUL15(a[ 1], b[ 5])
		+ MUL15(a[ 2], b[ 4])
		+ MUL15(a[ 3], b[ 3])
		+ MUL15(a[ 4], b[ 2])
		+ MUL15(a[ 5], b[ 1])
		+ MUL15(a[ 6], b[ 0]);
	t[ 7] = MUL15(a[ 0], b[ 7])
		+ MUL15(a[ 1], b[ 6])
		+ MUL15(a[ 2], b[ 5])
		+ MUL15(a[ 3], b[ 4])
		+ MUL15(a[ 4], b[ 3])
		+ MUL15(a[ 5], b[ 2])
		+ MUL15(a[ 6], b[ 1])
		+ MUL15(a[ 7], b[ 0]);
	t[ 8] = MUL15(a[ 0], b[ 8])
		+ MUL15(a[ 1], b[ 7])
		+ MUL15(a[ 2], b[ 6])
		+ MUL15(a[ 3], b[ 5])
		+ MUL15(a[ 4], b[ 4])
		+ MUL15(a[ 5], b[ 3])
		+ MUL15(a[ 6], b[ 2])
		+ MUL15(a[ 7], b[ 1])
		+ MUL15(a[ 8], b[ 0]);
	t[ 9] = MUL15(a[ 0], b[ 9])
		+ MUL15(a[ 1], b[ 8])
		+ MUL15(a[ 2], b[ 7])
		+ MUL15(a[ 3], b[ 6])
		+ MUL15(a[ 4], b[ 5])
		+ MUL15(a[ 5], b[ 4])
		+ MUL15(a[ 6], b[ 3])
		+ MUL15(a[ 7], b[ 2])
		+ MUL15(a[ 8], b[ 1])
		+ MUL15(a[ 9], b[ 0]);
	t[10] = MUL15(a[ 0], b[10])
		+ MUL15(a[ 1], b[ 9])
		+ MUL15(a[ 2], b[ 8])
		+ MUL15(a[ 3], b[ 7])
		+ MUL15(a[ 4], b[ 6])
		+ MUL15(a[ 5], b[ 5])
		+ MUL15(a[ 6], b[ 4])
		+ MUL15(a[ 7], b[ 3])
		+ MUL15(a[ 8], b[ 2])
		+ MUL15(a[ 9], b[ 1])
		+ MUL15(a[10], b[ 0]);
	t[11] = MUL15(a[ 0], b[11])
		+ MUL15(a[ 1], b[10])
		+ MUL15(a[ 2], b[ 9])
		+ MUL15(a[ 3], b[ 8])
		+ MUL15(a[ 4], b[ 7])
		+ MUL15(a[ 5], b[ 6])
		+ MUL15(a[ 6], b[ 5])
		+ MUL15(a[ 7], b[ 4])
		+ MUL15(a[ 8], b[ 3])
		+ MUL15(a[ 9], b[ 2])
		+ MUL15(a[10], b[ 1])
		+ MUL15(a[11], b[ 0]);
	t[12] = MUL15(a[ 0], b[12])
		+ MUL15(a[ 1], b[11])
		+ MUL15(a[ 2], b[10])
		+ MUL15(a[ 3], b[ 9])
		+ MUL15(a[ 4], b[ 8])
		+ MUL15(a[ 5], b[ 7])
		+ MUL15(a[ 6], b[ 6])
		+ MUL15(a[ 7], b[ 5])
		+ MUL15(a[ 8], b[ 4])
		+ MUL15(a[ 9], b[ 3])
		+ MUL15(a[10], b[ 2])
		+ MUL15(a[11], b[ 1])
		+ MUL15(a[12], b[ 0]);
	t[13] = MUL15(a[ 0], b[13])
		+ MUL15(a[ 1], b[12])
		+ MUL15(a[ 2], b[11])
		+ MUL15(a[ 3], b[10])
		+ MUL15(a[ 4], b[ 9])
		+ MUL15(a[ 5], b[ 8])
		+ MUL15(a[ 6], b[ 7])
		+ MUL15(a[ 7], b[ 6])
		+ MUL15(a[ 8], b[ 5])
		+ MUL15(a[ 9], b[ 4])
		+ MUL15(a[10], b[ 3])
		+ MUL15(a[11], b[ 2])
		+ MUL15(a[12], b[ 1])
		+ MUL15(a[13], b[ 0]);
	t[14] = MUL15(a[ 0], b[14])
		+ MUL15(a[ 1], b[13])
		+ MUL15(a[ 2], b[12])
		+ MUL15(a[ 3], b[11])
		+ MUL15(a[ 4], b[10])
		+ MUL15(a[ 5], b[ 9])
		+ MUL15(a[ 6], b[ 8])
		+ MUL15(a[ 7], b[ 7])
		+ MUL15(a[ 8], b[ 6])
		+ MUL15(a[ 9], b[ 5])
		+ MUL15(a[10], b[ 4])
		+ MUL15(a[11], b[ 3])
		+ MUL15(a[12], b[ 2])
		+ MUL15(a[13], b[ 1])
		+ MUL15(a[14], b[ 0]);
	t[15] = MUL15(a[ 0], b[15])
		+ MUL15(a[ 1], b[14])
		+ MUL15(a[ 2], b[13])
		+ MUL15(a[ 3], b[12])
		+ MUL15(a[ 4], b[11])
		+ MUL15(a[ 5], b[10])
		+ MUL15(a[ 6], b[ 9])
		+ MUL15(a[ 7], b[ 8])
		+ MUL15(a[ 8], b[ 7])
		+ MUL15(a[ 9], b[ 6])
		+ MUL15(a[10], b[ 5])
		+ MUL15(a[11], b[ 4])
		+ MUL15(a[12], b[ 3])
		+ MUL15(a[13], b[ 2])
		+ MUL15(a[14], b[ 1])
		+ MUL15(a[15], b[ 0]);
	t[16] = MUL15(a[ 0], b[16])
		+ MUL15(a[ 1], b[15])
		+ MUL15(a[ 2], b[14])
		+ MUL15(a[ 3], b[13])
		+ MUL15(a[ 4], b[12])
		+ MUL15(a[ 5], b[11])
		+ MUL15(a[ 6], b[10])
		+ MUL15(a[ 7], b[ 9])
		+ MUL15(a[ 8], b[ 8])
		+ MUL15(a[ 9], b[ 7])
		+ MUL15(a[10], b[ 6])
		+ MUL15(a[11], b[ 5])
		+ MUL15(a[12], b[ 4])
		+ MUL15(a[13], b[ 3])
		+ MUL15(a[14], b[ 2])
		+ MUL15(a[15], b[ 1])
		+ MUL15(a[16], b[ 0]);
	t[17] = MUL15(a[ 0], b[17])
		+ MUL15(a[ 1], b[16])
		+ MUL15(a[ 2], b[15])
		+ MUL15(a[ 3], b[14])
		+ MUL15(a[ 4], b[13])
		+ MUL15(a[ 5], b[12])
		+ MUL15(a[ 6], b[11])
		+ MUL15(a[ 7], b[10])
		+ MUL15(a[ 8], b[ 9])
		+ MUL15(a[ 9], b[ 8])
		+ MUL15(a[10], b[ 7])
		+ MUL15(a[11], b[ 6])
		+ MUL15(a[12], b[ 5])
		+ MUL15(a[13], b[ 4])
		+ MUL15(a[14], b[ 3])
		+ MUL15(a[15], b[ 2])
		+ MUL15(a[16], b[ 1])
		+ MUL15(a[17], b[ 0]);
	t[18] = MUL15(a[ 0], b[18])
		+ MUL15(a[ 1], b[17])
		+ MUL15(a[ 2], b[16])
		+ MUL15(a[ 3], b[15])
		+ MUL15(a[ 4], b[14])
		+ MUL15(a[ 5], b[13])
		+ MUL15(a[ 6], b[12])
		+ MUL15(a[ 7], b[11])
		+ MUL15(a[ 8], b[10])
		+ MUL15(a[ 9], b[ 9])
		+ MUL15(a[10], b[ 8])
		+ MUL15(a[11], b[ 7])
		+ MUL15(a[12], b[ 6])
		+ MUL15(a[13], b[ 5])
		+ MUL15(a[14], b[ 4])
		+ MUL15(a[15], b[ 3])
		+ MUL15(a[16], b[ 2])
		+ MUL15(a[17], b[ 1])
		+ MUL15(a[18], b[ 0]);
	t[19] = MUL15(a[ 0], b[19])
		+ MUL15(a[ 1], b[18])
		+ MUL15(a[ 2], b[17])
		+ MUL15(a[ 3], b[16])
		+ MUL15(a[ 4], b[15])
		+ MUL15(a[ 5], b[14])
		+ MUL15(a[ 6], b[13])
		+ MUL15(a[ 7], b[12])
		+ MUL15(a[ 8], b[11])
		+ MUL15(a[ 9], b[10])
		+ MUL15(a[10], b[ 9])
		+ MUL15(a[11], b[ 8])
		+ MUL15(a[12], b[ 7])
		+ MUL15(a[13], b[ 6])
		+ MUL15(a[14], b[ 5])
		+ MUL15(a[15], b[ 4])
		+ MUL15(a[16], b[ 3])
		+ MUL15(a[17], b[ 2])
		+ MUL15(a[18], b[ 1])
		+ MUL15(a[19], b[ 0]);
	t[20] = MUL15(a[ 1], b[19])
		+ MUL15(a[ 2], b[18])
		+ MUL15(a[ 3], b[17])
		+ MUL15(a[ 4], b[16])
		+ MUL15(a[ 5], b[15])
		+ MUL15(a[ 6], b[14])
		+ MUL15(a[ 7], b[13])
		+ MUL15(a[ 8], b[12])
		+ MUL15(a[ 9], b[11])
		+ MUL15(a[10], b[10])
		+ MUL15(a[11], b[ 9])
		+ MUL15(a[12], b[ 8])
		+ MUL15(a[13], b[ 7])
		+ MUL15(a[14], b[ 6])
		+ MUL15(a[15], b[ 5])
		+ MUL15(a[16], b[ 4])
		+ MUL15(a[17], b[ 3])
		+ MUL15(a[18], b[ 2])
		+ MUL15(a[19], b[ 1]);
	t[21] = MUL15(a[ 2], b[19])
		+ MUL15(a[ 3], b[18])
		+ MUL15(a[ 4], b[17])
		+ MUL15(a[ 5], b[16])
		+ MUL15(a[ 6], b[15])
		+ MUL15(a[ 7], b[14])
		+ MUL15(a[ 8], b[13])
		+ MUL15(a[ 9], b[12])
		+ MUL15(a[10], b[11])
		+ MUL15(a[11], b[10])
		+ MUL15(a[12], b[ 9])
		+ MUL15(a[13], b[ 8])
		+ MUL15(a[14], b[ 7])
		+ MUL15(a[15], b[ 6])
		+ MUL15(a[16], b[ 5])
		+ MUL15(a[17], b[ 4])
		+ MUL15(a[18], b[ 3])
		+ MUL15(a[19], b[ 2]);
	t[22] = MUL15(a[ 3], b[19])
		+ MUL15(a[ 4], b[18])
		+ MUL15(a[ 5], b[17])
		+ MUL15(a[ 6], b[16])
		+ MUL15(a[ 7], b[15])
		+ MUL15(a[ 8], b[14])
		+ MUL15(a[ 9], b[13])
		+ MUL15(a[10], b[12])
		+ MUL15(a[11], b[11])
		+ MUL15(a[12], b[10])
		+ MUL15(a[13], b[ 9])
		+ MUL15(a[14], b[ 8])
		+ MUL15(a[15], b[ 7])
		+ MUL15(a[16], b[ 6])
		+ MUL15(a[17], b[ 5])
		+ MUL15(a[18], b[ 4])
		+ MUL15(a[19], b[ 3]);
	t[23] = MUL15(a[ 4], b[19])
		+ MUL15(a[ 5], b[18])
		+ MUL15(a[ 6], b[17])
		+ MUL15(a[ 7], b[16])
		+ MUL15(a[ 8], b[15])
		+ MUL15(a[ 9], b[14])
		+ MUL15(a[10], b[13])
		+ MUL15(a[11], b[12])
		+ MUL15(a[12], b[11])
		+ MUL15(a[13], b[10])
		+ MUL15(a[14], b[ 9])
		+ MUL15(a[15], b[ 8])
		+ MUL15(a[16], b[ 7])
		+ MUL15(a[17], b[ 6])
		+ MUL15(a[18], b[ 5])
		+ MUL15(a[19], b[ 4]);
	t[24] = MUL15(a[ 5], b[19])
		+ MUL15(a[ 6], b[18])
		+ MUL15(a[ 7], b[17])
		+ MUL15(a[ 8], b[16])
		+ MUL15(a[ 9], b[15])
		+ MUL15(a[10], b[14])
		+ MUL15(a[11], b[13])
		+ MUL15(a[12], b[12])
		+ MUL15(a[13], b[11])
		+ MUL15(a[14], b[10])
		+ MUL15(a[15], b[ 9])
		+ MUL15(a[16], b[ 8])
		+ MUL15(a[17], b[ 7])
		+ MUL15(a[18], b[ 6])
		+ MUL15(a[19], b[ 5]);
	t[25] = MUL15(a[ 6], b[19])
		+ MUL15(a[ 7], b[18])
		+ MUL15(a[ 8], b[17])
		+ MUL15(a[ 9], b[16])
		+ MUL15(a[10], b[15])
		+ MUL15(a[11], b[14])
		+ MUL15(a[12], b[13])
		+ MUL15(a[13], b[12])
		+ MUL15(a[14], b[11])
		+ MUL15(a[15], b[10])
		+ MUL15(a[16], b[ 9])
		+ MUL15(a[17], b[ 8])
		+ MUL15(a[18], b[ 7])
		+ MUL15(a[19], b[ 6]);
	t[26] = MUL15(a[ 7], b[19])
		+ MUL15(a[ 8], b[18])
		+ MUL15(a[ 9], b[17])
		+ MUL15(a[10], b[16])
		+ MUL15(a[11], b[15])
		+ MUL15(a[12], b[14])
		+ MUL15(a[13], b[13])
		+ MUL15(a[14], b[12])
		+ MUL15(a[15], b[11])
		+ MUL15(a[16], b[10])
		+ MUL15(a[17], b[ 9])
		+ MUL15(a[18], b[ 8])
		+ MUL15(a[19], b[ 7]);
	t[27] = MUL15(a[ 8], b[19])
		+ MUL15(a[ 9], b[18])
		+ MUL15(a[10], b[17])
		+ MUL15(a[11], b[16])
		+ MUL15(a[12], b[15])
		+ MUL15(a[13], b[14])
		+ MUL15(a[14], b[13])
		+ MUL15(a[15], b[12])
		+ MUL15(a[16], b[11])
		+ MUL15(a[17], b[10])
		+ MUL15(a[18], b[ 9])
		+ MUL15(a[19], b[ 8]);
	t[28] = MUL15(a[ 9], b[19])
		+ MUL15(a[10], b[18])
		+ MUL15(a[11], b[17])
		+ MUL15(a[12], b[16])
		+ MUL15(a[13], b[15])
		+ MUL15(a[14], b[14])
		+ MUL15(a[15], b[13])
		+ MUL15(a[16], b[12])
		+ MUL15(a[17], b[11])
		+ MUL15(a[18], b[10])
		+ MUL15(a[19], b[ 9]);
	t[29] = MUL15(a[10], b[19])
		+ MUL15(a[11], b[18])
		+ MUL15(a[12], b[17])
		+ MUL15(a[13], b[16])
		+ MUL15(a[14], b[15])
		+ MUL15(a[15], b[14])
		+ MUL15(a[16], b[13])
		+ MUL15(a[17], b[12])
		+ MUL15(a[18], b[11])
		+ MUL15(a[19], b[10]);
	t[30] = MUL15(a[11], b[19])
		+ MUL15(a[12], b[18])
		+ MUL15(a[13], b[17])
		+ MUL15(a[14], b[16])
		+ MUL15(a[15], b[15])
		+ MUL15(a[16], b[14])
		+ MUL15(a[17], b[13])
		+ MUL15(a[18], b[12])
		+ MUL15(a[19], b[11]);
	t[31] = MUL15(a[12], b[19])
		+ MUL15(a[13], b[18])
		+ MUL15(a[14], b[17])
		+ MUL15(a[15], b[16])
		+ MUL15(a[16], b[15])
		+ MUL15(a[17], b[14])
		+ MUL15(a[18], b[13])
		+ MUL15(a[19], b[12]);
	t[32] = MUL15(a[13], b[19])
		+ MUL15(a[14], b[18])
		+ MUL15(a[15], b[17])
		+ MUL15(a[16], b[16])
		+ MUL15(a[17], b[15])
		+ MUL15(a[18], b[14])
		+ MUL15(a[19], b[13]);
	t[33] = MUL15(a[14], b[19])
		+ MUL15(a[15], b[18])
		+ MUL15(a[16], b[17])
		+ MUL15(a[17], b[16])
		+ MUL15(a[18], b[15])
		+ MUL15(a[19], b[14]);
	t[34] = MUL15(a[15], b[19])
		+ MUL15(a[16], b[18])
		+ MUL15(a[17], b[17])
		+ MUL15(a[18], b[16])
		+ MUL15(a[19], b[15]);
	t[35] = MUL15(a[16], b[19])
		+ MUL15(a[17], b[18])
		+ MUL15(a[18], b[17])
		+ MUL15(a[19], b[16]);
	t[36] = MUL15(a[17], b[19])
		+ MUL15(a[18], b[18])
		+ MUL15(a[19], b[17]);
	t[37] = MUL15(a[18], b[19])
		+ MUL15(a[19], b[18]);
	t[38] = MUL15(a[19], b[19]);
	d[39] = norm13(d, t, 39);
}

static void
square20(uint32_t *d, const uint32_t *a)
{
	uint32_t t[39];

	t[ 0] = MUL15(a[ 0], a[ 0]);
	t[ 1] = ((MUL15(a[ 0], a[ 1])) << 1);
	t[ 2] = MUL15(a[ 1], a[ 1])
		+ ((MUL15(a[ 0], a[ 2])) << 1);
	t[ 3] = ((MUL15(a[ 0], a[ 3])
		+ MUL15(a[ 1], a[ 2])) << 1);
	t[ 4] = MUL15(a[ 2], a[ 2])
		+ ((MUL15(a[ 0], a[ 4])
		+ MUL15(a[ 1], a[ 3])) << 1);
	t[ 5] = ((MUL15(a[ 0], a[ 5])
		+ MUL15(a[ 1], a[ 4])
		+ MUL15(a[ 2], a[ 3])) << 1);
	t[ 6] = MUL15(a[ 3], a[ 3])
		+ ((MUL15(a[ 0], a[ 6])
		+ MUL15(a[ 1], a[ 5])
		+ MUL15(a[ 2], a[ 4])) << 1);
	t[ 7] = ((MUL15(a[ 0], a[ 7])
		+ MUL15(a[ 1], a[ 6])
		+ MUL15(a[ 2], a[ 5])
		+ MUL15(a[ 3], a[ 4])) << 1);
	t[ 8] = MUL15(a[ 4], a[ 4])
		+ ((MUL15(a[ 0], a[ 8])
		+ MUL15(a[ 1], a[ 7])
		+ MUL15(a[ 2], a[ 6])
		+ MUL15(a[ 3], a[ 5])) << 1);
	t[ 9] = ((MUL15(a[ 0], a[ 9])
		+ MUL15(a[ 1], a[ 8])
		+ MUL15(a[ 2], a[ 7])
		+ MUL15(a[ 3], a[ 6])
		+ MUL15(a[ 4], a[ 5])) << 1);
	t[10] = MUL15(a[ 5], a[ 5])
		+ ((MUL15(a[ 0], a[10])
		+ MUL15(a[ 1], a[ 9])
		+ MUL15(a[ 2], a[ 8])
		+ MUL15(a[ 3], a[ 7])
		+ MUL15(a[ 4], a[ 6])) << 1);
	t[11] = ((MUL15(a[ 0], a[11])
		+ MUL15(a[ 1], a[10])
		+ MUL15(a[ 2], a[ 9])
		+ MUL15(a[ 3], a[ 8])
		+ MUL15(a[ 4], a[ 7])
		+ MUL15(a[ 5], a[ 6])) << 1);
	t[12] = MUL15(a[ 6], a[ 6])
		+ ((MUL15(a[ 0], a[12])
		+ MUL15(a[ 1], a[11])
		+ MUL15(a[ 2], a[10])
		+ MUL15(a[ 3], a[ 9])
		+ MUL15(a[ 4], a[ 8])
		+ MUL15(a[ 5], a[ 7])) << 1);
	t[13] = ((MUL15(a[ 0], a[13])
		+ MUL15(a[ 1], a[12])
		+ MUL15(a[ 2], a[11])
		+ MUL15(a[ 3], a[10])
		+ MUL15(a[ 4], a[ 9])
		+ MUL15(a[ 5], a[ 8])
		+ MUL15(a[ 6], a[ 7])) << 1);
	t[14] = MUL15(a[ 7], a[ 7])
		+ ((MUL15(a[ 0], a[14])
		+ MUL15(a[ 1], a[13])
		+ MUL15(a[ 2], a[12])
		+ MUL15(a[ 3], a[11])
		+ MUL15(a[ 4], a[10])
		+ MUL15(a[ 5], a[ 9])
		+ MUL15(a[ 6], a[ 8])) << 1);
	t[15] = ((MUL15(a[ 0], a[15])
		+ MUL15(a[ 1], a[14])
		+ MUL15(a[ 2], a[13])
		+ MUL15(a[ 3], a[12])
		+ MUL15(a[ 4], a[11])
		+ MUL15(a[ 5], a[10])
		+ MUL15(a[ 6], a[ 9])
		+ MUL15(a[ 7], a[ 8])) << 1);
	t[16] = MUL15(a[ 8], a[ 8])
		+ ((MUL15(a[ 0], a[16])
		+ MUL15(a[ 1], a[15])
		+ MUL15(a[ 2], a[14])
		+ MUL15(a[ 3], a[13])
		+ MUL15(a[ 4], a[12])
		+ MUL15(a[ 5], a[11])
		+ MUL15(a[ 6], a[10])
		+ MUL15(a[ 7], a[ 9])) << 1);
	t[17] = ((MUL15(a[ 0], a[17])
		+ MUL15(a[ 1], a[16])
		+ MUL15(a[ 2], a[15])
		+ MUL15(a[ 3], a[14])
		+ MUL15(a[ 4], a[13])
		+ MUL15(a[ 5], a[12])
		+ MUL15(a[ 6], a[11])
		+ MUL15(a[ 7], a[10])
		+ MUL15(a[ 8], a[ 9])) << 1);
	t[18] = MUL15(a[ 9], a[ 9])
		+ ((MUL15(a[ 0], a[18])
		+ MUL15(a[ 1], a[17])
		+ MUL15(a[ 2], a[16])
		+ MUL15(a[ 3], a[15])
		+ MUL15(a[ 4], a[14])
		+ MUL15(a[ 5], a[13])
		+ MUL15(a[ 6], a[12])
		+ MUL15(a[ 7], a[11])
		+ MUL15(a[ 8], a[10])) << 1);
	t[19] = ((MUL15(a[ 0], a[19])
		+ MUL15(a[ 1], a[18])
		+ MUL15(a[ 2], a[17])
		+ MUL15(a[ 3], a[16])
		+ MUL15(a[ 4], a[15])
		+ MUL15(a[ 5], a[14])
		+ MUL15(a[ 6], a[13])
		+ MUL15(a[ 7], a[12])
		+ MUL15(a[ 8], a[11])
		+ MUL15(a[ 9], a[10])) << 1);
	t[20] = MUL15(a[10], a[10])
		+ ((MUL15(a[ 1], a[19])
		+ MUL15(a[ 2], a[18])
		+ MUL15(a[ 3], a[17])
		+ MUL15(a[ 4], a[16])
		+ MUL15(a[ 5], a[15])
		+ MUL15(a[ 6], a[14])
		+ MUL15(a[ 7], a[13])
		+ MUL15(a[ 8], a[12])
		+ MUL15(a[ 9], a[11])) << 1);
	t[21] = ((MUL15(a[ 2], a[19])
		+ MUL15(a[ 3], a[18])
		+ MUL15(a[ 4], a[17])
		+ MUL15(a[ 5], a[16])
		+ MUL15(a[ 6], a[15])
		+ MUL15(a[ 7], a[14])
		+ MUL15(a[ 8], a[13])
		+ MUL15(a[ 9], a[12])
		+ MUL15(a[10], a[11])) << 1);
	t[22] = MUL15(a[11], a[11])
		+ ((MUL15(a[ 3], a[19])
		+ MUL15(a[ 4], a[18])
		+ MUL15(a[ 5], a[17])
		+ MUL15(a[ 6], a[16])
		+ MUL15(a[ 7], a[15])
		+ MUL15(a[ 8], a[14])
		+ MUL15(a[ 9], a[13])
		+ MUL15(a[10], a[12])) << 1);
	t[23] = ((MUL15(a[ 4], a[19])
		+ MUL15(a[ 5], a[18])
		+ MUL15(a[ 6], a[17])
		+ MUL15(a[ 7], a[16])
		+ MUL15(a[ 8], a[15])
		+ MUL15(a[ 9], a[14])
		+ MUL15(a[10], a[13])
		+ MUL15(a[11], a[12])) << 1);
	t[24] = MUL15(a[12], a[12])
		+ ((MUL15(a[ 5], a[19])
		+ MUL15(a[ 6], a[18])
		+ MUL15(a[ 7], a[17])
		+ MUL15(a[ 8], a[16])
		+ MUL15(a[ 9], a[15])
		+ MUL15(a[10], a[14])
		+ MUL15(a[11], a[13])) << 1);
	t[25] = ((MUL15(a[ 6], a[19])
		+ MUL15(a[ 7], a[18])
		+ MUL15(a[ 8], a[17])
		+ MUL15(a[ 9], a[16])
		+ MUL15(a[10], a[15])
		+ MUL15(a[11], a[14])
		+ MUL15(a[12], a[13])) << 1);
	t[26] = MUL15(a[13], a[13])
		+ ((MUL15(a[ 7], a[19])
		+ MUL15(a[ 8], a[18])
		+ MUL15(a[ 9], a[17])
		+ MUL15(a[10], a[16])
		+ MUL15(a[11], a[15])
		+ MUL15(a[12], a[14])) << 1);
	t[27] = ((MUL15(a[ 8], a[19])
		+ MUL15(a[ 9], a[18])
		+ MUL15(a[10], a[17])
		+ MUL15(a[11], a[16])
		+ MUL15(a[12], a[15])
		+ MUL15(a[13], a[14])) << 1);
	t[28] = MUL15(a[14], a[14])
		+ ((MUL15(a[ 9], a[19])
		+ MUL15(a[10], a[18])
		+ MUL15(a[11], a[17])
		+ MUL15(a[12], a[16])
		+ MUL15(a[13], a[15])) << 1);
	t[29] = ((MUL15(a[10], a[19])
		+ MUL15(a[11], a[18])
		+ MUL15(a[12], a[17])
		+ MUL15(a[13], a[16])
		+ MUL15(a[14], a[15])) << 1);
	t[30] = MUL15(a[15], a[15])
		+ ((MUL15(a[11], a[19])
		+ MUL15(a[12], a[18])
		+ MUL15(a[13], a[17])
		+ MUL15(a[14], a[16])) << 1);
	t[31] = ((MUL15(a[12], a[19])
		+ MUL15(a[13], a[18])
		+ MUL15(a[14], a[17])
		+ MUL15(a[15], a[16])) << 1);
	t[32] = MUL15(a[16], a[16])
		+ ((MUL15(a[13], a[19])
		+ MUL15(a[14], a[18])
		+ MUL15(a[15], a[17])) << 1);
	t[33] = ((MUL15(a[14], a[19])
		+ MUL15(a[15], a[18])
		+ MUL15(a[16], a[17])) << 1);
	t[34] = MUL15(a[17], a[17])
		+ ((MUL15(a[15], a[19])
		+ MUL15(a[16], a[18])) << 1);
	t[35] = ((MUL15(a[16], a[19])
		+ MUL15(a[17], a[18])) << 1);
	t[36] = MUL15(a[18], a[18])
		+ ((MUL15(a[17], a[19])) << 1);
	t[37] = ((MUL15(a[18], a[19])) << 1);
	t[38] = MUL15(a[19], a[19]);
	d[39] = norm13(d, t, 39);
}

#endif

/*
 * Modulus for field F256 (field for point coordinates in curve P-256).
 */
static const uint32_t F256[] = {
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x001F,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0400, 0x0000,
	0x0000, 0x1FF8, 0x1FFF, 0x01FF
};

/*
 * The 'b' curve equation coefficient for P-256.
 */
static const uint32_t P256_B[] = {
	0x004B, 0x1E93, 0x0F89, 0x1C78, 0x03BC, 0x187B, 0x114E, 0x1619,
	0x1D06, 0x0328, 0x01AF, 0x0D31, 0x1557, 0x15DE, 0x1ECF, 0x127C,
	0x0A3A, 0x0EC5, 0x118D, 0x00B5
};

/*
 * Perform a "short reduction" in field F256 (field for curve P-256).
 * The source value should be less than 262 bits; on output, it will
 * be at most 257 bits, and less than twice the modulus.
 */
static void
reduce_f256(uint32_t *d)
{
	uint32_t x;

	x = d[19] >> 9;
	d[19] &= 0x01FF;
	d[17] += x << 3;
	d[14] -= x << 10;
	d[7] -= x << 5;
	d[0] += x;
	norm13(d, d, 20);
}

/*
 * Perform a "final reduction" in field F256 (field for curve P-256).
 * The source value must be less than twice the modulus. If the value
 * is not lower than the modulus, then the modulus is subtracted and
 * this function returns 1; otherwise, it leaves it untouched and it
 * returns 0.
 */
static uint32_t
reduce_final_f256(uint32_t *d)
{
	uint32_t t[20];
	uint32_t cc;
	int i;

	memcpy(t, d, sizeof t);
	cc = 0;
	for (i = 0; i < 20; i ++) {
		uint32_t w;

		w = t[i] - F256[i] - cc;
		cc = w >> 31;
		t[i] = w & 0x1FFF;
	}
	cc ^= 1;
	CCOPY(cc, d, t, sizeof t);
	return cc;
}

/*
 * Perform a multiplication of two integers modulo
 * 2^256-2^224+2^192+2^96-1 (for NIST curve P-256). Operands are arrays
 * of 20 words, each containing 13 bits of data, in little-endian order.
 * On input, upper word may be up to 13 bits (hence value up to 2^260-1);
 * on output, value fits on 257 bits and is lower than twice the modulus.
 */
static void
mul_f256(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	uint32_t t[40], cc;
	int i;

	/*
	 * Compute raw multiplication. All result words fit in 13 bits
	 * each.
	 */
	mul20(t, a, b);

	/*
	 * Modular reduction: each high word in added/subtracted where
	 * necessary.
	 *
	 * The modulus is:
	 *    p = 2^256 - 2^224 + 2^192 + 2^96 - 1
	 * Therefore:
	 *    2^256 = 2^224 - 2^192 - 2^96 + 1 mod p
	 *
	 * For a word x at bit offset n (n >= 256), we have:
	 *    x*2^n = x*2^(n-32) - x*2^(n-64)
	 *            - x*2^(n - 160) + x*2^(n-256) mod p
	 *
	 * Thus, we can nullify the high word if we reinject it at some
	 * proper emplacements.
	 */
	for (i = 39; i >= 20; i --) {
		uint32_t x;

		x = t[i];
		t[i - 2] += ARSH(x, 6);
		t[i - 3] += (x << 7) & 0x1FFF;
		t[i - 4] -= ARSH(x, 12);
		t[i - 5] -= (x << 1) & 0x1FFF;
		t[i - 12] -= ARSH(x, 4);
		t[i - 13] -= (x << 9) & 0x1FFF;
		t[i - 19] += ARSH(x, 9);
		t[i - 20] += (x << 4) & 0x1FFF;
	}

	/*
	 * Propagate carries. This is a signed propagation, and the
	 * result may be negative. The loop above may enlarge values,
	 * but not two much: worst case is the chain involving t[i - 3],
	 * in which a value may be added to itself up to 7 times. Since
	 * starting values are 13-bit each, all words fit on 20 bits
	 * (21 to account for the sign bit).
	 */
	cc = norm13(t, t, 20);

	/*
	 * Perform modular reduction again for the bits beyond 256 (the carry
	 * and the bits 256..259). Since the largest shift below is by 10
	 * bits, and the values fit on 21 bits, values fit in 32-bit words,
	 * thereby allowing injecting full word values.
	 */
	cc = (cc << 4) | (t[19] >> 9);
	t[19] &= 0x01FF;
	t[17] += cc << 3;
	t[14] -= cc << 10;
	t[7] -= cc << 5;
	t[0] += cc;

	/*
	 * If the carry is negative, then after carry propagation, we may
	 * end up with a value which is negative, and we don't want that.
	 * Thus, in that case, we add the modulus. Note that the subtraction
	 * result, when the carry is negative, is always smaller than the
	 * modulus, so the extra addition will not make the value exceed
	 * twice the modulus.
	 */
	cc >>= 31;
	t[0] -= cc;
	t[7] += cc << 5;
	t[14] += cc << 10;
	t[17] -= cc << 3;
	t[19] += cc << 9;

	norm13(d, t, 20);
}

/*
 * Square an integer modulo 2^256-2^224+2^192+2^96-1 (for NIST curve
 * P-256). Operand is an array of 20 words, each containing 13 bits of
 * data, in little-endian order. On input, upper word may be up to 13
 * bits (hence value up to 2^260-1); on output, value fits on 257 bits
 * and is lower than twice the modulus.
 */
static void
square_f256(uint32_t *d, const uint32_t *a)
{
	uint32_t t[40], cc;
	int i;

	/*
	 * Compute raw square. All result words fit in 13 bits each.
	 */
	square20(t, a);

	/*
	 * Modular reduction: each high word in added/subtracted where
	 * necessary.
	 *
	 * The modulus is:
	 *    p = 2^256 - 2^224 + 2^192 + 2^96 - 1
	 * Therefore:
	 *    2^256 = 2^224 - 2^192 - 2^96 + 1 mod p
	 *
	 * For a word x at bit offset n (n >= 256), we have:
	 *    x*2^n = x*2^(n-32) - x*2^(n-64)
	 *            - x*2^(n - 160) + x*2^(n-256) mod p
	 *
	 * Thus, we can nullify the high word if we reinject it at some
	 * proper emplacements.
	 */
	for (i = 39; i >= 20; i --) {
		uint32_t x;

		x = t[i];
		t[i - 2] += ARSH(x, 6);
		t[i - 3] += (x << 7) & 0x1FFF;
		t[i - 4] -= ARSH(x, 12);
		t[i - 5] -= (x << 1) & 0x1FFF;
		t[i - 12] -= ARSH(x, 4);
		t[i - 13] -= (x << 9) & 0x1FFF;
		t[i - 19] += ARSH(x, 9);
		t[i - 20] += (x << 4) & 0x1FFF;
	}

	/*
	 * Propagate carries. This is a signed propagation, and the
	 * result may be negative. The loop above may enlarge values,
	 * but not two much: worst case is the chain involving t[i - 3],
	 * in which a value may be added to itself up to 7 times. Since
	 * starting values are 13-bit each, all words fit on 20 bits
	 * (21 to account for the sign bit).
	 */
	cc = norm13(t, t, 20);

	/*
	 * Perform modular reduction again for the bits beyond 256 (the carry
	 * and the bits 256..259). Since the largest shift below is by 10
	 * bits, and the values fit on 21 bits, values fit in 32-bit words,
	 * thereby allowing injecting full word values.
	 */
	cc = (cc << 4) | (t[19] >> 9);
	t[19] &= 0x01FF;
	t[17] += cc << 3;
	t[14] -= cc << 10;
	t[7] -= cc << 5;
	t[0] += cc;

	/*
	 * If the carry is negative, then after carry propagation, we may
	 * end up with a value which is negative, and we don't want that.
	 * Thus, in that case, we add the modulus. Note that the subtraction
	 * result, when the carry is negative, is always smaller than the
	 * modulus, so the extra addition will not make the value exceed
	 * twice the modulus.
	 */
	cc >>= 31;
	t[0] -= cc;
	t[7] += cc << 5;
	t[14] += cc << 10;
	t[17] -= cc << 3;
	t[19] += cc << 9;

	norm13(d, t, 20);
}

/*
 * Jacobian coordinates for a point in P-256: affine coordinates (X,Y)
 * are such that:
 *   X = x / z^2
 *   Y = y / z^3
 * For the point at infinity, z = 0.
 * Each point thus admits many possible representations.
 *
 * Coordinates are represented in arrays of 32-bit integers, each holding
 * 13 bits of data. Values may also be slightly greater than the modulus,
 * but they will always be lower than twice the modulus.
 */
typedef struct {
	uint32_t x[20];
	uint32_t y[20];
	uint32_t z[20];
} p256_jacobian;

/*
 * Convert a point to affine coordinates:
 *  - If the point is the point at infinity, then all three coordinates
 *    are set to 0.
 *  - Otherwise, the 'z' coordinate is set to 1, and the 'x' and 'y'
 *    coordinates are the 'X' and 'Y' affine coordinates.
 * The coordinates are guaranteed to be lower than the modulus.
 */
static void
p256_to_affine(p256_jacobian *P)
{
	uint32_t t1[20], t2[20];
	int i;

	/*
	 * Invert z with a modular exponentiation: the modulus is
	 * p = 2^256 - 2^224 + 2^192 + 2^96 - 1, and the exponent is
	 * p-2. Exponent bit pattern (from high to low) is:
	 *  - 32 bits of value 1
	 *  - 31 bits of value 0
	 *  - 1 bit of value 1
	 *  - 96 bits of value 0
	 *  - 94 bits of value 1
	 *  - 1 bit of value 0
	 *  - 1 bit of value 1
	 * Thus, we precompute z^(2^31-1) to speed things up.
	 *
	 * If z = 0 (point at infinity) then the modular exponentiation
	 * will yield 0, which leads to the expected result (all three
	 * coordinates set to 0).
	 */

	/*
	 * A simple square-and-multiply for z^(2^31-1). We could save about
	 * two dozen multiplications here with an addition chain, but
	 * this would require a bit more code, and extra stack buffers.
	 */
	memcpy(t1, P->z, sizeof P->z);
	for (i = 0; i < 30; i ++) {
		square_f256(t1, t1);
		mul_f256(t1, t1, P->z);
	}

	/*
	 * Square-and-multiply. Apart from the squarings, we have a few
	 * multiplications to set bits to 1; we multiply by the original z
	 * for setting 1 bit, and by t1 for setting 31 bits.
	 */
	memcpy(t2, P->z, sizeof P->z);
	for (i = 1; i < 256; i ++) {
		square_f256(t2, t2);
		switch (i) {
		case 31:
		case 190:
		case 221:
		case 252:
			mul_f256(t2, t2, t1);
			break;
		case 63:
		case 253:
		case 255:
			mul_f256(t2, t2, P->z);
			break;
		}
	}

	/*
	 * Now that we have 1/z, multiply x by 1/z^2 and y by 1/z^3.
	 */
	mul_f256(t1, t2, t2);
	mul_f256(P->x, t1, P->x);
	mul_f256(t1, t1, t2);
	mul_f256(P->y, t1, P->y);
	reduce_final_f256(P->x);
	reduce_final_f256(P->y);

	/*
	 * Multiply z by 1/z. If z = 0, then this will yield 0, otherwise
	 * this will set z to 1.
	 */
	mul_f256(P->z, P->z, t2);
	reduce_final_f256(P->z);
}

/*
 * Double a point in P-256. This function works for all valid points,
 * including the point at infinity.
 */
static void
p256_double(p256_jacobian *Q)
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
	uint32_t t1[20], t2[20], t3[20], t4[20];
	int i;

	/*
	 * Compute z^2 in t1.
	 */
	square_f256(t1, Q->z);

	/*
	 * Compute x-z^2 in t2 and x+z^2 in t1.
	 */
	for (i = 0; i < 20; i ++) {
		t2[i] = (F256[i] << 1) + Q->x[i] - t1[i];
		t1[i] += Q->x[i];
	}
	norm13(t1, t1, 20);
	norm13(t2, t2, 20);

	/*
	 * Compute 3*(x+z^2)*(x-z^2) in t1.
	 */
	mul_f256(t3, t1, t2);
	for (i = 0; i < 20; i ++) {
		t1[i] = MUL15(3, t3[i]);
	}
	norm13(t1, t1, 20);

	/*
	 * Compute 4*x*y^2 (in t2) and 2*y^2 (in t3).
	 */
	square_f256(t3, Q->y);
	for (i = 0; i < 20; i ++) {
		t3[i] <<= 1;
	}
	norm13(t3, t3, 20);
	mul_f256(t2, Q->x, t3);
	for (i = 0; i < 20; i ++) {
		t2[i] <<= 1;
	}
	norm13(t2, t2, 20);
	reduce_f256(t2);

	/*
	 * Compute x' = m^2 - 2*s.
	 */
	square_f256(Q->x, t1);
	for (i = 0; i < 20; i ++) {
		Q->x[i] += (F256[i] << 2) - (t2[i] << 1);
	}
	norm13(Q->x, Q->x, 20);
	reduce_f256(Q->x);

	/*
	 * Compute z' = 2*y*z.
	 */
	mul_f256(t4, Q->y, Q->z);
	for (i = 0; i < 20; i ++) {
		Q->z[i] = t4[i] << 1;
	}
	norm13(Q->z, Q->z, 20);
	reduce_f256(Q->z);

	/*
	 * Compute y' = m*(s - x') - 8*y^4. Note that we already have
	 * 2*y^2 in t3.
	 */
	for (i = 0; i < 20; i ++) {
		t2[i] += (F256[i] << 1) - Q->x[i];
	}
	norm13(t2, t2, 20);
	mul_f256(Q->y, t1, t2);
	square_f256(t4, t3);
	for (i = 0; i < 20; i ++) {
		Q->y[i] += (F256[i] << 2) - (t4[i] << 1);
	}
	norm13(Q->y, Q->y, 20);
	reduce_f256(Q->y);
}

/*
 * Add point P2 to point P1.
 *
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
 *   - P1 and P2 have the same Y coordinate
 *   - P1 == 0 and P2 == 0
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
	uint32_t t1[20], t2[20], t3[20], t4[20], t5[20], t6[20], t7[20];
	uint32_t ret;
	int i;

	/*
	 * Compute u1 = x1*z2^2 (in t1) and s1 = y1*z2^3 (in t3).
	 */
	square_f256(t3, P2->z);
	mul_f256(t1, P1->x, t3);
	mul_f256(t4, P2->z, t3);
	mul_f256(t3, P1->y, t4);

	/*
	 * Compute u2 = x2*z1^2 (in t2) and s2 = y2*z1^3 (in t4).
	 */
	square_f256(t4, P1->z);
	mul_f256(t2, P2->x, t4);
	mul_f256(t5, P1->z, t4);
	mul_f256(t4, P2->y, t5);

	/*
	 * Compute h = h2 - u1 (in t2) and r = s2 - s1 (in t4).
	 * We need to test whether r is zero, so we will do some extra
	 * reduce.
	 */
	for (i = 0; i < 20; i ++) {
		t2[i] += (F256[i] << 1) - t1[i];
		t4[i] += (F256[i] << 1) - t3[i];
	}
	norm13(t2, t2, 20);
	norm13(t4, t4, 20);
	reduce_f256(t4);
	reduce_final_f256(t4);
	ret = 0;
	for (i = 0; i < 20; i ++) {
		ret |= t4[i];
	}
	ret = (ret | -ret) >> 31;

	/*
	 * Compute u1*h^2 (in t6) and h^3 (in t5);
	 */
	square_f256(t7, t2);
	mul_f256(t6, t1, t7);
	mul_f256(t5, t7, t2);

	/*
	 * Compute x3 = r^2 - h^3 - 2*u1*h^2.
	 */
	square_f256(P1->x, t4);
	for (i = 0; i < 20; i ++) {
		P1->x[i] += (F256[i] << 3) - t5[i] - (t6[i] << 1);
	}
	norm13(P1->x, P1->x, 20);
	reduce_f256(P1->x);

	/*
	 * Compute y3 = r*(u1*h^2 - x3) - s1*h^3.
	 */
	for (i = 0; i < 20; i ++) {
		t6[i] += (F256[i] << 1) - P1->x[i];
	}
	norm13(t6, t6, 20);
	mul_f256(P1->y, t4, t6);
	mul_f256(t1, t5, t3);
	for (i = 0; i < 20; i ++) {
		P1->y[i] += (F256[i] << 1) - t1[i];
	}
	norm13(P1->y, P1->y, 20);
	reduce_f256(P1->y);

	/*
	 * Compute z3 = h*z1*z2.
	 */
	mul_f256(t1, P1->z, P2->z);
	mul_f256(P1->z, t1, t2);

	return ret;
}

/*
 * Add point P2 to point P1. This is a specialised function for the
 * case when P2 is a non-zero point in affine coordinate.
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
 *   - P1 and P2 have the same Y coordinate
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
 */
static uint32_t
p256_add_mixed(p256_jacobian *P1, const p256_jacobian *P2)
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
	uint32_t t1[20], t2[20], t3[20], t4[20], t5[20], t6[20], t7[20];
	uint32_t ret;
	int i;

	/*
	 * Compute u1 = x1 (in t1) and s1 = y1 (in t3).
	 */
	memcpy(t1, P1->x, sizeof t1);
	memcpy(t3, P1->y, sizeof t3);

	/*
	 * Compute u2 = x2*z1^2 (in t2) and s2 = y2*z1^3 (in t4).
	 */
	square_f256(t4, P1->z);
	mul_f256(t2, P2->x, t4);
	mul_f256(t5, P1->z, t4);
	mul_f256(t4, P2->y, t5);

	/*
	 * Compute h = h2 - u1 (in t2) and r = s2 - s1 (in t4).
	 * We need to test whether r is zero, so we will do some extra
	 * reduce.
	 */
	for (i = 0; i < 20; i ++) {
		t2[i] += (F256[i] << 1) - t1[i];
		t4[i] += (F256[i] << 1) - t3[i];
	}
	norm13(t2, t2, 20);
	norm13(t4, t4, 20);
	reduce_f256(t4);
	reduce_final_f256(t4);
	ret = 0;
	for (i = 0; i < 20; i ++) {
		ret |= t4[i];
	}
	ret = (ret | -ret) >> 31;

	/*
	 * Compute u1*h^2 (in t6) and h^3 (in t5);
	 */
	square_f256(t7, t2);
	mul_f256(t6, t1, t7);
	mul_f256(t5, t7, t2);

	/*
	 * Compute x3 = r^2 - h^3 - 2*u1*h^2.
	 */
	square_f256(P1->x, t4);
	for (i = 0; i < 20; i ++) {
		P1->x[i] += (F256[i] << 3) - t5[i] - (t6[i] << 1);
	}
	norm13(P1->x, P1->x, 20);
	reduce_f256(P1->x);

	/*
	 * Compute y3 = r*(u1*h^2 - x3) - s1*h^3.
	 */
	for (i = 0; i < 20; i ++) {
		t6[i] += (F256[i] << 1) - P1->x[i];
	}
	norm13(t6, t6, 20);
	mul_f256(P1->y, t4, t6);
	mul_f256(t1, t5, t3);
	for (i = 0; i < 20; i ++) {
		P1->y[i] += (F256[i] << 1) - t1[i];
	}
	norm13(P1->y, P1->y, 20);
	reduce_f256(P1->y);

	/*
	 * Compute z3 = h*z1*z2.
	 */
	mul_f256(P1->z, P1->z, t2);

	return ret;
}

/*
 * Decode a P-256 point. This function does not support the point at
 * infinity. Returned value is 0 if the point is invalid, 1 otherwise.
 */
static uint32_t
p256_decode(p256_jacobian *P, const void *src, size_t len)
{
	const unsigned char *buf;
	uint32_t tx[20], ty[20], t1[20], t2[20];
	uint32_t bad;
	int i;

	if (len != 65) {
		return 0;
	}
	buf = src;

	/*
	 * First byte must be 0x04 (uncompressed format). We could support
	 * "hybrid format" (first byte is 0x06 or 0x07, and encodes the
	 * least significant bit of the Y coordinate), but it is explicitly
	 * forbidden by RFC 5480 (section 2.2).
	 */
	bad = NEQ(buf[0], 0x04);

	/*
	 * Decode the coordinates, and check that they are both lower
	 * than the modulus.
	 */
	tx[19] = be8_to_le13(tx, buf + 1, 32);
	ty[19] = be8_to_le13(ty, buf + 33, 32);
	bad |= reduce_final_f256(tx);
	bad |= reduce_final_f256(ty);

	/*
	 * Check curve equation.
	 */
	square_f256(t1, tx);
	mul_f256(t1, tx, t1);
	square_f256(t2, ty);
	for (i = 0; i < 20; i ++) {
		t1[i] += (F256[i] << 3) - MUL15(3, tx[i]) + P256_B[i] - t2[i];
	}
	norm13(t1, t1, 20);
	reduce_f256(t1);
	reduce_final_f256(t1);
	for (i = 0; i < 20; i ++) {
		bad |= t1[i];
	}

	/*
	 * Copy coordinates to the point structure.
	 */
	memcpy(P->x, tx, sizeof tx);
	memcpy(P->y, ty, sizeof ty);
	memset(P->z, 0, sizeof P->z);
	P->z[0] = 1;
	return EQ(bad, 0);
}

/*
 * Encode a point into a buffer. This function assumes that the point is
 * valid, in affine coordinates, and not the point at infinity.
 */
static void
p256_encode(void *dst, const p256_jacobian *P)
{
	unsigned char *buf;

	buf = dst;
	buf[0] = 0x04;
	le13_to_be8(buf + 1, 32, P->x);
	le13_to_be8(buf + 33, 32, P->y);
}

/*
 * Multiply a curve point by an integer. The integer is assumed to be
 * lower than the curve order, and the base point must not be the point
 * at infinity.
 */
static void
p256_mul(p256_jacobian *P, const unsigned char *x, size_t xlen)
{
	/*
	 * qz is a flag that is initially 1, and remains equal to 1
	 * as long as the point is the point at infinity.
	 *
	 * We use a 2-bit window to handle multiplier bits by pairs.
	 * The precomputed window really is the points P2 and P3.
	 */
	uint32_t qz;
	p256_jacobian P2, P3, Q, T, U;

	/*
	 * Compute window values.
	 */
	P2 = *P;
	p256_double(&P2);
	P3 = *P;
	p256_add(&P3, &P2);

	/*
	 * We start with Q = 0. We process multiplier bits 2 by 2.
	 */
	memset(&Q, 0, sizeof Q);
	qz = 1;
	while (xlen -- > 0) {
		int k;

		for (k = 6; k >= 0; k -= 2) {
			uint32_t bits;
			uint32_t bnz;

			p256_double(&Q);
			p256_double(&Q);
			T = *P;
			U = Q;
			bits = (*x >> k) & (uint32_t)3;
			bnz = NEQ(bits, 0);
			CCOPY(EQ(bits, 2), &T, &P2, sizeof T);
			CCOPY(EQ(bits, 3), &T, &P3, sizeof T);
			p256_add(&U, &T);
			CCOPY(bnz & qz, &Q, &T, sizeof Q);
			CCOPY(bnz & ~qz, &Q, &U, sizeof Q);
			qz &= ~bnz;
		}
		x ++;
	}
	*P = Q;
}

/*
 * Precomputed window: k*G points, where G is the curve generator, and k
 * is an integer from 1 to 15 (inclusive). The X and Y coordinates of
 * the point are encoded as 20 words of 13 bits each (little-endian
 * order); 13-bit words are then grouped 2-by-2 into 32-bit words
 * (little-endian order within each word).
 */
static const uint32_t Gwin[15][20] = {

	{ 0x04C60296, 0x02721176, 0x19D00F4A, 0x102517AC,
	  0x13B8037D, 0x0748103C, 0x1E730E56, 0x08481FE2,
	  0x0F97012C, 0x00D605F4, 0x1DFA11F5, 0x0C801A0D,
	  0x0F670CBB, 0x0AED0CC5, 0x115E0E33, 0x181F0785,
	  0x13F514A7, 0x0FF30E3B, 0x17171E1A, 0x009F18D0 },

	{ 0x1B341978, 0x16911F11, 0x0D9A1A60, 0x1C4E1FC8,
	  0x1E040969, 0x096A06B0, 0x091C0030, 0x09EF1A29,
	  0x18C40D03, 0x00F91C9E, 0x13C313D1, 0x096F0748,
	  0x011419E0, 0x1CC713A6, 0x1DD31DAD, 0x1EE80C36,
	  0x1ECD0C69, 0x1A0800A4, 0x08861B8E, 0x000E1DD5 },

	{ 0x173F1D6C, 0x02CC06F1, 0x14C21FB4, 0x043D1EB6,
	  0x0F3606B7, 0x1A971C59, 0x1BF71951, 0x01481323,
	  0x068D0633, 0x00BD12F9, 0x13EA1032, 0x136209E8,
	  0x1C1E19A7, 0x06C7013E, 0x06C10AB0, 0x14C908BB,
	  0x05830CE1, 0x1FEF18DD, 0x00620998, 0x010E0D19 },

	{ 0x18180852, 0x0604111A, 0x0B771509, 0x1B6F0156,
	  0x00181FE2, 0x1DCC0AF4, 0x16EF0659, 0x11F70E80,
	  0x11A912D0, 0x01C414D2, 0x027618C6, 0x05840FC6,
	  0x100215C4, 0x187E0C3B, 0x12771C96, 0x150C0B5D,
	  0x0FF705FD, 0x07981C67, 0x1AD20C63, 0x01C11C55 },

	{ 0x1E8113ED, 0x0A940370, 0x12920215, 0x1FA31D6F,
	  0x1F7C0C82, 0x10CD03F7, 0x02640560, 0x081A0B5E,
	  0x1BD21151, 0x00A21642, 0x0D0B0DA4, 0x0176113F,
	  0x04440D1D, 0x001A1360, 0x1068012F, 0x1F141E49,
	  0x10DF136B, 0x0E4F162B, 0x0D44104A, 0x01C1105F },

	{ 0x011411A9, 0x01551A4F, 0x0ADA0C6B, 0x01BD0EC8,
	  0x18120C74, 0x112F1778, 0x099202CB, 0x0C05124B,
	  0x195316A4, 0x01600685, 0x1E3B1FE2, 0x189014E3,
	  0x0B5E1FD7, 0x0E0311F8, 0x08E000F7, 0x174E00DE,
	  0x160702DF, 0x1B5A15BF, 0x03A11237, 0x01D01704 },

	{ 0x0C3D12A3, 0x0C501C0C, 0x17AD1300, 0x1715003F,
	  0x03F719F8, 0x18031ED8, 0x1D980667, 0x0F681896,
	  0x1B7D00BF, 0x011C14CE, 0x0FA000B4, 0x1C3501B0,
	  0x0D901C55, 0x06790C10, 0x029E0736, 0x0DEB0400,
	  0x034F183A, 0x030619B4, 0x0DEF0033, 0x00E71AC7 },

	{ 0x1B7D1393, 0x1B3B1076, 0x0BED1B4D, 0x13011F3A,
	  0x0E0E1238, 0x156A132B, 0x013A02D3, 0x160A0D01,
	  0x1CED1EE9, 0x00C5165D, 0x184C157E, 0x08141A83,
	  0x153C0DA5, 0x1ED70F9D, 0x05170D51, 0x02CF13B8,
	  0x18AE1771, 0x1B04113F, 0x05EC11E9, 0x015A16B3 },

	{ 0x04A41EE0, 0x1D1412E4, 0x1C591D79, 0x118511B7,
	  0x14F00ACB, 0x1AE31E1C, 0x049C0D51, 0x016E061E,
	  0x1DB71EDF, 0x01D41A35, 0x0E8208FA, 0x14441293,
	  0x011F1E85, 0x1D54137A, 0x026B114F, 0x151D0832,
	  0x00A50964, 0x1F9C1E1C, 0x064B12C9, 0x005409D1 },

	{ 0x062B123F, 0x0C0D0501, 0x183704C3, 0x08E31120,
	  0x0A2E0A6C, 0x14440FED, 0x090A0D1E, 0x13271964,
	  0x0B590A3A, 0x019D1D9B, 0x05780773, 0x09770A91,
	  0x0F770CA3, 0x053F19D4, 0x02C80DED, 0x1A761304,
	  0x091E0DD9, 0x15D201B8, 0x151109AA, 0x010F0198 },

	{ 0x05E101D1, 0x072314DD, 0x045F1433, 0x1A041541,
	  0x10B3142E, 0x01840736, 0x1C1B19DB, 0x098B0418,
	  0x1DBC083B, 0x007D1444, 0x01511740, 0x11DD1F3A,
	  0x04ED0E2F, 0x1B4B1A62, 0x10480D04, 0x09E911A2,
	  0x04211AFA, 0x19140893, 0x04D60CC4, 0x01210648 },

	{ 0x112703C4, 0x018B1BA1, 0x164C1D50, 0x05160BE0,
	  0x0BCC1830, 0x01CB1554, 0x13291732, 0x1B2B1918,
	  0x0DED0817, 0x00E80775, 0x0A2401D3, 0x0BFE08B3,
	  0x0E531199, 0x058616E9, 0x04770B91, 0x110F0C55,
	  0x19C11554, 0x0BFB1159, 0x03541C38, 0x000E1C2D },

	{ 0x10390C01, 0x02BB0751, 0x0AC5098E, 0x096C17AB,
	  0x03C90E28, 0x10BD18BF, 0x002E1F2D, 0x092B0986,
	  0x1BD700AC, 0x002E1F20, 0x1E3D1FD8, 0x077718BB,
	  0x06F919C4, 0x187407ED, 0x11370E14, 0x081E139C,
	  0x00481ADB, 0x14AB0289, 0x066A0EBE, 0x00C70ED6 },

	{ 0x0694120B, 0x124E1CC9, 0x0E2F0570, 0x17CF081A,
	  0x078906AC, 0x066D17CF, 0x1B3207F4, 0x0C5705E9,
	  0x10001C38, 0x00A919DE, 0x06851375, 0x0F900BD8,
	  0x080401BA, 0x0EEE0D42, 0x1B8B11EA, 0x0B4519F0,
	  0x090F18C0, 0x062E1508, 0x0DD909F4, 0x01EB067C },

	{ 0x0CDC1D5F, 0x0D1818F9, 0x07781636, 0x125B18E8,
	  0x0D7003AF, 0x13110099, 0x1D9B1899, 0x175C1EB7,
	  0x0E34171A, 0x01E01153, 0x081A0F36, 0x0B391783,
	  0x1D1F147E, 0x19CE16D7, 0x11511B21, 0x1F2C10F9,
	  0x12CA0E51, 0x05A31D39, 0x171A192E, 0x016B0E4F }
};

/*
 * Lookup one of the Gwin[] values, by index. This is constant-time.
 */
static void
lookup_Gwin(p256_jacobian *T, uint32_t idx)
{
	uint32_t xy[20];
	uint32_t k;
	size_t u;

	memset(xy, 0, sizeof xy);
	for (k = 0; k < 15; k ++) {
		uint32_t m;

		m = -EQ(idx, k + 1);
		for (u = 0; u < 20; u ++) {
			xy[u] |= m & Gwin[k][u];
		}
	}
	for (u = 0; u < 10; u ++) {
		T->x[(u << 1) + 0] = xy[u] & 0xFFFF;
		T->x[(u << 1) + 1] = xy[u] >> 16;
		T->y[(u << 1) + 0] = xy[u + 10] & 0xFFFF;
		T->y[(u << 1) + 1] = xy[u + 10] >> 16;
	}
	memset(T->z, 0, sizeof T->z);
	T->z[0] = 1;
}

/*
 * Multiply the generator by an integer. The integer is assumed non-zero
 * and lower than the curve order.
 */
static void
p256_mulgen(p256_jacobian *P, const unsigned char *x, size_t xlen)
{
	/*
	 * qz is a flag that is initially 1, and remains equal to 1
	 * as long as the point is the point at infinity.
	 *
	 * We use a 4-bit window to handle multiplier bits by groups
	 * of 4. The precomputed window is constant static data, with
	 * points in affine coordinates; we use a constant-time lookup.
	 */
	p256_jacobian Q;
	uint32_t qz;

	memset(&Q, 0, sizeof Q);
	qz = 1;
	while (xlen -- > 0) {
		int k;
		unsigned bx;

		bx = *x ++;
		for (k = 0; k < 2; k ++) {
			uint32_t bits;
			uint32_t bnz;
			p256_jacobian T, U;

			p256_double(&Q);
			p256_double(&Q);
			p256_double(&Q);
			p256_double(&Q);
			bits = (bx >> 4) & 0x0F;
			bnz = NEQ(bits, 0);
			lookup_Gwin(&T, bits);
			U = Q;
			p256_add_mixed(&U, &T);
			CCOPY(bnz & qz, &Q, &T, sizeof Q);
			CCOPY(bnz & ~qz, &Q, &U, sizeof Q);
			qz &= ~bnz;
			bx <<= 4;
		}
	}
	*P = Q;
}

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

static uint32_t
api_mul(unsigned char *G, size_t Glen,
	const unsigned char *x, size_t xlen, int curve)
{
	uint32_t r;
	p256_jacobian P;

	(void)curve;
	r = p256_decode(&P, G, Glen);
	p256_mul(&P, x, xlen);
	if (Glen >= 65) {
		p256_to_affine(&P);
		p256_encode(G, &P);
	}
	return r;
}

static size_t
api_mulgen(unsigned char *R,
	const unsigned char *x, size_t xlen, int curve)
{
	p256_jacobian P;

	(void)curve;
	p256_mulgen(&P, x, xlen);
	p256_to_affine(&P);
	p256_encode(R, &P);
	return 65;

	/*
	const unsigned char *G;
	size_t Glen;

	G = api_generator(curve, &Glen);
	memcpy(R, G, Glen);
	api_mul(R, Glen, x, xlen, curve);
	return Glen;
	*/
}

static uint32_t
api_muladd(unsigned char *A, const unsigned char *B, size_t len,
	const unsigned char *x, size_t xlen,
	const unsigned char *y, size_t ylen, int curve)
{
	p256_jacobian P, Q;
	uint32_t r, t, z;
	int i;

	(void)curve;
	r = p256_decode(&P, A, len);
	p256_mul(&P, x, xlen);
	if (B == NULL) {
		p256_mulgen(&Q, y, ylen);
	} else {
		r &= p256_decode(&Q, B, len);
		p256_mul(&Q, y, ylen);
	}

	/*
	 * The final addition may fail in case both points are equal.
	 */
	t = p256_add(&P, &Q);
	reduce_final_f256(P.z);
	z = 0;
	for (i = 0; i < 20; i ++) {
		z |= P.z[i];
	}
	z = EQ(z, 0);
	p256_double(&Q);

	/*
	 * If z is 1 then either P+Q = 0 (t = 1) or P = Q (t = 0). So we
	 * have the following:
	 *
	 *   z = 0, t = 0   return P (normal addition)
	 *   z = 0, t = 1   return P (normal addition)
	 *   z = 1, t = 0   return Q (a 'double' case)
	 *   z = 1, t = 1   report an error (P+Q = 0)
	 */
	CCOPY(z & ~t, &P, &Q, sizeof Q);
	p256_to_affine(&P);
	p256_encode(A, &P);
	r &= ~(z & t);
	return r;
}

/* see bearssl_ec.h */
const br_ec_impl br_ec_p256_m15 = {
	(uint32_t)0x00800000,
	&api_generator,
	&api_order,
	&api_xoff,
	&api_mul,
	&api_mulgen,
	&api_muladd
};
