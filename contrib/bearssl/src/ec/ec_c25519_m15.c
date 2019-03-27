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

/* obsolete
#include <stdio.h>
#include <stdlib.h>
static void
print_int(const char *name, const uint32_t *x)
{
	size_t u;
	unsigned char tmp[36];

	printf("%s = ", name);
	for (u = 0; u < 20; u ++) {
		if (x[u] > 0x1FFF) {
			printf("INVALID:");
			for (u = 0; u < 20; u ++) {
				printf(" %04X", x[u]);
			}
			printf("\n");
			return;
		}
	}
	memset(tmp, 0, sizeof tmp);
	for (u = 0; u < 20; u ++) {
		uint32_t w;
		int j, k;

		w = x[u];
		j = 13 * (int)u;
		k = j & 7;
		if (k != 0) {
			w <<= k;
			j -= k;
		}
		k = j >> 3;
		tmp[35 - k] |= (unsigned char)w;
		tmp[34 - k] |= (unsigned char)(w >> 8);
		tmp[33 - k] |= (unsigned char)(w >> 16);
		tmp[32 - k] |= (unsigned char)(w >> 24);
	}
	for (u = 4; u < 36; u ++) {
		printf("%02X", tmp[u]);
	}
	printf("\n");
}
*/

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
 * Convert an integer from unsigned little-endian encoding to a sequence of
 * 13-bit words in little-endian order. The final "partial" word is
 * returned.
 */
static uint32_t
le8_to_le13(uint32_t *dst, const unsigned char *src, size_t len)
{
	uint32_t acc;
	int acc_len;

	acc = 0;
	acc_len = 0;
	while (len -- > 0) {
		acc |= (uint32_t)(*src ++) << acc_len;
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
 * little-endian encoding. The total encoding length is provided; all
 * the destination bytes will be filled.
 */
static void
le13_to_le8(unsigned char *dst, size_t len, const uint32_t *src)
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
		*dst ++ = (unsigned char)acc;
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
 * Perform a "final reduction" in field F255 (field for Curve25519)
 * The source value must be less than twice the modulus. If the value
 * is not lower than the modulus, then the modulus is subtracted and
 * this function returns 1; otherwise, it leaves it untouched and it
 * returns 0.
 */
static uint32_t
reduce_final_f255(uint32_t *d)
{
	uint32_t t[20];
	uint32_t cc;
	int i;

	memcpy(t, d, sizeof t);
	cc = 19;
	for (i = 0; i < 20; i ++) {
		uint32_t w;

		w = t[i] + cc;
		cc = w >> 13;
		t[i] = w & 0x1FFF;
	}
	cc = t[19] >> 8;
	t[19] &= 0xFF;
	CCOPY(cc, d, t, sizeof t);
	return cc;
}

static void
f255_mulgen(uint32_t *d, const uint32_t *a, const uint32_t *b, int square)
{
	uint32_t t[40], cc, w;

	/*
	 * Compute raw multiplication. All result words fit in 13 bits
	 * each; upper word (t[39]) must fit on 5 bits, since the product
	 * of two 256-bit integers must fit on 512 bits.
	 */
	if (square) {
		square20(t, a);
	} else {
		mul20(t, a, b);
	}

	/*
	 * Modular reduction: each high word is added where necessary.
	 * Since the modulus is 2^255-19 and word 20 corresponds to
	 * offset 20*13 = 260, word 20+k must be added to word k with
	 * a factor of 19*2^5 = 608. The extra bits in word 19 are also
	 * added that way.
	 */
	cc = MUL15(t[19] >> 8, 19);
	t[19] &= 0xFF;

#define MM1(x)   do { \
		w = t[x] + cc + MUL15(t[(x) + 20], 608); \
		t[x] = w & 0x1FFF; \
		cc = w >> 13; \
	} while (0)

	MM1( 0);
	MM1( 1);
	MM1( 2);
	MM1( 3);
	MM1( 4);
	MM1( 5);
	MM1( 6);
	MM1( 7);
	MM1( 8);
	MM1( 9);
	MM1(10);
	MM1(11);
	MM1(12);
	MM1(13);
	MM1(14);
	MM1(15);
	MM1(16);
	MM1(17);
	MM1(18);
	MM1(19);

#undef MM1

	cc = MUL15(w >> 8, 19);
	t[19] &= 0xFF;

#define MM2(x)   do { \
		w = t[x] + cc; \
		d[x] = w & 0x1FFF; \
		cc = w >> 13; \
	} while (0)

	MM2( 0);
	MM2( 1);
	MM2( 2);
	MM2( 3);
	MM2( 4);
	MM2( 5);
	MM2( 6);
	MM2( 7);
	MM2( 8);
	MM2( 9);
	MM2(10);
	MM2(11);
	MM2(12);
	MM2(13);
	MM2(14);
	MM2(15);
	MM2(16);
	MM2(17);
	MM2(18);
	MM2(19);

#undef MM2
}

