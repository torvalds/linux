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

#define MUL128(hi, lo, x, y)   do { \
		unsigned __int128 mul128tmp; \
		mul128tmp = (unsigned __int128)(x) * (unsigned __int128)(y); \
		(hi) = (uint64_t)(mul128tmp >> 64); \
		(lo) = (uint64_t)mul128tmp; \
	} while (0)

#elif BR_UMUL128

#include <intrin.h>

#define MUL128(hi, lo, x, y)   do { \
		(lo) = _umul128((x), (y), &(hi)); \
	} while (0)

#endif

#define MASK42   ((uint64_t)0x000003FFFFFFFFFF)
#define MASK44   ((uint64_t)0x00000FFFFFFFFFFF)

/*
 * The "accumulator" word is nominally a 130-bit value. We split it into
 * words of 44 bits, each held in a 64-bit variable.
 *
 * If the current accumulator is a = a0 + a1*W + a2*W^2 (where W = 2^44)
 * and r = r0 + r1*W + r2*W^2, then:
 *
 *   a*r = (a0*r0)
 *       + (a0*r1 + a1*r0) * W
 *       + (a0*r2 + a1*r1 + a2*r0) * W^2
 *       + (a1*r2 + a2*r1) * W^3
 *       + (a2*r2) * W^4
 *
 * We want to reduce that value modulo p = 2^130-5, so W^3 = 20 mod p,
 * and W^4 = 20*W mod p. Thus, if we define u1 = 20*r1 and u2 = 20*r2,
 * then the equations above become:
 *
 *  b0 = a0*r0 + a1*u2 + a2*u1
 *  b1 = a0*r1 + a1*r0 + a2*u2
 *  b2 = a0*r2 + a1*r1 + a2*r0
 *
 * In order to make u1 fit in 44 bits, we can change these equations
 * into:
 *
 *  b0 = a0*r0 + a1*u2 + a2*t1
 *  b1 = a0*r1 + a1*r0 + a2*t2
 *  b2 = a0*r2 + a1*r1 + a2*r0
 *
 * Where t1 is u1 truncated to 44 bits, and t2 is u2 added to the extra
 * bits of u1. Note that since r is clamped down to a 124-bit value, the
 * values u2 and t2 fit on 44 bits too.
 *
 * The bx values are larger than 44 bits, so we may split them into a
 * lower half (cx, 44 bits) and an upper half (dx). The new values for
 * the accumulator are then:
 *
 *  e0 = c0 + 20*d2
 *  e1 = c1 + d0
 *  e2 = c2 + d1
 *
 * The equations allow for some room, i.e. the ax values may be larger
 * than 44 bits. Similarly, the ex values will usually be larger than
 * the ax. Thus, some sort of carry propagation must be done regularly,
 * though not necessarily at each iteration. In particular, we do not
 * need to compute the additions (for the bx values) over 128-bit
 * quantities; we can stick to 64-bit computations.
 *
 *
 * Since the 128-bit result of a 64x64 multiplication is actually
 * represented over two 64-bit registers, it is cheaper to arrange for
 * any split that happens between the "high" and "low" halves to be on
 * that 64-bit boundary. This is done by left shifting the rx, ux and tx
 * by 20 bits (since they all fit on 44 bits each, this shift is
 * always possible).
 */

