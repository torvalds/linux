/*
 * PKCS #1 (RSA Encryption)
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef PKCS1_H
#define PKCS1_H

struct crypto_public_key;
struct asn1_oid;

int pkcs1_encrypt(int block_type, struct crypto_rsa_key *key,
		  int use_private, const u8 *in, size_t inlen,
		  u8 *out, size_t *outlen);
int pkcs1_v15_private_key_decrypt(struct crypto_rsa_key *key,
				  const u8 *in, size_t inlen,
				  u8 *out, size_t *outlen);
int pkcs1_decrypt_public_key(struct crypto_rsa_key *key,
			     const u8 *crypt, size_t crypt_len,
			     u8 *plain, size_t *plain_len);
int pkcs1_v15_sig_ver(struct crypto_public_key *pk,
		      const u8 *s, size_t s_len,
		      const struct asn1_oid *hash_alg,
		      const u8 *hash, size_t hash_len);

#endif /* PKCS1_H */
