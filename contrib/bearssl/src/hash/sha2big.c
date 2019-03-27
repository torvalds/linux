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

#define ROTR(x, n)    (((uint64_t)(x) << (64 - (n))) | ((uint64_t)(x) >> (n)))

#define BSG5_0(x)      (ROTR(x, 28) ^ ROTR(x, 34) ^ ROTR(x, 39))
#define BSG5_1(x)      (ROTR(x, 14) ^ ROTR(x, 18) ^ ROTR(x, 41))
#define SSG5_0(x)      (ROTR(x, 1) ^ ROTR(x, 8) ^ (uint64_t)((x) >> 7))
#define SSG5_1(x)      (ROTR(x, 19) ^ ROTR(x, 61) ^ (uint64_t)((x) >> 6))

static const uint64_t IV384[8] = {
	0xCBBB9D5DC1059ED8, 0x629A292A367CD507,
	0x9159015A3070DD17, 0x152FECD8F70E5939,
	0x67332667FFC00B31, 0x8EB44A8768581511,
	0xDB0C2E0D64F98FA7, 0x47B5481DBEFA4FA4
};

static const uint64_t IV512[8] = {
	0x6A09E667F3BCC908, 0xBB67AE8584CAA73B,
	0x3C6EF372FE94F82B, 0xA54FF53A5F1D36F1,
	0x510E527FADE682D1, 0x9B05688C2B3E6C1F,
	0x1F83D9ABFB41BD6B, 0x5BE0CD19137E2179
};

static const uint64_t K[80] = {
	0x428A2F98D728AE22, 0x7137449123EF65CD,
	0xB5C0FBCFEC4D3B2F, 0xE9B5DBA58189DBBC,
	0x3956C25BF348B538, 0x59F111F1B605D019,
	0x923F82A4AF194F9B, 0xAB1C5ED5DA6D8118,
	0xD807AA98A3030242, 0x12835B0145706FBE,
	0x243185BE4EE4B28C, 0x550C7DC3D5FFB4E2,
	0x72BE5D74F27B896F, 0x80DEB1FE3B1696B1,
	0x9BDC06A725C71235, 0xC19BF174CF692694,
	0xE49B69C19EF14AD2, 0xEFBE4786384F25E3,
	0x0FC19DC68B8CD5B5, 0x240CA1CC77AC9C65,
	0x2DE92C6F592B0275, 0x4A7484AA6EA6E483,
	0x5CB0A9DCBD41FBD4, 0x76F988DA831153B5,
	0x983E5152EE66DFAB, 0xA831C66D2DB43210,
	0xB00327C898FB213F, 0xBF597FC7BEEF0EE4,
	0xC6E00BF33DA88FC2, 0xD5A79147930AA725,
	0x06CA6351E003826F, 0x142929670A0E6E70,
	0x27B70A8546D22FFC, 0x2E1B21385C26C926,
	0x4D2C6DFC5AC42AED, 0x53380D139D95B3DF,
	0x650A73548BAF63DE, 0x766A0ABB3C77B2A8,
	0x81C2C92E47EDAEE6, 0x92722C851482353B,
	0xA2BFE8A14CF10364, 0xA81A664BBC423001,
	0xC24B8B70D0F89791, 0xC76C51A30654BE30,
	0xD192E819D6EF5218, 0xD69906245565A910,
	0xF40E35855771202A, 0x106AA07032BBD1B8,
	0x19A4C116B8D2D0C8, 0x1E376C085141AB53,
	0x2748774CDF8EEB99, 0x34B0BCB5E19B48A8,
	0x391C0CB3C5C95A63, 0x4ED8AA4AE3418ACB,
	0x5B9CCA4F7763E373, 0x682E6FF3D6B2B8A3,
	0x748F82EE5DEFB2FC, 0x78A5636F43172F60,
	0x84C87814A1F0AB72, 0x8CC702081A6439EC,
	0x90BEFFFA23631E28, 0xA4506CEBDE82BDE9,
	0xBEF9A3F7B2C67915, 0xC67178F2E372532B,
	0xCA273ECEEA26619C, 0xD186B8C721C0C207,
	0xEADA7DD6CDE0EB1E, 0xF57D4F7FEE6ED178,
	0x06F067AA72176FBA, 0x0A637DC5A2C898A6,
	0x113F9804BEF90DAE, 0x1B710B35131C471B,
	0x28DB77F523047D84, 0x32CAAB7B40C72493,
	0x3C9EBE0A15C9BEBC, 0x431D67C49C100D4C,
	0x4CC5D4BECB3E42B6, 0x597F299CFC657E2A,
	0x5FCB6FAB3AD6FAEC, 0x6C44198C4A475817
};

