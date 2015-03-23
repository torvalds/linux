/*
 * TLSv1 common routines
 * Copyright (c) 2006-2007, Jouni Malinen <j@w1.fi>
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

#include "common.h"
#include "x509v3.h"
#include "tlsv1_common.h"


/*
 * TODO:
 * RFC 2246 Section 9: Mandatory to implement TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA
 * Add support for commonly used cipher suites; don't bother with exportable
 * suites.
 */ 

static const struct tls_cipher_suite tls_cipher_suites[] = {
	{ TLS_NULL_WITH_NULL_NULL, TLS_KEY_X_NULL, TLS_CIPHER_NULL,
	  TLS_HASH_NULL },
	{ TLS_RSA_WITH_RC4_128_MD5, TLS_KEY_X_RSA, TLS_CIPHER_RC4_128,
	  TLS_HASH_MD5 },
	{ TLS_RSA_WITH_RC4_128_SHA, TLS_KEY_X_RSA, TLS_CIPHER_RC4_128,
	  TLS_HASH_SHA },
	{ TLS_RSA_WITH_DES_CBC_SHA, TLS_KEY_X_RSA, TLS_CIPHER_DES_CBC,
	  TLS_HASH_SHA },
	{ TLS_RSA_WITH_3DES_EDE_CBC_SHA, TLS_KEY_X_RSA,
	  TLS_CIPHER_3DES_EDE_CBC, TLS_HASH_SHA },
 	{ TLS_DH_anon_WITH_RC4_128_MD5, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_RC4_128, TLS_HASH_MD5 },
 	{ TLS_DH_anon_WITH_DES_CBC_SHA, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_DES_CBC, TLS_HASH_SHA },
 	{ TLS_DH_anon_WITH_3DES_EDE_CBC_SHA, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_3DES_EDE_CBC, TLS_HASH_SHA },
	{ TLS_RSA_WITH_AES_128_CBC_SHA, TLS_KEY_X_RSA, TLS_CIPHER_AES_128_CBC,
	  TLS_HASH_SHA },
	{ TLS_DH_anon_WITH_AES_128_CBC_SHA, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_AES_128_CBC, TLS_HASH_SHA },
	{ TLS_RSA_WITH_AES_256_CBC_SHA, TLS_KEY_X_RSA, TLS_CIPHER_AES_256_CBC,
	  TLS_HASH_SHA },
	{ TLS_DH_anon_WITH_AES_256_CBC_SHA, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_AES_256_CBC, TLS_HASH_SHA }
};

#define NUM_ELEMS(a) (sizeof(a) / sizeof((a)[0]))
#define NUM_TLS_CIPHER_SUITES NUM_ELEMS(tls_cipher_suites)


static const struct tls_cipher_data tls_ciphers[] = {
	{ TLS_CIPHER_NULL,         TLS_CIPHER_STREAM,  0,  0,  0,
	  CRYPTO_CIPHER_NULL },
	{ TLS_CIPHER_IDEA_CBC,     TLS_CIPHER_BLOCK,  16, 16,  8,
	  CRYPTO_CIPHER_NULL },
	{ TLS_CIPHER_RC2_CBC_40,   TLS_CIPHER_BLOCK,   5, 16,  0,
	  CRYPTO_CIPHER_ALG_RC2 },
	{ TLS_CIPHER_RC4_40,       TLS_CIPHER_STREAM,  5, 16,  0,
	  CRYPTO_CIPHER_ALG_RC4 },
	{ TLS_CIPHER_RC4_128,      TLS_CIPHER_STREAM, 16, 16,  0,
	  CRYPTO_CIPHER_ALG_RC4 },
	{ TLS_CIPHER_DES40_CBC,    TLS_CIPHER_BLOCK,   5,  8,  8,
	  CRYPTO_CIPHER_ALG_DES },
	{ TLS_CIPHER_DES_CBC,      TLS_CIPHER_BLOCK,   8,  8,  8,
	  CRYPTO_CIPHER_ALG_DES },
	{ TLS_CIPHER_3DES_EDE_CBC, TLS_CIPHER_BLOCK,  24, 24,  8,
	  CRYPTO_CIPHER_ALG_3DES },
	{ TLS_CIPHER_AES_128_CBC,  TLS_CIPHER_BLOCK,  16, 16, 16,
	  CRYPTO_CIPHER_ALG_AES },
	{ TLS_CIPHER_AES_256_CBC,  TLS_CIPHER_BLOCK,  32, 32, 16,
	  CRYPTO_CIPHER_ALG_AES }
};

#define NUM_TLS_CIPHER_DATA NUM_ELEMS(tls_ciphers)


/**
 * tls_get_cipher_suite - Get TLS cipher suite
 * @suite: Cipher suite identifier
 * Returns: Pointer to the cipher data or %NULL if not found
 */
const struct tls_cipher_suite * tls_get_cipher_suite(u16 suite)
{
	size_t i;
	for (i = 0; i < NUM_TLS_CIPHER_SUITES; i++)
		if (tls_cipher_suites[i].suite == suite)
			return &tls_cipher_suites[i];
	return NULL;
}


