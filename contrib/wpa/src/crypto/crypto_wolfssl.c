/*
 * Wrapper functions for libwolfssl
 * Copyright (c) 2004-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto.h"

/* wolfSSL headers */
#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/md4.h>
#include <wolfssl/wolfcrypt/md5.h>
#include <wolfssl/wolfcrypt/sha.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/sha512.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/pwdbased.h>
#include <wolfssl/wolfcrypt/arc4.h>
#include <wolfssl/wolfcrypt/des3.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/dh.h>
#include <wolfssl/wolfcrypt/cmac.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/openssl/bn.h>


#ifndef CONFIG_FIPS

int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	Md4 md4;
	size_t i;

	if (TEST_FAIL())
		return -1;

	wc_InitMd4(&md4);

	for (i = 0; i < num_elem; i++)
		wc_Md4Update(&md4, addr[i], len[i]);

	wc_Md4Final(&md4, mac);

	return 0;
}


int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	wc_Md5 md5;
	size_t i;

	if (TEST_FAIL())
		return -1;

	wc_InitMd5(&md5);

	for (i = 0; i < num_elem; i++)
		wc_Md5Update(&md5, addr[i], len[i]);

	wc_Md5Final(&md5, mac);

	return 0;
}

#endif /* CONFIG_FIPS */


int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	wc_Sha sha;
	size_t i;

	if (TEST_FAIL())
		return -1;

	wc_InitSha(&sha);

	for (i = 0; i < num_elem; i++)
		wc_ShaUpdate(&sha, addr[i], len[i]);

	wc_ShaFinal(&sha, mac);

	return 0;
}


#ifndef NO_SHA256_WRAPPER
int sha256_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	wc_Sha256 sha256;
	size_t i;

	if (TEST_FAIL())
		return -1;

	wc_InitSha256(&sha256);

	for (i = 0; i < num_elem; i++)
		wc_Sha256Update(&sha256, addr[i], len[i]);

	wc_Sha256Final(&sha256, mac);

	return 0;
}
#endif /* NO_SHA256_WRAPPER */


#ifdef CONFIG_SHA384
int sha384_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	wc_Sha384 sha384;
	size_t i;

	if (TEST_FAIL())
		return -1;

	wc_InitSha384(&sha384);

	for (i = 0; i < num_elem; i++)
		wc_Sha384Update(&sha384, addr[i], len[i]);

	wc_Sha384Final(&sha384, mac);

	return 0;
}
#endif /* CONFIG_SHA384 */


#ifdef CONFIG_SHA512
int sha512_vector(size_t num_elem, const u8 *addr[], const size_t *len,
		  u8 *mac)
{
	wc_Sha512 sha512;
	size_t i;

	if (TEST_FAIL())
		return -1;

	wc_InitSha512(&sha512);

	for (i = 0; i < num_elem; i++)
		wc_Sha512Update(&sha512, addr[i], len[i]);

	wc_Sha512Final(&sha512, mac);

	return 0;
}
#endif /* CONFIG_SHA512 */


static int wolfssl_hmac_vector(int type, const u8 *key,
			       size_t key_len, size_t num_elem,
			       const u8 *addr[], const size_t *len, u8 *mac,
			       unsigned int mdlen)
{
	Hmac hmac;
	size_t i;

	(void) mdlen;

	if (TEST_FAIL())
		return -1;

	if (wc_HmacSetKey(&hmac, type, key, (word32) key_len) != 0)
		return -1;
	for (i = 0; i < num_elem; i++)
		if (wc_HmacUpdate(&hmac, addr[i], len[i]) != 0)
			return -1;
	if (wc_HmacFinal(&hmac, mac) != 0)
		return -1;
	return 0;
}


#ifndef CONFIG_FIPS

int hmac_md5_vector(const u8 *key, size_t key_len, size_t num_elem,
		    const u8 *addr[], const size_t *len, u8 *mac)
{
	return wolfssl_hmac_vector(WC_MD5, key, key_len, num_elem, addr, len,
				   mac, 16);
}


int hmac_md5(const u8 *key, size_t key_len, const u8 *data, size_t data_len,
	     u8 *mac)
{
	return hmac_md5_vector(key, key_len, 1, &data, &data_len, mac);
}

#endif /* CONFIG_FIPS */


int hmac_sha1_vector(const u8 *key, size_t key_len, size_t num_elem,
		     const u8 *addr[], const size_t *len, u8 *mac)
{
	return wolfssl_hmac_vector(WC_SHA, key, key_len, num_elem, addr, len,
				   mac, 20);
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
	return wolfssl_hmac_vector(WC_SHA256, key, key_len, num_elem, addr, len,
				   mac, 32);
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
	return wolfssl_hmac_vector(WC_SHA384, key, key_len, num_elem, addr, len,
				   mac, 48);
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
	return wolfssl_hmac_vector(WC_SHA512, key, key_len, num_elem, addr, len,
				   mac, 64);
}


int hmac_sha512(const u8 *key, size_t key_len, const u8 *data,
		size_t data_len, u8 *mac)
{
	return hmac_sha512_vector(key, key_len, 1, &data, &data_len, mac);
}

#endif /* CONFIG_SHA512 */


int pbkdf2_sha1(const char *passphrase, const u8 *ssid, size_t ssid_len,
		int iterations, u8 *buf, size_t buflen)
{
	if (wc_PBKDF2(buf, (const byte*)passphrase, os_strlen(passphrase), ssid,
		      ssid_len, iterations, buflen, WC_SHA) != 0)
		return -1;
	return 0;
}


#ifdef CONFIG_DES
int des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
	Des des;
	u8  pkey[8], next, tmp;
	int i;

	/* Add parity bits to the key */
	next = 0;
	for (i = 0; i < 7; i++) {
		tmp = key[i];
		pkey[i] = (tmp >> i) | next | 1;
		next = tmp << (7 - i);
	}
	pkey[i] = next | 1;

	wc_Des_SetKey(&des, pkey, NULL, DES_ENCRYPTION);
	wc_Des_EcbEncrypt(&des, cypher, clear, DES_BLOCK_SIZE);

	return 0;
}
#endif /* CONFIG_DES */


