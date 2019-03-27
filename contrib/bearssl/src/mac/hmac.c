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

static inline size_t
block_size(const br_hash_class *dig)
{
	unsigned ls;
	
	ls = (unsigned)(dig->desc >> BR_HASHDESC_LBLEN_OFF)
		& BR_HASHDESC_LBLEN_MASK;
	return (size_t)1 << ls;
}

static void
process_key(const br_hash_class **hc, void *ks,
	const void *key, size_t key_len, unsigned bb)
{
	unsigned char tmp[256];
	size_t blen, u;

	blen = block_size(*hc);
	memcpy(tmp, key, key_len);
	for (u = 0; u < key_len; u ++) {
		tmp[u] ^= (unsigned char)bb;
	}
	memset(tmp + key_len, bb, blen - key_len);
	(*hc)->init(hc);
	(*hc)->update(hc, tmp, blen);
	(*hc)->state(hc, ks);
}

/* see bearssl.h */
void
br_hmac_key_init(br_hmac_key_context *kc,
	const br_hash_class *dig, const void *key, size_t key_len)
{
	br_hash_compat_context hc;
	unsigned char kbuf[64];

	kc->dig_vtable = dig;
	hc.vtable = dig;
	if (key_len > block_size(dig)) {
		dig->init(&hc.vtable);
		dig->update(&hc.vtable, key, key_len);
		dig->out(&hc.vtable, kbuf);
		key = kbuf;
		key_len = br_digest_size(dig);
	}
	process_key(&hc.vtable, kc->ksi, key, key_len, 0x36);
	process_key(&hc.vtable, kc->kso, key, key_len, 0x5C);
}

/* see bearssl.h */
void
br_hmac_init(br_hmac_context *ctx,
	const br_hmac_key_context *kc, size_t out_len)
{
	const br_hash_class *dig;
	size_t blen, hlen;

	dig = kc->dig_vtable;
	blen = block_size(dig);
	dig->init(&ctx->dig.vtable);
	dig->set_state(&ctx->dig.vtable, kc->ksi, (uint64_t)blen);
	memcpy(ctx->kso, kc->kso, sizeof kc->kso);
	hlen = br_digest_size(dig);
	if (out_len > 0 && out_len < hlen) {
		hlen = out_len;
	}
	ctx->out_len = hlen;
}

/* see bearssl.h */
void
br_hmac_update(br_hmac_context *ctx, const void *data, size_t len)
{
	ctx->dig.vtable->update(&ctx->dig.vtable, data, len);
}

/* see bearssl.h */
size_t
br_hmac_out(const br_hmac_context *ctx, void *out)
{
	const br_hash_class *dig;
	br_hash_compat_context hc;
	unsigned char tmp[64];
	size_t blen, hlen;

	dig = ctx->dig.vtable;
	dig->out(&ctx->dig.vtable, tmp);
	blen = block_size(dig);
	dig->init(&hc.vtable);
	dig->set_state(&hc.vtable, ctx->kso, (uint64_t)blen);
	hlen = br_digest_size(dig);
	dig->update(&hc.vtable, tmp, hlen);
	dig->out(&hc.vtable, tmp);
	memcpy(out, tmp, ctx->out_len);
	return ctx->out_len;
}
