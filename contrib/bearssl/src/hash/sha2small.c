/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
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

#define CH(X, Y, Z)    ((((Y) ^ (Z)) & (X)) ^ (Z))
#define MAJ(X, Y, Z)   (((Y) & (Z)) | (((Y) | (Z)) & (X)))

#define ROTR(x, n)    (((uint32_t)(x) << (32 - (n))) | ((uint32_t)(x) >> (n)))

#define BSG2_0(x)      (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define BSG2_1(x)      (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SSG2_0(x)      (ROTR(x, 7) ^ ROTR(x, 18) ^ (uint32_t)((x) >> 3))
#define SSG2_1(x)      (ROTR(x, 17) ^ ROTR(x, 19) ^ (uint32_t)((x) >> 10))

/* see inner.h */
const uint32_t br_sha224_IV[8] = {
	0xC1059ED8, 0x367CD507, 0x3070DD17, 0xF70E5939,
	0xFFC00B31, 0x68581511, 0x64F98FA7, 0xBEFA4FA4
};

/* see inner.h */
const uint32_t br_sha256_IV[8] = {
	0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
	0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

static const uint32_t K[64] = {
	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
	0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
	0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
	0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
	0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
	0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
	0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
	0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
	0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

/* see inner.h */
void
br_sha2small_round(const unsigned char *buf, uint32_t *val)
{

#define SHA2_STEP(A, B, C, D, E, F, G, H, j)   do { \
		uint32_t T1, T2; \
		T1 = H + BSG2_1(E) + CH(E, F, G) + K[j] + w[j]; \
		T2 = BSG2_0(A) + MAJ(A, B, C); \
		D += T1; \
		H = T1 + T2; \
	} while (0)

	int i;
	uint32_t a, b, c, d, e, f, g, h;
	uint32_t w[64];

	br_range_dec32be(w, 16, buf);
	for (i = 16; i < 64; i ++) {
		w[i] = SSG2_1(w[i - 2]) + w[i - 7]
			+ SSG2_0(w[i - 15]) + w[i - 16];
	}
	a = val[0];
	b = val[1];
	c = val[2];
	d = val[3];
	e = val[4];
	f = val[5];
	g = val[6];
	h = val[7];
	for (i = 0; i < 64; i += 8) {
		SHA2_STEP(a, b, c, d, e, f, g, h, i + 0);
		SHA2_STEP(h, a, b, c, d, e, f, g, i + 1);
		SHA2_STEP(g, h, a, b, c, d, e, f, i + 2);
		SHA2_STEP(f, g, h, a, b, c, d, e, i + 3);
		SHA2_STEP(e, f, g, h, a, b, c, d, i + 4);
		SHA2_STEP(d, e, f, g, h, a, b, c, i + 5);
		SHA2_STEP(c, d, e, f, g, h, a, b, i + 6);
		SHA2_STEP(b, c, d, e, f, g, h, a, i + 7);
	}
	val[0] += a;
	val[1] += b;
	val[2] += c;
	val[3] += d;
	val[4] += e;
	val[5] += f;
	val[6] += g;
	val[7] += h;

#if 0
/* obsolete */
#define SHA2_MEXP1(pc)   do { \
		W[pc] = br_dec32be(buf + ((pc) << 2)); \
	} while (0)

#define SHA2_MEXP2(pc)   do { \
		W[(pc) & 0x0F] = SSG2_1(W[((pc) - 2) & 0x0F]) \
			+ W[((pc) - 7) & 0x0F] \
			+ SSG2_0(W[((pc) - 15) & 0x0F]) + W[(pc) & 0x0F]; \
	} while (0)

#define SHA2_STEPn(n, a, b, c, d, e, f, g, h, pc)   do { \
		uint32_t t1, t2; \
		SHA2_MEXP ## n(pc); \
		t1 = h + BSG2_1(e) + CH(e, f, g) \
			+ K[pcount + (pc)] + W[(pc) & 0x0F]; \
		t2 = BSG2_0(a) + MAJ(a, b, c); \
		d += t1; \
		h = t1 + t2; \
	} while (0)

