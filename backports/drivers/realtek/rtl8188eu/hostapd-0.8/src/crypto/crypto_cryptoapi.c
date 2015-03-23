/*
 * Crypto wrapper for Microsoft CryptoAPI
 * Copyright (c) 2005-2009, Jouni Malinen <j@w1.fi>
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
#include <windows.h>
#include <wincrypt.h>

#include "common.h"
#include "crypto.h"

#ifndef MS_ENH_RSA_AES_PROV
#ifdef UNICODE
#define MS_ENH_RSA_AES_PROV \
L"Microsoft Enhanced RSA and AES Cryptographic Provider (Prototype)"
#else
#define MS_ENH_RSA_AES_PROV \
"Microsoft Enhanced RSA and AES Cryptographic Provider (Prototype)"
#endif
#endif /* MS_ENH_RSA_AES_PROV */

#ifndef CALG_HMAC
#define CALG_HMAC (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_HMAC)
#endif

#ifdef __MINGW32_VERSION
/*
 * MinGW does not yet include all the needed definitions for CryptoAPI, so
 * define here whatever extra is needed.
 */

static BOOL WINAPI
(*CryptImportPublicKeyInfo)(HCRYPTPROV hCryptProv, DWORD dwCertEncodingType,
			    PCERT_PUBLIC_KEY_INFO pInfo, HCRYPTKEY *phKey)
= NULL; /* to be loaded from crypt32.dll */


static int mingw_load_crypto_func(void)
{
	HINSTANCE dll;

	/* MinGW does not yet have full CryptoAPI support, so load the needed
	 * function here. */

	if (CryptImportPublicKeyInfo)
		return 0;

	dll = LoadLibrary("crypt32");
	if (dll == NULL) {
		wpa_printf(MSG_DEBUG, "CryptoAPI: Could not load crypt32 "
			   "library");
		return -1;
	}

	CryptImportPublicKeyInfo = GetProcAddress(
		dll, "CryptImportPublicKeyInfo");
	if (CryptImportPublicKeyInfo == NULL) {
		wpa_printf(MSG_DEBUG, "CryptoAPI: Could not get "
			   "CryptImportPublicKeyInfo() address from "
			   "crypt32 library");
		return -1;
	}

	return 0;
}

#else /* __MINGW32_VERSION */

static int mingw_load_crypto_func(void)
{
	return 0;
}

#endif /* __MINGW32_VERSION */


static void cryptoapi_report_error(const char *msg)
{
	char *s, *pos;
	DWORD err = GetLastError();

	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			  FORMAT_MESSAGE_FROM_SYSTEM,
			  NULL, err, 0, (LPTSTR) &s, 0, NULL) == 0) {
 		wpa_printf(MSG_DEBUG, "CryptoAPI: %s: %d", msg, (int) err);
	}

	pos = s;
	while (*pos) {
		if (*pos == '\n' || *pos == '\r') {
			*pos = '\0';
			break;
		}
		pos++;
	}

	wpa_printf(MSG_DEBUG, "CryptoAPI: %s: %d: (%s)", msg, (int) err, s);
	LocalFree(s);
}


int cryptoapi_hash_vector(ALG_ID alg, size_t hash_len, size_t num_elem,
			  const u8 *addr[], const size_t *len, u8 *mac)
{
	HCRYPTPROV prov;
	HCRYPTHASH hash;
	size_t i;
	DWORD hlen;
	int ret = 0;

	if (!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, 0)) {
		cryptoapi_report_error("CryptAcquireContext");
		return -1;
	}

	if (!CryptCreateHash(prov, alg, 0, 0, &hash)) {
		cryptoapi_report_error("CryptCreateHash");
		CryptReleaseContext(prov, 0);
		return -1;
	}

	for (i = 0; i < num_elem; i++) {
		if (!CryptHashData(hash, (BYTE *) addr[i], len[i], 0)) {
			cryptoapi_report_error("CryptHashData");
			CryptDestroyHash(hash);
			CryptReleaseContext(prov, 0);
		}
	}

	hlen = hash_len;
	if (!CryptGetHashParam(hash, HP_HASHVAL, mac, &hlen, 0)) {
		cryptoapi_report_error("CryptGetHashParam");
		ret = -1;
	}

	CryptDestroyHash(hash);
	CryptReleaseContext(prov, 0);

	return ret;
}


int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return cryptoapi_hash_vector(CALG_MD4, 16, num_elem, addr, len, mac);
}


