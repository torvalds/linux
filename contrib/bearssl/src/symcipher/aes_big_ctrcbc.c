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

/* see bearssl_block.h */
void
br_aes_big_ctrcbc_init(br_aes_big_ctrcbc_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_big_ctrcbc_vtable;
	ctx->num_rounds = br_aes_keysched(ctx->skey, key, len);
}

static void
xorbuf(void *dst, const void *src, size_t len)
{
	unsigned char *d;
	const unsigned char *s;

	d = dst;
	s = src;
	while (len -- > 0) {
		*d ++ ^= *s ++;
	}
}

/* see bearssl_block.h */
void
br_aes_big_ctrcbc_ctr(const br_aes_big_ctrcbc_keys *ctx,
	void *ctr, void *data, size_t len)
{
	unsigned char *buf, *bctr;
	uint32_t cc0, cc1, cc2, cc3;

	buf = data;
	bctr = ctr;
	cc3 = br_dec32be(bctr +  0);
	cc2 = br_dec32be(bctr +  4);
	cc1 = br_dec32be(bctr +  8);
	cc0 = br_dec32be(bctr + 12);
	while (len > 0) {
		unsigned char tmp[16];
		uint32_t carry;

		br_enc32be(tmp +  0, cc3);
		br_enc32be(tmp +  4, cc2);
		br_enc32be(tmp +  8, cc1);
		br_enc32be(tmp + 12, cc0);
		br_aes_big_encrypt(ctx->num_rounds, ctx->skey, tmp);
		xorbuf(buf, tmp, 16);
		buf += 16;
		len -= 16;
		cc0 ++;
		carry = (~(cc0 | -cc0)) >> 31;
		cc1 += carry;
		carry &= (~(cc1 | -cc1)) >> 31;
		cc2 += carry;
		carry &= (~(cc2 | -cc2)) >> 31;
		cc3 += carry;
	}
	br_enc32be(bctr +  0, cc3);
	br_enc32be(bctr +  4, cc2);
	br_enc32be(bctr +  8, cc1);
	br_enc32be(bctr + 12, cc0);
}

/* see bearssl_block.h */
void
br_aes_big_ctrcbc_mac(const br_aes_big_ctrcbc_keys *ctx,
	void *cbcmac, const void *data, size_t len)
{
	const unsigned char *buf;

	buf = data;
	while (len > 0) {
		xorbuf(cbcmac, buf, 16);
		br_aes_big_encrypt(ctx->num_rounds, ctx->skey, cbcmac);
		buf += 16;
		len -= 16;
	}
}

/* see bearssl_block.h */
void
br_aes_big_ctrcbc_encrypt(const br_aes_big_ctrcbc_keys *ctx,
	void *ctr, void *cbcmac, void *data, size_t len)
{
	br_aes_big_ctrcbc_ctr(ctx, ctr, data, len);
	br_aes_big_ctrcbc_mac(ctx, cbcmac, data, len);
}

/* see bearssl_block.h */
void
br_aes_big_ctrcbc_decrypt(const br_aes_big_ctrcbc_keys *ctx,
	void *ctr, void *cbcmac, void *data, size_t len)
{
	br_aes_big_ctrcbc_mac(ctx, cbcmac, data, len);
	br_aes_big_ctrcbc_ctr(ctx, ctr, data, len);
}

/* see bearssl_block.h */
const br_block_ctrcbc_class br_aes_big_ctrcbc_vtable = {
	sizeof(br_aes_big_ctrcbc_keys),
	16,
	4,
	(void (*)(const br_block_ctrcbc_class **, const void *, size_t))
		&br_aes_big_ctrcbc_init,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, void *, size_t))
		&br_aes_big_ctrcbc_encrypt,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, void *, size_t))
		&br_aes_big_ctrcbc_decrypt,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, size_t))
		&br_aes_big_ctrcbc_ctr,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, const void *, size_t))
		&br_aes_big_ctrcbc_mac
};