static void
poly1305_inner_big(uint64_t *acc, uint64_t *r, const void *data, size_t len)
{

#define MX(hi, lo, m0, m1, m2)   do { \
		uint64_t mxhi, mxlo; \
		MUL128(mxhi, mxlo, a0, m0); \
		(hi) = mxhi; \
		(lo) = mxlo >> 20; \
		MUL128(mxhi, mxlo, a1, m1); \
		(hi) += mxhi; \
		(lo) += mxlo >> 20; \
		MUL128(mxhi, mxlo, a2, m2); \
		(hi) += mxhi; \
		(lo) += mxlo >> 20; \
	} while (0)

	const unsigned char *buf;
	uint64_t a0, a1, a2;
	uint64_t r0, r1, r2, t1, t2, u2;

	r0 = r[0];
	r1 = r[1];
	r2 = r[2];
	t1 = r[3];
	t2 = r[4];
	u2 = r[5];
	a0 = acc[0];
	a1 = acc[1];
	a2 = acc[2];
	buf = data;

	while (len > 0) {
		uint64_t v0, v1, v2;
		uint64_t c0, c1, c2, d0, d1, d2;

		v0 = br_dec64le(buf + 0);
		v1 = br_dec64le(buf + 8);
		v2 = v1 >> 24;
		v1 = ((v0 >> 44) | (v1 << 20)) & MASK44;
		v0 &= MASK44;
		a0 += v0;
		a1 += v1;
		a2 += v2 + ((uint64_t)1 << 40);
		MX(d0, c0, r0, u2, t1);
		MX(d1, c1, r1, r0, t2);
		MX(d2, c2, r2, r1, r0);
		a0 = c0 + 20 * d2;
		a1 = c1 + d0;
		a2 = c2 + d1;

		v0 = br_dec64le(buf + 16);
		v1 = br_dec64le(buf + 24);
		v2 = v1 >> 24;
		v1 = ((v0 >> 44) | (v1 << 20)) & MASK44;
		v0 &= MASK44;
		a0 += v0;
		a1 += v1;
		a2 += v2 + ((uint64_t)1 << 40);
		MX(d0, c0, r0, u2, t1);
		MX(d1, c1, r1, r0, t2);
		MX(d2, c2, r2, r1, r0);
		a0 = c0 + 20 * d2;
		a1 = c1 + d0;
		a2 = c2 + d1;

		v0 = br_dec64le(buf + 32);
		v1 = br_dec64le(buf + 40);
		v2 = v1 >> 24;
		v1 = ((v0 >> 44) | (v1 << 20)) & MASK44;
		v0 &= MASK44;
		a0 += v0;
		a1 += v1;
		a2 += v2 + ((uint64_t)1 << 40);
		MX(d0, c0, r0, u2, t1);
		MX(d1, c1, r1, r0, t2);
		MX(d2, c2, r2, r1, r0);
		a0 = c0 + 20 * d2;
		a1 = c1 + d0;
		a2 = c2 + d1;

		v0 = br_dec64le(buf + 48);
		v1 = br_dec64le(buf + 56);
		v2 = v1 >> 24;
		v1 = ((v0 >> 44) | (v1 << 20)) & MASK44;
		v0 &= MASK44;
		a0 += v0;
		a1 += v1;
		a2 += v2 + ((uint64_t)1 << 40);
		MX(d0, c0, r0, u2, t1);
		MX(d1, c1, r1, r0, t2);
		MX(d2, c2, r2, r1, r0);
		a0 = c0 + 20 * d2;
		a1 = c1 + d0;
		a2 = c2 + d1;

		a1 += a0 >> 44;
		a0 &= MASK44;
		a2 += a1 >> 44;
		a1 &= MASK44;
		a0 += 20 * (a2 >> 44);
		a2 &= MASK44;

		buf += 64;
		len -= 64;
	}
	acc[0] = a0;
	acc[1] = a1;
	acc[2] = a2;

#undef MX
}