#define SHA2_STEP1(a, b, c, d, e, f, g, h, pc) \
	SHA2_STEPn(1, a, b, c, d, e, f, g, h, pc)
#define SHA2_STEP2(a, b, c, d, e, f, g, h, pc) \
	SHA2_STEPn(2, a, b, c, d, e, f, g, h, pc)

	uint32_t A, B, C, D, E, F, G, H;
	uint32_t W[16];
	unsigned pcount;

	A = val[0];
	B = val[1];
	C = val[2];
	D = val[3];
	E = val[4];
	F = val[5];
	G = val[6];
	H = val[7];
	pcount = 0;
	SHA2_STEP1(A, B, C, D, E, F, G, H,  0);
	SHA2_STEP1(H, A, B, C, D, E, F, G,  1);
	SHA2_STEP1(G, H, A, B, C, D, E, F,  2);
	SHA2_STEP1(F, G, H, A, B, C, D, E,  3);
	SHA2_STEP1(E, F, G, H, A, B, C, D,  4);
	SHA2_STEP1(D, E, F, G, H, A, B, C,  5);
	SHA2_STEP1(C, D, E, F, G, H, A, B,  6);
	SHA2_STEP1(B, C, D, E, F, G, H, A,  7);
	SHA2_STEP1(A, B, C, D, E, F, G, H,  8);
	SHA2_STEP1(H, A, B, C, D, E, F, G,  9);
	SHA2_STEP1(G, H, A, B, C, D, E, F, 10);
	SHA2_STEP1(F, G, H, A, B, C, D, E, 11);
	SHA2_STEP1(E, F, G, H, A, B, C, D, 12);
	SHA2_STEP1(D, E, F, G, H, A, B, C, 13);
	SHA2_STEP1(C, D, E, F, G, H, A, B, 14);
	SHA2_STEP1(B, C, D, E, F, G, H, A, 15);
	for (pcount = 16; pcount < 64; pcount += 16) {
		SHA2_STEP2(A, B, C, D, E, F, G, H,  0);
		SHA2_STEP2(H, A, B, C, D, E, F, G,  1);
		SHA2_STEP2(G, H, A, B, C, D, E, F,  2);
		SHA2_STEP2(F, G, H, A, B, C, D, E,  3);
		SHA2_STEP2(E, F, G, H, A, B, C, D,  4);
		SHA2_STEP2(D, E, F, G, H, A, B, C,  5);
		SHA2_STEP2(C, D, E, F, G, H, A, B,  6);
		SHA2_STEP2(B, C, D, E, F, G, H, A,  7);
		SHA2_STEP2(A, B, C, D, E, F, G, H,  8);
		SHA2_STEP2(H, A, B, C, D, E, F, G,  9);
		SHA2_STEP2(G, H, A, B, C, D, E, F, 10);
		SHA2_STEP2(F, G, H, A, B, C, D, E, 11);
		SHA2_STEP2(E, F, G, H, A, B, C, D, 12);
		SHA2_STEP2(D, E, F, G, H, A, B, C, 13);
		SHA2_STEP2(C, D, E, F, G, H, A, B, 14);
		SHA2_STEP2(B, C, D, E, F, G, H, A, 15);
	}
	val[0] += A;
	val[1] += B;
	val[2] += C;
	val[3] += D;
	val[4] += E;
	val[5] += F;
	val[6] += G;
	val[7] += H;
#endif
}

static void
sha2small_update(br_sha224_context *cc, const void *data, size_t len)
{
	const unsigned char *buf;
	size_t ptr;

	buf = data;
	ptr = (size_t)cc->count & 63;
	cc->count += (uint64_t)len;
	while (len > 0) {
		size_t clen;

		clen = 64 - ptr;
		if (clen > len) {
			clen = len;
		}
		memcpy(cc->buf + ptr, buf, clen);
		ptr += clen;
		buf += clen;
		len -= clen;
		if (ptr == 64) {
			br_sha2small_round(cc->buf, cc->val);
			ptr = 0;
		}
	}
}