void * aes_encrypt_init(const u8 *key, size_t len)
{
	Aes *aes;

	if (TEST_FAIL())
		return NULL;

	aes = os_malloc(sizeof(Aes));
	if (!aes)
		return NULL;

	if (wc_AesSetKey(aes, key, len, NULL, AES_ENCRYPTION) < 0) {
		os_free(aes);
		return NULL;
	}

	return aes;
}


int aes_encrypt(void *ctx, const u8 *plain, u8 *crypt)
{
	wc_AesEncryptDirect(ctx, crypt, plain);
	return 0;
}


void aes_encrypt_deinit(void *ctx)
{
	os_free(ctx);
}


void * aes_decrypt_init(const u8 *key, size_t len)
{
	Aes *aes;

	if (TEST_FAIL())
		return NULL;

	aes = os_malloc(sizeof(Aes));
	if (!aes)
		return NULL;

	if (wc_AesSetKey(aes, key, len, NULL, AES_DECRYPTION) < 0) {
		os_free(aes);
		return NULL;
	}

	return aes;
}


int aes_decrypt(void *ctx, const u8 *crypt, u8 *plain)
{
	wc_AesDecryptDirect(ctx, plain, crypt);
	return 0;
}


void aes_decrypt_deinit(void *ctx)
{
	os_free(ctx);
}


int aes_128_cbc_encrypt(const u8 *key, const u8 *iv, u8 *data, size_t data_len)
{
	Aes aes;
	int ret;

	if (TEST_FAIL())
		return -1;

	ret = wc_AesSetKey(&aes, key, 16, iv, AES_ENCRYPTION);
	if (ret != 0)
		return -1;

	ret = wc_AesCbcEncrypt(&aes, data, data, data_len);
	if (ret != 0)
		return -1;
	return 0;
}


int aes_128_cbc_decrypt(const u8 *key, const u8 *iv, u8 *data, size_t data_len)
{
	Aes aes;
	int ret;

	if (TEST_FAIL())
		return -1;

	ret = wc_AesSetKey(&aes, key, 16, iv, AES_DECRYPTION);
	if (ret != 0)
		return -1;

	ret = wc_AesCbcDecrypt(&aes, data, data, data_len);
	if (ret != 0)
		return -1;
	return 0;
}


int aes_wrap(const u8 *kek, size_t kek_len, int n, const u8 *plain, u8 *cipher)
{
	int ret;

	if (TEST_FAIL())
		return -1;

	ret = wc_AesKeyWrap(kek, kek_len, plain, n * 8, cipher, (n + 1) * 8,
			    NULL);
	return ret != (n + 1) * 8 ? -1 : 0;
}


int aes_unwrap(const u8 *kek, size_t kek_len, int n, const u8 *cipher,
	       u8 *plain)
{
	int ret;

	if (TEST_FAIL())
		return -1;

	ret = wc_AesKeyUnWrap(kek, kek_len, cipher, (n + 1) * 8, plain, n * 8,
			      NULL);
	return ret != n * 8 ? -1 : 0;
}


#ifndef CONFIG_NO_RC4
int rc4_skip(const u8 *key, size_t keylen, size_t skip, u8 *data,
	     size_t data_len)
{
#ifndef NO_RC4
	Arc4 arc4;
	unsigned char skip_buf[16];

	wc_Arc4SetKey(&arc4, key, keylen);

	while (skip >= sizeof(skip_buf)) {
		size_t len = skip;

		if (len > sizeof(skip_buf))
			len = sizeof(skip_buf);
		wc_Arc4Process(&arc4, skip_buf, skip_buf, len);
		skip -= len;
	}

	wc_Arc4Process(&arc4, data, data, data_len);

	return 0;
#else /* NO_RC4 */
	return -1;
#endif /* NO_RC4 */
}
#endif /* CONFIG_NO_RC4 */


#if defined(EAP_IKEV2) || defined(EAP_IKEV2_DYNAMIC) \
		       || defined(EAP_SERVER_IKEV2)
union wolfssl_cipher {
	Aes aes;
	Des3 des3;
	Arc4 arc4;
};

struct crypto_cipher {
	enum crypto_cipher_alg alg;
	union wolfssl_cipher enc;
	union wolfssl_cipher dec;
};

struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	struct crypto_cipher *ctx;

	ctx = os_zalloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	switch (alg) {
#ifndef CONFIG_NO_RC4
#ifndef NO_RC4
	case CRYPTO_CIPHER_ALG_RC4:
		wc_Arc4SetKey(&ctx->enc.arc4, key, key_len);
		wc_Arc4SetKey(&ctx->dec.arc4, key, key_len);
		break;
#endif /* NO_RC4 */
#endif /* CONFIG_NO_RC4 */
#ifndef NO_AES
	case CRYPTO_CIPHER_ALG_AES:
		switch (key_len) {
		case 16:
		case 24:
		case 32:
			break;
		default:
			os_free(ctx);
			return NULL;
		}
		if (wc_AesSetKey(&ctx->enc.aes, key, key_len, iv,
				 AES_ENCRYPTION) ||
		    wc_AesSetKey(&ctx->dec.aes, key, key_len, iv,
				 AES_DECRYPTION)) {
			os_free(ctx);
			return NULL;
		}
		break;
#endif /* NO_AES */
#ifndef NO_DES3
	case CRYPTO_CIPHER_ALG_3DES:
		if (key_len != DES3_KEYLEN ||
		    wc_Des3_SetKey(&ctx->enc.des3, key, iv, DES_ENCRYPTION) ||
		    wc_Des3_SetKey(&ctx->dec.des3, key, iv, DES_DECRYPTION)) {
			os_free(ctx);
			return NULL;
		}
		break;
#endif /* NO_DES3 */
	case CRYPTO_CIPHER_ALG_RC2:
	case CRYPTO_CIPHER_ALG_DES:
	default:
		os_free(ctx);
		return NULL;
	}

	ctx->alg = alg;

	return ctx;
}


