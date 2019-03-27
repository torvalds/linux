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

static void
in_cbc_init(br_sslrec_in_cbc_context *cc,
	const br_block_cbcdec_class *bc_impl,
	const void *bc_key, size_t bc_key_len,
	const br_hash_class *dig_impl,
	const void *mac_key, size_t mac_key_len, size_t mac_out_len,
	const void *iv)
{
	cc->vtable = &br_sslrec_in_cbc_vtable;
	cc->seq = 0;
	bc_impl->init(&cc->bc.vtable, bc_key, bc_key_len);
	br_hmac_key_init(&cc->mac, dig_impl, mac_key, mac_key_len);
	cc->mac_len = mac_out_len;
	if (iv == NULL) {
		memset(cc->iv, 0, sizeof cc->iv);
		cc->explicit_IV = 1;
	} else {
		memcpy(cc->iv, iv, bc_impl->block_size);
		cc->explicit_IV = 0;
	}
}

static int
cbc_check_length(const br_sslrec_in_cbc_context *cc, size_t rlen)
{
	/*
	 * Plaintext size: at most 16384 bytes
	 * Padding: at most 256 bytes
	 * MAC: mac_len extra bytes
	 * TLS 1.1+: each record has an explicit IV
	 *
	 * Minimum length includes at least one byte of padding, and the
	 * MAC.
	 *
	 * Total length must be a multiple of the block size.
	 */
	size_t blen;
	size_t min_len, max_len;

	blen = cc->bc.vtable->block_size;
	min_len = (blen + cc->mac_len) & ~(blen - 1);
	max_len = (16384 + 256 + cc->mac_len) & ~(blen - 1);
	if (cc->explicit_IV) {
		min_len += blen;
		max_len += blen;
	}
	return min_len <= rlen && rlen <= max_len;
}

/*
 * Rotate array buf[] of length 'len' to the left (towards low indices)
 * by 'num' bytes if ctl is 1; otherwise, leave it unchanged. This is
 * constant-time. 'num' MUST be lower than 'len'. 'len' MUST be lower
 * than or equal to 64.
 */
static void
cond_rotate(uint32_t ctl, unsigned char *buf, size_t len, size_t num)
{
	unsigned char tmp[64];
	size_t u, v;

	for (u = 0, v = num; u < len; u ++) {
		tmp[u] = MUX(ctl, buf[v], buf[u]);
		if (++ v == len) {
			v = 0;
		}
	}
	memcpy(buf, tmp, len);
}

