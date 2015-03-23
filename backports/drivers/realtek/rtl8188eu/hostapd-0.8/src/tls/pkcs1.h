/*
 * PKCS #1 (RSA Encryption)
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
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

#ifndef PKCS1_H
#define PKCS1_H

int pkcs1_encrypt(int block_type, struct crypto_rsa_key *key,
		  int use_private, const u8 *in, size_t inlen,
		  u8 *out, size_t *outlen);
int pkcs1_v15_private_key_decrypt(struct crypto_rsa_key *key,
				  const u8 *in, size_t inlen,
				  u8 *out, size_t *outlen);
int pkcs1_decrypt_public_key(struct crypto_rsa_key *key,
			     const u8 *crypt, size_t crypt_len,
			     u8 *plain, size_t *plain_len);

#endif /* PKCS1_H */
