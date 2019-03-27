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
 * GCM initialisation. This does everything except setting the vtable,
 * which depends on whether this is a context for encrypting or for
 * decrypting.
 */
static void
gen_gcm_init(br_sslrec_gcm_context *cc,
	const br_block_ctr_class *bc_impl,
	const void *key, size_t key_len,
	br_ghash gh_impl,
	const void *iv)
{
	unsigned char tmp[12];

	cc->seq = 0;
	bc_impl->init(&cc->bc.vtable, key, key_len);
	cc->gh = gh_impl;
	memcpy(cc->iv, iv, sizeof cc->iv);
	memset(cc->h, 0, sizeof cc->h);
	memset(tmp, 0, sizeof tmp);
	bc_impl->run(&cc->bc.vtable, tmp, 0, cc->h, sizeof cc->h);
}

static void
in_gcm_init(br_sslrec_gcm_context *cc,
	const br_block_ctr_class *bc_impl,
	const void *key, size_t key_len,
	br_ghash gh_impl,
	const void *iv)
{
	cc->vtable.in = &br_sslrec_in_gcm_vtable;
	gen_gcm_init(cc, bc_impl, key, key_len, gh_impl, iv);
}

static int
gcm_check_length(const br_sslrec_gcm_context *cc, size_t rlen)
{
	/*
	 * GCM adds a fixed overhead:
	 *   8 bytes for the nonce_explicit (before the ciphertext)
	 *  16 bytes for the authentication tag (after the ciphertext)
	 */
	(void)cc;
	return rlen >= 24 && rlen <= (16384 + 24);
}

/*
 * Compute the authentication tag. The value written in 'tag' must still
 * be CTR-encrypted.
 */
static void
do_tag(br_sslrec_gcm_context *cc,
	int record_type, unsigned version,
	void *data, size_t len, void *tag)
{
	unsigned char header[13];
	unsigned char footer[16];

	/*
	 * Compute authentication tag. Three elements must be injected in
	 * sequence, each possibly 0-padded to reach a length multiple
	 * of the block size: the 13-byte header (sequence number, record
	 * type, protocol version, record length), the cipher text, and
	 * the word containing the encodings of the bit lengths of the two
	 * other elements.
	 */
	br_enc64be(header, cc->seq ++);
	header[8] = (unsigned char)record_type;
	br_enc16be(header + 9, version);
	br_enc16be(header + 11, len);
	br_enc64be(footer, (uint64_t)(sizeof header) << 3);
	br_enc64be(footer + 8, (uint64_t)len << 3);
	memset(tag, 0, 16);
	cc->gh(tag, cc->h, header, sizeof header);
	cc->gh(tag, cc->h, data, len);
	cc->gh(tag, cc->h, footer, sizeof footer);
}

/*
 * Do CTR encryption. This also does CTR encryption of a single block at
 * address 'xortag' with the counter value appropriate for the final
 * processing of the authentication tag.
 */
static void
do_ctr(br_sslrec_gcm_context *cc, const void *nonce, void *data, size_t len,
	void *xortag)
{
	unsigned char iv[12];

	memcpy(iv, cc->iv, 4);
	memcpy(iv + 4, nonce, 8);
	cc->bc.vtable->run(&cc->bc.vtable, iv, 2, data, len);
	cc->bc.vtable->run(&cc->bc.vtable, iv, 1, xortag, 16);
}

static unsigned char *
gcm_decrypt(br_sslrec_gcm_context *cc,
	int record_type, unsigned version, void *data, size_t *data_len)
{
	unsigned char *buf;
	size_t len, u;
	uint32_t bad;
	unsigned char tag[16];

	buf = (unsigned char *)data + 8;
	len = *data_len - 24;
	do_tag(cc, record_type, version, buf, len, tag);
	do_ctr(cc, data, buf, len, tag);

	/*
	 * Compare the computed tag with the value from the record. It
	 * is possibly useless to do a constant-time comparison here,
	 * but it does not hurt.
	 */
	bad = 0;
	for (u = 0; u < 16; u ++) {
		bad |= tag[u] ^ buf[len + u];
	}
	if (bad) {
		return NULL;
	}
	*data_len = len;
	return buf;
}

/* see bearssl_ssl.h */
const br_sslrec_in_gcm_class br_sslrec_in_gcm_vtable = {
	{
		sizeof(br_sslrec_gcm_context),
		(int (*)(const br_sslrec_in_class *const *, size_t))
			&gcm_check_length,
		(unsigned char *(*)(const br_sslrec_in_class **,
			int, unsigned, void *, size_t *))
			&gcm_decrypt
	},
	(void (*)(const br_sslrec_in_gcm_class **,
		const br_block_ctr_class *, const void *, size_t,
		br_ghash, const void *))
		&in_gcm_init
};

static void
out_gcm_init(br_sslrec_gcm_context *cc,
	const br_block_ctr_class *bc_impl,
	const void *key, size_t key_len,
	br_ghash gh_impl,
	const void *iv)
{
	cc->vtable.out = &br_sslrec_out_gcm_vtable;
	gen_gcm_init(cc, bc_impl, key, key_len, gh_impl, iv);
}

static void
gcm_max_plaintext(const br_sslrec_gcm_context *cc,
	size_t *start, size_t *end)
{
	size_t len;

	(void)cc;
	*start += 8;
	len = *end - *start - 16;
	if (len > 16384) {
		len = 16384;
	}
	*end = *start + len;
}

static unsigned char *
gcm_encrypt(br_sslrec_gcm_context *cc,
	int record_type, unsigned version, void *data, size_t *data_len)
{
	unsigned char *buf;
	size_t u, len;
	unsigned char tmp[16];

	buf = (unsigned char *)data;
	len = *data_len;
	memset(tmp, 0, sizeof tmp);
	br_enc64be(buf - 8, cc->seq);
	do_ctr(cc, buf - 8, buf, len, tmp);
	do_tag(cc, record_type, version, buf, len, buf + len);
	for (u = 0; u < 16; u ++) {
		buf[len + u] ^= tmp[u];
	}
	len += 24;
	buf -= 13;
	buf[0] = (unsigned char)record_type;
	br_enc16be(buf + 1, version);
	br_enc16be(buf + 3, len);
	*data_len = len + 5;
	return buf;
}

/* see bearssl_ssl.h */
const br_sslrec_out_gcm_class br_sslrec_out_gcm_vtable = {
	{
		sizeof(br_sslrec_gcm_context),
		(void (*)(const br_sslrec_out_class *const *,
			size_t *, size_t *))
			&gcm_max_plaintext,
		(unsigned char *(*)(const br_sslrec_out_class **,
			int, unsigned, void *, size_t *))
			&gcm_encrypt
	},
	(void (*)(const br_sslrec_out_gcm_class **,
		const br_block_ctr_class *, const void *, size_t,
		br_ghash, const void *))
		&out_gcm_init
};
