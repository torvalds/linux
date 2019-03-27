/*
 * HMAC-SHA512 KDF (RFC 5295) and HKDF-Expand(SHA512) (RFC 5869)
 * Copyright (c) 2014-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "sha512.h"


/**
 * hmac_sha512_kdf - HMAC-SHA512 based KDF (RFC 5295)
 * @secret: Key for KDF
 * @secret_len: Length of the key in bytes
 * @label: A unique label for each purpose of the KDF or %NULL to select
 *	RFC 5869 HKDF-Expand() with arbitrary seed (= info)
 * @seed: Seed value to bind into the key
 * @seed_len: Length of the seed
 * @out: Buffer for the generated pseudo-random key
 * @outlen: Number of bytes of key to generate
 * Returns: 0 on success, -1 on failure.
 *
 * This function is used to derive new, cryptographically separate keys from a
 * given key in ERP. This KDF is defined in RFC 5295, Chapter 3.1.2. When used
 * with label = NULL and seed = info, this matches HKDF-Expand() defined in
 * RFC 5869, Chapter 2.3.
 */
int hmac_sha512_kdf(const u8 *secret, size_t secret_len,
		    const char *label, const u8 *seed, size_t seed_len,
		    u8 *out, size_t outlen)
{
	u8 T[SHA512_MAC_LEN];
	u8 iter = 1;
	const unsigned char *addr[4];
	size_t len[4];
	size_t pos, clen;

	addr[0] = T;
	len[0] = SHA512_MAC_LEN;
	if (label) {
		addr[1] = (const unsigned char *) label;
		len[1] = os_strlen(label) + 1;
	} else {
		addr[1] = (const u8 *) "";
		len[1] = 0;
	}
	addr[2] = seed;
	len[2] = seed_len;
	addr[3] = &iter;
	len[3] = 1;

	if (hmac_sha512_vector(secret, secret_len, 3, &addr[1], &len[1], T) < 0)
		return -1;

	pos = 0;
	for (;;) {
		clen = outlen - pos;
		if (clen > SHA512_MAC_LEN)
			clen = SHA512_MAC_LEN;
		os_memcpy(out + pos, T, clen);
		pos += clen;

		if (pos == outlen)
			break;

		if (iter == 255) {
			os_memset(out, 0, outlen);
			os_memset(T, 0, SHA512_MAC_LEN);
			return -1;
		}
		iter++;

		if (hmac_sha512_vector(secret, secret_len, 4, addr, len, T) < 0)
		{
			os_memset(out, 0, outlen);
			os_memset(T, 0, SHA512_MAC_LEN);
			return -1;
		}
	}

	os_memset(T, 0, SHA512_MAC_LEN);
	return 0;
}