const struct tls_cipher_data * tls_get_cipher_data(tls_cipher cipher)
{
	size_t i;
	for (i = 0; i < NUM_TLS_CIPHER_DATA; i++)
		if (tls_ciphers[i].cipher == cipher)
			return &tls_ciphers[i];
	return NULL;
}


int tls_server_key_exchange_allowed(tls_cipher cipher)
{
	const struct tls_cipher_suite *suite;

	/* RFC 2246, Section 7.4.3 */
	suite = tls_get_cipher_suite(cipher);
	if (suite == NULL)
		return 0;

	switch (suite->key_exchange) {
	case TLS_KEY_X_DHE_DSS:
	case TLS_KEY_X_DHE_DSS_EXPORT:
	case TLS_KEY_X_DHE_RSA:
	case TLS_KEY_X_DHE_RSA_EXPORT:
	case TLS_KEY_X_DH_anon_EXPORT:
	case TLS_KEY_X_DH_anon:
		return 1;
	case TLS_KEY_X_RSA_EXPORT:
		return 1 /* FIX: public key len > 512 bits */;
	default:
		return 0;
	}
}


/**
 * tls_parse_cert - Parse DER encoded X.509 certificate and get public key
 * @buf: ASN.1 DER encoded certificate
 * @len: Length of the buffer
 * @pk: Buffer for returning the allocated public key
 * Returns: 0 on success, -1 on failure
 *
 * This functions parses an ASN.1 DER encoded X.509 certificate and retrieves
 * the public key from it. The caller is responsible for freeing the public key
 * by calling crypto_public_key_free().
 */
int tls_parse_cert(const u8 *buf, size_t len, struct crypto_public_key **pk)
{
	struct x509_certificate *cert;

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: Parse ASN.1 DER certificate",
		    buf, len);

	*pk = crypto_public_key_from_cert(buf, len);
	if (*pk)
		return 0;

	cert = x509_certificate_parse(buf, len);
	if (cert == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to parse X.509 "
			   "certificate");
		return -1;
	}

	/* TODO
	 * verify key usage (must allow encryption)
	 *
	 * All certificate profiles, key and cryptographic formats are
	 * defined by the IETF PKIX working group [PKIX]. When a key
	 * usage extension is present, the digitalSignature bit must be
	 * set for the key to be eligible for signing, as described
	 * above, and the keyEncipherment bit must be present to allow
	 * encryption, as described above. The keyAgreement bit must be
	 * set on Diffie-Hellman certificates. (PKIX: RFC 3280)
	 */

	*pk = crypto_public_key_import(cert->public_key, cert->public_key_len);
	x509_certificate_free(cert);

	if (*pk == NULL) {
		wpa_printf(MSG_ERROR, "TLSv1: Failed to import "
			   "server public key");
		return -1;
	}

	return 0;
}


int tls_verify_hash_init(struct tls_verify_hash *verify)
{
	tls_verify_hash_free(verify);
	verify->md5_client = crypto_hash_init(CRYPTO_HASH_ALG_MD5, NULL, 0);
	verify->md5_server = crypto_hash_init(CRYPTO_HASH_ALG_MD5, NULL, 0);
	verify->md5_cert = crypto_hash_init(CRYPTO_HASH_ALG_MD5, NULL, 0);
	verify->sha1_client = crypto_hash_init(CRYPTO_HASH_ALG_SHA1, NULL, 0);
	verify->sha1_server = crypto_hash_init(CRYPTO_HASH_ALG_SHA1, NULL, 0);
	verify->sha1_cert = crypto_hash_init(CRYPTO_HASH_ALG_SHA1, NULL, 0);
	if (verify->md5_client == NULL || verify->md5_server == NULL ||
	    verify->md5_cert == NULL || verify->sha1_client == NULL ||
	    verify->sha1_server == NULL || verify->sha1_cert == NULL) {
		tls_verify_hash_free(verify);
		return -1;
	}
	return 0;
}


void tls_verify_hash_add(struct tls_verify_hash *verify, const u8 *buf,
			 size_t len)
{
	if (verify->md5_client && verify->sha1_client) {
		crypto_hash_update(verify->md5_client, buf, len);
		crypto_hash_update(verify->sha1_client, buf, len);
	}
	if (verify->md5_server && verify->sha1_server) {
		crypto_hash_update(verify->md5_server, buf, len);
		crypto_hash_update(verify->sha1_server, buf, len);
	}
	if (verify->md5_cert && verify->sha1_cert) {
		crypto_hash_update(verify->md5_cert, buf, len);
		crypto_hash_update(verify->sha1_cert, buf, len);
	}
}


void tls_verify_hash_free(struct tls_verify_hash *verify)
{
	crypto_hash_finish(verify->md5_client, NULL, NULL);
	crypto_hash_finish(verify->md5_server, NULL, NULL);
	crypto_hash_finish(verify->md5_cert, NULL, NULL);
	crypto_hash_finish(verify->sha1_client, NULL, NULL);
	crypto_hash_finish(verify->sha1_server, NULL, NULL);
	crypto_hash_finish(verify->sha1_cert, NULL, NULL);
	verify->md5_client = NULL;
	verify->md5_server = NULL;
	verify->md5_cert = NULL;
	verify->sha1_client = NULL;
	verify->sha1_server = NULL;
	verify->sha1_cert = NULL;
}