/*
 * Perform a multiplication of two integers modulo 2^255-19.
 * Operands are arrays of 20 words, each containing 13 bits of data, in
 * little-endian order. Input value may be up to 2^256-1; on output, value
 * fits on 256 bits and is lower than twice the modulus.
 *
 * f255_mul() is the general multiplication, f255_square() is specialised
 * for squarings.
 */
#define f255_mul(d, a, b)   f255_mulgen(d, a, b, 0)
#define f255_square(d, a)   f255_mulgen(d, a, a, 1)

/*
 * Add two values in F255. Partial reduction is performed (down to less
 * than twice the modulus).
 */
static void
f255_add(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	int i;
	uint32_t cc, w;

	cc = 0;
	for (i = 0; i < 20; i ++) {
		w = a[i] + b[i] + cc;
		d[i] = w & 0x1FFF;
		cc = w >> 13;
	}
	cc = MUL15(w >> 8, 19);
	d[19] &= 0xFF;
	for (i = 0; i < 20; i ++) {
		w = d[i] + cc;
		d[i] = w & 0x1FFF;
		cc = w >> 13;
	}
}

/*
 * Subtract one value from another in F255. Partial reduction is
 * performed (down to less than twice the modulus).
 */
static void
f255_sub(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	/*
	 * We actually compute a - b + 2*p, so that the final value is
	 * necessarily positive.
	 */
	int i;
	uint32_t cc, w;

	cc = (uint32_t)-38;
	for (i = 0; i < 20; i ++) {
		w = a[i] - b[i] + cc;
		d[i] = w & 0x1FFF;
		cc = ARSH(w, 13);
	}
	cc = MUL15((w + 0x200) >> 8, 19);
	d[19] &= 0xFF;
	for (i = 0; i < 20; i ++) {
		w = d[i] + cc;
		d[i] = w & 0x1FFF;
		cc = w >> 13;
	}
}

/*
 * Multiply an integer by the 'A24' constant (121665). Partial reduction
 * is performed (down to less than twice the modulus).
 */
static void
f255_mul_a24(uint32_t *d, const uint32_t *a)
{
	int i;
	uint32_t cc, w;

	cc = 0;
	for (i = 0; i < 20; i ++) {
		w = MUL15(a[i], 121665) + cc;
		d[i] = w & 0x1FFF;
		cc = w >> 13;
	}
	cc = MUL15(w >> 8, 19);
	d[19] &= 0xFF;
	for (i = 0; i < 20; i ++) {
		w = d[i] + cc;
		d[i] = w & 0x1FFF;
		cc = w >> 13;
	}
}

