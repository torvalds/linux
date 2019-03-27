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

/* see bearssl.h */
void
br_md5sha1_init(br_md5sha1_context *cc)
{
	cc->vtable = &br_md5sha1_vtable;
	memcpy(cc->val_md5, br_md5_IV, sizeof cc->val_md5);
	memcpy(cc->val_sha1, br_sha1_IV, sizeof cc->val_sha1);
	cc->count = 0;
}

/* see bearssl.h */
void
br_md5sha1_update(br_md5sha1_context *cc, const void *data, size_t len)
{
	const unsigned char *buf;
	size_t ptr;

	buf = data;
	ptr = (size_t)cc->count & 63;
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
		cc->count += (uint64_t)clen;
		if (ptr == 64) {
			br_md5_round(cc->buf, cc->val_md5);
			br_sha1_round(cc->buf, cc->val_sha1);
			ptr = 0;
		}
	}
}

/* see bearssl.h */
void
br_md5sha1_out(const br_md5sha1_context *cc, void *dst)
{
	unsigned char buf[64];
	uint32_t val_md5[4];
	uint32_t val_sha1[5];
	size_t ptr;
	unsigned char *out;
	uint64_t count;

	count = cc->count;
	ptr = (size_t)count & 63;
	memcpy(buf, cc->buf, ptr);
	memcpy(val_md5, cc->val_md5, sizeof val_md5);
	memcpy(val_sha1, cc->val_sha1, sizeof val_sha1);
	buf[ptr ++] = 0x80;
	if (ptr > 56) {
		memset(buf + ptr, 0, 64 - ptr);
		br_md5_round(buf, val_md5);
		br_sha1_round(buf, val_sha1);
		memset(buf, 0, 56);
	} else {
		memset(buf + ptr, 0, 56 - ptr);
	}
	count <<= 3;
	br_enc64le(buf + 56, count);
	br_md5_round(buf, val_md5);
	br_enc64be(buf + 56, count);
	br_sha1_round(buf, val_sha1);
	out = dst;
	br_range_enc32le(out, val_md5, 4);
	br_range_enc32be(out + 16, val_sha1, 5);
}

/* see bearssl.h */
uint64_t
br_md5sha1_state(const br_md5sha1_context *cc, void *dst)
{
	unsigned char *out;

	out = dst;
	br_range_enc32le(out, cc->val_md5, 4);
	br_range_enc32be(out + 16, cc->val_sha1, 5);
	return cc->count;
}

/* see bearssl.h */
void
br_md5sha1_set_state(br_md5sha1_context *cc, const void *stb, uint64_t count)
{
	const unsigned char *buf;

	buf = stb;
	br_range_dec32le(cc->val_md5, 4, buf);
	br_range_dec32be(cc->val_sha1, 5, buf + 16);
	cc->count = count;
}

/* see bearssl.h */
const br_hash_class br_md5sha1_vtable = {
	sizeof(br_md5sha1_context),
	BR_HASHDESC_ID(br_md5sha1_ID)
		| BR_HASHDESC_OUT(36)
		| BR_HASHDESC_STATE(36)
		| BR_HASHDESC_LBLEN(6),
	(void (*)(const br_hash_class **))&br_md5sha1_init,
	(void (*)(const br_hash_class **, const void *, size_t))
		&br_md5sha1_update,
	(void (*)(const br_hash_class *const *, void *))
		&br_md5sha1_out,
	(uint64_t (*)(const br_hash_class *const *, void *))
		&br_md5sha1_state,
	(void (*)(const br_hash_class **, const void *, uint64_t))
		&br_md5sha1_set_state
};
