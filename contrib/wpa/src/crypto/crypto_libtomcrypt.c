/*
 * WPA Supplicant / Crypto wrapper for LibTomCrypt (for internal TLSv1)
 * Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <tomcrypt.h>

#include "common.h"
#include "crypto.h"

#ifndef mp_init_multi
#define mp_init_multi                ltc_init_multi
#define mp_clear_multi               ltc_deinit_multi
#define mp_unsigned_bin_size(a)      ltc_mp.unsigned_size(a)
#define mp_to_unsigned_bin(a, b)     ltc_mp.unsigned_write(a, b)
#define mp_read_unsigned_bin(a, b, c) ltc_mp.unsigned_read(a, b, c)
#define mp_exptmod(a,b,c,d)          ltc_mp.exptmod(a,b,c,d)
#endif


int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	hash_state md;
	size_t i;

	md4_init(&md);
	for (i = 0; i < num_elem; i++)
		md4_process(&md, addr[i], len[i]);
	md4_done(&md, mac);
	return 0;
}


int des_encrypt(const u8 *clear, const u8 *key, u8 *cypher)
{
	u8 pkey[8], next, tmp;
	int i;
	symmetric_key skey;

	/* Add parity bits to the key */
	next = 0;
	for (i = 0; i < 7; i++) {
		tmp = key[i];
		pkey[i] = (tmp >> i) | next | 1;
		next = tmp << (7 - i);
	}
	pkey[i] = next | 1;

	des_setup(pkey, 8, 0, &skey);
	des_ecb_encrypt(clear, cypher, &skey);
	des_done(&skey);
	return 0;
}


int md5_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	hash_state md;
	size_t i;

	md5_init(&md);
	for (i = 0; i < num_elem; i++)
		md5_process(&md, addr[i], len[i]);
	md5_done(&md, mac);
	return 0;
}


int sha1_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	hash_state md;
	size_t i;

	sha1_init(&md);
	for (i = 0; i < num_elem; i++)
		sha1_process(&md, addr[i], len[i]);
	sha1_done(&md, mac);
	return 0;
}


void * aes_encrypt_init(const u8 *key, size_t len)
{
	symmetric_key *skey;
	skey = os_malloc(sizeof(*skey));
	if (skey == NULL)
		return NULL;
	if (aes_setup(key, len, 0, skey) != CRYPT_OK) {
		os_free(skey);
		return NULL;
	}
	return skey;
}


int aes_encrypt(void *ctx, const u8 *plain, u8 *crypt)
{
	symmetric_key *skey = ctx;
	return aes_ecb_encrypt(plain, crypt, skey) == CRYPT_OK ? 0 : -1;
}


void aes_encrypt_deinit(void *ctx)
{
	symmetric_key *skey = ctx;
	aes_done(skey);
	os_free(skey);
}


void * aes_decrypt_init(const u8 *key, size_t len)
{
	symmetric_key *skey;
	skey = os_malloc(sizeof(*skey));
	if (skey == NULL)
		return NULL;
	if (aes_setup(key, len, 0, skey) != CRYPT_OK) {
		os_free(skey);
		return NULL;
	}
	return skey;
}


int aes_decrypt(void *ctx, const u8 *crypt, u8 *plain)
{
	symmetric_key *skey = ctx;
	return aes_ecb_encrypt(plain, (u8 *) crypt, skey) == CRYPT_OK ? 0 : -1;
}


void aes_decrypt_deinit(void *ctx)
{
	symmetric_key *skey = ctx;
	aes_done(skey);
	os_free(skey);
}


struct crypto_hash {
	enum crypto_hash_alg alg;
	int error;
	union {
		hash_state md;
		hmac_state hmac;
	} u;
};


