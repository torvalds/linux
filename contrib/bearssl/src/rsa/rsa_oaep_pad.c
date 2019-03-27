/*
 * Copyright (c) 2018 Thomas Pornin <pornin@bolet.org>
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
 * Hash some data. This is put as a separate function so that stack
 * allocation of the hash function context is done only for the duration
 * of the hash.
 */
static void
hash_data(const br_hash_class *dig, void *dst, const void *src, size_t len)
{
	br_hash_compat_context hc;

	hc.vtable = dig;
	dig->init(&hc.vtable);
	dig->update(&hc.vtable, src, len);
	dig->out(&hc.vtable, dst);
}

/* see inner.h */
size_t
br_rsa_oaep_pad(const br_prng_class **rnd, const br_hash_class *dig,
	const void *label, size_t label_len,
	const br_rsa_public_key *pk,
	void *dst, size_t dst_max_len,
	const void *src, size_t src_len)
{
	size_t k, hlen;
	unsigned char *buf;

	hlen = br_digest_size(dig);

	/*
	 * Compute actual modulus length (in bytes).
	 */
	k = pk->nlen;
	while (k > 0 && pk->n[k - 1] == 0) {
		k --;
	}

	/*
	 * An error is reported if:
	 *  - the modulus is too short;
	 *  - the source message length is too long;
	 *  - the destination buffer is too short.
	 */
	if (k < ((hlen << 1) + 2)
		|| src_len > (k - (hlen << 1) - 2)
		|| dst_max_len < k)
	{
		return 0;
	}

	/*
	 * Apply padding. At this point, things cannot fail.
	 */
	buf = dst;

	/*
	 * Assemble: DB = lHash || PS || 0x01 || M
	 * We first place the source message M with memmove(), so that
	 * overlaps between source and destination buffers are supported.
	 */
	memmove(buf + k - src_len, src, src_len);
	hash_data(dig, buf + 1 + hlen, label, label_len);
	memset(buf + 1 + (hlen << 1), 0, k - src_len - (hlen << 1) - 2);
	buf[k - src_len - 1] = 0x01;

	/*
	 * Make the random seed.
	 */
	(*rnd)->generate(rnd, buf + 1, hlen);

	/*
	 * Mask DB with the mask generated from the seed.
	 */
	br_mgf1_xor(buf + 1 + hlen, k - hlen - 1, dig, buf + 1, hlen);

	/*
	 * Mask the seed with the mask generated from the masked DB.
	 */
	br_mgf1_xor(buf + 1, hlen, dig, buf + 1 + hlen, k - hlen - 1);

	/*
	 * Padding result: EM = 0x00 || maskedSeed || maskedDB.
	 */
	buf[0] = 0x00;
	return k;
}
