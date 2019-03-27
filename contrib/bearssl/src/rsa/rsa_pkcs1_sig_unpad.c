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

/* see bearssl_rsa.h */
uint32_t
br_rsa_pkcs1_sig_unpad(const unsigned char *sig, size_t sig_len,
	const unsigned char *hash_oid, size_t hash_len,
	unsigned char *hash_out)
{
	static const unsigned char pad1[] = {
		0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	};

	unsigned char pad2[43];
	size_t u, x2, x3, pad_len, zlen;

	if (sig_len < 11) {
		return 0;
	}

	/*
	 * Expected format:
	 *  00 01 FF ... FF 00 30 x1 30 x2 06 x3 OID [ 05 00 ] 04 x4 HASH
	 *
	 * with the following rules:
	 *
	 *  -- Total length is that of the modulus and the signature
	 *     (this was already verified by br_rsa_i31_public()).
	 *
	 *  -- There are at least eight bytes of value 0xFF.
	 *
	 *  -- x4 is equal to the hash length (hash_len).
	 *
	 *  -- x3 is equal to the encoded OID value length (so x3 is the
	 *     first byte of hash_oid[]).
	 *
	 *  -- If the "05 00" is present, then x2 == x3 + 4; otherwise,
	 *     x2 == x3 + 2.
	 *
	 *  -- x1 == x2 + x4 + 4.
	 *
	 * So the total length after the last "FF" is either x3 + x4 + 11
	 * (with the "05 00") or x3 + x4 + 9 (without the "05 00").
	 */

	/*
	 * Check the "00 01 FF .. FF 00" with at least eight 0xFF bytes.
	 * The comparison is valid because we made sure that the signature
	 * is at least 11 bytes long.
	 */
	if (memcmp(sig, pad1, sizeof pad1) != 0) {
		return 0;
	}
	for (u = sizeof pad1; u < sig_len; u ++) {
		if (sig[u] != 0xFF) {
			break;
		}
	}

	/*
	 * Remaining length is sig_len - u bytes (including the 00 just
	 * after the last FF). This must be equal to one of the two
	 * possible values (depending on whether the "05 00" sequence is
	 * present or not).
	 */
	if (hash_oid == NULL) {
		if (sig_len - u != hash_len + 1 || sig[u] != 0x00) {
			return 0;
		}
	} else {
		x3 = hash_oid[0];
		pad_len = x3 + 9;
		memset(pad2, 0, pad_len);
		zlen = sig_len - u - hash_len;
		if (zlen == pad_len) {
			x2 = x3 + 2;
		} else if (zlen == pad_len + 2) {
			x2 = x3 + 4;
			pad_len = zlen;
			pad2[pad_len - 4] = 0x05;
		} else {
			return 0;
		}
		pad2[1] = 0x30;
		pad2[2] = x2 + hash_len + 4;
		pad2[3] = 0x30;
		pad2[4] = x2;
		pad2[5] = 0x06;
		memcpy(pad2 + 6, hash_oid, x3 + 1);
		pad2[pad_len - 2] = 0x04;
		pad2[pad_len - 1] = hash_len;
		if (memcmp(pad2, sig + u, pad_len) != 0) {
			return 0;
		}
	}
	memcpy(hash_out, sig + sig_len - hash_len, hash_len);
	return 1;
}