struct crypto_hash * crypto_hash_init(enum crypto_hash_alg alg, const u8 *key,
				      size_t key_len)
{
	struct crypto_hash *ctx;

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	ctx->alg = alg;

	switch (alg) {
	case CRYPTO_HASH_ALG_MD5:
		if (md5_init(&ctx->u.md) != CRYPT_OK)
			goto fail;
		break;
	case CRYPTO_HASH_ALG_SHA1:
		if (sha1_init(&ctx->u.md) != CRYPT_OK)
			goto fail;
		break;
	case CRYPTO_HASH_ALG_HMAC_MD5:
		if (hmac_init(&ctx->u.hmac, find_hash("md5"), key, key_len) !=
		    CRYPT_OK)
			goto fail;
		break;
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		if (hmac_init(&ctx->u.hmac, find_hash("sha1"), key, key_len) !=
		    CRYPT_OK)
			goto fail;
		break;
	default:
		goto fail;
	}

	return ctx;

fail:
	os_free(ctx);
	return NULL;
}

void crypto_hash_update(struct crypto_hash *ctx, const u8 *data, size_t len)
{
	if (ctx == NULL || ctx->error)
		return;

	switch (ctx->alg) {
	case CRYPTO_HASH_ALG_MD5:
		ctx->error = md5_process(&ctx->u.md, data, len) != CRYPT_OK;
		break;
	case CRYPTO_HASH_ALG_SHA1:
		ctx->error = sha1_process(&ctx->u.md, data, len) != CRYPT_OK;
		break;
	case CRYPTO_HASH_ALG_HMAC_MD5:
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		ctx->error = hmac_process(&ctx->u.hmac, data, len) != CRYPT_OK;
		break;
	}
}


int crypto_hash_finish(struct crypto_hash *ctx, u8 *mac, size_t *len)
{
	int ret = 0;
	unsigned long clen;

	if (ctx == NULL)
		return -2;

	if (mac == NULL || len == NULL) {
		os_free(ctx);
		return 0;
	}

	if (ctx->error) {
		os_free(ctx);
		return -2;
	}

	switch (ctx->alg) {
	case CRYPTO_HASH_ALG_MD5:
		if (*len < 16) {
			*len = 16;
			os_free(ctx);
			return -1;
		}
		*len = 16;
		if (md5_done(&ctx->u.md, mac) != CRYPT_OK)
			ret = -2;
		break;
	case CRYPTO_HASH_ALG_SHA1:
		if (*len < 20) {
			*len = 20;
			os_free(ctx);
			return -1;
		}
		*len = 20;
		if (sha1_done(&ctx->u.md, mac) != CRYPT_OK)
			ret = -2;
		break;
	case CRYPTO_HASH_ALG_HMAC_SHA1:
		if (*len < 20) {
			*len = 20;
			os_free(ctx);
			return -1;
		}
		/* continue */
	case CRYPTO_HASH_ALG_HMAC_MD5:
		if (*len < 16) {
			*len = 16;
			os_free(ctx);
			return -1;
		}
		clen = *len;
		if (hmac_done(&ctx->u.hmac, mac, &clen) != CRYPT_OK) {
			os_free(ctx);
			return -1;
		}
		*len = clen;
		break;
	default:
		ret = -2;
		break;
	}

	os_free(ctx);

	return ret;
}


struct crypto_cipher {
	int rc4;
	union {
		symmetric_CBC cbc;
		struct {
			size_t used_bytes;
			u8 key[16];
			size_t keylen;
		} rc4;
	} u;
};


