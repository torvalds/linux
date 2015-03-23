/*
 * Crypto wrapper functions for NSS
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
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
#include <nspr/prtypes.h>
#include <nspr/plarenas.h>
#include <nspr/plhash.h>
#include <nspr/prtime.h>
#include <nspr/prinrval.h>
#include <nspr/prclist.h>
#include <nspr/prlock.h>
#include <nss/sechash.h>
#include <nss/pk11pub.h>

#include "common.h"
#include "crypto.h"


static int nss_hash(HASH_HashType type, unsigned int max_res_len,
		    size_t num_elem, const u8 *addr[], const size_t *len,
		    u8 *mac)
{
	HASHContext *ctx;
	size_t i;
	unsigned int reslen;

	ctx = HASH_Create(type);
	if (ctx == NULL)
		return -1;

	HASH_Begin(ctx);
	for (i = 0; i < num_elem; i++)
		HASH_Update(ctx, addr[i], len[i]);
	HASH_End(ctx, mac, &reslen, max_res_len);
	HASH_Destroy(ctx);

	return 0;
}


void des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
	PK11Context *ctx = NULL;
	PK11SlotInfo *slot;
	SECItem *param = NULL;
	PK11SymKey *symkey = NULL;
	SECItem item;
	int olen;
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

	slot = PK11_GetBestSlot(CKM_DES_ECB, NULL);
	if (slot == NULL) {
		wpa_printf(MSG_ERROR, "NSS: PK11_GetBestSlot failed");
		goto out;
	}

	item.type = siBuffer;
	item.data = pkey;
	item.len = 8;
	symkey = PK11_ImportSymKey(slot, CKM_DES_ECB, PK11_OriginDerive,
				   CKA_ENCRYPT, &item, NULL);
	if (symkey == NULL) {
		wpa_printf(MSG_ERROR, "NSS: PK11_ImportSymKey failed");
		goto out;
	}

	param = PK11_GenerateNewParam(CKM_DES_ECB, symkey);
	if (param == NULL) {
		wpa_printf(MSG_ERROR, "NSS: PK11_GenerateNewParam failed");
		goto out;
	}

	ctx = PK11_CreateContextBySymKey(CKM_DES_ECB, CKA_ENCRYPT,
					 symkey, param);
	if (ctx == NULL) {
		wpa_printf(MSG_ERROR, "NSS: PK11_CreateContextBySymKey("
			   "CKM_DES_ECB) failed");
		goto out;
	}

	if (PK11_CipherOp(ctx, cypher, &olen, 8, (void *) clear, 8) !=
	    SECSuccess) {
		wpa_printf(MSG_ERROR, "NSS: PK11_CipherOp failed");
		goto out;
	}

out:
	if (ctx)
		PK11_DestroyContext(ctx, PR_TRUE);
	if (symkey)
		PK11_FreeSymKey(symkey);
	if (param)
		SECITEM_FreeItem(param, PR_TRUE);
}


int rc4_skip(const u8 *key, size_t keylen, size_t skip,
	     u8 *data, size_t data_len)
{
	return -1;
}


int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return nss_hash(HASH_AlgMD5, 16, num_elem, addr, len, mac);
}


int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return nss_hash(HASH_AlgSHA1, 20, num_elem, addr, len, mac);
}


int sha256_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	return nss_hash(HASH_AlgSHA256, 32, num_elem, addr, len, mac);
}


void * aes_encrypt_init(const u8 *key, size_t len)
{
	return NULL;
}


void aes_encrypt(void *ctx, const u8 *plain, u8 *crypt)
{
}


void aes_encrypt_deinit(void *ctx)
{
}


void * aes_decrypt_init(const u8 *key, size_t len)
{
	return NULL;
}


void aes_decrypt(void *ctx, const u8 *crypt, u8 *plain)
{
}


void aes_decrypt_deinit(void *ctx)
{
}


int crypto_mod_exp(const u8 *base, size_t base_len,
		   const u8 *power, size_t power_len,
		   const u8 *modulus, size_t modulus_len,
		   u8 *result, size_t *result_len)
{
	return -1;
}


struct crypto_cipher {
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	return NULL;
}


int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	return -1;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	return -1;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
}
