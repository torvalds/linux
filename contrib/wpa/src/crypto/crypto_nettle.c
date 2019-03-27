/*
 * Wrapper functions for libnettle and libgmp
 * Copyright (c) 2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <nettle/nettle-meta.h>
#include <nettle/des.h>
#undef des_encrypt
#include <nettle/hmac.h>
#include <nettle/aes.h>
#undef aes_encrypt
#undef aes_decrypt
#include <nettle/arcfour.h>
#include <nettle/bignum.h>

#include "common.h"
#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "sha384.h"
#include "sha512.h"
#include "crypto.h"


int des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
	struct des_ctx ctx;
	u8 pkey[8], next, tmp;
	int i;

	/* Add parity bits to the key */
	next = 0;
	for (i = 0; i < 7; i++) {
		tmp = key[i];
		pkey[i] = (tmp >> i) | next | 1;
		next = tmp << (7 - i);
	}
	pkey[i] = next | 1;

	nettle_des_set_key(&ctx, pkey);
	nettle_des_encrypt(&ctx, DES_BLOCK_SIZE, cypher, clear);
	os_memset(&ctx, 0, sizeof(ctx));
	return 0;
}


static int nettle_digest_vector(const struct nettle_hash *alg, size_t num_elem,
				const u8 *addr[], const size_t *len, u8 *mac)
{
	void *ctx;
	size_t i;

	if (TEST_FAIL())
		return -1;

	ctx = os_malloc(alg->context_size);
	if (!ctx)
		return -1;
	alg->init(ctx);
	for (i = 0; i < num_elem; i++)
		alg->update(ctx, len[i], addr[i]);
	alg->digest(ctx, alg->digest_size, mac);
	bin_clear_free(ctx, alg->context_size);
	return 0;
}


int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return nettle_digest_vector(&nettle_md4, num_elem, addr, len, mac);
}


int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return nettle_digest_vector(&nettle_md5, num_elem, addr, len, mac);
}


int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return nettle_digest_vector(&nettle_sha1, num_elem, addr, len, mac);
}


int sha256_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return nettle_digest_vector(&nettle_sha256, num_elem, addr, len, mac);
}


int sha384_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return nettle_digest_vector(&nettle_sha384, num_elem, addr, len, mac);
}


int sha512_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return nettle_digest_vector(&nettle_sha512, num_elem, addr, len, mac);
}


int hmac_md5_vector(const u8 *key, size_t key_len, size_t num_elem,
		    const u8 *addr[], const size_t *len, u8 *mac)
{
	struct hmac_md5_ctx ctx;
	size_t i;

	if (TEST_FAIL())
		return -1;

	hmac_md5_set_key(&ctx, key_len, key);
	for (i = 0; i < num_elem; i++)
		hmac_md5_update(&ctx, len[i], addr[i]);
	hmac_md5_digest(&ctx, MD5_DIGEST_SIZE, mac);
	os_memset(&ctx, 0, sizeof(ctx));
	return 0;
}


int hmac_md5(const u8 *key, size_t key_len, const u8 *data, size_t data_len,
	     u8 *mac)
{
	return hmac_md5_vector(key, key_len, 1, &data, &data_len, mac);
}


int hmac_sha1_vector(const u8 *key, size_t key_len, size_t num_elem,
		     const u8 *addr[], const size_t *len, u8 *mac)
{
	struct hmac_sha1_ctx ctx;
	size_t i;

	if (TEST_FAIL())
		return -1;

	hmac_sha1_set_key(&ctx, key_len, key);
	for (i = 0; i < num_elem; i++)
		hmac_sha1_update(&ctx, len[i], addr[i]);
	hmac_sha1_digest(&ctx, SHA1_DIGEST_SIZE, mac);
	os_memset(&ctx, 0, sizeof(ctx));
	return 0;
}


int hmac_sha1(const u8 *key, size_t key_len, const u8 *data, size_t data_len,
	       u8 *mac)
{
	return hmac_sha1_vector(key, key_len, 1, &data, &data_len, mac);
}


#ifdef CONFIG_SHA256

int hmac_sha256_vector(const u8 *key, size_t key_len, size_t num_elem,
		       const u8 *addr[], const size_t *len, u8 *mac)
{
	struct hmac_sha256_ctx ctx;
	size_t i;

	if (TEST_FAIL())
		return -1;

	hmac_sha256_set_key(&ctx, key_len, key);
	for (i = 0; i < num_elem; i++)
		hmac_sha256_update(&ctx, len[i], addr[i]);
	hmac_sha256_digest(&ctx, SHA256_DIGEST_SIZE, mac);
	os_memset(&ctx, 0, sizeof(ctx));
	return 0;
}


int hmac_sha256(const u8 *key, size_t key_len, const u8 *data,
		size_t data_len, u8 *mac)
{
	return hmac_sha256_vector(key, key_len, 1, &data, &data_len, mac);
}

#endif /* CONFIG_SHA256 */


#ifdef CONFIG_SHA384

int hmac_sha384_vector(const u8 *key, size_t key_len, size_t num_elem,
		       const u8 *addr[], const size_t *len, u8 *mac)
{
	struct hmac_sha384_ctx ctx;
	size_t i;

	if (TEST_FAIL())
		return -1;

	hmac_sha384_set_key(&ctx, key_len, key);
	for (i = 0; i < num_elem; i++)
		hmac_sha384_update(&ctx, len[i], addr[i]);
	hmac_sha384_digest(&ctx, SHA384_DIGEST_SIZE, mac);
	os_memset(&ctx, 0, sizeof(ctx));
	return 0;
}


