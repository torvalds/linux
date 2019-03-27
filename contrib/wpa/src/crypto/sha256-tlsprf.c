/*
 * TLS PRF P_SHA256
 * Copyright (c) 2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "sha256.h"


/**
 * tls_prf_sha256 - Pseudo-Random Function for TLS v1.2 (P_SHA256, RFC 5246)
 * @secret: Key for PRF
 * @secret_len: Length of the key in bytes
 * @label: A unique label for each purpose of the PRF
 * @seed: Seed value to bind into the key
 * @seed_len: Length of the seed
 * @out: Buffer for the generated pseudo-random key
 * @outlen: Number of bytes of key to generate
 * Returns: 0 on success, -1 on failure.
 *
 * This function is used to derive new, cryptographically separate keys from a
 * given key in TLS. This PRF is defined in RFC 2246, Chapter 5.
 */
void tls_prf_sha256(const u8 *secret, size_t secret_len, const char *label,
		    const u8 *seed, size_t seed_len, u8 *out, size_t outlen)
{
	size_t clen;
	u8 A[SHA256_MAC_LEN];
	u8 P[SHA256_MAC_LEN];
	size_t pos;
	const unsigned char *addr[3];
	size_t len[3];

	addr[0] = A;
	len[0] = SHA256_MAC_LEN;
	addr[1] = (unsigned char *) label;
	len[1] = os_strlen(label);
	addr[2] = seed;
	len[2] = seed_len;

	/*
	 * RFC 5246, Chapter 5
	 * A(0) = seed, A(i) = HMAC(secret, A(i-1))
	 * P_hash = HMAC(secret, A(1) + seed) + HMAC(secret, A(2) + seed) + ..
	 * PRF(secret, label, seed) = P_SHA256(secret, label + seed)
	 */

	hmac_sha256_vector(secret, secret_len, 2, &addr[1], &len[1], A);

	pos = 0;
	while (pos < outlen) {
		hmac_sha256_vector(secret, secret_len, 3, addr, len, P);
		hmac_sha256(secret, secret_len, A, SHA256_MAC_LEN, A);

		clen = outlen - pos;
		if (clen > SHA256_MAC_LEN)
			clen = SHA256_MAC_LEN;
		os_memcpy(out + pos, P, clen);
		pos += clen;
	}
}
