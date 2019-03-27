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
	unsigned char tmp[40];

	printf("%s = ", name);
	for (u = 0; u < 9; u ++) {
		if (x[u] > 0x3FFFFFFF) {
			printf("INVALID:");
			for (u = 0; u < 9; u ++) {
				printf(" %08X", x[u]);
			}
			printf("\n");
			return;
		}
	}
	memset(tmp, 0, sizeof tmp);
	for (u = 0; u < 9; u ++) {
		uint64_t w;
		int j, k;

		w = x[u];
		j = 30 * (int)u;
		k = j & 7;
		if (k != 0) {
			w <<= k;
			j -= k;
		}
		k = j >> 3;
		for (j = 0; j < 8; j ++) {
			tmp[39 - k - j] |= (unsigned char)w;
			w >>= 8;
		}
	}
	for (u = 8; u < 40; u ++) {
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
 * 30-bit words in little-endian order. The final "partial" word is
 * returned.
 */
static uint32_t
le8_to_le30(uint32_t *dst, const unsigned char *src, size_t len)
{
	uint32_t acc;
	int acc_len;

	acc = 0;
	acc_len = 0;
	while (len -- > 0) {
		uint32_t b;

		b = *src ++;
		if (acc_len < 22) {
			acc |= b << acc_len;
			acc_len += 8;
		} else {
			*dst ++ = (acc | (b << acc_len)) & 0x3FFFFFFF;
			acc = b >> (30 - acc_len);
			acc_len -= 22;
		}
	}
	return acc;
}

/*
 * Convert an integer (30-bit words, little-endian) to unsigned
 * little-endian encoding. The total encoding length is provided; all
 * the destination bytes will be filled.
 */
static void
le30_to_le8(unsigned char *dst, size_t len, const uint32_t *src)
{
	uint32_t acc;
	int acc_len;

	acc = 0;
	acc_len = 0;
	while (len -- > 0) {
		if (acc_len < 8) {
			uint32_t w;

			w = *src ++;
			*dst ++ = (unsigned char)(acc | (w << acc_len));
			acc = w >> (8 - acc_len);
			acc_len += 22;
		} else {
			*dst ++ = (unsigned char)acc;
			acc >>= 8;
			acc_len -= 8;
		}
	}
}

/*
 * Multiply two integers. Source integers are represented as arrays of
 * nine 30-bit words, for values up to 2^270-1. Result is encoded over
 * 18 words of 30 bits each.
 */
static void
mul9(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	/*
	 * Maximum intermediate result is no more than
	 * 10376293531797946367, which fits in 64 bits. Reason:
	 *
	 *   10376293531797946367 = 9 * (2^30-1)^2 + 9663676406
	 *   10376293531797946367 < 9663676407 * 2^30
	 *
	 * Thus, adding together 9 products of 30-bit integers, with
	 * a carry of at most 9663676406, yields an integer that fits
	 * on 64 bits and generates a carry of at most 9663676406.
	 */
	uint64_t t[17];
	uint64_t cc;
	int i;

	t[ 0] = MUL31(a[0], b[0]);
	t[ 1] = MUL31(a[0], b[1])
		+ MUL31(a[1], b[0]);
	t[ 2] = MUL31(a[0], b[2])
		+ MUL31(a[1], b[1])
		+ MUL31(a[2], b[0]);
	t[ 3] = MUL31(a[0], b[3])
		+ MUL31(a[1], b[2])
		+ MUL31(a[2], b[1])
		+ MUL31(a[3], b[0]);
	t[ 4] = MUL31(a[0], b[4])
		+ MUL31(a[1], b[3])
		+ MUL31(a[2], b[2])
		+ MUL31(a[3], b[1])
		+ MUL31(a[4], b[0]);
	t[ 5] = MUL31(a[0], b[5])
		+ MUL31(a[1], b[4])
		+ MUL31(a[2], b[3])
		+ MUL31(a[3], b[2])
		+ MUL31(a[4], b[1])
		+ MUL31(a[5], b[0]);
	t[ 6] = MUL31(a[0], b[6])
		+ MUL31(a[1], b[5])
		+ MUL31(a[2], b[4])
		+ MUL31(a[3], b[3])
		+ MUL31(a[4], b[2])
		+ MUL31(a[5], b[1])
		+ MUL31(a[6], b[0]);
	t[ 7] = MUL31(a[0], b[7])
		+ MUL31(a[1], b[6])
		+ MUL31(a[2], b[5])
		+ MUL31(a[3], b[4])
		+ MUL31(a[4], b[3])
		+ MUL31(a[5], b[2])
		+ MUL31(a[6], b[1])
		+ MUL31(a[7], b[0]);
	t[ 8] = MUL31(a[0], b[8])
		+ MUL31(a[1], b[7])
		+ MUL31(a[2], b[6])
		+ MUL31(a[3], b[5])
		+ MUL31(a[4], b[4])
		+ MUL31(a[5], b[3])
		+ MUL31(a[6], b[2])
		+ MUL31(a[7], b[1])
		+ MUL31(a[8], b[0]);
	t[ 9] = MUL31(a[1], b[8])
		+ MUL31(a[2], b[7])
		+ MUL31(a[3], b[6])
		+ MUL31(a[4], b[5])
		+ MUL31(a[5], b[4])
		+ MUL31(a[6], b[3])
		+ MUL31(a[7], b[2])
		+ MUL31(a[8], b[1]);
	t[10] = MUL31(a[2], b[8])
		+ MUL31(a[3], b[7])
		+ MUL31(a[4], b[6])
		+ MUL31(a[5], b[5])
		+ MUL31(a[6], b[4])
		+ MUL31(a[7], b[3])
		+ MUL31(a[8], b[2]);
	t[11] = MUL31(a[3], b[8])
		+ MUL31(a[4], b[7])
		+ MUL31(a[5], b[6])
		+ MUL31(a[6], b[5])
		+ MUL31(a[7], b[4])
		+ MUL31(a[8], b[3]);
	t[12] = MUL31(a[4], b[8])
		+ MUL31(a[5], b[7])
		+ MUL31(a[6], b[6])
		+ MUL31(a[7], b[5])
		+ MUL31(a[8], b[4]);
	t[13] = MUL31(a[5], b[8])
		+ MUL31(a[6], b[7])
		+ MUL31(a[7], b[6])
		+ MUL31(a[8], b[5]);
	t[14] = MUL31(a[6], b[8])
		+ MUL31(a[7], b[7])
		+ MUL31(a[8], b[6]);
	t[15] = MUL31(a[7], b[8])
		+ MUL31(a[8], b[7]);
	t[16] = MUL31(a[8], b[8]);

	/*
	 * Propagate carries.
	 */
	cc = 0;
	for (i = 0; i < 17; i ++) {
		uint64_t w;

		w = t[i] + cc;
		d[i] = (uint32_t)w & 0x3FFFFFFF;
		cc = w >> 30;
	}
	d[17] = (uint32_t)cc;
}

/*
 * Square a 270-bit integer, represented as an array of nine 30-bit words.
 * Result uses 18 words of 30 bits each.
 */
static void
square9(uint32_t *d, const uint32_t *a)
{
	uint64_t t[17];
	uint64_t cc;
	int i;

	t[ 0] = MUL31(a[0], a[0]);
	t[ 1] = ((MUL31(a[0], a[1])) << 1);
	t[ 2] = MUL31(a[1], a[1])
		+ ((MUL31(a[0], a[2])) << 1);
	t[ 3] = ((MUL31(a[0], a[3])
		+ MUL31(a[1], a[2])) << 1);
	t[ 4] = MUL31(a[2], a[2])
		+ ((MUL31(a[0], a[4])
		+ MUL31(a[1], a[3])) << 1);
	t[ 5] = ((MUL31(a[0], a[5])
		+ MUL31(a[1], a[4])
		+ MUL31(a[2], a[3])) << 1);
	t[ 6] = MUL31(a[3], a[3])
		+ ((MUL31(a[0], a[6])
		+ MUL31(a[1], a[5])
		+ MUL31(a[2], a[4])) << 1);
	t[ 7] = ((MUL31(a[0], a[7])
		+ MUL31(a[1], a[6])
		+ MUL31(a[2], a[5])
		+ MUL31(a[3], a[4])) << 1);
	t[ 8] = MUL31(a[4], a[4])
		+ ((MUL31(a[0], a[8])
		+ MUL31(a[1], a[7])
		+ MUL31(a[2], a[6])
		+ MUL31(a[3], a[5])) << 1);
	t[ 9] = ((MUL31(a[1], a[8])
		+ MUL31(a[2], a[7])
		+ MUL31(a[3], a[6])
		+ MUL31(a[4], a[5])) << 1);
	t[10] = MUL31(a[5], a[5])
		+ ((MUL31(a[2], a[8])
		+ MUL31(a[3], a[7])
		+ MUL31(a[4], a[6])) << 1);
	t[11] = ((MUL31(a[3], a[8])
		+ MUL31(a[4], a[7])
		+ MUL31(a[5], a[6])) << 1);
	t[12] = MUL31(a[6], a[6])
		+ ((MUL31(a[4], a[8])
		+ MUL31(a[5], a[7])) << 1);
	t[13] = ((MUL31(a[5], a[8])
		+ MUL31(a[6], a[7])) << 1);
	t[14] = MUL31(a[7], a[7])
		+ ((MUL31(a[6], a[8])) << 1);
	t[15] = ((MUL31(a[7], a[8])) << 1);
	t[16] = MUL31(a[8], a[8]);

	/*
	 * Propagate carries.
	 */
	cc = 0;
	for (i = 0; i < 17; i ++) {
		uint64_t w;

		w = t[i] + cc;
		d[i] = (uint32_t)w & 0x3FFFFFFF;
		cc = w >> 30;
	}
	d[17] = (uint32_t)cc;
}

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
	uint32_t t[9];
	uint32_t cc;
	int i;

	memcpy(t, d, sizeof t);
	cc = 19;
	for (i = 0; i < 9; i ++) {
		uint32_t w;

		w = t[i] + cc;
		cc = w >> 30;
		t[i] = w & 0x3FFFFFFF;
	}
	cc = t[8] >> 15;
	t[8] &= 0x7FFF;
	CCOPY(cc, d, t, sizeof t);
	return cc;
}

