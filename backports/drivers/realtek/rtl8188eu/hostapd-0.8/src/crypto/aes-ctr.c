/*
 * AES-128 CTR
 *
 * Copyright (c) 2003-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "aes.h"
#include "aes_wrap.h"

/**
 * aes_128_ctr_encrypt - AES-128 CTR mode encryption
 * @key: Key for encryption (16 bytes)
 * @nonce: Nonce for counter mode (16 bytes)
 * @data: Data to encrypt in-place
 * @data_len: Length of data in bytes
 * Returns: 0 on success, -1 on failure
 */
int aes_128_ctr_encrypt(const u8 *key, const u8 *nonce,
			u8 *data, size_t data_len)
{
	void *ctx;
	size_t j, len, left = data_len;
	int i;
	u8 *pos = data;
	u8 counter[AES_BLOCK_SIZE], buf[AES_BLOCK_SIZE];

	ctx = aes_encrypt_init(key, 16);
	if (ctx == NULL)
		return -1;
	os_memcpy(counter, nonce, AES_BLOCK_SIZE);

	while (left > 0) {
		aes_encrypt(ctx, counter, buf);

		len = (left < AES_BLOCK_SIZE) ? left : AES_BLOCK_SIZE;
		for (j = 0; j < len; j++)
			pos[j] ^= buf[j];
		pos += len;
		left -= len;

		for (i = AES_BLOCK_SIZE - 1; i >= 0; i--) {
			counter[i]++;
			if (counter[i])
				break;
		}
	}
	aes_encrypt_deinit(ctx);
	return 0;
}