static unsigned char *
cbc_decrypt(br_sslrec_in_cbc_context *cc,
	int record_type, unsigned version, void *data, size_t *data_len)
{
	/*
	 * We represent all lengths on 32-bit integers, because:
	 * -- SSL record lengths always fit in 32 bits;
	 * -- our constant-time primitives operate on 32-bit integers.
	 */
	unsigned char *buf;
	uint32_t u, v, len, blen, min_len, max_len;
	uint32_t good, pad_len, rot_count, len_withmac, len_nomac;
	unsigned char tmp1[64], tmp2[64];
	int i;
	br_hmac_context hc;

	buf = data;
	len = *data_len;
	blen = cc->bc.vtable->block_size;

	/*
	 * Decrypt data, and skip the explicit IV (if applicable). Note
	 * that the total length is supposed to have been verified by
	 * the caller. If there is an explicit IV, then we actually
	 * "decrypt" it using the implicit IV (from previous record),
	 * which is useless but harmless.
	 */
	cc->bc.vtable->run(&cc->bc.vtable, cc->iv, data, len);
	if (cc->explicit_IV) {
		buf += blen;
		len -= blen;
	}

	/*
	 * Compute minimum and maximum length of plaintext + MAC. These
	 * lengths can be inferred from the outside: they are not secret.
	 */
	min_len = (cc->mac_len + 256 < len) ? len - 256 : cc->mac_len;
	max_len = len - 1;

	/*
	 * Use the last decrypted byte to compute the actual payload
	 * length. Take care not to underflow (we use unsigned types).
	 */
	pad_len = buf[max_len];
	good = LE(pad_len, (uint32_t)(max_len - min_len));
	len = MUX(good, (uint32_t)(max_len - pad_len), min_len);

	/*
	 * Check padding contents: all padding bytes must be equal to
	 * the value of pad_len.
	 */
	for (u = min_len; u < max_len; u ++) {
		good &= LT(u, len) | EQ(buf[u], pad_len);
	}

	/*
	 * Extract the MAC value. This is done in one pass, but results
	 * in a "rotated" MAC value depending on where it actually
	 * occurs. The 'rot_count' value is set to the offset of the
	 * first MAC byte within tmp1[].
	 *
	 * min_len and max_len are also adjusted to the minimum and
	 * maximum lengths of the plaintext alone (without the MAC).
	 */
	len_withmac = (uint32_t)len;
	len_nomac = len_withmac - cc->mac_len;
	min_len -= cc->mac_len;
	rot_count = 0;
	memset(tmp1, 0, cc->mac_len);
	v = 0;
	for (u = min_len; u < max_len; u ++) {
		tmp1[v] |= MUX(GE(u, len_nomac) & LT(u, len_withmac),
			buf[u], 0x00);
		rot_count = MUX(EQ(u, len_nomac), v, rot_count);
		if (++ v == cc->mac_len) {
			v = 0;
		}
	}
	max_len -= cc->mac_len;

	/*
	 * Rotate back the MAC value. The loop below does the constant-time
	 * rotation in time n*log n for a MAC output of length n. We assume
	 * that the MAC output length is no more than 64 bytes, so the
	 * rotation count fits on 6 bits.
	 */
	for (i = 5; i >= 0; i --) {
		uint32_t rc;

		rc = (uint32_t)1 << i;
		cond_rotate(rot_count >> i, tmp1, cc->mac_len, rc);
		rot_count &= ~rc;
	}

	/*
	 * Recompute the HMAC value. The input is the concatenation of
	 * the sequence number (8 bytes), the record header (5 bytes),
	 * and the payload.
	 *
	 * At that point, min_len is the minimum plaintext length, but
	 * max_len still includes the MAC length.
	 */
	br_enc64be(tmp2, cc->seq ++);
	tmp2[8] = (unsigned char)record_type;
	br_enc16be(tmp2 + 9, version);
	br_enc16be(tmp2 + 11, len_nomac);
	br_hmac_init(&hc, &cc->mac, cc->mac_len);
	br_hmac_update(&hc, tmp2, 13);
	br_hmac_outCT(&hc, buf, len_nomac, min_len, max_len, tmp2);

	/*
	 * Compare the extracted and recomputed MAC values.
	 */
	for (u = 0; u < cc->mac_len; u ++) {
		good &= EQ0(tmp1[u] ^ tmp2[u]);
	}

	/*
	 * Check that the plaintext length is valid. The previous
	 * check was on the encrypted length, but the padding may have
	 * turned shorter than expected.
	 *
	 * Once this final test is done, the critical "constant-time"
	 * section ends and we can make conditional jumps again.
	 */
	good &= LE(len_nomac, 16384);

	if (!good) {
		return 0;
	}
	*data_len = len_nomac;
	return buf;
}

/* see bearssl_ssl.h */
const br_sslrec_in_cbc_class br_sslrec_in_cbc_vtable = {
	{
		sizeof(br_sslrec_in_cbc_context),
		(int (*)(const br_sslrec_in_class *const *, size_t))
			&cbc_check_length,
		(unsigned char *(*)(const br_sslrec_in_class **,
			int, unsigned, void *, size_t *))
			&cbc_decrypt
	},
	(void (*)(const br_sslrec_in_cbc_class **,
		const br_block_cbcdec_class *, const void *, size_t,
		const br_hash_class *, const void *, size_t, size_t,
		const void *))
		&in_cbc_init
};

/*
 * For CBC output:
 *
 * -- With TLS 1.1+, there is an explicit IV. Generation method uses
 * HMAC, computed over the current sequence number, and the current MAC
 * key. The resulting value is truncated to the size of a block, and
 * added at the head of the plaintext; it will get encrypted along with
 * the data. This custom generation mechanism is "safe" under the
 * assumption that HMAC behaves like a random oracle; since the MAC for
 * a record is computed over the concatenation of the sequence number,
 * the record header and the plaintext, the HMAC-for-IV will not collide
 * with the normal HMAC.
 *
 * -- With TLS 1.0, for application data, we want to enforce a 1/n-1
 * split, as a countermeasure against chosen-plaintext attacks. We thus
 * need to leave some room in the buffer for that extra record.
 */

static void
out_cbc_init(br_sslrec_out_cbc_context *cc,
	const br_block_cbcenc_class *bc_impl,
	const void *bc_key, size_t bc_key_len,
	const br_hash_class *dig_impl,
	const void *mac_key, size_t mac_key_len, size_t mac_out_len,
	const void *iv)
{
	cc->vtable = &br_sslrec_out_cbc_vtable;
	cc->seq = 0;
	bc_impl->init(&cc->bc.vtable, bc_key, bc_key_len);
	br_hmac_key_init(&cc->mac, dig_impl, mac_key, mac_key_len);
	cc->mac_len = mac_out_len;
	if (iv == NULL) {
		memset(cc->iv, 0, sizeof cc->iv);
		cc->explicit_IV = 1;
	} else {
		memcpy(cc->iv, iv, bc_impl->block_size);
		cc->explicit_IV = 0;
	}
}

