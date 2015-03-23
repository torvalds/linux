/*
 * WPA Supplicant / wrapper functions for libcrypto
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
#include <openssl/opensslv.h>
#include <openssl/err.h>
#include <openssl/des.h>
#include <openssl/aes.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/dh.h>

#include "common.h"
#include "wpabuf.h"
#include "dh_group5.h"
#include "crypto.h"

#if OPENSSL_VERSION_NUMBER < 0x00907000
#define DES_key_schedule des_key_schedule
#define DES_cblock des_cblock
#define DES_set_key(key, schedule) des_set_key((key), *(schedule))
#define DES_ecb_encrypt(input, output, ks, enc) \
	des_ecb_encrypt((input), (output), *(ks), (enc))
#endif /* openssl < 0.9.7 */

static BIGNUM * get_group5_prime(void)
{
#if OPENSSL_VERSION_NUMBER < 0x00908000
	static const unsigned char RFC3526_PRIME_1536[] = {
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC9,0x0F,0xDA,0xA2,
		0x21,0x68,0xC2,0x34,0xC4,0xC6,0x62,0x8B,0x80,0xDC,0x1C,0xD1,
		0x29,0x02,0x4E,0x08,0x8A,0x67,0xCC,0x74,0x02,0x0B,0xBE,0xA6,
		0x3B,0x13,0x9B,0x22,0x51,0x4A,0x08,0x79,0x8E,0x34,0x04,0xDD,
		0xEF,0x95,0x19,0xB3,0xCD,0x3A,0x43,0x1B,0x30,0x2B,0x0A,0x6D,
		0xF2,0x5F,0x14,0x37,0x4F,0xE1,0x35,0x6D,0x6D,0x51,0xC2,0x45,
		0xE4,0x85,0xB5,0x76,0x62,0x5E,0x7E,0xC6,0xF4,0x4C,0x42,0xE9,
		0xA6,0x37,0xED,0x6B,0x0B,0xFF,0x5C,0xB6,0xF4,0x06,0xB7,0xED,
		0xEE,0x38,0x6B,0xFB,0x5A,0x89,0x9F,0xA5,0xAE,0x9F,0x24,0x11,
		0x7C,0x4B,0x1F,0xE6,0x49,0x28,0x66,0x51,0xEC,0xE4,0x5B,0x3D,
		0xC2,0x00,0x7C,0xB8,0xA1,0x63,0xBF,0x05,0x98,0xDA,0x48,0x36,
		0x1C,0x55,0xD3,0x9A,0x69,0x16,0x3F,0xA8,0xFD,0x24,0xCF,0x5F,
		0x83,0x65,0x5D,0x23,0xDC,0xA3,0xAD,0x96,0x1C,0x62,0xF3,0x56,
		0x20,0x85,0x52,0xBB,0x9E,0xD5,0x29,0x07,0x70,0x96,0x96,0x6D,
		0x67,0x0C,0x35,0x4E,0x4A,0xBC,0x98,0x04,0xF1,0x74,0x6C,0x08,
		0xCA,0x23,0x73,0x27,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	};
        return BN_bin2bn(RFC3526_PRIME_1536, sizeof(RFC3526_PRIME_1536), NULL);
#else /* openssl < 0.9.8 */
	return get_rfc3526_prime_1536(NULL);
#endif /* openssl < 0.9.8 */
}

#if OPENSSL_VERSION_NUMBER < 0x00908000
#ifndef OPENSSL_NO_SHA256
#ifndef OPENSSL_FIPS
#define NO_SHA256_WRAPPER
#endif
#endif

#endif /* openssl < 0.9.8 */

#ifdef OPENSSL_NO_SHA256
#define NO_SHA256_WRAPPER
#endif