static void
poly1305_inner_small(uint64_t *acc, uint64_t *r, const void *data, size_t len)
{
	const unsigned char *buf;
	uint64_t a0, a1, a2;
	uint64_t r0, r1, r2, t1, t2, u2;

	r0 = r[0];
	r1 = r[1];
	r2 = r[2];
	t1 = r[3];
	t2 = r[4];
	u2 = r[5];
	a0 = acc[0];
	a1 = acc[1];
	a2 = acc[2];
	buf = data;

	while (len > 0) {
		uint64_t v0, v1, v2;
		uint64_t c0, c1, c2, d0, d1, d2;
		unsigned char tmp[16];

		if (len < 16) {
			memcpy(tmp, buf, len);
			memset(tmp + len, 0, (sizeof tmp) - len);
			buf = tmp;
			len = 16;
		}
		v0 = br_dec64le(buf + 0);
		v1 = br_dec64le(buf + 8);

		v2 = v1 >> 24;
		v1 = ((v0 >> 44) | (v1 << 20)) & MASK44;
		v0 &= MASK44;

		a0 += v0;
		a1 += v1;
		a2 += v2 + ((uint64_t)1 << 40);

#define MX(hi, lo, m0, m1, m2)   do { \
		uint64_t mxhi, mxlo; \
		MUL128(mxhi, mxlo, a0, m0); \
		(hi) = mxhi; \
		(lo) = mxlo >> 20; \
		MUL128(mxhi, mxlo, a1, m1); \
		(hi) += mxhi; \
		(lo) += mxlo >> 20; \
		MUL128(mxhi, mxlo, a2, m2); \
		(hi) += mxhi; \
		(lo) += mxlo >> 20; \
	} while (0)

		MX(d0, c0, r0, u2, t1);
		MX(d1, c1, r1, r0, t2);
		MX(d2, c2, r2, r1, r0);

#undef MX

		a0 = c0 + 20 * d2;
		a1 = c1 + d0;
		a2 = c2 + d1;

		a1 += a0 >> 44;
		a0 &= MASK44;
		a2 += a1 >> 44;
		a1 &= MASK44;
		a0 += 20 * (a2 >> 44);
		a2 &= MASK44;

		buf += 16;
		len -= 16;
	}
	acc[0] = a0;
	acc[1] = a1;
	acc[2] = a2;
}

static inline void
poly1305_inner(uint64_t *acc, uint64_t *r, const void *data, size_t len)
{
	if (len >= 64) {
		size_t len2;

		len2 = len & ~(size_t)63;
		poly1305_inner_big(acc, r, data, len2);
		data = (const unsigned char *)data + len2;
		len -= len2;
	}
	if (len > 0) {
		poly1305_inner_small(acc, r, data, len);
	}
}

