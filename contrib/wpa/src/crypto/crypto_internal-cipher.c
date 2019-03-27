/*
 * Crypto wrapper for internal crypto implementation - Cipher wrappers
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto.h"
#include "aes.h"
#include "des_i.h"


struct crypto_cipher {
	enum crypto_cipher_alg alg;
	union {
		struct {
			size_t used_bytes;
			u8 key[16];
			size_t keylen;
		} rc4;
		struct {
			u8 cbc[32];
			void *ctx_enc;
			void *ctx_dec;
		} aes;
		struct {
			struct des3_key_s key;
			u8 cbc[8];
		} des3;
		struct {
			u32 ek[32];
			u32 dk[32];
			u8 cbc[8];
		} des;
	} u;
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	struct crypto_cipher *ctx;

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	ctx->alg = alg;

	switch (alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		if (key_len > sizeof(ctx->u.rc4.key)) {
			os_free(ctx);
			return NULL;
		}
		ctx->u.rc4.keylen = key_len;
		os_memcpy(ctx->u.rc4.key, key, key_len);
		break;
	case CRYPTO_CIPHER_ALG_AES:
		ctx->u.aes.ctx_enc = aes_encrypt_init(key, key_len);
		if (ctx->u.aes.ctx_enc == NULL) {
			os_free(ctx);
			return NULL;
		}
		ctx->u.aes.ctx_dec = aes_decrypt_init(key, key_len);
		if (ctx->u.aes.ctx_dec == NULL) {
			aes_encrypt_deinit(ctx->u.aes.ctx_enc);
			os_free(ctx);
			return NULL;
		}
		os_memcpy(ctx->u.aes.cbc, iv, AES_BLOCK_SIZE);
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		if (key_len != 24) {
			os_free(ctx);
			return NULL;
		}
		des3_key_setup(key, &ctx->u.des3.key);
		os_memcpy(ctx->u.des3.cbc, iv, 8);
		break;
	case CRYPTO_CIPHER_ALG_DES:
		if (key_len != 8) {
			os_free(ctx);
			return NULL;
		}
		des_key_setup(key, ctx->u.des.ek, ctx->u.des.dk);
		os_memcpy(ctx->u.des.cbc, iv, 8);
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
	size_t i, j, blocks;

	switch (ctx->alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		if (plain != crypt)
			os_memcpy(crypt, plain, len);
		rc4_skip(ctx->u.rc4.key, ctx->u.rc4.keylen,
			 ctx->u.rc4.used_bytes, crypt, len);
		ctx->u.rc4.used_bytes += len;
		break;
	case CRYPTO_CIPHER_ALG_AES:
		if (len % AES_BLOCK_SIZE)
			return -1;
		blocks = len / AES_BLOCK_SIZE;
		for (i = 0; i < blocks; i++) {
			for (j = 0; j < AES_BLOCK_SIZE; j++)
				ctx->u.aes.cbc[j] ^= plain[j];
			aes_encrypt(ctx->u.aes.ctx_enc, ctx->u.aes.cbc,
				    ctx->u.aes.cbc);
			os_memcpy(crypt, ctx->u.aes.cbc, AES_BLOCK_SIZE);
			plain += AES_BLOCK_SIZE;
			crypt += AES_BLOCK_SIZE;
		}
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		if (len % 8)
			return -1;
		blocks = len / 8;
		for (i = 0; i < blocks; i++) {
			for (j = 0; j < 8; j++)
				ctx->u.des3.cbc[j] ^= plain[j];
			des3_encrypt(ctx->u.des3.cbc, &ctx->u.des3.key,
				     ctx->u.des3.cbc);
			os_memcpy(crypt, ctx->u.des3.cbc, 8);
			plain += 8;
			crypt += 8;
		}
		break;
	case CRYPTO_CIPHER_ALG_DES:
		if (len % 8)
			return -1;
		blocks = len / 8;
		for (i = 0; i < blocks; i++) {
			for (j = 0; j < 8; j++)
				ctx->u.des3.cbc[j] ^= plain[j];
			des_block_encrypt(ctx->u.des.cbc, ctx->u.des.ek,
					  ctx->u.des.cbc);
			os_memcpy(crypt, ctx->u.des.cbc, 8);
			plain += 8;
			crypt += 8;
		}
		break;
	default:
		return -1;
	}

	return 0;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	size_t i, j, blocks;
	u8 tmp[32];

	switch (ctx->alg) {
	case CRYPTO_CIPHER_ALG_RC4:
		if (plain != crypt)
			os_memcpy(plain, crypt, len);
		rc4_skip(ctx->u.rc4.key, ctx->u.rc4.keylen,
			 ctx->u.rc4.used_bytes, plain, len);
		ctx->u.rc4.used_bytes += len;
		break;
	case CRYPTO_CIPHER_ALG_AES:
		if (len % AES_BLOCK_SIZE)
			return -1;
		blocks = len / AES_BLOCK_SIZE;
		for (i = 0; i < blocks; i++) {
			os_memcpy(tmp, crypt, AES_BLOCK_SIZE);
			aes_decrypt(ctx->u.aes.ctx_dec, crypt, plain);
			for (j = 0; j < AES_BLOCK_SIZE; j++)
				plain[j] ^= ctx->u.aes.cbc[j];
			os_memcpy(ctx->u.aes.cbc, tmp, AES_BLOCK_SIZE);
			plain += AES_BLOCK_SIZE;
			crypt += AES_BLOCK_SIZE;
		}
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		if (len % 8)
			return -1;
		blocks = len / 8;
		for (i = 0; i < blocks; i++) {
			os_memcpy(tmp, crypt, 8);
			des3_decrypt(crypt, &ctx->u.des3.key, plain);
			for (j = 0; j < 8; j++)
				plain[j] ^= ctx->u.des3.cbc[j];
			os_memcpy(ctx->u.des3.cbc, tmp, 8);
			plain += 8;
			crypt += 8;
		}
		break;
	case CRYPTO_CIPHER_ALG_DES:
		if (len % 8)
			return -1;
		blocks = len / 8;
		for (i = 0; i < blocks; i++) {
			os_memcpy(tmp, crypt, 8);
			des_block_decrypt(crypt, ctx->u.des.dk, plain);
			for (j = 0; j < 8; j++)
				plain[j] ^= ctx->u.des.cbc[j];
			os_memcpy(ctx->u.des.cbc, tmp, 8);
			plain += 8;
			crypt += 8;
		}
		break;
	default:
		return -1;
	}

	return 0;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	switch (ctx->alg) {
	case CRYPTO_CIPHER_ALG_AES:
		aes_encrypt_deinit(ctx->u.aes.ctx_enc);
		aes_decrypt_deinit(ctx->u.aes.ctx_dec);
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		break;
	default:
		break;
	}
	os_free(ctx);
}