struct crypto_cipher * crypto_cipher_init(enum crypto_cipher_alg alg,
					  const u8 *iv, const u8 *key,
					  size_t key_len)
{
	struct crypto_cipher *ctx;
	int idx, res, rc4 = 0;

	switch (alg) {
	case CRYPTO_CIPHER_ALG_AES:
		idx = find_cipher("aes");
		break;
	case CRYPTO_CIPHER_ALG_3DES:
		idx = find_cipher("3des");
		break;
	case CRYPTO_CIPHER_ALG_DES:
		idx = find_cipher("des");
		break;
	case CRYPTO_CIPHER_ALG_RC2:
		idx = find_cipher("rc2");
		break;
	case CRYPTO_CIPHER_ALG_RC4:
		idx = -1;
		rc4 = 1;
		break;
	default:
		return NULL;
	}

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	if (rc4) {
		ctx->rc4 = 1;
		if (key_len > sizeof(ctx->u.rc4.key)) {
			os_free(ctx);
			return NULL;
		}
		ctx->u.rc4.keylen = key_len;
		os_memcpy(ctx->u.rc4.key, key, key_len);
	} else {
		res = cbc_start(idx, iv, key, key_len, 0, &ctx->u.cbc);
		if (res != CRYPT_OK) {
			wpa_printf(MSG_DEBUG, "LibTomCrypt: Cipher start "
				   "failed: %s", error_to_string(res));
			os_free(ctx);
			return NULL;
		}
	}

	return ctx;
}

int crypto_cipher_encrypt(struct crypto_cipher *ctx, const u8 *plain,
			  u8 *crypt, size_t len)
{
	int res;

	if (ctx->rc4) {
		if (plain != crypt)
			os_memcpy(crypt, plain, len);
		rc4_skip(ctx->u.rc4.key, ctx->u.rc4.keylen,
			 ctx->u.rc4.used_bytes, crypt, len);
		ctx->u.rc4.used_bytes += len;
		return 0;
	}

	res = cbc_encrypt(plain, crypt, len, &ctx->u.cbc);
	if (res != CRYPT_OK) {
		wpa_printf(MSG_DEBUG, "LibTomCrypt: CBC encryption "
			   "failed: %s", error_to_string(res));
		return -1;
	}
	return 0;
}


int crypto_cipher_decrypt(struct crypto_cipher *ctx, const u8 *crypt,
			  u8 *plain, size_t len)
{
	int res;

	if (ctx->rc4) {
		if (plain != crypt)
			os_memcpy(plain, crypt, len);
		rc4_skip(ctx->u.rc4.key, ctx->u.rc4.keylen,
			 ctx->u.rc4.used_bytes, plain, len);
		ctx->u.rc4.used_bytes += len;
		return 0;
	}

	res = cbc_decrypt(crypt, plain, len, &ctx->u.cbc);
	if (res != CRYPT_OK) {
		wpa_printf(MSG_DEBUG, "LibTomCrypt: CBC decryption "
			   "failed: %s", error_to_string(res));
		return -1;
	}

	return 0;
}


void crypto_cipher_deinit(struct crypto_cipher *ctx)
{
	if (!ctx->rc4)
		cbc_done(&ctx->u.cbc);
	os_free(ctx);
}


struct crypto_public_key {
	rsa_key rsa;
};

struct crypto_private_key {
	rsa_key rsa;
};


struct crypto_public_key * crypto_public_key_import(const u8 *key, size_t len)
{
	int res;
	struct crypto_public_key *pk;

	pk = os_zalloc(sizeof(*pk));
	if (pk == NULL)
		return NULL;

	res = rsa_import(key, len, &pk->rsa);
	if (res != CRYPT_OK) {
		wpa_printf(MSG_ERROR, "LibTomCrypt: Failed to import "
			   "public key (res=%d '%s')",
			   res, error_to_string(res));
		os_free(pk);
		return NULL;
	}

	if (pk->rsa.type != PK_PUBLIC) {
		wpa_printf(MSG_ERROR, "LibTomCrypt: Public key was not of "
			   "correct type");
		rsa_free(&pk->rsa);
		os_free(pk);
		return NULL;
	}

	return pk;
}