int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	switch (ctx->alg) {
#ifndef CONFIG_NO_RC4
#ifndef NO_RC4
	case CRYPTO_CIPHER_ALG_RC4:
		wc_Arc4Process(&ctx->enc.arc4, crypt, plain, len);
		return 0;
#endif /* NO_RC4 */
#endif /* CONFIG_NO_RC4 */
#ifndef NO_AES
	case CRYPTO_CIPHER_ALG_AES:
		if (wc_AesCbcEncrypt(&ctx->enc.aes, crypt, plain, len) != 0)
			return -1;
		return 0;
#endif /* NO_AES */
#ifndef NO_DES3
	case CRYPTO_CIPHER_ALG_3DES:
		if (wc_Des3_CbcEncrypt(&ctx->enc.des3, crypt, plain, len) != 0)
			return -1;
		return 0;
#endif /* NO_DES3 */
	default:
		return -1;
	}
	return -1;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	switch (ctx->alg) {
#ifndef CONFIG_NO_RC4
#ifndef NO_RC4
	case CRYPTO_CIPHER_ALG_RC4:
		wc_Arc4Process(&ctx->dec.arc4, plain, crypt, len);
		return 0;
#endif /* NO_RC4 */
#endif /* CONFIG_NO_RC4 */
#ifndef NO_AES
	case CRYPTO_CIPHER_ALG_AES:
		if (wc_AesCbcDecrypt(&ctx->dec.aes, plain, crypt, len) != 0)
			return -1;
		return 0;
#endif /* NO_AES */
#ifndef NO_DES3
	case CRYPTO_CIPHER_ALG_3DES:
		if (wc_Des3_CbcDecrypt(&ctx->dec.des3, plain, crypt, len) != 0)
			return -1;
		return 0;
#endif /* NO_DES3 */
	default:
		return -1;
	}
	return -1;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	os_free(ctx);
}

#endif


#ifdef CONFIG_WPS_NFC

static const unsigned char RFC3526_PRIME_1536[] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2,
	0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
	0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6,
	0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
	0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D,
	0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
	0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9,
	0xA6, 0x37, 0xED, 0x6B, 0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
	0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5, 0xAE, 0x9F, 0x24, 0x11,
	0x7C, 0x4B, 0x1F, 0xE6, 0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D,
	0xC2, 0x00, 0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05, 0x98, 0xDA, 0x48, 0x36,
	0x1C, 0x55, 0xD3, 0x9A, 0x69, 0x16, 0x3F, 0xA8, 0xFD, 0x24, 0xCF, 0x5F,
	0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96, 0x1C, 0x62, 0xF3, 0x56,
	0x20, 0x85, 0x52, 0xBB, 0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D,
	0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04, 0xF1, 0x74, 0x6C, 0x08,
	0xCA, 0x23, 0x73, 0x27, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const unsigned char RFC3526_GENERATOR_1536[] = {
	0x02
};

#define RFC3526_LEN sizeof(RFC3526_PRIME_1536)


void * dh5_init(struct wpabuf **priv, struct wpabuf **publ)
{
	WC_RNG rng;
	DhKey *ret = NULL;
	DhKey *dh = NULL;
	struct wpabuf *privkey = NULL;
	struct wpabuf *pubkey = NULL;
	word32 priv_sz, pub_sz;

	*priv = NULL;
	wpabuf_free(*publ);
	*publ = NULL;

	dh = XMALLOC(sizeof(DhKey), NULL, DYNAMIC_TYPE_TMP_BUFFER);
	if (!dh)
		return NULL;
	wc_InitDhKey(dh);

	if (wc_InitRng(&rng) != 0) {
		XFREE(dh, NULL, DYNAMIC_TYPE_TMP_BUFFER);
		return NULL;
	}

	privkey = wpabuf_alloc(RFC3526_LEN);
	pubkey = wpabuf_alloc(RFC3526_LEN);
	if (!privkey || !pubkey)
		goto done;

	if (wc_DhSetKey(dh, RFC3526_PRIME_1536, sizeof(RFC3526_PRIME_1536),
			RFC3526_GENERATOR_1536, sizeof(RFC3526_GENERATOR_1536))
	    != 0)
		goto done;

	if (wc_DhGenerateKeyPair(dh, &rng, wpabuf_mhead(privkey), &priv_sz,
				 wpabuf_mhead(pubkey), &pub_sz) != 0)
		goto done;

	wpabuf_put(privkey, priv_sz);
	wpabuf_put(pubkey, pub_sz);

	ret = dh;
	*priv = privkey;
	*publ = pubkey;
	dh = NULL;
	privkey = NULL;
	pubkey = NULL;
done:
	wpabuf_clear_free(pubkey);
	wpabuf_clear_free(privkey);
	if (dh) {
		wc_FreeDhKey(dh);
		XFREE(dh, NULL, DYNAMIC_TYPE_TMP_BUFFER);
	}
	wc_FreeRng(&rng);
	return ret;
}