static int openssl_digest_vector(const EVP_MD *type, int non_fips,
				 size_t num_elem, const u8 *addr[],
				 const size_t *len, u8 *mac)
{
	EVP_MD_CTX ctx;
	size_t i;
	unsigned int mac_len;

	EVP_MD_CTX_init(&ctx);
#ifdef CONFIG_FIPS
#ifdef OPENSSL_FIPS
	if (non_fips)
		EVP_MD_CTX_set_flags(&ctx, EVP_MD_CTX_FLAG_NON_FIPS_ALLOW);
#endif /* OPENSSL_FIPS */
#endif /* CONFIG_FIPS */
	if (!EVP_DigestInit_ex(&ctx, type, NULL)) {
		wpa_printf(MSG_ERROR, "OpenSSL: EVP_DigestInit_ex failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}
	for (i = 0; i < num_elem; i++) {
		if (!EVP_DigestUpdate(&ctx, addr[i], len[i])) {
			wpa_printf(MSG_ERROR, "OpenSSL: EVP_DigestUpdate "
				   "failed: %s",
				   ERR_error_string(ERR_get_error(), NULL));
			return -1;
		}
	}
	if (!EVP_DigestFinal(&ctx, mac, &mac_len)) {
		wpa_printf(MSG_ERROR, "OpenSSL: EVP_DigestFinal failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		return -1;
	}

	return 0;
}


int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return openssl_digest_vector(EVP_md4(), 0, num_elem, addr, len, mac);
}


void des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
	u8 pkey[8], next, tmp;
	int i;
	DES_key_schedule ks;

	/* Add parity bits to the key */
	next = 0;
	for (i = 0; i < 7; i++) {
		tmp = key[i];
		pkey[i] = (tmp >> i) | next | 1;
		next = tmp << (7 - i);
	}
	pkey[i] = next | 1;

	DES_set_key(&pkey, &ks);
	DES_ecb_encrypt((DES_cblock *) clear, (DES_cblock *) cypher, &ks,
			DES_ENCRYPT);
}


int rc4_skip(const u8 *key, size_t keylen, size_t skip,
	     u8 *data, size_t data_len)
{
#ifdef OPENSSL_NO_RC4
	return -1;
#else /* OPENSSL_NO_RC4 */
	EVP_CIPHER_CTX ctx;
	int outl;
	int res = -1;
	unsigned char skip_buf[16];

	EVP_CIPHER_CTX_init(&ctx);
	if (!EVP_CIPHER_CTX_set_padding(&ctx, 0) ||
	    !EVP_CipherInit_ex(&ctx, EVP_rc4(), NULL, NULL, NULL, 1) ||
	    !EVP_CIPHER_CTX_set_key_length(&ctx, keylen) ||
	    !EVP_CipherInit_ex(&ctx, NULL, NULL, key, NULL, 1))
		goto out;

	while (skip >= sizeof(skip_buf)) {
		size_t len = skip;
		if (len > sizeof(skip_buf))
			len = sizeof(skip_buf);
		if (!EVP_CipherUpdate(&ctx, skip_buf, &outl, skip_buf, len))
			goto out;
		skip -= len;
	}

	if (EVP_CipherUpdate(&ctx, data, &outl, data, data_len))
		res = 0;

out:
	EVP_CIPHER_CTX_cleanup(&ctx);
	return res;
#endif /* OPENSSL_NO_RC4 */
}


int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return openssl_digest_vector(EVP_md5(), 0, num_elem, addr, len, mac);
}


#ifdef CONFIG_FIPS
int md5_vector_non_fips_allow(size_t num_elem, const u8 *addr[],
			      const size_t *len, u8 *mac)
{
	return openssl_digest_vector(EVP_md5(), 1, num_elem, addr, len, mac);
}
#endif /* CONFIG_FIPS */


int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return openssl_digest_vector(EVP_sha1(), 0, num_elem, addr, len, mac);
}


#ifndef NO_SHA256_WRAPPER
int sha256_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	return openssl_digest_vector(EVP_sha256(), 0, num_elem, addr, len,
				     mac);
}
#endif /* NO_SHA256_WRAPPER */