struct crypto_private_key * crypto_private_key_import(const u8 *key,
						      size_t len,
						      const char *passwd)
{
	int res;
	struct crypto_private_key *pk;

	pk = os_zalloc(sizeof(*pk));
	if (pk == NULL)
		return NULL;

	res = rsa_import(key, len, &pk->rsa);
	if (res != CRYPT_OK) {
		wpa_printf(MSG_ERROR, "LibTomCrypt: Failed to import "
			   "private key (res=%d '%s')",
			   res, error_to_string(res));
		os_free(pk);
		return NULL;
	}

	if (pk->rsa.type != PK_PRIVATE) {
		wpa_printf(MSG_ERROR, "LibTomCrypt: Private key was not of "
			   "correct type");
		rsa_free(&pk->rsa);
		os_free(pk);
		return NULL;
	}

	return pk;
}


struct crypto_public_key * crypto_public_key_from_cert(const u8 *buf,
						       size_t len)
{
	/* No X.509 support in LibTomCrypt */
	return NULL;
}


static int pkcs1_generate_encryption_block(u8 block_type, size_t modlen,
					   const u8 *in, size_t inlen,
					   u8 *out, size_t *outlen)
{
	size_t ps_len;
	u8 *pos;

	/*
	 * PKCS #1 v1.5, 8.1:
	 *
	 * EB = 00 || BT || PS || 00 || D
	 * BT = 00 or 01 for private-key operation; 02 for public-key operation
	 * PS = k-3-||D||; at least eight octets
	 * (BT=0: PS=0x00, BT=1: PS=0xff, BT=2: PS=pseudorandom non-zero)
	 * k = length of modulus in octets (modlen)
	 */

	if (modlen < 12 || modlen > *outlen || inlen > modlen - 11) {
		wpa_printf(MSG_DEBUG, "PKCS #1: %s - Invalid buffer "
			   "lengths (modlen=%lu outlen=%lu inlen=%lu)",
			   __func__, (unsigned long) modlen,
			   (unsigned long) *outlen,
			   (unsigned long) inlen);
		return -1;
	}

	pos = out;
	*pos++ = 0x00;
	*pos++ = block_type; /* BT */
	ps_len = modlen - inlen - 3;
	switch (block_type) {
	case 0:
		os_memset(pos, 0x00, ps_len);
		pos += ps_len;
		break;
	case 1:
		os_memset(pos, 0xff, ps_len);
		pos += ps_len;
		break;
	case 2:
		if (os_get_random(pos, ps_len) < 0) {
			wpa_printf(MSG_DEBUG, "PKCS #1: %s - Failed to get "
				   "random data for PS", __func__);
			return -1;
		}
		while (ps_len--) {
			if (*pos == 0x00)
				*pos = 0x01;
			pos++;
		}
		break;
	default:
		wpa_printf(MSG_DEBUG, "PKCS #1: %s - Unsupported block type "
			   "%d", __func__, block_type);
		return -1;
	}
	*pos++ = 0x00;
	os_memcpy(pos, in, inlen); /* D */

	return 0;
}


static int crypto_rsa_encrypt_pkcs1(int block_type, rsa_key *key, int key_type,
				    const u8 *in, size_t inlen,
				    u8 *out, size_t *outlen)
{
	unsigned long len, modlen;
	int res;

	modlen = mp_unsigned_bin_size(key->N);

	if (pkcs1_generate_encryption_block(block_type, modlen, in, inlen,
					    out, outlen) < 0)
		return -1;

	len = *outlen;
	res = rsa_exptmod(out, modlen, out, &len, key_type, key);
	if (res != CRYPT_OK) {
		wpa_printf(MSG_DEBUG, "LibTomCrypt: rsa_exptmod failed: %s",
			   error_to_string(res));
		return -1;
	}
	*outlen = len;

	return 0;
}


int crypto_public_key_encrypt_pkcs1_v15(struct crypto_public_key *key,
					const u8 *in, size_t inlen,
					u8 *out, size_t *outlen)
{
	return crypto_rsa_encrypt_pkcs1(2, &key->rsa, PK_PUBLIC, in, inlen,
					out, outlen);
}