static void
sha2big_round(const unsigned char *buf, uint64_t *val)
{

#define SHA2BIG_STEP(A, B, C, D, E, F, G, H, j)   do { \
		uint64_t T1, T2; \
		T1 = H + BSG5_1(E) + CH(E, F, G) + K[j] + w[j]; \
		T2 = BSG5_0(A) + MAJ(A, B, C); \
		D += T1; \
		H = T1 + T2; \
	} while (0)

	int i;
	uint64_t a, b, c, d, e, f, g, h;
	uint64_t w[80];

	br_range_dec64be(w, 16, buf);
	for (i = 16; i < 80; i ++) {
		w[i] = SSG5_1(w[i - 2]) + w[i - 7]
			+ SSG5_0(w[i - 15]) + w[i - 16];
	}
	a = val[0];
	b = val[1];
	c = val[2];
	d = val[3];
	e = val[4];
	f = val[5];
	g = val[6];
	h = val[7];
	for (i = 0; i < 80; i += 8) {
		SHA2BIG_STEP(a, b, c, d, e, f, g, h, i + 0);
		SHA2BIG_STEP(h, a, b, c, d, e, f, g, i + 1);
		SHA2BIG_STEP(g, h, a, b, c, d, e, f, i + 2);
		SHA2BIG_STEP(f, g, h, a, b, c, d, e, i + 3);
		SHA2BIG_STEP(e, f, g, h, a, b, c, d, i + 4);
		SHA2BIG_STEP(d, e, f, g, h, a, b, c, i + 5);
		SHA2BIG_STEP(c, d, e, f, g, h, a, b, i + 6);
		SHA2BIG_STEP(b, c, d, e, f, g, h, a, i + 7);
	}
	val[0] += a;
	val[1] += b;
	val[2] += c;
	val[3] += d;
	val[4] += e;
	val[5] += f;
	val[6] += g;
	val[7] += h;
}

static void
sha2big_update(br_sha384_context *cc, const void *data, size_t len)
{
	const unsigned char *buf;
	size_t ptr;

	buf = data;
	ptr = (size_t)cc->count & 127;
	cc->count += (uint64_t)len;
	while (len > 0) {
		size_t clen;

		clen = 128 - ptr;
		if (clen > len) {
			clen = len;
		}
		memcpy(cc->buf + ptr, buf, clen);
		ptr += clen;
		buf += clen;
		len -= clen;
		if (ptr == 128) {
			sha2big_round(cc->buf, cc->val);
			ptr = 0;
		}
	}
}

static void
sha2big_out(const br_sha384_context *cc, void *dst, int num)
{
	unsigned char buf[128];
	uint64_t val[8];
	size_t ptr;

	ptr = (size_t)cc->count & 127;
	memcpy(buf, cc->buf, ptr);
	memcpy(val, cc->val, sizeof val);
	buf[ptr ++] = 0x80;
	if (ptr > 112) {
		memset(buf + ptr, 0, 128 - ptr);
		sha2big_round(buf, val);
		memset(buf, 0, 112);
	} else {
		memset(buf + ptr, 0, 112 - ptr);
	}
	br_enc64be(buf + 112, cc->count >> 61);
	br_enc64be(buf + 120, cc->count << 3);
	sha2big_round(buf, val);
	br_range_enc64be(dst, val, num);
}

/* see bearssl.h */
void
br_sha384_init(br_sha384_context *cc)
{
	cc->vtable = &br_sha384_vtable;
	memcpy(cc->val, IV384, sizeof IV384);
	cc->count = 0;
}

/* see bearssl.h */
void
br_sha384_update(br_sha384_context *cc, const void *data, size_t len)
{
	sha2big_update(cc, data, len);
}

/* see bearssl.h */
void
br_sha384_out(const br_sha384_context *cc, void *dst)
{
	sha2big_out(cc, dst, 6);
}

/* see bearssl.h */
uint64_t
br_sha384_state(const br_sha384_context *cc, void *dst)
{
	br_range_enc64be(dst, cc->val, 8);
	return cc->count;
}

/* see bearssl.h */
void
br_sha384_set_state(br_sha384_context *cc, const void *stb, uint64_t count)
{
	br_range_dec64be(cc->val, 8, stb);
	cc->count = count;
}

/* see bearssl.h */
void
br_sha512_init(br_sha512_context *cc)
{
	cc->vtable = &br_sha512_vtable;
	memcpy(cc->val, IV512, sizeof IV512);
	cc->count = 0;
}

/* see bearssl.h */
void
br_sha512_out(const br_sha512_context *cc, void *dst)
{
	sha2big_out(cc, dst, 8);
}

/* see bearssl.h */
const br_hash_class br_sha384_vtable = {
	sizeof(br_sha384_context),
	BR_HASHDESC_ID(br_sha384_ID)
		| BR_HASHDESC_OUT(48)
		| BR_HASHDESC_STATE(64)
		| BR_HASHDESC_LBLEN(7)
		| BR_HASHDESC_MD_PADDING
		| BR_HASHDESC_MD_PADDING_BE
		| BR_HASHDESC_MD_PADDING_128,
	(void (*)(const br_hash_class **))&br_sha384_init,
	(void (*)(const br_hash_class **, const void *, size_t))
		&br_sha384_update,
	(void (*)(const br_hash_class *const *, void *))&br_sha384_out,
	(uint64_t (*)(const br_hash_class *const *, void *))&br_sha384_state,
	(void (*)(const br_hash_class **, const void *, uint64_t))
		&br_sha384_set_state
};

/* see bearssl.h */
const br_hash_class br_sha512_vtable = {
	sizeof(br_sha512_context),
	BR_HASHDESC_ID(br_sha512_ID)
		| BR_HASHDESC_OUT(64)
		| BR_HASHDESC_STATE(64)
		| BR_HASHDESC_LBLEN(7)
		| BR_HASHDESC_MD_PADDING
		| BR_HASHDESC_MD_PADDING_BE
		| BR_HASHDESC_MD_PADDING_128,
	(void (*)(const br_hash_class **))&br_sha512_init,
	(void (*)(const br_hash_class **, const void *, size_t))
		&br_sha512_update,
	(void (*)(const br_hash_class *const *, void *))&br_sha512_out,
	(uint64_t (*)(const br_hash_class *const *, void *))&br_sha512_state,
	(void (*)(const br_hash_class **, const void *, uint64_t))
		&br_sha512_set_state
};