static const unsigned char GEN[] = {
	0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char ORDER[] = {
	0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const unsigned char *
api_generator(int curve, size_t *len)
{
	(void)curve;
	*len = 32;
	return GEN;
}

static const unsigned char *
api_order(int curve, size_t *len)
{
	(void)curve;
	*len = 32;
	return ORDER;
}

static size_t
api_xoff(int curve, size_t *len)
{
	(void)curve;
	*len = 32;
	return 0;
}

static void
cswap(uint32_t *a, uint32_t *b, uint32_t ctl)
{
	int i;

	ctl = -ctl;
	for (i = 0; i < 20; i ++) {
		uint32_t aw, bw, tw;

		aw = a[i];
		bw = b[i];
		tw = ctl & (aw ^ bw);
		a[i] = aw ^ tw;
		b[i] = bw ^ tw;
	}
}

static uint32_t
api_mul(unsigned char *G, size_t Glen,
	const unsigned char *kb, size_t kblen, int curve)
{
	uint32_t x1[20], x2[20], x3[20], z2[20], z3[20];
	uint32_t a[20], aa[20], b[20], bb[20];
	uint32_t c[20], d[20], e[20], da[20], cb[20];
	unsigned char k[32];
	uint32_t swap;
	int i;

	(void)curve;

	/*
	 * Points are encoded over exactly 32 bytes. Multipliers must fit
	 * in 32 bytes as well.
	 * RFC 7748 mandates that the high bit of the last point byte must
	 * be ignored/cleared.
	 */
	if (Glen != 32 || kblen > 32) {
		return 0;
	}
	G[31] &= 0x7F;

	/*
	 * Initialise variables x1, x2, z2, x3 and z3. We set all of them
	 * into Montgomery representation.
	 */
	x1[19] = le8_to_le13(x1, G, 32);
	memcpy(x3, x1, sizeof x1);
	memset(z2, 0, sizeof z2);
	memset(x2, 0, sizeof x2);
	x2[0] = 1;
	memset(z3, 0, sizeof z3);
	z3[0] = 1;

	memset(k, 0, (sizeof k) - kblen);
	memcpy(k + (sizeof k) - kblen, kb, kblen);
	k[31] &= 0xF8;
	k[0] &= 0x7F;
	k[0] |= 0x40;

	/* obsolete
	print_int("x1", x1);
	*/

	swap = 0;
	for (i = 254; i >= 0; i --) {
		uint32_t kt;

		kt = (k[31 - (i >> 3)] >> (i & 7)) & 1;
		swap ^= kt;
		cswap(x2, x3, swap);
		cswap(z2, z3, swap);
		swap = kt;

		/* obsolete
		print_int("x2", x2);
		print_int("z2", z2);
		print_int("x3", x3);
		print_int("z3", z3);
		*/

		f255_add(a, x2, z2);
		f255_square(aa, a);
		f255_sub(b, x2, z2);
		f255_square(bb, b);
		f255_sub(e, aa, bb);
		f255_add(c, x3, z3);
		f255_sub(d, x3, z3);
		f255_mul(da, d, a);
		f255_mul(cb, c, b);

		/* obsolete
		print_int("a ", a);
		print_int("aa", aa);
		print_int("b ", b);
		print_int("bb", bb);
		print_int("e ", e);
		print_int("c ", c);
		print_int("d ", d);
		print_int("da", da);
		print_int("cb", cb);
		*/

		f255_add(x3, da, cb);
		f255_square(x3, x3);
		f255_sub(z3, da, cb);
		f255_square(z3, z3);
		f255_mul(z3, z3, x1);
		f255_mul(x2, aa, bb);
		f255_mul_a24(z2, e);
		f255_add(z2, z2, aa);
		f255_mul(z2, e, z2);

		/* obsolete
		print_int("x2", x2);
		print_int("z2", z2);
		print_int("x3", x3);
		print_int("z3", z3);
		*/
	}
	cswap(x2, x3, swap);
	cswap(z2, z3, swap);

	/*
	 * Inverse z2 with a modular exponentiation. This is a simple
	 * square-and-multiply algorithm; we mutualise most non-squarings
	 * since the exponent contains almost only ones.
	 */
	memcpy(a, z2, sizeof z2);
	for (i = 0; i < 15; i ++) {
		f255_square(a, a);
		f255_mul(a, a, z2);
	}
	memcpy(b, a, sizeof a);
	for (i = 0; i < 14; i ++) {
		int j;

		for (j = 0; j < 16; j ++) {
			f255_square(b, b);
		}
		f255_mul(b, b, a);
	}
	for (i = 14; i >= 0; i --) {
		f255_square(b, b);
		if ((0xFFEB >> i) & 1) {
			f255_mul(b, z2, b);
		}
	}
	f255_mul(x2, x2, b);
	reduce_final_f255(x2);
	le13_to_le8(G, 32, x2);
	return 1;
}

static size_t
api_mulgen(unsigned char *R,
	const unsigned char *x, size_t xlen, int curve)
{
	const unsigned char *G;
	size_t Glen;

	G = api_generator(curve, &Glen);
	memcpy(R, G, Glen);
	api_mul(R, Glen, x, xlen, curve);
	return Glen;
}

static uint32_t
api_muladd(unsigned char *A, const unsigned char *B, size_t len,
	const unsigned char *x, size_t xlen,
	const unsigned char *y, size_t ylen, int curve)
{
	/*
	 * We don't implement this method, since it is used for ECDSA
	 * only, and there is no ECDSA over Curve25519 (which instead
	 * uses EdDSA).
	 */
	(void)A;
	(void)B;
	(void)len;
	(void)x;
	(void)xlen;
	(void)y;
	(void)ylen;
	(void)curve;
	return 0;
}

/* see bearssl_ec.h */
const br_ec_impl br_ec_c25519_m15 = {
	(uint32_t)0x20000000,
	&api_generator,
	&api_order,
	&api_xoff,
	&api_mul,
	&api_mulgen,
	&api_muladd
};