int crypto_private_key_sign_pkcs1(struct crypto_private_key *key,
				  const u8 *in, size_t inlen,
				  u8 *out, size_t *outlen)
{
	return crypto_rsa_encrypt_pkcs1(1, &key->rsa, PK_PRIVATE, in, inlen,
					out, outlen);
}


void crypto_public_key_free(struct crypto_public_key *key)
{
	if (key) {
		rsa_free(&key->rsa);
		os_free(key);
	}
}


void crypto_private_key_free(struct crypto_private_key *key)
{
	if (key) {
		rsa_free(&key->rsa);
		os_free(key);
	}
}


int crypto_public_key_decrypt_pkcs1(struct crypto_public_key *key,
				    const u8 *crypt, size_t crypt_len,
				    u8 *plain, size_t *plain_len)
{
	int res;
	unsigned long len;
	u8 *pos;

	len = *plain_len;
	res = rsa_exptmod(crypt, crypt_len, plain, &len, PK_PUBLIC,
			  &key->rsa);
	if (res != CRYPT_OK) {
		wpa_printf(MSG_DEBUG, "LibTomCrypt: rsa_exptmod failed: %s",
			   error_to_string(res));
		return -1;
	}

	/*
	 * PKCS #1 v1.5, 8.1:
	 *
	 * EB = 00 || BT || PS || 00 || D
	 * BT = 01
	 * PS = k-3-||D|| times FF
	 * k = length of modulus in octets
	 */

	if (len < 3 + 8 + 16 /* min hash len */ ||
	    plain[0] != 0x00 || plain[1] != 0x01 || plain[2] != 0xff) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature EB "
			   "structure");
		return -1;
	}

	pos = plain + 3;
	while (pos < plain + len && *pos == 0xff)
		pos++;
	if (pos - plain - 2 < 8) {
		/* PKCS #1 v1.5, 8.1: At least eight octets long PS */
		wpa_printf(MSG_INFO, "LibTomCrypt: Too short signature "
			   "padding");
		return -1;
	}

	if (pos + 16 /* min hash len */ >= plain + len || *pos != 0x00) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature EB "
			   "structure (2)");
		return -1;
	}
	pos++;
	len -= pos - plain;

	/* Strip PKCS #1 header */
	os_memmove(plain, pos, len);
	*plain_len = len;

	return 0;
}


int crypto_global_init(void)
{
	ltc_mp = tfm_desc;
	/* TODO: only register algorithms that are really needed */
	if (register_hash(&md4_desc) < 0 ||
	    register_hash(&md5_desc) < 0 ||
	    register_hash(&sha1_desc) < 0 ||
	    register_cipher(&aes_desc) < 0 ||
	    register_cipher(&des_desc) < 0 ||
	    register_cipher(&des3_desc) < 0) {
		wpa_printf(MSG_ERROR, "TLSv1: Failed to register "
			   "hash/cipher functions");
		return -1;
	}

	return 0;
}


void crypto_global_deinit(void)
{
}


#ifdef CONFIG_MODEXP

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
	void *b, *p, *m, *r;

	if (mp_init_multi(&b, &p, &m, &r, NULL) != CRYPT_OK)
		return -1;

	if (mp_read_unsigned_bin(b, (u8 *) base, base_len) != CRYPT_OK ||
	    mp_read_unsigned_bin(p, (u8 *) power, power_len) != CRYPT_OK ||
	    mp_read_unsigned_bin(m, (u8 *) modulus, modulus_len) != CRYPT_OK)
		goto fail;

	if (mp_exptmod(b, p, m, r) != CRYPT_OK)
		goto fail;

	*result_len = mp_unsigned_bin_size(r);
	if (mp_to_unsigned_bin(r, result) != CRYPT_OK)
		goto fail;

	mp_clear_multi(b, p, m, r, NULL);
	return 0;

fail:
	mp_clear_multi(b, p, m, r, NULL);
	return -1;
}

#endif /* CONFIG_MODEXP */