void * dh5_init_fixed(const struct wpabuf *priv, const struct wpabuf *publ)
{
	DhKey *ret = NULL;
	DhKey *dh;
	byte *secret;
	word32 secret_sz;

	dh = XMALLOC(sizeof(DhKey), NULL, DYNAMIC_TYPE_TMP_BUFFER);
	if (!dh)
		return NULL;
	wc_InitDhKey(dh);

	secret = XMALLOC(RFC3526_LEN, NULL, DYNAMIC_TYPE_TMP_BUFFER);
	if (!secret)
		goto done;

	if (wc_DhSetKey(dh, RFC3526_PRIME_1536, sizeof(RFC3526_PRIME_1536),
			RFC3526_GENERATOR_1536, sizeof(RFC3526_GENERATOR_1536))
	    != 0)
		goto done;

	if (wc_DhAgree(dh, secret, &secret_sz, wpabuf_head(priv),
		       wpabuf_len(priv), RFC3526_GENERATOR_1536,
		       sizeof(RFC3526_GENERATOR_1536)) != 0)
		goto done;

	if (secret_sz != wpabuf_len(publ) ||
	    os_memcmp(secret, wpabuf_head(publ), secret_sz) != 0)
		goto done;

	ret = dh;
	dh = NULL;
done:
	if (dh) {
		wc_FreeDhKey(dh);
		XFREE(dh, NULL, DYNAMIC_TYPE_TMP_BUFFER);
	}
	XFREE(secret, NULL, DYNAMIC_TYPE_TMP_BUFFER);
	return ret;
}


struct wpabuf * dh5_derive_shared(void *ctx, const struct wpabuf *peer_public,
				  const struct wpabuf *own_private)
{
	struct wpabuf *ret = NULL;
	struct wpabuf *secret;
	word32 secret_sz;

	secret = wpabuf_alloc(RFC3526_LEN);
	if (!secret)
		goto done;

	if (wc_DhAgree(ctx, wpabuf_mhead(secret), &secret_sz,
		       wpabuf_head(own_private), wpabuf_len(own_private),
		       wpabuf_head(peer_public), wpabuf_len(peer_public)) != 0)
		goto done;

	wpabuf_put(secret, secret_sz);

	ret = secret;
	secret = NULL;
done:
	wpabuf_clear_free(secret);
	return ret;
}


void dh5_free(void *ctx)
{
	if (!ctx)
		return;

	wc_FreeDhKey(ctx);
	XFREE(ctx, NULL, DYNAMIC_TYPE_TMP_BUFFER);
}

#endif /* CONFIG_WPS_NFC */


int crypto_dh_init(u8 generator, const u8 *prime, size_t prime_len, u8 *privkey,
		   u8 *pubkey)
{
	int ret = -1;
	WC_RNG rng;
	DhKey *dh = NULL;
	word32 priv_sz, pub_sz;

	if (TEST_FAIL())
		return -1;

	dh = os_malloc(sizeof(DhKey));
	if (!dh)
		return -1;
	wc_InitDhKey(dh);

	if (wc_InitRng(&rng) != 0) {
		os_free(dh);
		return -1;
	}

	if (wc_DhSetKey(dh, prime, prime_len, &generator, 1) != 0)
		goto done;

	if (wc_DhGenerateKeyPair(dh, &rng, privkey, &priv_sz, pubkey, &pub_sz)
	    != 0)
		goto done;

	if (priv_sz < prime_len) {
		size_t pad_sz = prime_len - priv_sz;

		os_memmove(privkey + pad_sz, privkey, priv_sz);
		os_memset(privkey, 0, pad_sz);
	}

	if (pub_sz < prime_len) {
		size_t pad_sz = prime_len - pub_sz;

		os_memmove(pubkey + pad_sz, pubkey, pub_sz);
		os_memset(pubkey, 0, pad_sz);
	}
	ret = 0;
done:
	wc_FreeDhKey(dh);
	os_free(dh);
	wc_FreeRng(&rng);
	return ret;
}


int crypto_dh_derive_secret(u8 generator, const u8 *prime, size_t prime_len,
			    const u8 *privkey, size_t privkey_len,
			    const u8 *pubkey, size_t pubkey_len,
			    u8 *secret, size_t *len)
{
	int ret = -1;
	DhKey *dh;
	word32 secret_sz;

	dh = os_malloc(sizeof(DhKey));
	if (!dh)
		return -1;
	wc_InitDhKey(dh);

	if (wc_DhSetKey(dh, prime, prime_len, &generator, 1) != 0)
		goto done;

	if (wc_DhAgree(dh, secret, &secret_sz, privkey, privkey_len, pubkey,
		       pubkey_len) != 0)
		goto done;

	*len = secret_sz;
	ret = 0;
done:
	wc_FreeDhKey(dh);
	os_free(dh);
	return ret;
}


#ifdef CONFIG_FIPS
int crypto_get_random(void *buf, size_t len)
{
	int ret = 0;
	WC_RNG rng;

	if (wc_InitRng(&rng) != 0)
		return -1;
	if (wc_RNG_GenerateBlock(&rng, buf, len) != 0)
		ret = -1;
	wc_FreeRng(&rng);
	return ret;
}
#endif /* CONFIG_FIPS */


#if defined(EAP_PWD) || defined(EAP_SERVER_PWD)
struct crypto_hash {
	Hmac hmac;
	int size;
};


struct crypto_hash * crypto_hash_init(enum crypto_hash_alg alg, const u8 *key,
				      size_t key_len)
{
	struct crypto_hash *ret = NULL;
	struct crypto_hash *hash;
	int type;

	hash = os_zalloc(sizeof(*hash));
	if (!hash)
		goto done;

	switch (alg) {
#ifndef NO_MD5
	case CRYPTO_HASH_ALG_HMAC_MD5:
		hash->size = 16;
		type = WC_MD5;
		break;
#endif /* NO_MD5 */
#ifndef NO_SHA
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		type = WC_SHA;
		hash->size = 20;
		break;
#endif /* NO_SHA */
#ifdef CONFIG_SHA256
#ifndef NO_SHA256
	case CRYPTO_HASH_ALG_HMAC_SHA256:
		type = WC_SHA256;
		hash->size = 32;
		break;
#endif /* NO_SHA256 */
#endif /* CONFIG_SHA256 */
	default:
		goto done;
	}

	if (wc_HmacSetKey(&hash->hmac, type, key, key_len) != 0)
		goto done;

	ret = hash;
	hash = NULL;
done:
	os_free(hash);
	return ret;
}


void crypto_hash_update(struct crypto_hash *ctx, const u8 *data, size_t len)
{
	if (!ctx)
		return;
	wc_HmacUpdate(&ctx->hmac, data, len);
}