/* see bearssl_block.h */
void
br_poly1305_ctmulq_run(const void *key, const void *iv,
	void *data, size_t len, const void *aad, size_t aad_len,
	void *tag, br_chacha20_run ichacha, int encrypt)
{
	unsigned char pkey[32], foot[16];
	uint64_t r[6], acc[3], r0, r1;
	uint32_t v0, v1, v2, v3, v4;
	uint64_t w0, w1, w2, w3;
	uint32_t ctl;

	/*
	 * Compute the MAC key. The 'r' value is the first 16 bytes of
	 * pkey[].
	 */
	memset(pkey, 0, sizeof pkey);
	ichacha(key, iv, 0, pkey, sizeof pkey);

	/*
	 * If encrypting, ChaCha20 must run first, followed by Poly1305.
	 * When decrypting, the operations are reversed.
	 */
	if (encrypt) {
		ichacha(key, iv, 1, data, len);
	}

	/*
	 * Run Poly1305. We must process the AAD, then ciphertext, then
	 * the footer (with the lengths). Note that the AAD and ciphertext
	 * are meant to be padded with zeros up to the next multiple of 16,
	 * and the length of the footer is 16 bytes as well.
	 */

	/*
	 * Apply the "clamping" on r.
	 */
	pkey[ 3] &= 0x0F;
	pkey[ 4] &= 0xFC;
	pkey[ 7] &= 0x0F;
	pkey[ 8] &= 0xFC;
	pkey[11] &= 0x0F;
	pkey[12] &= 0xFC;
	pkey[15] &= 0x0F;

	/*
	 * Decode the 'r' value into 44-bit words, left-shifted by 20 bits.
	 * Also compute the u1 and u2 values.
	 */
	r0 = br_dec64le(pkey +  0);
	r1 = br_dec64le(pkey +  8);
	r[0] = r0 << 20;
	r[1] = ((r0 >> 24) | (r1 << 40)) & ~(uint64_t)0xFFFFF;
	r[2] = (r1 >> 4) & ~(uint64_t)0xFFFFF;
	r1 = 20 * (r[1] >> 20);
	r[3] = r1 << 20;
	r[5] = 20 * r[2];
	r[4] = (r[5] + (r1 >> 24)) & ~(uint64_t)0xFFFFF;

	/*
	 * Accumulator is 0.
	 */
	acc[0] = 0;
	acc[1] = 0;
	acc[2] = 0;

	/*
	 * Process the additional authenticated data, ciphertext, and
	 * footer in due order.
	 */
	br_enc64le(foot, (uint64_t)aad_len);
	br_enc64le(foot + 8, (uint64_t)len);
	poly1305_inner(acc, r, aad, aad_len);
	poly1305_inner(acc, r, data, len);
	poly1305_inner_small(acc, r, foot, sizeof foot);

	/*
	 * Finalise modular reduction. At that point, the value consists
	 * in three 44-bit values (the lowest one might be slightly above
	 * 2^44). Two loops shall be sufficient.
	 */
	acc[1] += (acc[0] >> 44);
	acc[0] &= MASK44;
	acc[2] += (acc[1] >> 44);
	acc[1] &= MASK44;
	acc[0] += 5 * (acc[2] >> 42);
	acc[2] &= MASK42;
	acc[1] += (acc[0] >> 44);
	acc[0] &= MASK44;
	acc[2] += (acc[1] >> 44);
	acc[1] &= MASK44;
	acc[0] += 5 * (acc[2] >> 42);
	acc[2] &= MASK42;

	/*
	 * The value may still fall in the 2^130-5..2^130-1 range, in
	 * which case we must reduce it again. The code below selects,
	 * in constant-time, between 'acc' and 'acc-p'. We encode the
	 * value over four 32-bit integers to finish the operation.
	 */
	v0 = (uint32_t)acc[0];
	v1 = (uint32_t)(acc[0] >> 32) | ((uint32_t)acc[1] << 12);
	v2 = (uint32_t)(acc[1] >> 20) | ((uint32_t)acc[2] << 24);
	v3 = (uint32_t)(acc[2] >> 8);
	v4 = (uint32_t)(acc[2] >> 40);

	ctl = GT(v0, 0xFFFFFFFA);
	ctl &= EQ(v1, 0xFFFFFFFF);
	ctl &= EQ(v2, 0xFFFFFFFF);
	ctl &= EQ(v3, 0xFFFFFFFF);
	ctl &= EQ(v4, 0x00000003);
	v0 = MUX(ctl, v0 + 5, v0);
	v1 = MUX(ctl, 0, v1);
	v2 = MUX(ctl, 0, v2);
	v3 = MUX(ctl, 0, v3);

	/*
	 * Add the "s" value. This is done modulo 2^128. Don't forget
	 * carry propagation...
	 */
	w0 = (uint64_t)v0 + (uint64_t)br_dec32le(pkey + 16);
	w1 = (uint64_t)v1 + (uint64_t)br_dec32le(pkey + 20) + (w0 >> 32);
	w2 = (uint64_t)v2 + (uint64_t)br_dec32le(pkey + 24) + (w1 >> 32);
	w3 = (uint64_t)v3 + (uint64_t)br_dec32le(pkey + 28) + (w2 >> 32);
	v0 = (uint32_t)w0;
	v1 = (uint32_t)w1;
	v2 = (uint32_t)w2;
	v3 = (uint32_t)w3;

	/*
	 * Encode the tag.
	 */
	br_enc32le((unsigned char *)tag +  0, v0);
	br_enc32le((unsigned char *)tag +  4, v1);
	br_enc32le((unsigned char *)tag +  8, v2);
	br_enc32le((unsigned char *)tag + 12, v3);

	/*
	 * If decrypting, then ChaCha20 runs _after_ Poly1305.
	 */
	if (!encrypt) {
		ichacha(key, iv, 1, data, len);
	}
}

/* see bearssl_block.h */
br_poly1305_run
br_poly1305_ctmulq_get(void)
{
	return &br_poly1305_ctmulq_run;
}

#else

/* see bearssl_block.h */
br_poly1305_run
br_poly1305_ctmulq_get(void)
{
	return 0;
}

#endif