/*
 * Perform a multiplication of two integers modulo 2^255-19.
 * Operands are arrays of 9 words, each containing 30 bits of data, in
 * little-endian order. Input value may be up to 2^256-1; on output, value
 * fits on 256 bits and is lower than twice the modulus.
 */
static void
f255_mul(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	uint32_t t[18], cc;
	int i;

	/*
	 * Compute raw multiplication. All result words fit in 30 bits
	 * each; upper word (t[17]) must fit on 2 bits, since the product
	 * of two 256-bit integers must fit on 512 bits.
	 */
	mul9(t, a, b);

	/*
	 * Modular reduction: each high word is added where necessary.
	 * Since the modulus is 2^255-19 and word 9 corresponds to
	 * offset 9*30 = 270, word 9+k must be added to word k with
	 * a factor of 19*2^15 = 622592. The extra bits in word 8 are also
	 * added that way.
	 *
	 * Keeping the carry on 32 bits helps with 32-bit architectures,
	 * and does not noticeably impact performance on 64-bit systems.
	 */
	cc = MUL15(t[8] >> 15, 19);  /* at most 19*(2^15-1) = 622573 */
	t[8] &= 0x7FFF;
	for (i = 0; i < 9; i ++) {
		uint64_t w;

		w = (uint64_t)t[i] + (uint64_t)cc + MUL31(t[i + 9], 622592);
		t[i] = (uint32_t)w & 0x3FFFFFFF;
		cc = (uint32_t)(w >> 30);  /* at most 622592 */
	}

	/*
	 * Original product was up to (2^256-1)^2, i.e. a 512-bit integer.
	 * This was split into two parts (upper of 257 bits, lower of 255
	 * bits), and the upper was added to the lower with a factor 19,
	 * which means that the intermediate value is less than 77*2^255
	 * (19*2^257 + 2^255). Therefore, the extra bits "t[8] >> 15" are
	 * less than 77, and the initial carry cc is at most 76*19 = 1444.
	 */
	cc = MUL15(t[8] >> 15, 19);
	t[8] &= 0x7FFF;
	for (i = 0; i < 9; i ++) {
		uint32_t z;

		z = t[i] + cc;
		d[i] = z & 0x3FFFFFFF;
		cc = z >> 30;
	}

	/*
	 * Final result is at most 2^255 + 1443. In particular, the last
	 * carry is necessarily 0, since t[8] was truncated to 15 bits.
	 */
}

