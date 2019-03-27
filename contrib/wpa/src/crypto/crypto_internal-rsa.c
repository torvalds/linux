/*
 * Crypto wrapper for internal crypto implementation - RSA parts
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto.h"
#include "tls/rsa.h"
#include "tls/pkcs1.h"
#include "tls/pkcs8.h"

/* Dummy structures; these are just typecast to struct crypto_rsa_key */
struct crypto_public_key;
struct crypto_private_key;


struct crypto_public_key * crypto_public_key_import(const u8 *key, size_t len)
{
	return (struct crypto_public_key *)
		crypto_rsa_import_public_key(key, len);
}


struct crypto_public_key *
crypto_public_key_import_parts(const u8 *n, size_t n_len,
			       const u8 *e, size_t e_len)
{
	return (struct crypto_public_key *)
		crypto_rsa_import_public_key_parts(n, n_len, e, e_len);
}


struct crypto_private_key * crypto_private_key_import(const u8 *key,
						      size_t len,
						      const char *passwd)
{
	struct crypto_private_key *res;

	/* First, check for possible PKCS #8 encoding */
	res = pkcs8_key_import(key, len);
	if (res)
		return res;

	if (passwd) {
		/* Try to parse as encrypted PKCS #8 */
		res = pkcs8_enc_key_import(key, len, passwd);
		if (res)
			return res;
	}

	/* Not PKCS#8, so try to import PKCS #1 encoded RSA private key */
	wpa_printf(MSG_DEBUG, "Trying to parse PKCS #1 encoded RSA private "
		   "key");
	return (struct crypto_private_key *)
		crypto_rsa_import_private_key(key, len);
}


struct crypto_public_key * crypto_public_key_from_cert(const u8 *buf,
						       size_t len)
{
	/* No X.509 support in crypto_internal.c */
	return NULL;
}


int crypto_public_key_encrypt_pkcs1_v15(struct crypto_public_key *key,
					const u8 *in, size_t inlen,
					u8 *out, size_t *outlen)
{
	return pkcs1_encrypt(2, (struct crypto_rsa_key *) key,
			     0, in, inlen, out, outlen);
}


int crypto_private_key_decrypt_pkcs1_v15(struct crypto_private_key *key,
					 const u8 *in, size_t inlen,
					 u8 *out, size_t *outlen)
{
	return pkcs1_v15_private_key_decrypt((struct crypto_rsa_key *) key,
					     in, inlen, out, outlen);
}


int crypto_private_key_sign_pkcs1(struct crypto_private_key *key,
				  const u8 *in, size_t inlen,
				  u8 *out, size_t *outlen)
{
	return pkcs1_encrypt(1, (struct crypto_rsa_key *) key,
			     1, in, inlen, out, outlen);
}


void crypto_public_key_free(struct crypto_public_key *key)
{
	crypto_rsa_free((struct crypto_rsa_key *) key);
}


void crypto_private_key_free(struct crypto_private_key *key)
{
	crypto_rsa_free((struct crypto_rsa_key *) key);
}


int crypto_public_key_decrypt_pkcs1(struct crypto_public_key *key,
				    const u8 *crypt, size_t crypt_len,
				    u8 *plain, size_t *plain_len)
{
	return pkcs1_decrypt_public_key((struct crypto_rsa_key *) key,
					crypt, crypt_len, plain, plain_len);
}