void * aes_encrypt_init(const u8 *key, size_t len)
{
	AES_KEY *ak;
	ak = os_malloc(sizeof(*ak));
	if (ak == NULL)
		return NULL;
	if (AES_set_encrypt_key(key, 8 * len, ak) < 0) {
		os_free(ak);
		return NULL;
	}
	return ak;
}


void aes_encrypt(void *ctx, const u8 *plain, u8 *crypt)
{
	AES_encrypt(plain, crypt, ctx);
}


void aes_encrypt_deinit(void *ctx)
{
	os_free(ctx);
}


void * aes_decrypt_init(const u8 *key, size_t len)
{
	AES_KEY *ak;
	ak = os_malloc(sizeof(*ak));
	if (ak == NULL)
		return NULL;
	if (AES_set_decrypt_key(key, 8 * len, ak) < 0) {
		os_free(ak);
		return NULL;
	}
	return ak;
}


void aes_decrypt(void *ctx, const u8 *crypt, u8 *plain)
{
	AES_decrypt(crypt, plain, ctx);
}


void aes_decrypt_deinit(void *ctx)
{
	os_free(ctx);
}


int crypto_mod_exp(const u8 *base, size_t base_len,
		   const u8 *power, size_t power_len,
		   const u8 *modulus, size_t modulus_len,
		   u8 *result, size_t *result_len)
{
	BIGNUM *bn_base, *bn_exp, *bn_modulus, *bn_result;
	int ret = -1;
	BN_CTX *ctx;

	ctx = BN_CTX_new();
	if (ctx == NULL)
		return -1;

	bn_base = BN_bin2bn(base, base_len, NULL);
	bn_exp = BN_bin2bn(power, power_len, NULL);
	bn_modulus = BN_bin2bn(modulus, modulus_len, NULL);
	bn_result = BN_new();

	if (bn_base == NULL || bn_exp == NULL || bn_modulus == NULL ||
	    bn_result == NULL)
		goto error;

	if (BN_mod_exp(bn_result, bn_base, bn_exp, bn_modulus, ctx) != 1)
		goto error;

	*result_len = BN_bn2bin(bn_result, result);
	ret = 0;

error:
	BN_free(bn_base);
	BN_free(bn_exp);
	BN_free(bn_modulus);
	BN_free(bn_result);
	BN_CTX_free(ctx);
	return ret;
}


struct crypto_cipher {
	EVP_CIPHER_CTX enc;
	EVP_CIPHER_CTX dec;
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	struct crypto_cipher *ctx;
	const EVP_CIPHER *cipher;

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	switch (alg) {
#ifndef OPENSSL_NO_RC4
	case CRYPTO_CIPHER_ALG_RC4:
		cipher = EVP_rc4();
		break;
#endif /* OPENSSL_NO_RC4 */
#ifndef OPENSSL_NO_AES
	case CRYPTO_CIPHER_ALG_AES:
		switch (key_len) {
		case 16:
			cipher = EVP_aes_128_cbc();
			break;
		case 24:
			cipher = EVP_aes_192_cbc();
			break;
		case 32:
			cipher = EVP_aes_256_cbc();
			break;
		default:
			os_free(ctx);
			return NULL;
		}
		break;
#endif /* OPENSSL_NO_AES */
#ifndef OPENSSL_NO_DES
	case CRYPTO_CIPHER_ALG_3DES:
		cipher = EVP_des_ede3_cbc();
		break;
	case CRYPTO_CIPHER_ALG_DES:
		cipher = EVP_des_cbc();
		break;
#endif /* OPENSSL_NO_DES */
#ifndef OPENSSL_NO_RC2
	case CRYPTO_CIPHER_ALG_RC2:
		cipher = EVP_rc2_ecb();
		break;
#endif /* OPENSSL_NO_RC2 */
	default:
		os_free(ctx);
		return NULL;
	}