int crypto_hash_finish(struct crypto_hash *ctx, u8 *mac, size_t *len)
{
	int ret = 0;

	if (!ctx)
		return -2;

	if (!mac || !len)
		goto done;

	if (wc_HmacFinal(&ctx->hmac, mac) != 0) {
		ret = -1;
		goto done;
	}

	*len = ctx->size;
	ret = 0;
done:
	bin_clear_free(ctx, sizeof(*ctx));
	return ret;
}

#endif


int omac1_aes_vector(const u8 *key, size_t key_len, size_t num_elem,
		     const u8 *addr[], const size_t *len, u8 *mac)
{
	Cmac cmac;
	size_t i;
	word32 sz;

	if (TEST_FAIL())
		return -1;

	if (wc_InitCmac(&cmac, key, key_len, WC_CMAC_AES, NULL) != 0)
		return -1;

	for (i = 0; i < num_elem; i++)
		if (wc_CmacUpdate(&cmac, addr[i], len[i]) != 0)
			return -1;

	sz = AES_BLOCK_SIZE;
	if (wc_CmacFinal(&cmac, mac, &sz) != 0 || sz != AES_BLOCK_SIZE)
		return -1;

	return 0;
}


int omac1_aes_128_vector(const u8 *key, size_t num_elem,
			 const u8 *addr[], const size_t *len, u8 *mac)
{
	return omac1_aes_vector(key, 16, num_elem, addr, len, mac);
}


int omac1_aes_128(const u8 *key, const u8 *data, size_t data_len, u8 *mac)
{
	return omac1_aes_128_vector(key, 1, &data, &data_len, mac);
}


int omac1_aes_256(const u8 *key, const u8 *data, size_t data_len, u8 *mac)
{
	return omac1_aes_vector(key, 32, 1, &data, &data_len, mac);
}


struct crypto_bignum * crypto_bignum_init(void)
{
	mp_int *a;

	if (TEST_FAIL())
		return NULL;

	a = os_malloc(sizeof(*a));
	if (!a || mp_init(a) != MP_OKAY) {
		os_free(a);
		a = NULL;
	}

	return (struct crypto_bignum *) a;
}


struct crypto_bignum * crypto_bignum_init_set(const u8 *buf, size_t len)
{
	mp_int *a;

	if (TEST_FAIL())
		return NULL;

	a = (mp_int *) crypto_bignum_init();
	if (!a)
		return NULL;

	if (mp_read_unsigned_bin(a, buf, len) != MP_OKAY) {
		os_free(a);
		a = NULL;
	}

	return (struct crypto_bignum *) a;
}


void crypto_bignum_deinit(struct crypto_bignum *n, int clear)
{
	if (!n)
		return;

	if (clear)
		mp_forcezero((mp_int *) n);
	mp_clear((mp_int *) n);
	os_free((mp_int *) n);
}


int crypto_bignum_to_bin(const struct crypto_bignum *a,
			 u8 *buf, size_t buflen, size_t padlen)
{
	int num_bytes, offset;

	if (TEST_FAIL())
		return -1;

	if (padlen > buflen)
		return -1;

	num_bytes = (mp_count_bits((mp_int *) a) + 7) / 8;
	if ((size_t) num_bytes > buflen)
		return -1;
	if (padlen > (size_t) num_bytes)
		offset = padlen - num_bytes;
	else
		offset = 0;

	os_memset(buf, 0, offset);
	mp_to_unsigned_bin((mp_int *) a, buf + offset);

	return num_bytes + offset;
}


int crypto_bignum_rand(struct crypto_bignum *r, const struct crypto_bignum *m)
{
	int ret = 0;
	WC_RNG rng;

	if (wc_InitRng(&rng) != 0)
		return -1;
	if (mp_rand_prime((mp_int *) r,
			  (mp_count_bits((mp_int *) m) + 7) / 8 * 2,
			  &rng, NULL) != 0)
		ret = -1;
	if (ret == 0 &&
	    mp_mod((mp_int *) r, (mp_int *) m, (mp_int *) r) != 0)
		ret = -1;
	wc_FreeRng(&rng);
	return ret;
}


int crypto_bignum_add(const struct crypto_bignum *a,
		      const struct crypto_bignum *b,
		      struct crypto_bignum *r)
{
	return mp_add((mp_int *) a, (mp_int *) b,
		      (mp_int *) r) == MP_OKAY ? 0 : -1;
}


int crypto_bignum_mod(const struct crypto_bignum *a,
		      const struct crypto_bignum *m,
		      struct crypto_bignum *r)
{
	return mp_mod((mp_int *) a, (mp_int *) m,
		      (mp_int *) r) == MP_OKAY ? 0 : -1;
}


int crypto_bignum_exptmod(const struct crypto_bignum *b,
			  const struct crypto_bignum *e,
			  const struct crypto_bignum *m,
			  struct crypto_bignum *r)
{
	if (TEST_FAIL())
		return -1;

	return mp_exptmod((mp_int *) b, (mp_int *) e, (mp_int *) m,
			  (mp_int *) r) == MP_OKAY ?  0 : -1;
}


int crypto_bignum_inverse(const struct crypto_bignum *a,
			  const struct crypto_bignum *m,
			  struct crypto_bignum *r)
{
	if (TEST_FAIL())
		return -1;

	return mp_invmod((mp_int *) a, (mp_int *) m,
			 (mp_int *) r) == MP_OKAY ? 0 : -1;
}


int crypto_bignum_sub(const struct crypto_bignum *a,
		      const struct crypto_bignum *b,
		      struct crypto_bignum *r)
{
	if (TEST_FAIL())
		return -1;

	return mp_add((mp_int *) a, (mp_int *) b,
		      (mp_int *) r) == MP_OKAY ? 0 : -1;
}


int crypto_bignum_div(const struct crypto_bignum *a,
		      const struct crypto_bignum *b,
		      struct crypto_bignum *d)
{
	if (TEST_FAIL())
		return -1;