void des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
	u8 next, tmp;
	int i;
	HCRYPTPROV prov;
	HCRYPTKEY ckey;
	DWORD dlen;
	struct {
		BLOBHEADER hdr;
		DWORD len;
		BYTE key[8];
	} key_blob;
	DWORD mode = CRYPT_MODE_ECB;

	key_blob.hdr.bType = PLAINTEXTKEYBLOB;
	key_blob.hdr.bVersion = CUR_BLOB_VERSION;
	key_blob.hdr.reserved = 0;
	key_blob.hdr.aiKeyAlg = CALG_DES;
	key_blob.len = 8;

	/* Add parity bits to the key */
	next = 0;
	for (i = 0; i < 7; i++) {
		tmp = key[i];
		key_blob.key[i] = (tmp >> i) | next | 1;
		next = tmp << (7 - i);
	}
	key_blob.key[i] = next | 1;

	if (!CryptAcquireContext(&prov, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL,
				 CRYPT_VERIFYCONTEXT)) {
 		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptAcquireContext failed: "
			   "%d", (int) GetLastError());
		return;
	}

	if (!CryptImportKey(prov, (BYTE *) &key_blob, sizeof(key_blob), 0, 0,
			    &ckey)) {
 		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptImportKey failed: %d",
			   (int) GetLastError());
		CryptReleaseContext(prov, 0);
		return;
	}

	if (!CryptSetKeyParam(ckey, KP_MODE, (BYTE *) &mode, 0)) {
 		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptSetKeyParam(KP_MODE) "
			   "failed: %d", (int) GetLastError());
		CryptDestroyKey(ckey);
		CryptReleaseContext(prov, 0);
		return;
	}

	os_memcpy(cypher, clear, 8);
	dlen = 8;
	if (!CryptEncrypt(ckey, 0, FALSE, 0, cypher, &dlen, 8)) {
		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptEncrypt failed: %d",
			   (int) GetLastError());
		os_memset(cypher, 0, 8);
	}

	CryptDestroyKey(ckey);
	CryptReleaseContext(prov, 0);
}


int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return cryptoapi_hash_vector(CALG_MD5, 16, num_elem, addr, len, mac);
}


int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	return cryptoapi_hash_vector(CALG_SHA, 20, num_elem, addr, len, mac);
}


struct aes_context {
	HCRYPTPROV prov;
	HCRYPTKEY ckey;
};


void * aes_encrypt_init(const u8 *key, size_t len)
{
	struct aes_context *akey;
	struct {
		BLOBHEADER hdr;
		DWORD len;
		BYTE key[16];
	} key_blob;
	DWORD mode = CRYPT_MODE_ECB;

	if (len != 16)
		return NULL;

	key_blob.hdr.bType = PLAINTEXTKEYBLOB;
	key_blob.hdr.bVersion = CUR_BLOB_VERSION;
	key_blob.hdr.reserved = 0;
	key_blob.hdr.aiKeyAlg = CALG_AES_128;
	key_blob.len = len;
	os_memcpy(key_blob.key, key, len);

	akey = os_zalloc(sizeof(*akey));
	if (akey == NULL)
		return NULL;

	if (!CryptAcquireContext(&akey->prov, NULL,
				 MS_ENH_RSA_AES_PROV, PROV_RSA_AES,
				 CRYPT_VERIFYCONTEXT)) {
 		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptAcquireContext failed: "
			   "%d", (int) GetLastError());
		os_free(akey);
		return NULL;
	}

	if (!CryptImportKey(akey->prov, (BYTE *) &key_blob, sizeof(key_blob),
			    0, 0, &akey->ckey)) {
 		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptImportKey failed: %d",
			   (int) GetLastError());
		CryptReleaseContext(akey->prov, 0);
		os_free(akey);
		return NULL;
	}

	if (!CryptSetKeyParam(akey->ckey, KP_MODE, (BYTE *) &mode, 0)) {
 		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptSetKeyParam(KP_MODE) "
			   "failed: %d", (int) GetLastError());
		CryptDestroyKey(akey->ckey);
		CryptReleaseContext(akey->prov, 0);
		os_free(akey);
		return NULL;
	}

	return akey;
}


