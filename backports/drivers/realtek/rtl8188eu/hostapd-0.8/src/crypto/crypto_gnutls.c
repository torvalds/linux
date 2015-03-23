/*
 * WPA Supplicant / wrapper functions for libgcrypt
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
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
#include <gcrypt.h>

#include "common.h"
#include "crypto.h"

int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	gcry_md_hd_t hd;
	unsigned char *p;
	size_t i;

	if (gcry_md_open(&hd, GCRY_MD_MD4, 0) != GPG_ERR_NO_ERROR)
		return -1;
	for (i = 0; i < num_elem; i++)
		gcry_md_write(hd, addr[i], len[i]);
	p = gcry_md_read(hd, GCRY_MD_MD4);
	if (p)
		memcpy(mac, p, gcry_md_get_algo_dlen(GCRY_MD_MD4));
	gcry_md_close(hd);
	return 0;
}


void des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
	gcry_cipher_hd_t hd;
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

	gcry_cipher_open(&hd, GCRY_CIPHER_DES, GCRY_CIPHER_MODE_ECB, 0);
	gcry_err_code(gcry_cipher_setkey(hd, pkey, 8));
	gcry_cipher_encrypt(hd, cypher, 8, clear, 8);
	gcry_cipher_close(hd);
}


int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	gcry_md_hd_t hd;
	unsigned char *p;
	size_t i;

	if (gcry_md_open(&hd, GCRY_MD_MD5, 0) != GPG_ERR_NO_ERROR)
		return -1;
	for (i = 0; i < num_elem; i++)
		gcry_md_write(hd, addr[i], len[i]);
	p = gcry_md_read(hd, GCRY_MD_MD5);
	if (p)
		memcpy(mac, p, gcry_md_get_algo_dlen(GCRY_MD_MD5));
	gcry_md_close(hd);
	return 0;
}


int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	gcry_md_hd_t hd;
	unsigned char *p;
	size_t i;

	if (gcry_md_open(&hd, GCRY_MD_SHA1, 0) != GPG_ERR_NO_ERROR)
		return -1;
	for (i = 0; i < num_elem; i++)
		gcry_md_write(hd, addr[i], len[i]);
	p = gcry_md_read(hd, GCRY_MD_SHA1);
	if (p)
		memcpy(mac, p, gcry_md_get_algo_dlen(GCRY_MD_SHA1));
	gcry_md_close(hd);
	return 0;
}


void * aes_encrypt_init(const u8 *key, size_t len)
{
	gcry_cipher_hd_t hd;

	if (gcry_cipher_open(&hd, GCRY_CIPHER_AES, GCRY_CIPHER_MODE_ECB, 0) !=
	    GPG_ERR_NO_ERROR) {
		printf("cipher open failed\n");
		return NULL;
	}
	if (gcry_cipher_setkey(hd, key, len) != GPG_ERR_NO_ERROR) {
		printf("setkey failed\n");
		gcry_cipher_close(hd);
		return NULL;
	}

	return hd;
}


void aes_encrypt(void *ctx, const u8 *plain, u8 *crypt)
{
	gcry_cipher_hd_t hd = ctx;
	gcry_cipher_encrypt(hd, crypt, 16, plain, 16);
}


void aes_encrypt_deinit(void *ctx)
{
	gcry_cipher_hd_t hd = ctx;
	gcry_cipher_close(hd);
}


void * aes_decrypt_init(const u8 *key, size_t len)
{
	gcry_cipher_hd_t hd;

	if (gcry_cipher_open(&hd, GCRY_CIPHER_AES, GCRY_CIPHER_MODE_ECB, 0) !=
	    GPG_ERR_NO_ERROR)
		return NULL;
	if (gcry_cipher_setkey(hd, key, len) != GPG_ERR_NO_ERROR) {
		gcry_cipher_close(hd);
		return NULL;
	}

	return hd;
}


void aes_decrypt(void *ctx, const u8 *crypt, u8 *plain)
{
	gcry_cipher_hd_t hd = ctx;
	gcry_cipher_decrypt(hd, plain, 16, crypt, 16);
}


void aes_decrypt_deinit(void *ctx)
{
	gcry_cipher_hd_t hd = ctx;
	gcry_cipher_close(hd);
}


int crypto_mod_exp(const u8 *base, size_t base_len,
		   const u8 *power, size_t power_len,
		   const u8 *modulus, size_t modulus_len,
		   u8 *result, size_t *result_len)
{
	gcry_mpi_t bn_base = NULL, bn_exp = NULL, bn_modulus = NULL,
		bn_result = NULL;
	int ret = -1;

	if (gcry_mpi_scan(&bn_base, GCRYMPI_FMT_USG, base, base_len, NULL) !=
	    GPG_ERR_NO_ERROR ||
	    gcry_mpi_scan(&bn_exp, GCRYMPI_FMT_USG, power, power_len, NULL) !=
	    GPG_ERR_NO_ERROR ||
	    gcry_mpi_scan(&bn_modulus, GCRYMPI_FMT_USG, modulus, modulus_len,
			  NULL) != GPG_ERR_NO_ERROR)
		goto error;
	bn_result = gcry_mpi_new(modulus_len * 8);

	gcry_mpi_powm(bn_result, bn_base, bn_exp, bn_modulus);

	if (gcry_mpi_print(GCRYMPI_FMT_USG, result, *result_len, result_len,
			   bn_result) != GPG_ERR_NO_ERROR)
		goto error;

	ret = 0;

error:
	gcry_mpi_release(bn_base);
	gcry_mpi_release(bn_exp);
	gcry_mpi_release(bn_modulus);
	gcry_mpi_release(bn_result);
	return ret;
}


struct crypto_cipher {
	gcry_cipher_hd_t enc;
	gcry_cipher_hd_t dec;
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	struct crypto_cipher *ctx;
	gcry_error_t res;
	enum gcry_cipher_algos a;
	int ivlen;

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	switch (alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		a = GCRY_CIPHER_ARCFOUR;
		res = gcry_cipher_open(&ctx->enc, a, GCRY_CIPHER_MODE_STREAM,
				       0);
		gcry_cipher_open(&ctx->dec, a, GCRY_CIPHER_MODE_STREAM, 0);
		break;
	case CRYPTO_CIPHER_ALG_AES:
		if (key_len == 24)
			a = GCRY_CIPHER_AES192;
		else if (key_len == 32)
			a = GCRY_CIPHER_AES256;
		else
			a = GCRY_CIPHER_AES;
		res = gcry_cipher_open(&ctx->enc, a, GCRY_CIPHER_MODE_CBC, 0);
		gcry_cipher_open(&ctx->dec, a, GCRY_CIPHER_MODE_CBC, 0);
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		a = GCRY_CIPHER_3DES;
		res = gcry_cipher_open(&ctx->enc, a, GCRY_CIPHER_MODE_CBC, 0);
		gcry_cipher_open(&ctx->dec, a, GCRY_CIPHER_MODE_CBC, 0);
		break;
	case CRYPTO_CIPHER_ALG_DES:
		a = GCRY_CIPHER_DES;
		res = gcry_cipher_open(&ctx->enc, a, GCRY_CIPHER_MODE_CBC, 0);
		gcry_cipher_open(&ctx->dec, a, GCRY_CIPHER_MODE_CBC, 0);
		break;
	case CRYPTO_CIPHER_ALG_RC2:
		if (key_len == 5)
			a = GCRY_CIPHER_RFC2268_40;
		else
			a = GCRY_CIPHER_RFC2268_128;
		res = gcry_cipher_open(&ctx->enc, a, GCRY_CIPHER_MODE_CBC, 0);
		gcry_cipher_open(&ctx->dec, a, GCRY_CIPHER_MODE_CBC, 0);
		break;
	default:
		os_free(ctx);
		return NULL;
	}

	if (res != GPG_ERR_NO_ERROR) {
		os_free(ctx);
		return NULL;
	}

	if (gcry_cipher_setkey(ctx->enc, key, key_len) != GPG_ERR_NO_ERROR ||
	    gcry_cipher_setkey(ctx->dec, key, key_len) != GPG_ERR_NO_ERROR) {
		gcry_cipher_close(ctx->enc);
		gcry_cipher_close(ctx->dec);
		os_free(ctx);
		return NULL;
	}

	ivlen = gcry_cipher_get_algo_blklen(a);
	if (gcry_cipher_setiv(ctx->enc, iv, ivlen) != GPG_ERR_NO_ERROR ||
	    gcry_cipher_setiv(ctx->dec, iv, ivlen) != GPG_ERR_NO_ERROR) {
		gcry_cipher_close(ctx->enc);
		gcry_cipher_close(ctx->dec);
		os_free(ctx);
		return NULL;
	}

	return ctx;
}


int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	if (gcry_cipher_encrypt(ctx->enc, crypt, len, plain, len) !=
	    GPG_ERR_NO_ERROR)
		return -1;
	return 0;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	if (gcry_cipher_decrypt(ctx->dec, plain, len, crypt, len) !=
	    GPG_ERR_NO_ERROR)
		return -1;
	return 0;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	gcry_cipher_close(ctx->enc);
	gcry_cipher_close(ctx->dec);
	os_free(ctx);
}