	return mp_div((mp_int *) a, (mp_int *) b, (mp_int *) d,
		      NULL) == MP_OKAY ? 0 : -1;
}


int crypto_bignum_mulmod(const struct crypto_bignum *a,
			 const struct crypto_bignum *b,
			 const struct crypto_bignum *m,
			 struct crypto_bignum *d)
{
	if (TEST_FAIL())
		return -1;

	return mp_mulmod((mp_int *) a, (mp_int *) b, (mp_int *) m,
			 (mp_int *) d) == MP_OKAY ?  0 : -1;
}


int crypto_bignum_rshift(const struct crypto_bignum *a, int n,
			 struct crypto_bignum *r)
{
	if (mp_copy((mp_int *) a, (mp_int *) r) != MP_OKAY)
		return -1;
	mp_rshb((mp_int *) r, n);
	return 0;
}


int crypto_bignum_cmp(const struct crypto_bignum *a,
		      const struct crypto_bignum *b)
{
	return mp_cmp((mp_int *) a, (mp_int *) b);
}


int crypto_bignum_bits(const struct crypto_bignum *a)
{
	return mp_count_bits((mp_int *) a);
}


int crypto_bignum_is_zero(const struct crypto_bignum *a)
{
	return mp_iszero((mp_int *) a);
}


int crypto_bignum_is_one(const struct crypto_bignum *a)
{
	return mp_isone((const mp_int *) a);
}

int crypto_bignum_is_odd(const struct crypto_bignum *a)
{
	return mp_isodd((mp_int *) a);
}


int crypto_bignum_legendre(const struct crypto_bignum *a,
			   const struct crypto_bignum *p)
{
	mp_int t;
	int ret;
	int res = -2;

	if (TEST_FAIL())
		return -2;

	if (mp_init(&t) != MP_OKAY)
		return -2;

	/* t = (p-1) / 2 */
	ret = mp_sub_d((mp_int *) p, 1, &t);
	if (ret == MP_OKAY)
		mp_rshb(&t, 1);
	if (ret == MP_OKAY)
		ret = mp_exptmod((mp_int *) a, &t, (mp_int *) p, &t);
	if (ret == MP_OKAY) {
		if (mp_isone(&t))
			res = 1;
		else if (mp_iszero(&t))
			res = 0;
		else
			res = -1;
	}

	mp_clear(&t);
	return res;
}


#ifdef CONFIG_ECC

int ecc_map(ecc_point *, mp_int *, mp_digit);
int ecc_projective_add_point(ecc_point *P, ecc_point *Q, ecc_point *R,
			     mp_int *a, mp_int *modulus, mp_digit mp);

struct crypto_ec {
	ecc_key key;
	mp_int a;
	mp_int prime;
	mp_int order;
	mp_digit mont_b;
	mp_int b;
};


struct crypto_ec * crypto_ec_init(int group)
{
	int built = 0;
	struct crypto_ec *e;
	int curve_id;

	/* Map from IANA registry for IKE D-H groups to OpenSSL NID */
	switch (group) {
	case 19:
		curve_id = ECC_SECP256R1;
		break;
	case 20:
		curve_id = ECC_SECP384R1;
		break;
	case 21:
		curve_id = ECC_SECP521R1;
		break;
	case 25:
		curve_id = ECC_SECP192R1;
		break;
	case 26:
		curve_id = ECC_SECP224R1;
		break;
#ifdef HAVE_ECC_BRAINPOOL
	case 27:
		curve_id = ECC_BRAINPOOLP224R1;
		break;
	case 28:
		curve_id = ECC_BRAINPOOLP256R1;
		break;
	case 29:
		curve_id = ECC_BRAINPOOLP384R1;
		break;
	case 30:
		curve_id = ECC_BRAINPOOLP512R1;
		break;
#endif /* HAVE_ECC_BRAINPOOL */
	default:
		return NULL;
	}

	e = os_zalloc(sizeof(*e));
	if (!e)
		return NULL;

	if (wc_ecc_init(&e->key) != 0 ||
	    wc_ecc_set_curve(&e->key, 0, curve_id) != 0 ||
	    mp_init(&e->a) != MP_OKAY ||
	    mp_init(&e->prime) != MP_OKAY ||
	    mp_init(&e->order) != MP_OKAY ||
	    mp_init(&e->b) != MP_OKAY ||
	    mp_read_radix(&e->a, e->key.dp->Af, 16) != MP_OKAY ||
	    mp_read_radix(&e->b, e->key.dp->Bf, 16) != MP_OKAY ||
	    mp_read_radix(&e->prime, e->key.dp->prime, 16) != MP_OKAY ||
	    mp_read_radix(&e->order, e->key.dp->order, 16) != MP_OKAY ||
	    mp_montgomery_setup(&e->prime, &e->mont_b) != MP_OKAY)
		goto done;

	built = 1;
done:
	if (!built) {
		crypto_ec_deinit(e);
		e = NULL;
	}
	return e;
}


void crypto_ec_deinit(struct crypto_ec* e)
{
	if (!e)
		return;

	mp_clear(&e->b);
	mp_clear(&e->order);
	mp_clear(&e->prime);
	mp_clear(&e->a);
	wc_ecc_free(&e->key);
	os_free(e);
}


int crypto_ec_cofactor(struct crypto_ec *e, struct crypto_bignum *cofactor)
{
	if (!e || !cofactor)
		return -1;

	mp_set((mp_int *) cofactor, e->key.dp->cofactor);
	return 0;
}


struct crypto_ec_point * crypto_ec_point_init(struct crypto_ec *e)
{
	if (TEST_FAIL())
		return NULL;
	if (!e)
		return NULL;
	return (struct crypto_ec_point *) wc_ecc_new_point();
}


size_t crypto_ec_prime_len(struct crypto_ec *e)
{
	return (mp_count_bits(&e->prime) + 7) / 8;
}