static void
cbc_max_plaintext(const br_sslrec_out_cbc_context *cc,
	size_t *start, size_t *end)
{
	size_t blen, len;

	blen = cc->bc.vtable->block_size;
	if (cc->explicit_IV) {
		*start += blen;
	} else {
		*start += 4 + ((cc->mac_len + blen + 1) & ~(blen - 1));
	}
	len = (*end - *start) & ~(blen - 1);
	len -= 1 + cc->mac_len;
	if (len > 16384) {
		len = 16384;
	}
	*end = *start + len;
}

static unsigned char *
cbc_encrypt(br_sslrec_out_cbc_context *cc,
	int record_type, unsigned version, void *data, size_t *data_len)
{
	unsigned char *buf, *rbuf;
	size_t len, blen, plen;
	unsigned char tmp[13];
	br_hmac_context hc;

	buf = data;
	len = *data_len;
	blen = cc->bc.vtable->block_size;

	/*
	 * If using TLS 1.0, with more than one byte of plaintext, and
	 * the record is application data, then we need to compute
	 * a "split". We do not perform the split on other record types
	 * because it turned out that some existing, deployed
	 * implementations of SSL/TLS do not tolerate the splitting of
	 * some message types (in particular the Finished message).
	 *
	 * If using TLS 1.1+, then there is an explicit IV. We produce
	 * that IV by adding an extra initial plaintext block, whose
	 * value is computed with HMAC over the record sequence number.
	 */
	if (cc->explicit_IV) {
		/*
		 * We use here the fact that all the HMAC variants we
		 * support can produce at least 16 bytes, while all the
		 * block ciphers we support have blocks of no more than
		 * 16 bytes. Thus, we can always truncate the HMAC output
		 * down to the block size.
		 */
		br_enc64be(tmp, cc->seq);
		br_hmac_init(&hc, &cc->mac, blen);
		br_hmac_update(&hc, tmp, 8);
		br_hmac_out(&hc, buf - blen);
		rbuf = buf - blen - 5;
	} else {
		if (len > 1 && record_type == BR_SSL_APPLICATION_DATA) {
			/*
			 * To do the split, we use a recursive invocation;
			 * since we only give one byte to the inner call,
			 * the recursion stops there.
			 *
			 * We need to compute the exact size of the extra
			 * record, so that the two resulting records end up
			 * being sequential in RAM.
			 *
			 * We use here the fact that cbc_max_plaintext()
			 * adjusted the start offset to leave room for the
			 * initial fragment.
			 */
			size_t xlen;

			rbuf = buf - 4
				- ((cc->mac_len + blen + 1) & ~(blen - 1));
			rbuf[0] = buf[0];
			xlen = 1;
			rbuf = cbc_encrypt(cc, record_type,
				version, rbuf, &xlen);
			buf ++;
			len --;
		} else {
			rbuf = buf - 5;
		}
	}

	/*
	 * Compute MAC.
	 */
	br_enc64be(tmp, cc->seq ++);
	tmp[8] = record_type;
	br_enc16be(tmp + 9, version);
	br_enc16be(tmp + 11, len);
	br_hmac_init(&hc, &cc->mac, cc->mac_len);
	br_hmac_update(&hc, tmp, 13);
	br_hmac_update(&hc, buf, len);
	br_hmac_out(&hc, buf + len);
	len += cc->mac_len;

	/*
	 * Add padding.
	 */
	plen = blen - (len & (blen - 1));
	memset(buf + len, (unsigned)plen - 1, plen);
	len += plen;

	/*
	 * If an explicit IV is used, the corresponding extra block was
	 * already put in place earlier; we just have to account for it
	 * here.
	 */
	if (cc->explicit_IV) {
		buf -= blen;
		len += blen;
	}

	/*
	 * Encrypt the whole thing. If there is an explicit IV, we also
	 * encrypt it, which is fine (encryption of a uniformly random
	 * block is still a uniformly random block).
	 */
	cc->bc.vtable->run(&cc->bc.vtable, cc->iv, buf, len);

	/*
	 * Add the header and return.
	 */
	buf[-5] = record_type;
	br_enc16be(buf - 4, version);
	br_enc16be(buf - 2, len);
	*data_len = (size_t)((buf + len) - rbuf);
	return rbuf;
}

/* see bearssl_ssl.h */
const br_sslrec_out_cbc_class br_sslrec_out_cbc_vtable = {
	{
		sizeof(br_sslrec_out_cbc_context),
		(void (*)(const br_sslrec_out_class *const *,
			size_t *, size_t *))
			&cbc_max_plaintext,
		(unsigned char *(*)(const br_sslrec_out_class **,
			int, unsigned, void *, size_t *))
			&cbc_encrypt
	},
	(void (*)(const br_sslrec_out_cbc_class **,
		const br_block_cbcenc_class *, const void *, size_t,
		const br_hash_class *, const void *, size_t, size_t,
		const void *))
		&out_cbc_init
};
