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

/*
 * An aggregate context that is large enough for all supported hash
 * functions.
 */
typedef union {
	const br_hash_class *vtable;
	br_md5_context md5;
	br_sha1_context sha1;
	br_sha224_context sha224;
	br_sha256_context sha256;
	br_sha384_context sha384;
	br_sha512_context sha512;
} gen_hash_context;

/*
 * Get the offset to the state for a specific hash function within the
 * context structure. This shall be called only for the supported hash
 * functions,
 */
static size_t
get_state_offset(int id)
{
	if (id >= 5) {
		/*
		 * SHA-384 has id 5, and SHA-512 has id 6. Both use
		 * eight 64-bit words for their state.
		 */
		return offsetof(br_multihash_context, val_64)
			+ ((size_t)(id - 5) * (8 * sizeof(uint64_t)));
	} else {
		/*
		 * MD5 has id 1, SHA-1 has id 2, SHA-224 has id 3 and
		 * SHA-256 has id 4. They use 32-bit words for their
		 * states (4 words for MD5, 5 for SHA-1, 8 for SHA-224
		 * and 8 for SHA-256).
		 */
		unsigned x;

		x = id - 1;
		x = ((x + (x & (x >> 1))) << 2) + (x >> 1);
		return offsetof(br_multihash_context, val_32)
			+ x * sizeof(uint32_t);
	}
}

/* see bearssl_hash.h */
void
br_multihash_zero(br_multihash_context *ctx)
{
	/*
	 * This is not standard, but yields very short and efficient code,
	 * and it works "everywhere".
	 */
	memset(ctx, 0, sizeof *ctx);
}

/* see bearssl_hash.h */
void
br_multihash_init(br_multihash_context *ctx)
{
	int i;

	ctx->count = 0;
	for (i = 1; i <= 6; i ++) {
		const br_hash_class *hc;

		hc = ctx->impl[i - 1];
		if (hc != NULL) {
			gen_hash_context g;

			hc->init(&g.vtable);
			hc->state(&g.vtable,
				(unsigned char *)ctx + get_state_offset(i));
		}
	}
}

/* see bearssl_hash.h */
void
br_multihash_update(br_multihash_context *ctx, const void *data, size_t len)
{
	const unsigned char *buf;
	size_t ptr;

	buf = data;
	ptr = (size_t)ctx->count & 127;
	while (len > 0) {
		size_t clen;

		clen = 128 - ptr;
		if (clen > len) {
			clen = len;
		}
		memcpy(ctx->buf + ptr, buf, clen);
		ptr += clen;
		buf += clen;
		len -= clen;
		ctx->count += (uint64_t)clen;
		if (ptr == 128) {
			int i;

			for (i = 1; i <= 6; i ++) {
				const br_hash_class *hc;

				hc = ctx->impl[i - 1];
				if (hc != NULL) {
					gen_hash_context g;
					unsigned char *state;

					state = (unsigned char *)ctx
						+ get_state_offset(i);
					hc->set_state(&g.vtable,
						state, ctx->count - 128);
					hc->update(&g.vtable, ctx->buf, 128);
					hc->state(&g.vtable, state);
				}
			}
			ptr = 0;
		}
	}
}

/* see bearssl_hash.h */
size_t
br_multihash_out(const br_multihash_context *ctx, int id, void *dst)
{
	const br_hash_class *hc;
	gen_hash_context g;
	const unsigned char *state;

	hc = ctx->impl[id - 1];
	if (hc == NULL) {
		return 0;
	}
	state = (const unsigned char *)ctx + get_state_offset(id);
	hc->set_state(&g.vtable, state, ctx->count & ~(uint64_t)127);
	hc->update(&g.vtable, ctx->buf, ctx->count & (uint64_t)127);
	hc->out(&g.vtable, dst);
	return (hc->desc >> BR_HASHDESC_OUT_OFF) & BR_HASHDESC_OUT_MASK;
}