size_t crypto_ec_prime_len_bits(struct crypto_ec *e)
{
	return mp_count_bits(&e->prime);
}


size_t crypto_ec_order_len(struct crypto_ec *e)
{
	return (mp_count_bits(&e->order) + 7) / 8;
}


const struct crypto_bignum * crypto_ec_get_prime(struct crypto_ec *e)
{
	return (const struct crypto_bignum *) &e->prime;
}


const struct crypto_bignum * crypto_ec_get_order(struct crypto_ec *e)
{
	return (const struct crypto_bignum *) &e->order;
}


void crypto_ec_point_deinit(struct crypto_ec_point *p, int clear)
{
	ecc_point *point = (ecc_point *) p;

	if (!p)
		return;

	if (clear) {
		mp_forcezero(point->x);
		mp_forcezero(point->y);
		mp_forcezero(point->z);
	}
	wc_ecc_del_point(point);
}


int crypto_ec_point_x(struct crypto_ec *e, const struct crypto_ec_point *p,
		      struct crypto_bignum *x)
{
	return mp_copy(((ecc_point *) p)->x, (mp_int *) x) == MP_OKAY ? 0 : -1;
}


int crypto_ec_point_to_bin(struct crypto_ec *e,
			   const struct crypto_ec_point *point, u8 *x, u8 *y)
{
	ecc_point *p = (ecc_point *) point;

	if (TEST_FAIL())
		return -1;

	if (!mp_isone(p->z)) {
		if (ecc_map(p, &e->prime, e->mont_b) != MP_OKAY)
			return -1;
	}

	if (x) {
		if (crypto_bignum_to_bin((struct crypto_bignum *)p->x, x,
					 e->key.dp->size,
					 e->key.dp->size) <= 0)
			return -1;
	}

	if (y) {
		if (crypto_bignum_to_bin((struct crypto_bignum *) p->y, y,
					 e->key.dp->size,
					 e->key.dp->size) <= 0)
			return -1;
	}

	return 0;
}


struct crypto_ec_point * crypto_ec_point_from_bin(struct crypto_ec *e,
						  const u8 *val)
{
	ecc_point *point = NULL;
	int loaded = 0;

	if (TEST_FAIL())
		return NULL;

	point = wc_ecc_new_point();
	if (!point)
		goto done;

	if (mp_read_unsigned_bin(point->x, val, e->key.dp->size) != MP_OKAY)
		goto done;
	val += e->key.dp->size;
	if (mp_read_unsigned_bin(point->y, val, e->key.dp->size) != MP_OKAY)
		goto done;
	mp_set(point->z, 1);

	loaded = 1;
done:
	if (!loaded) {
		wc_ecc_del_point(point);
		point = NULL;
	}
	return (struct crypto_ec_point *) point;
}


int crypto_ec_point_add(struct crypto_ec *e, const struct crypto_ec_point *a,
			const struct crypto_ec_point *b,
			struct crypto_ec_point *c)
{
	mp_int mu;
	ecc_point *ta = NULL, *tb = NULL;
	ecc_point *pa = (ecc_point *) a, *pb = (ecc_point *) b;
	mp_int *modulus = &e->prime;
	int ret;

	if (TEST_FAIL())
		return -1;

	ret = mp_init(&mu);
	if (ret != MP_OKAY)
		return -1;

	ret = mp_montgomery_calc_normalization(&mu, modulus);
	if (ret != MP_OKAY) {
		mp_clear(&mu);
		return -1;
	}

	if (!mp_isone(&mu)) {
		ta = wc_ecc_new_point();
		if (!ta) {
			mp_clear(&mu);
			return -1;
		}
		tb = wc_ecc_new_point();
		if (!tb) {
			wc_ecc_del_point(ta);
			mp_clear(&mu);
			return -1;
		}

		if (mp_mulmod(pa->x, &mu, modulus, ta->x) != MP_OKAY ||
		    mp_mulmod(pa->y, &mu, modulus, ta->y) != MP_OKAY ||
		    mp_mulmod(pa->z, &mu, modulus, ta->z) != MP_OKAY ||
		    mp_mulmod(pb->x, &mu, modulus, tb->x) != MP_OKAY ||
		    mp_mulmod(pb->y, &mu, modulus, tb->y) != MP_OKAY ||
		    mp_mulmod(pb->z, &mu, modulus, tb->z) != MP_OKAY) {
			ret = -1;
			goto end;
		}
		pa = ta;
		pb = tb;
	}

	ret = ecc_projective_add_point(pa, pb, (ecc_point *) c, &e->a,
				       &e->prime, e->mont_b);
	if (ret != 0) {
		ret = -1;
		goto end;
	}

	if (ecc_map((ecc_point *) c, &e->prime, e->mont_b) != MP_OKAY)
		ret = -1;
	else
		ret = 0;
end:
	wc_ecc_del_point(tb);
	wc_ecc_del_point(ta);
	mp_clear(&mu);
	return ret;
}


int crypto_ec_point_mul(struct crypto_ec *e, const struct crypto_ec_point *p,
			const struct crypto_bignum *b,
			struct crypto_ec_point *res)
{
	int ret;

	if (TEST_FAIL())
		return -1;

	ret = wc_ecc_mulmod((mp_int *) b, (ecc_point *) p, (ecc_point *) res,
			    &e->a, &e->prime, 1);
	return ret == 0 ? 0 : -1;
}


int crypto_ec_point_invert(struct crypto_ec *e, struct crypto_ec_point *p)
{
	ecc_point *point = (ecc_point *) p;

	if (TEST_FAIL())
		return -1;

	if (mp_sub(&e->prime, point->y, point->y) != MP_OKAY)
		return -1;

	return 0;
}