void aes_encrypt(void *ctx, const u8 *plain, u8 *crypt)
{
	struct aes_context *akey = ctx;
	DWORD dlen;

	os_memcpy(crypt, plain, 16);
	dlen = 16;
	if (!CryptEncrypt(akey->ckey, 0, FALSE, 0, crypt, &dlen, 16)) {
		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptEncrypt failed: %d",
			   (int) GetLastError());
		os_memset(crypt, 0, 16);
	}
}


void aes_encrypt_deinit(void *ctx)
{
	struct aes_context *akey = ctx;
	if (akey) {
		CryptDestroyKey(akey->ckey);
		CryptReleaseContext(akey->prov, 0);
		os_free(akey);
	}
}


void * aes_decrypt_init(const u8 *key, size_t len)
{
	return aes_encrypt_init(key, len);
}


void aes_decrypt(void *ctx, const u8 *crypt, u8 *plain)
{
	struct aes_context *akey = ctx;
	DWORD dlen;

	os_memcpy(plain, crypt, 16);
	dlen = 16;

	if (!CryptDecrypt(akey->ckey, 0, FALSE, 0, plain, &dlen)) {
		wpa_printf(MSG_DEBUG, "CryptoAPI: CryptDecrypt failed: %d",
			   (int) GetLastError());
	}
}


void aes_decrypt_deinit(void *ctx)
{
	aes_encrypt_deinit(ctx);
}


struct crypto_hash {
	enum crypto_hash_alg alg;
	int error;
	HCRYPTPROV prov;
	HCRYPTHASH hash;
	HCRYPTKEY key;
};

struct crypto_hash * crypto_hash_init(enum crypto_hash_alg alg, const u8 *key,
				      size_t key_len)
{
	struct crypto_hash *ctx;
	ALG_ID calg;
	struct {
		BLOBHEADER hdr;
		DWORD len;
		BYTE key[32];
	} key_blob;

	os_memset(&key_blob, 0, sizeof(key_blob));
	switch (alg) {
	case CRYPTO_HASH_ALG_MD5:
		calg = CALG_MD5;
		break;
	case CRYPTO_HASH_ALG_SHA1:
		calg = CALG_SHA;
		break;
	case CRYPTO_HASH_ALG_HMAC_MD5:
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		calg = CALG_HMAC;
		key_blob.hdr.bType = PLAINTEXTKEYBLOB;
		key_blob.hdr.bVersion = CUR_BLOB_VERSION;
		key_blob.hdr.reserved = 0;
		/*
		 * Note: RC2 is not really used, but that can be used to
		 * import HMAC keys of up to 16 byte long.
		 * CRYPT_IPSEC_HMAC_KEY flag for CryptImportKey() is needed to
		 * be able to import longer keys (HMAC-SHA1 uses 20-byte key).
		 */
		key_blob.hdr.aiKeyAlg = CALG_RC2;
		key_blob.len = key_len;
		if (key_len > sizeof(key_blob.key))
			return NULL;
		os_memcpy(key_blob.key, key, key_len);
		break;
	default:
		return NULL;
	}

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	ctx->alg = alg;

	if (!CryptAcquireContext(&ctx->prov, NULL, NULL, PROV_RSA_FULL, 0)) {
		cryptoapi_report_error("CryptAcquireContext");
		os_free(ctx);
		return NULL;
	}

	if (calg == CALG_HMAC) {
#ifndef CRYPT_IPSEC_HMAC_KEY
#define CRYPT_IPSEC_HMAC_KEY 0x00000100
#endif
		if (!CryptImportKey(ctx->prov, (BYTE *) &key_blob,
				    sizeof(key_blob), 0, CRYPT_IPSEC_HMAC_KEY,
				    &ctx->key)) {
			cryptoapi_report_error("CryptImportKey");
			CryptReleaseContext(ctx->prov, 0);
			os_free(ctx);
			return NULL;
		}
	}

	if (!CryptCreateHash(ctx->prov, calg, ctx->key, 0, &ctx->hash)) {
		cryptoapi_report_error("CryptCreateHash");
		CryptReleaseContext(ctx->prov, 0);
		os_free(ctx);
		return NULL;
	}

	if (calg == CALG_HMAC) {
		HMAC_INFO info;
		os_memset(&info, 0, sizeof(info));
		switch (alg) {
		case CRYPTO_HASH_ALG_HMAC_MD5:
			info.HashAlgid = CALG_MD5;
			break;
		case CRYPTO_HASH_ALG_HMAC_SHA1:
			info.HashAlgid = CALG_SHA;
			break;
		default:
			/* unreachable */
			break;
		}

		if (!CryptSetHashParam(ctx->hash, HP_HMAC_INFO, (BYTE *) &info,
				       0)) {
			cryptoapi_report_error("CryptSetHashParam");
			CryptDestroyHash(ctx->hash);
			CryptReleaseContext(ctx->prov, 0);
			os_free(ctx);
			return NULL;
		}
	}

	return ctx;
}