/*
 * Perform a squaring of an integer modulo 2^255-19.
 * Operands are arrays of 9 words, each containing 30 bits of data, in
 * little-endian order. Input value may be up to 2^256-1; on output, value
 * fits on 256 bits and is lower than twice the modulus.
 */
static void
f255_square(uint32_t *d, const uint32_t *a)
{
	uint32_t t[18], cc;
	int i;

	/*
	 * Compute raw squaring. All result words fit in 30 bits
	 * each; upper word (t[17]) must fit on 2 bits, since the square
	 * of a 256-bit integers must fit on 512 bits.
	 */
	square9(t, a);

	/*
	 * Modular reduction: each high word is added where necessary.
	 * See f255_mul() for details on the reduction and carry limits.
	 */
	cc = MUL15(t[8] >> 15, 19);
	t[8] &= 0x7FFF;
	for (i = 0; i < 9; i ++) {
		uint64_t w;

		w = (uint64_t)t[i] + (uint64_t)cc + MUL31(t[i + 9], 622592);
		t[i] = (uint32_t)w & 0x3FFFFFFF;
		cc = (uint32_t)(w >> 30);
	}
	cc = MUL15(t[8] >> 15, 19);
	t[8] &= 0x7FFF;
	for (i = 0; i < 9; i ++) {
		uint32_t z;

		z = t[i] + cc;
		d[i] = z & 0x3FFFFFFF;
		cc = z >> 30;
	}
}