static void
sha2small_out(const br_sha224_context *cc, void *dst, int num)
{
	unsigned char buf[64];
	uint32_t val[8];
	size_t ptr;

	ptr = (size_t)cc->count & 63;
	memcpy(buf, cc->buf, ptr);
	memcpy(val, cc->val, sizeof val);
	buf[ptr ++] = 0x80;
	if (ptr > 56) {
		memset(buf + ptr, 0, 64 - ptr);
		br_sha2small_round(buf, val);
		memset(buf, 0, 56);
	} else {
		memset(buf + ptr, 0, 56 - ptr);
	}
	br_enc64be(buf + 56, cc->count << 3);
	br_sha2small_round(buf, val);
	br_range_enc32be(dst, val, num);
}

/* see bearssl.h */
void
br_sha224_init(br_sha224_context *cc)
{
	cc->vtable = &br_sha224_vtable;
	memcpy(cc->val, br_sha224_IV, sizeof cc->val);
	cc->count = 0;
}

/* see bearssl.h */
void
br_sha224_update(br_sha224_context *cc, const void *data, size_t len)
{
	sha2small_update(cc, data, len);
}

/* see bearssl.h */
void
br_sha224_out(const br_sha224_context *cc, void *dst)
{
	sha2small_out(cc, dst, 7);
}

/* see bearssl.h */
uint64_t
br_sha224_state(const br_sha224_context *cc, void *dst)
{
	br_range_enc32be(dst, cc->val, 8);
	return cc->count;
}

/* see bearssl.h */
void
br_sha224_set_state(br_sha224_context *cc, const void *stb, uint64_t count)
{
	br_range_dec32be(cc->val, 8, stb);
	cc->count = count;
}

/* see bearssl.h */
void
br_sha256_init(br_sha256_context *cc)
{
	cc->vtable = &br_sha256_vtable;
	memcpy(cc->val, br_sha256_IV, sizeof cc->val);
	cc->count = 0;
}

/* see bearssl.h */
void
br_sha256_out(const br_sha256_context *cc, void *dst)
{
	sha2small_out(cc, dst, 8);
}

/* see bearssl.h */
const br_hash_class br_sha224_vtable = {
	sizeof(br_sha224_context),
	BR_HASHDESC_ID(br_sha224_ID)
		| BR_HASHDESC_OUT(28)
		| BR_HASHDESC_STATE(32)
		| BR_HASHDESC_LBLEN(6)
		| BR_HASHDESC_MD_PADDING
		| BR_HASHDESC_MD_PADDING_BE,
	(void (*)(const br_hash_class **))&br_sha224_init,
	(void (*)(const br_hash_class **,
		const void *, size_t))&br_sha224_update,
	(void (*)(const br_hash_class *const *, void *))&br_sha224_out,
	(uint64_t (*)(const br_hash_class *const *, void *))&br_sha224_state,
	(void (*)(const br_hash_class **, const void *, uint64_t))
		&br_sha224_set_state
};

/* see bearssl.h */
const br_hash_class br_sha256_vtable = {
	sizeof(br_sha256_context),
	BR_HASHDESC_ID(br_sha256_ID)
		| BR_HASHDESC_OUT(32)
		| BR_HASHDESC_STATE(32)
		| BR_HASHDESC_LBLEN(6)
		| BR_HASHDESC_MD_PADDING
		| BR_HASHDESC_MD_PADDING_BE,
	(void (*)(const br_hash_class **))&br_sha256_init,
	(void (*)(const br_hash_class **,
		const void *, size_t))&br_sha256_update,
	(void (*)(const br_hash_class *const *, void *))&br_sha256_out,
	(uint64_t (*)(const br_hash_class *const *, void *))&br_sha256_state,
	(void (*)(const br_hash_class **, const void *, uint64_t))
		&br_sha256_set_state
};