void crypto_hash_update(struct crypto_hash *ctx, const u8 *data, size_t len)
{
	if (ctx == NULL || ctx->error)
		return;

	if (!CryptHashData(ctx->hash, (BYTE *) data, len, 0)) {
		cryptoapi_report_error("CryptHashData");
		ctx->error = 1;
	}
}


int crypto_hash_finish(struct crypto_hash *ctx, u8 *mac, size_t *len)
{
	int ret = 0;
	DWORD hlen;

	if (ctx == NULL)
		return -2;

	if (mac == NULL || len == NULL)
		goto done;

	if (ctx->error) {
		ret = -2;
		goto done;
	}

	hlen = *len;
	if (!CryptGetHashParam(ctx->hash, HP_HASHVAL, mac, &hlen, 0)) {
		cryptoapi_report_error("CryptGetHashParam");
		ret = -2;
	}
	*len = hlen;

done:
	if (ctx->alg == CRYPTO_HASH_ALG_HMAC_SHA1 ||
	    ctx->alg == CRYPTO_HASH_ALG_HMAC_MD5)
		CryptDestroyKey(ctx->key);

	os_free(ctx);

	return ret;
}


struct crypto_cipher {
	HCRYPTPROV prov;
	HCRYPTKEY key;
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{	
	struct crypto_cipher *ctx;
	struct {
		BLOBHEADER hdr;
		DWORD len;
		BYTE key[32];
	} key_blob;
	DWORD mode = CRYPT_MODE_CBC;

	key_blob.hdr.bType = PLAINTEXTKEYBLOB;
	key_blob.hdr.bVersion = CUR_BLOB_VERSION;
	key_blob.hdr.reserved = 0;
	key_blob.len = key_len;
	if (key_len > sizeof(key_blob.key))
		return NULL;
	os_memcpy(key_blob.key, key, key_len);

	switch (alg) {
	case CRYPTO_CIPHER_ALG_AES:
		if (key_len == 32)
			key_blob.hdr.aiKeyAlg = CALG_AES_256;
		else if (key_len == 24)
			key_blob.hdr.aiKeyAlg = CALG_AES_192;
		else
			key_blob.hdr.aiKeyAlg = CALG_AES_128;
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		key_blob.hdr.aiKeyAlg = CALG_3DES;
		break;
	case CRYPTO_CIPHER_ALG_DES:
		key_blob.hdr.aiKeyAlg = CALG_DES;
		break;
	case CRYPTO_CIPHER_ALG_RC2:
		key_blob.hdr.aiKeyAlg = CALG_RC2;
		break;
	case CRYPTO_CIPHER_ALG_RC4:
		key_blob.hdr.aiKeyAlg = CALG_RC4;
		break;
	default:
		return NULL;
	}

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	if (!CryptAcquireContext(&ctx->prov, NULL, MS_ENH_RSA_AES_PROV,
				 PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		cryptoapi_report_error("CryptAcquireContext");
		goto fail1;
	}

	if (!CryptImportKey(ctx->prov, (BYTE *) &key_blob,
			    sizeof(key_blob), 0, 0, &ctx->key)) {
 		cryptoapi_report_error("CryptImportKey");
		goto fail2;
	}

	if (!CryptSetKeyParam(ctx->key, KP_MODE, (BYTE *) &mode, 0)) {
 		cryptoapi_report_error("CryptSetKeyParam(KP_MODE)");
		goto fail3;
	}

	if (iv && !CryptSetKeyParam(ctx->key, KP_IV, (BYTE *) iv, 0)) {
 		cryptoapi_report_error("CryptSetKeyParam(KP_IV)");
		goto fail3;
	}

	return ctx;

fail3:
	CryptDestroyKey(ctx->key);
fail2:
	CryptReleaseContext(ctx->prov, 0);
fail1:
	os_free(ctx);
	return NULL;
}


int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	DWORD dlen;

	os_memcpy(crypt, plain, len);
	dlen = len;
	if (!CryptEncrypt(ctx->key, 0, FALSE, 0, crypt, &dlen, len)) {
 		cryptoapi_report_error("CryptEncrypt");
		os_memset(crypt, 0, len);
		return -1;
	}

	return 0;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	DWORD dlen;

	os_memcpy(plain, crypt, len);
	dlen = len;
	if (!CryptDecrypt(ctx->key, 0, FALSE, 0, plain, &dlen)) {
 		cryptoapi_report_error("CryptDecrypt");
		return -1;
	}

	return 0;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	CryptDestroyKey(ctx->key);
	CryptReleaseContext(ctx->prov, 0);
	os_free(ctx);
}


struct crypto_public_key {
	HCRYPTPROV prov;
	HCRYPTKEY rsa;
};

struct crypto_private_key {
	HCRYPTPROV prov;
	HCRYPTKEY rsa;
};


struct crypto_public_key * crypto_public_key_import(const u8 *key, size_t len)
{
	/* Use crypto_public_key_from_cert() instead. */
	return NULL;
}


struct crypto_private_key * crypto_private_key_import(const u8 *key,
						      size_t len,
						      const char *passwd)
{
	/* TODO */
	return NULL;
}


struct crypto_public_key * crypto_public_key_from_cert(const u8 *buf,
						       size_t len)
{
	struct crypto_public_key *pk;
	PCCERT_CONTEXT cc;

	pk = os_zalloc(sizeof(*pk));
	if (pk == NULL)
		return NULL;

	cc = CertCreateCertificateContext(X509_ASN_ENCODING |
					  PKCS_7_ASN_ENCODING, buf, len);
	if (!cc) {
 		cryptoapi_report_error("CryptCreateCertificateContext");
		os_free(pk);
		return NULL;
	}

	if (!CryptAcquireContext(&pk->prov, NULL, MS_DEF_PROV, PROV_RSA_FULL,
				 0)) {
 		cryptoapi_report_error("CryptAcquireContext");
		os_free(pk);
		CertFreeCertificateContext(cc);
		return NULL;
	}

	if (!CryptImportPublicKeyInfo(pk->prov, X509_ASN_ENCODING |
				      PKCS_7_ASN_ENCODING,
				      &cc->pCertInfo->SubjectPublicKeyInfo,
				      &pk->rsa)) {
 		cryptoapi_report_error("CryptImportPublicKeyInfo");
		CryptReleaseContext(pk->prov, 0);
		os_free(pk);
		CertFreeCertificateContext(cc);
		return NULL;
	}

	CertFreeCertificateContext(cc);

	return pk;
}


int crypto_public_key_encrypt_pkcs1_v15(struct crypto_public_key *key,
					const u8 *in, size_t inlen,
					u8 *out, size_t *outlen)
{
	DWORD clen;
	u8 *tmp;
	size_t i;

	if (*outlen < inlen)
		return -1;
	tmp = malloc(*outlen);
	if (tmp == NULL)
		return -1;

	os_memcpy(tmp, in, inlen);
	clen = inlen;
	if (!CryptEncrypt(key->rsa, 0, TRUE, 0, tmp, &clen, *outlen)) {
		wpa_printf(MSG_DEBUG, "CryptoAPI: Failed to encrypt using "
			   "public key: %d", (int) GetLastError());
		os_free(tmp);
		return -1;
	}

	*outlen = clen;

	/* Reverse the output */
	for (i = 0; i < *outlen; i++)
		out[i] = tmp[*outlen - 1 - i];

	os_free(tmp);

	return 0;
}


int crypto_private_key_sign_pkcs1(struct crypto_private_key *key,
				  const u8 *in, size_t inlen,
				  u8 *out, size_t *outlen)
{
	/* TODO */
	return -1;
}


void crypto_public_key_free(struct crypto_public_key *key)
{
	if (key) {
		CryptDestroyKey(key->rsa);
		CryptReleaseContext(key->prov, 0);
		os_free(key);
	}
}


void crypto_private_key_free(struct crypto_private_key *key)
{
	if (key) {
		CryptDestroyKey(key->rsa);
		CryptReleaseContext(key->prov, 0);
		os_free(key);
	}
}


int crypto_global_init(void)
{
	return mingw_load_crypto_func();
}


void crypto_global_deinit(void)
{
}


int crypto_mod_exp(const u8 *base, size_t base_len,
		   const u8 *power, size_t power_len,
		   const u8 *modulus, size_t modulus_len,
		   u8 *result, size_t *result_len)
{
	/* TODO */
	return -1;
}