int hmac_sha384(const u8 *key, size_t key_len, const u8 *data,
		size_t data_len, u8 *mac)
{
	return hmac_sha384_vector(key, key_len, 1, &data, &data_len, mac);
}

#endif /* CONFIG_SHA384 */


#ifdef CONFIG_SHA512

int hmac_sha512_vector(const u8 *key, size_t key_len, size_t num_elem,
		       const u8 *addr[], const size_t *len, u8 *mac)
{
	struct hmac_sha512_ctx ctx;
	size_t i;

	if (TEST_FAIL())
		return -1;

	hmac_sha512_set_key(&ctx, key_len, key);
	for (i = 0; i < num_elem; i++)
		hmac_sha512_update(&ctx, len[i], addr[i]);
	hmac_sha512_digest(&ctx, SHA512_DIGEST_SIZE, mac);
	os_memset(&ctx, 0, sizeof(ctx));
	return 0;
}


int hmac_sha512(const u8 *key, size_t key_len, const u8 *data,
		size_t data_len, u8 *mac)
{
	return hmac_sha512_vector(key, key_len, 1, &data, &data_len, mac);
}

#endif /* CONFIG_SHA512 */


void * aes_encrypt_init(const u8 *key, size_t len)
{
	struct aes_ctx *ctx;

	if (TEST_FAIL())
		return NULL;
	ctx = os_malloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	nettle_aes_set_encrypt_key(ctx, len, key);

	return ctx;
}


int aes_encrypt(void *ctx, const u8 *plain, u8 *crypt)
{
	struct aes_ctx *actx = ctx;
	nettle_aes_encrypt(actx, AES_BLOCK_SIZE, crypt, plain);
	return 0;
}


void aes_encrypt_deinit(void *ctx)
{
	struct aes_ctx *actx = ctx;
	bin_clear_free(actx, sizeof(*actx));
}


void * aes_decrypt_init(const u8 *key, size_t len)
{
	struct aes_ctx *ctx;

	if (TEST_FAIL())
		return NULL;
	ctx = os_malloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	nettle_aes_set_decrypt_key(ctx, len, key);

	return ctx;
}


int aes_decrypt(void *ctx, const u8 *crypt, u8 *plain)
{
	struct aes_ctx *actx = ctx;
	nettle_aes_decrypt(actx, AES_BLOCK_SIZE, plain, crypt);
	return 0;
}


void aes_decrypt_deinit(void *ctx)
{
	struct aes_ctx *actx = ctx;
	bin_clear_free(actx, sizeof(*actx));
}


int crypto_dh_init(u8 generator, const u8 *prime, size_t prime_len, u8 *privkey,
		   u8 *pubkey)
{
	size_t pubkey_len, pad;

	if (os_get_random(privkey, prime_len) < 0)
		return -1;
	if (os_memcmp(privkey, prime, prime_len) > 0) {
		/* Make sure private value is smaller than prime */
		privkey[0] = 0;
	}

	pubkey_len = prime_len;
	if (crypto_mod_exp(&generator, 1, privkey, prime_len, prime, prime_len,
			   pubkey, &pubkey_len) < 0)
		return -1;
	if (pubkey_len < prime_len) {
		pad = prime_len - pubkey_len;
		os_memmove(pubkey + pad, pubkey, pubkey_len);
		os_memset(pubkey, 0, pad);
	}

	return 0;
}


int crypto_dh_derive_secret(u8 generator, const u8 *prime, size_t prime_len,
			    const u8 *privkey, size_t privkey_len,
			    const u8 *pubkey, size_t pubkey_len,
			    u8 *secret, size_t *len)
{
	return crypto_mod_exp(pubkey, pubkey_len, privkey, privkey_len,
			      prime, prime_len, secret, len);
}


int crypto_mod_exp(const u8 *base, size_t base_len,
		   const u8 *power, size_t power_len,
		   const u8 *modulus, size_t modulus_len,
		   u8 *result, size_t *result_len)
{
	mpz_t bn_base, bn_exp, bn_modulus, bn_result;
	int ret = -1;
	size_t len;

	mpz_inits(bn_base, bn_exp, bn_modulus, bn_result, NULL);
	mpz_import(bn_base, base_len, 1, 1, 1, 0, base);
	mpz_import(bn_exp, power_len, 1, 1, 1, 0, power);
	mpz_import(bn_modulus, modulus_len, 1, 1, 1, 0, modulus);

	mpz_powm(bn_result, bn_base, bn_exp, bn_modulus);
	len = mpz_sizeinbase(bn_result, 2);
	len = (len + 7) / 8;
	if (*result_len < len)
		goto error;
	mpz_export(result, result_len, 1, 1, 1, 0, bn_result);
	ret = 0;

error:
	mpz_clears(bn_base, bn_exp, bn_modulus, bn_result, NULL);
	return ret;
}


struct crypto_cipher {
	enum crypto_cipher_alg alg;
	union {
		struct arcfour_ctx arcfour_ctx;
	} u;
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	struct crypto_cipher *ctx;

	ctx = os_zalloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->alg = alg;

	switch (alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		nettle_arcfour_set_key(&ctx->u.arcfour_ctx, key_len, key);
		break;
	default:
		os_free(ctx);
		return NULL;
	}

	return ctx;
}


int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	switch (ctx->alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		nettle_arcfour_crypt(&ctx->u.arcfour_ctx, len, crypt, plain);
		break;
	default:
		return -1;
	}

	return 0;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	switch (ctx->alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		nettle_arcfour_crypt(&ctx->u.arcfour_ctx, len, plain, crypt);
		break;
	default:
		return -1;
	}

	return 0;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	bin_clear_free(ctx, sizeof(*ctx));
}
