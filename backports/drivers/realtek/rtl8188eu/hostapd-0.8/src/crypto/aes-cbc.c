/*
 * AES-128 CBC
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
 * aes_128_cbc_encrypt - AES-128 CBC encryption
 * @key: Encryption key
 * @iv: Encryption IV for CBC mode (16 bytes)
 * @data: Data to encrypt in-place
 * @data_len: Length of data in bytes (must be divisible by 16)
 * Returns: 0 on success, -1 on failure
 */
int aes_128_cbc_encrypt(const u8 *key, const u8 *iv, u8 *data, size_t data_len)
{
	void *ctx;
	u8 cbc[AES_BLOCK_SIZE];
	u8 *pos = data;
	int i, j, blocks;

	ctx = aes_encrypt_init(key, 16);
	if (ctx == NULL)
		return -1;
	os_memcpy(cbc, iv, AES_BLOCK_SIZE);

	blocks = data_len / AES_BLOCK_SIZE;
	for (i = 0; i < blocks; i++) {
		for (j = 0; j < AES_BLOCK_SIZE; j++)
			cbc[j] ^= pos[j];
		aes_encrypt(ctx, cbc, cbc);
		os_memcpy(pos, cbc, AES_BLOCK_SIZE);
		pos += AES_BLOCK_SIZE;
	}
	aes_encrypt_deinit(ctx);
	return 0;
}


/**
 * aes_128_cbc_decrypt - AES-128 CBC decryption
 * @key: Decryption key
 * @iv: Decryption IV for CBC mode (16 bytes)
 * @data: Data to decrypt in-place
 * @data_len: Length of data in bytes (must be divisible by 16)
 * Returns: 0 on success, -1 on failure
 */
int aes_128_cbc_decrypt(const u8 *key, const u8 *iv, u8 *data, size_t data_len)
{
	void *ctx;
	u8 cbc[AES_BLOCK_SIZE], tmp[AES_BLOCK_SIZE];
	u8 *pos = data;
	int i, j, blocks;

	ctx = aes_decrypt_init(key, 16);
	if (ctx == NULL)
		return -1;
	os_memcpy(cbc, iv, AES_BLOCK_SIZE);

	blocks = data_len / AES_BLOCK_SIZE;
	for (i = 0; i < blocks; i++) {
		os_memcpy(tmp, pos, AES_BLOCK_SIZE);
		aes_decrypt(ctx, pos, pos);
		for (j = 0; j < AES_BLOCK_SIZE; j++)
			pos[j] ^= cbc[j];
		os_memcpy(cbc, tmp, AES_BLOCK_SIZE);
		pos += AES_BLOCK_SIZE;
	}
	aes_decrypt_deinit(ctx);
	return 0;
}
