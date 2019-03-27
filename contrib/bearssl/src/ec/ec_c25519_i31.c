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
 * Parameters for the field:
 *   - field modulus p = 2^255-19
 *   - R^2 mod p (R = 2^(31k) for the smallest k such that R >= p)
 */

static const uint32_t C255_P[] = {
	0x00000107,
	0x7FFFFFED, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x0000007F
};

#define P0I   0x286BCA1B

static const uint32_t C255_R2[] = {
	0x00000107,
	0x00000000, 0x02D20000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000
};

static const uint32_t C255_A24[] = {
	0x00000107,
	0x53000000, 0x0000468B, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000
};

/* obsolete
#include <stdio.h>
#include <stdlib.h>
static void
print_int_mont(const char *name, const uint32_t *x)
{
	uint32_t y[10];
	unsigned char tmp[32];
	size_t u;

	printf("%s = ", name);
	memcpy(y, x, sizeof y);
	br_i31_from_monty(y, C255_P, P0I);
	br_i31_encode(tmp, sizeof tmp, y);
	for (u = 0; u < sizeof tmp; u ++) {
		printf("%02X", tmp[u]);
	}
	printf("\n");
}
*/

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
	for (i = 0; i < 10; i ++) {
		uint32_t aw, bw, tw;

		aw = a[i];
		bw = b[i];
		tw = ctl & (aw ^ bw);
		a[i] = aw ^ tw;
		b[i] = bw ^ tw;
	}
}

static void
c255_add(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	uint32_t ctl;
	uint32_t t[10];

	memcpy(t, a, sizeof t);
	ctl = br_i31_add(t, b, 1);
	ctl |= NOT(br_i31_sub(t, C255_P, 0));
	br_i31_sub(t, C255_P, ctl);
	memcpy(d, t, sizeof t);
}

static void
c255_sub(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	uint32_t t[10];

	memcpy(t, a, sizeof t);
	br_i31_add(t, C255_P, br_i31_sub(t, b, 1));
	memcpy(d, t, sizeof t);
}

static void
c255_mul(uint32_t *d, const uint32_t *a, const uint32_t *b)
{
	uint32_t t[10];

	br_i31_montymul(t, a, b, C255_P, P0I);
	memcpy(d, t, sizeof t);
}

static void
byteswap(unsigned char *G)
{
	int i;

	for (i = 0; i < 16; i ++) {
		unsigned char t;

		t = G[i];
		G[i] = G[31 - i];
		G[31 - i] = t;
	}
}

static uint32_t
api_mul(unsigned char *G, size_t Glen,
	const unsigned char *kb, size_t kblen, int curve)
{
	uint32_t x1[10], x2[10], x3[10], z2[10], z3[10];
	uint32_t a[10], aa[10], b[10], bb[10];
	uint32_t c[10], d[10], e[10], da[10], cb[10];
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
	 * Byteswap the point encoding, because it uses little-endian, and
	 * the generic decoding routine uses big-endian.
	 */
	byteswap(G);

	/*
	 * Decode the point ('u' coordinate). This should be reduced
	 * modulo p, but we prefer to avoid the dependency on
	 * br_i31_decode_reduce(). Instead, we use br_i31_decode_mod()
	 * with a synthetic modulus of value 2^255 (this must work
	 * since G was truncated to 255 bits), then use a conditional
	 * subtraction. We use br_i31_decode_mod() and not
	 * br_i31_decode(), because the ec_prime_i31 implementation uses
	 * the former but not the latter.
	 *    br_i31_decode_reduce(a, G, 32, C255_P);
	 */
	br_i31_zero(b, 0x108);
	b[9] = 0x0080;
	br_i31_decode_mod(a, G, 32, b);
	a[0] = 0x107;
	br_i31_sub(a, C255_P, NOT(br_i31_sub(a, C255_P, 0)));

	/*
	 * Initialise variables x1, x2, z2, x3 and z3. We set all of them
	 * into Montgomery representation.
	 */
	br_i31_montymul(x1, a, C255_R2, C255_P, P0I);
	memcpy(x3, x1, sizeof x1);
	br_i31_zero(z2, C255_P[0]);
	memcpy(x2, z2, sizeof z2);
	x2[1] = 0x13000000;
	memcpy(z3, x2, sizeof x2);

	/*
	 * kb[] is in big-endian notation, but possibly shorter than k[].
	 */
	memset(k, 0, (sizeof k) - kblen);
	memcpy(k + (sizeof k) - kblen, kb, kblen);
	k[31] &= 0xF8;
	k[0] &= 0x7F;
	k[0] |= 0x40;

	/* obsolete
	print_int_mont("x1", x1);
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
		print_int_mont("x2", x2);
		print_int_mont("z2", z2);
		print_int_mont("x3", x3);
		print_int_mont("z3", z3);
		*/

		c255_add(a, x2, z2);
		c255_mul(aa, a, a);
		c255_sub(b, x2, z2);
		c255_mul(bb, b, b);
		c255_sub(e, aa, bb);
		c255_add(c, x3, z3);
		c255_sub(d, x3, z3);
		c255_mul(da, d, a);
		c255_mul(cb, c, b);

		/* obsolete
		print_int_mont("a ", a);
		print_int_mont("aa", aa);
		print_int_mont("b ", b);
		print_int_mont("bb", bb);
		print_int_mont("e ", e);
		print_int_mont("c ", c);
		print_int_mont("d ", d);
		print_int_mont("da", da);
		print_int_mont("cb", cb);
		*/

		c255_add(x3, da, cb);
		c255_mul(x3, x3, x3);
		c255_sub(z3, da, cb);
		c255_mul(z3, z3, z3);
		c255_mul(z3, z3, x1);
		c255_mul(x2, aa, bb);
		c255_mul(z2, C255_A24, e);
		c255_add(z2, z2, aa);
		c255_mul(z2, e, z2);

		/* obsolete
		print_int_mont("x2", x2);
		print_int_mont("z2", z2);
		print_int_mont("x3", x3);
		print_int_mont("z3", z3);
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
		c255_mul(a, a, a);
		c255_mul(a, a, z2);
	}
	memcpy(b, a, sizeof a);
	for (i = 0; i < 14; i ++) {
		int j;

		for (j = 0; j < 16; j ++) {
			c255_mul(b, b, b);
		}
		c255_mul(b, b, a);
	}
	for (i = 14; i >= 0; i --) {
		c255_mul(b, b, b);
		if ((0xFFEB >> i) & 1) {
			c255_mul(b, z2, b);
		}
	}
	c255_mul(b, x2, b);

	/*
	 * To avoid a dependency on br_i31_from_monty(), we use
	 * a Montgomery multiplication with 1.
	 *    memcpy(x2, b, sizeof b);
	 *    br_i31_from_monty(x2, C255_P, P0I);
	 */
	br_i31_zero(a, C255_P[0]);
	a[1] = 1;
	br_i31_montymul(x2, a, b, C255_P, P0I);

	br_i31_encode(G, 32, x2);
	byteswap(G);
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
const br_ec_impl br_ec_c25519_i31 = {
	(uint32_t)0x20000000,
	&api_generator,
	&api_order,
	&api_xoff,
	&api_mul,
	&api_mulgen,
	&api_muladd
};
