/*
 * SHA-384 hash implementation and interface functions
 * Copyright (c) 2003-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "sha384.h"
#include "crypto.h"


/**
 * hmac_sha384_vector - HMAC-SHA384 over data vector (RFC 2104)
 * @key: Key for HMAC operations
 * @key_len: Length of the key in bytes
 * @num_elem: Number of elements in the data vector
 * @addr: Pointers to the data areas
 * @len: Lengths of the data blocks
 * @mac: Buffer for the hash (48 bytes)
 * Returns: 0 on success, -1 on failure
 */
int hmac_sha384_vector(const u8 *key, size_t key_len, size_t num_elem,
		       const u8 *addr[], const size_t *len, u8 *mac)
{
	unsigned char k_pad[128]; /* padding - key XORd with ipad/opad */
	unsigned char tk[48];
	const u8 *_addr[6];
	size_t _len[6], i;

	if (num_elem > 5) {
		/*
		 * Fixed limit on the number of fragments to avoid having to
		 * allocate memory (which could fail).
		 */
		return -1;
	}

	/* if key is longer than 128 bytes reset it to key = SHA384(key) */
	if (key_len > 128) {
		if (sha384_vector(1, &key, &key_len, tk) < 0)
			return -1;
		key = tk;
		key_len = 48;
	}

	/* the HMAC_SHA384 transform looks like:
	 *
	 * SHA384(K XOR opad, SHA384(K XOR ipad, text))
	 *
	 * where K is an n byte key
	 * ipad is the byte 0x36 repeated 128 times
	 * opad is the byte 0x5c repeated 128 times
	 * and text is the data being protected */

	/* start out by storing key in ipad */
	os_memset(k_pad, 0, sizeof(k_pad));
	os_memcpy(k_pad, key, key_len);
	/* XOR key with ipad values */
	for (i = 0; i < 128; i++)
		k_pad[i] ^= 0x36;

	/* perform inner SHA384 */
	_addr[0] = k_pad;
	_len[0] = 128;
	for (i = 0; i < num_elem; i++) {
		_addr[i + 1] = addr[i];
		_len[i + 1] = len[i];
	}
	if (sha384_vector(1 + num_elem, _addr, _len, mac) < 0)
		return -1;

	os_memset(k_pad, 0, sizeof(k_pad));
	os_memcpy(k_pad, key, key_len);
	/* XOR key with opad values */
	for (i = 0; i < 128; i++)
		k_pad[i] ^= 0x5c;

	/* perform outer SHA384 */
	_addr[0] = k_pad;
	_len[0] = 128;
	_addr[1] = mac;
	_len[1] = SHA384_MAC_LEN;
	return sha384_vector(2, _addr, _len, mac);
}


/**
 * hmac_sha384 - HMAC-SHA384 over data buffer (RFC 2104)
 * @key: Key for HMAC operations
 * @key_len: Length of the key in bytes
 * @data: Pointers to the data area
 * @data_len: Length of the data area
 * @mac: Buffer for the hash (48 bytes)
 * Returns: 0 on success, -1 on failure
 */
int hmac_sha384(const u8 *key, size_t key_len, const u8 *data,
		size_t data_len, u8 *mac)
{
	return hmac_sha384_vector(key, key_len, 1, &data, &data_len, mac);
}