	EVP_CIPHER_CTX_init(&ctx->enc);
	EVP_CIPHER_CTX_set_padding(&ctx->enc, 0);
	if (!EVP_EncryptInit_ex(&ctx->enc, cipher, NULL, NULL, NULL) ||
	    !EVP_CIPHER_CTX_set_key_length(&ctx->enc, key_len) ||
	    !EVP_EncryptInit_ex(&ctx->enc, NULL, NULL, key, iv)) {
		EVP_CIPHER_CTX_cleanup(&ctx->enc);
		os_free(ctx);
		return NULL;
	}

	EVP_CIPHER_CTX_init(&ctx->dec);
	EVP_CIPHER_CTX_set_padding(&ctx->dec, 0);
	if (!EVP_DecryptInit_ex(&ctx->dec, cipher, NULL, NULL, NULL) ||
	    !EVP_CIPHER_CTX_set_key_length(&ctx->dec, key_len) ||
	    !EVP_DecryptInit_ex(&ctx->dec, NULL, NULL, key, iv)) {
		EVP_CIPHER_CTX_cleanup(&ctx->enc);
		EVP_CIPHER_CTX_cleanup(&ctx->dec);
		os_free(ctx);
		return NULL;
	}

	return ctx;
}


int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	int outl;
	if (!EVP_EncryptUpdate(&ctx->enc, crypt, &outl, plain, len))
		return -1;
	return 0;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	int outl;
	outl = len;
	if (!EVP_DecryptUpdate(&ctx->dec, plain, &outl, crypt, len))
		return -1;
	return 0;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	EVP_CIPHER_CTX_cleanup(&ctx->enc);
	EVP_CIPHER_CTX_cleanup(&ctx->dec);
	os_free(ctx);
}


void * dh5_init(struct wpabuf **priv, struct wpabuf **publ)
{
	DH *dh;
	struct wpabuf *pubkey = NULL, *privkey = NULL;
	size_t publen, privlen;

	*priv = NULL;
	*publ = NULL;

	dh = DH_new();
	if (dh == NULL)
		return NULL;

	dh->g = BN_new();
	if (dh->g == NULL || BN_set_word(dh->g, 2) != 1)
		goto err;

	dh->p = get_group5_prime();
	if (dh->p == NULL)
		goto err;

	if (DH_generate_key(dh) != 1)
		goto err;

	publen = BN_num_bytes(dh->pub_key);
	pubkey = wpabuf_alloc(publen);
	if (pubkey == NULL)
		goto err;
	privlen = BN_num_bytes(dh->priv_key);
	privkey = wpabuf_alloc(privlen);
	if (privkey == NULL)
		goto err;

	BN_bn2bin(dh->pub_key, wpabuf_put(pubkey, publen));
	BN_bn2bin(dh->priv_key, wpabuf_put(privkey, privlen));

	*priv = privkey;
	*publ = pubkey;
	return dh;

err:
	wpabuf_free(pubkey);
	wpabuf_free(privkey);
	DH_free(dh);
	return NULL;
}


struct wpabuf * dh5_derive_shared(void *ctx, const struct wpabuf *peer_public,
				  const struct wpabuf *own_private)
{
	BIGNUM *pub_key;
	struct wpabuf *res = NULL;
	size_t rlen;
	DH *dh = ctx;
	int keylen;

	if (ctx == NULL)
		return NULL;

	pub_key = BN_bin2bn(wpabuf_head(peer_public), wpabuf_len(peer_public),
			    NULL);
	if (pub_key == NULL)
		return NULL;

	rlen = DH_size(dh);
	res = wpabuf_alloc(rlen);
	if (res == NULL)
		goto err;

	keylen = DH_compute_key(wpabuf_mhead(res), pub_key, dh);
	if (keylen < 0)
		goto err;
	wpabuf_put(res, keylen);
	BN_free(pub_key);

	return res;

err:
	BN_free(pub_key);
	wpabuf_free(res);
	return NULL;
}


void dh5_free(void *ctx)
{
	DH *dh;
	if (ctx == NULL)
		return;
	dh = ctx;
	DH_free(dh);
}