/*
 * Add two values in F255. Partial reduction is performed (down to less
 * than twice the modulus).
 */
static void
f255_add(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	/*
	 * Since operand words fit on 30 bits, we can use 32-bit
	 * variables throughout.
	 */
	int i;
	uint32_t cc, w;

	cc = 0;
	for (i = 0; i < 9; i ++) {
		w = a[i] + b[i] + cc;
		d[i] = w & 0x3FFFFFFF;
		cc = w >> 30;
	}
	cc = MUL15(w >> 15, 19);
	d[8] &= 0x7FFF;
	for (i = 0; i < 9; i ++) {
		w = d[i] + cc;
		d[i] = w & 0x3FFFFFFF;
		cc = w >> 30;
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
	for (i = 0; i < 9; i ++) {
		w = a[i] - b[i] + cc;
		d[i] = w & 0x3FFFFFFF;
		cc = ARSH(w, 30);
	}
	cc = MUL15((w + 0x10000) >> 15, 19);
	d[8] &= 0x7FFF;
	for (i = 0; i < 9; i ++) {
		w = d[i] + cc;
		d[i] = w & 0x3FFFFFFF;
		cc = w >> 30;
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
	uint64_t w;
	uint32_t cc;

	/*
	 * a[] is over 256 bits, thus a[8] has length at most 16 bits.
	 * We single out the processing of the last word: intermediate
	 * value w is up to 121665*2^16, yielding a carry for the next
	 * loop of at most 19*(121665*2^16/2^15) = 4623289.
	 */
	cc = 0;
	for (i = 0; i < 8; i ++) {
		w = MUL31(a[i], 121665) + (uint64_t)cc;
		d[i] = (uint32_t)w & 0x3FFFFFFF;
		cc = (uint32_t)(w >> 30);
	}
	w = MUL31(a[8], 121665) + (uint64_t)cc;
	d[8] = (uint32_t)w & 0x7FFF;
	cc = MUL15((uint32_t)(w >> 15), 19);

	for (i = 0; i < 9; i ++) {
		uint32_t z;

		z = d[i] + cc;
		d[i] = z & 0x3FFFFFFF;
		cc = z >> 30;
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
	for (i = 0; i < 9; i ++) {
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
	uint32_t x1[9], x2[9], x3[9], z2[9], z3[9];
	uint32_t a[9], aa[9], b[9], bb[9];
	uint32_t c[9], d[9], e[9], da[9], cb[9];
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
	x1[8] = le8_to_le30(x1, G, 32);
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
	le30_to_le8(G, 32, x2);
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
const br_ec_impl br_ec_c25519_m31 = {
	(uint32_t)0x20000000,
	&api_generator,
	&api_order,
	&api_xoff,
	&api_mul,
	&api_mulgen,
	&api_muladd
};