int crypto_ec_point_solve_y_coord(struct crypto_ec *e,
				  struct crypto_ec_point *p,
				  const struct crypto_bignum *x, int y_bit)
{
	byte buf[1 + 2 * MAX_ECC_BYTES];
	int ret;
	int prime_len = crypto_ec_prime_len(e);

	if (TEST_FAIL())
		return -1;

	buf[0] = y_bit ? ECC_POINT_COMP_ODD : ECC_POINT_COMP_EVEN;
	ret = crypto_bignum_to_bin(x, buf + 1, prime_len, prime_len);
	if (ret <= 0)
		return -1;
	ret = wc_ecc_import_point_der(buf, 1 + 2 * ret, e->key.idx,
				      (ecc_point *) p);
	if (ret != 0)
		return -1;

	return 0;
}


struct crypto_bignum *
crypto_ec_point_compute_y_sqr(struct crypto_ec *e,
			      const struct crypto_bignum *x)
{
	mp_int *y2 = NULL;
	mp_int t;
	int calced = 0;

	if (TEST_FAIL())
		return NULL;

	if (mp_init(&t) != MP_OKAY)
		return NULL;

	y2 = (mp_int *) crypto_bignum_init();
	if (!y2)
		goto done;

	if (mp_sqrmod((mp_int *) x, &e->prime, y2) != 0 ||
	    mp_mulmod((mp_int *) x, y2, &e->prime, y2) != 0 ||
	    mp_mulmod((mp_int *) x, &e->a, &e->prime, &t) != 0 ||
	    mp_addmod(y2, &t, &e->prime, y2) != 0 ||
	    mp_addmod(y2, &e->b, &e->prime, y2) != 0)
		goto done;

	calced = 1;
done:
	if (!calced) {
		if (y2) {
			mp_clear(y2);
			os_free(y2);
		}
		mp_clear(&t);
	}

	return (struct crypto_bignum *) y2;
}


int crypto_ec_point_is_at_infinity(struct crypto_ec *e,
				   const struct crypto_ec_point *p)
{
	return wc_ecc_point_is_at_infinity((ecc_point *) p);
}


int crypto_ec_point_is_on_curve(struct crypto_ec *e,
				const struct crypto_ec_point *p)
{
	return wc_ecc_is_point((ecc_point *) p, &e->a, &e->b, &e->prime) ==
		MP_OKAY;
}


int crypto_ec_point_cmp(const struct crypto_ec *e,
			const struct crypto_ec_point *a,
			const struct crypto_ec_point *b)
{
	return wc_ecc_cmp_point((ecc_point *) a, (ecc_point *) b);
}


struct crypto_ecdh {
	struct crypto_ec *ec;
};

struct crypto_ecdh * crypto_ecdh_init(int group)
{
	struct crypto_ecdh *ecdh = NULL;
	WC_RNG rng;
	int ret;

	if (wc_InitRng(&rng) != 0)
		goto fail;

	ecdh = os_zalloc(sizeof(*ecdh));
	if (!ecdh)
		goto fail;

	ecdh->ec = crypto_ec_init(group);
	if (!ecdh->ec)
		goto fail;

	ret = wc_ecc_make_key_ex(&rng, ecdh->ec->key.dp->size, &ecdh->ec->key,
				 ecdh->ec->key.dp->id);
	if (ret < 0)
		goto fail;

done:
	wc_FreeRng(&rng);

	return ecdh;
fail:
	crypto_ecdh_deinit(ecdh);
	ecdh = NULL;
	goto done;
}


void crypto_ecdh_deinit(struct crypto_ecdh *ecdh)
{
	if (ecdh) {
		crypto_ec_deinit(ecdh->ec);
		os_free(ecdh);
	}
}


struct wpabuf * crypto_ecdh_get_pubkey(struct crypto_ecdh *ecdh, int inc_y)
{
	struct wpabuf *buf = NULL;
	int ret;
	int len = ecdh->ec->key.dp->size;

	buf = wpabuf_alloc(inc_y ? 2 * len : len);
	if (!buf)
		goto fail;

	ret = crypto_bignum_to_bin((struct crypto_bignum *)
				   ecdh->ec->key.pubkey.x, wpabuf_put(buf, len),
				   len, len);
	if (ret < 0)
		goto fail;
	if (inc_y) {
		ret = crypto_bignum_to_bin((struct crypto_bignum *)
					   ecdh->ec->key.pubkey.y,
					   wpabuf_put(buf, len), len, len);
		if (ret < 0)
			goto fail;
	}

done:
	return buf;
fail:
	wpabuf_free(buf);
	buf = NULL;
	goto done;
}


struct wpabuf * crypto_ecdh_set_peerkey(struct crypto_ecdh *ecdh, int inc_y,
					const u8 *key, size_t len)
{
	int ret;
	struct wpabuf *pubkey = NULL;
	struct wpabuf *secret = NULL;
	word32 key_len = ecdh->ec->key.dp->size;
	ecc_point *point = NULL;
	size_t need_key_len = inc_y ? 2 * key_len : key_len;

	if (len < need_key_len)
		goto fail;
	pubkey = wpabuf_alloc(1 + 2 * key_len);
	if (!pubkey)
		goto fail;
	wpabuf_put_u8(pubkey, inc_y ? ECC_POINT_UNCOMP : ECC_POINT_COMP_EVEN);
	wpabuf_put_data(pubkey, key, need_key_len);

	point = wc_ecc_new_point();
	if (!point)
		goto fail;

	ret = wc_ecc_import_point_der(wpabuf_mhead(pubkey), 1 + 2 * key_len,
				      ecdh->ec->key.idx, point);
	if (ret != MP_OKAY)
		goto fail;

	secret = wpabuf_alloc(key_len);
	if (!secret)
		goto fail;

	ret = wc_ecc_shared_secret_ex(&ecdh->ec->key, point,
				      wpabuf_put(secret, key_len), &key_len);
	if (ret != MP_OKAY)
		goto fail;

done:
	wc_ecc_del_point(point);
	wpabuf_free(pubkey);
	return secret;
fail:
	wpabuf_free(secret);
	secret = NULL;
	goto done;
}

#endif /* CONFIG_ECC */
