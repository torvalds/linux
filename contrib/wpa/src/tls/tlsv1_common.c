/*
 * TLSv1 common routines
 * Copyright (c) 2006-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
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
	{ TLS_DHE_RSA_WITH_DES_CBC_SHA, TLS_KEY_X_DHE_RSA, TLS_CIPHER_DES_CBC,
	  TLS_HASH_SHA},
	{ TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA, TLS_KEY_X_DHE_RSA,
	  TLS_CIPHER_3DES_EDE_CBC, TLS_HASH_SHA },
 	{ TLS_DH_anon_WITH_RC4_128_MD5, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_RC4_128, TLS_HASH_MD5 },
 	{ TLS_DH_anon_WITH_DES_CBC_SHA, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_DES_CBC, TLS_HASH_SHA },
 	{ TLS_DH_anon_WITH_3DES_EDE_CBC_SHA, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_3DES_EDE_CBC, TLS_HASH_SHA },
	{ TLS_RSA_WITH_AES_128_CBC_SHA, TLS_KEY_X_RSA, TLS_CIPHER_AES_128_CBC,
	  TLS_HASH_SHA },
	{ TLS_DHE_RSA_WITH_AES_128_CBC_SHA, TLS_KEY_X_DHE_RSA,
	  TLS_CIPHER_AES_128_CBC, TLS_HASH_SHA },
	{ TLS_DH_anon_WITH_AES_128_CBC_SHA, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_AES_128_CBC, TLS_HASH_SHA },
	{ TLS_RSA_WITH_AES_256_CBC_SHA, TLS_KEY_X_RSA, TLS_CIPHER_AES_256_CBC,
	  TLS_HASH_SHA },
	{ TLS_DHE_RSA_WITH_AES_256_CBC_SHA, TLS_KEY_X_DHE_RSA,
	  TLS_CIPHER_AES_256_CBC, TLS_HASH_SHA },
	{ TLS_DH_anon_WITH_AES_256_CBC_SHA, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_AES_256_CBC, TLS_HASH_SHA },
	{ TLS_RSA_WITH_AES_128_CBC_SHA256, TLS_KEY_X_RSA,
	  TLS_CIPHER_AES_128_CBC, TLS_HASH_SHA256 },
	{ TLS_RSA_WITH_AES_256_CBC_SHA256, TLS_KEY_X_RSA,
	  TLS_CIPHER_AES_256_CBC, TLS_HASH_SHA256 },
	{ TLS_DHE_RSA_WITH_AES_128_CBC_SHA256, TLS_KEY_X_DHE_RSA,
	  TLS_CIPHER_AES_128_CBC, TLS_HASH_SHA256 },
	{ TLS_DHE_RSA_WITH_AES_256_CBC_SHA256, TLS_KEY_X_DHE_RSA,
	  TLS_CIPHER_AES_256_CBC, TLS_HASH_SHA256 },
	{ TLS_DH_anon_WITH_AES_128_CBC_SHA256, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_AES_128_CBC, TLS_HASH_SHA256 },
	{ TLS_DH_anon_WITH_AES_256_CBC_SHA256, TLS_KEY_X_DH_anon,
	  TLS_CIPHER_AES_256_CBC, TLS_HASH_SHA256 }
};

#define NUM_TLS_CIPHER_SUITES ARRAY_SIZE(tls_cipher_suites)


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

#define NUM_TLS_CIPHER_DATA ARRAY_SIZE(tls_ciphers)


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
#ifdef CONFIG_TLSV12
	verify->sha256_client = crypto_hash_init(CRYPTO_HASH_ALG_SHA256, NULL,
						 0);
	verify->sha256_server = crypto_hash_init(CRYPTO_HASH_ALG_SHA256, NULL,
						 0);
	verify->sha256_cert = crypto_hash_init(CRYPTO_HASH_ALG_SHA256, NULL,
					       0);
	if (verify->sha256_client == NULL || verify->sha256_server == NULL ||
	    verify->sha256_cert == NULL) {
		tls_verify_hash_free(verify);
		return -1;
	}
#endif /* CONFIG_TLSV12 */
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
#ifdef CONFIG_TLSV12
	if (verify->sha256_client)
		crypto_hash_update(verify->sha256_client, buf, len);
	if (verify->sha256_server)
		crypto_hash_update(verify->sha256_server, buf, len);
	if (verify->sha256_cert)
		crypto_hash_update(verify->sha256_cert, buf, len);
#endif /* CONFIG_TLSV12 */
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
#ifdef CONFIG_TLSV12
	crypto_hash_finish(verify->sha256_client, NULL, NULL);
	crypto_hash_finish(verify->sha256_server, NULL, NULL);
	crypto_hash_finish(verify->sha256_cert, NULL, NULL);
	verify->sha256_client = NULL;
	verify->sha256_server = NULL;
	verify->sha256_cert = NULL;
#endif /* CONFIG_TLSV12 */
}


int tls_version_ok(u16 ver)
{
	if (ver == TLS_VERSION_1)
		return 1;
#ifdef CONFIG_TLSV11
	if (ver == TLS_VERSION_1_1)
		return 1;
#endif /* CONFIG_TLSV11 */
#ifdef CONFIG_TLSV12
	if (ver == TLS_VERSION_1_2)
		return 1;
#endif /* CONFIG_TLSV12 */

	return 0;
}


const char * tls_version_str(u16 ver)
{
	switch (ver) {
	case TLS_VERSION_1:
		return "1.0";
	case TLS_VERSION_1_1:
		return "1.1";
	case TLS_VERSION_1_2:
		return "1.2";
	}

	return "?";
}


int tls_prf(u16 ver, const u8 *secret, size_t secret_len, const char *label,
	    const u8 *seed, size_t seed_len, u8 *out, size_t outlen)
{
#ifdef CONFIG_TLSV12
	if (ver >= TLS_VERSION_1_2) {
		tls_prf_sha256(secret, secret_len, label, seed, seed_len,
			       out, outlen);
		return 0;
	}
#endif /* CONFIG_TLSV12 */

	return tls_prf_sha1_md5(secret, secret_len, label, seed, seed_len, out,
				outlen);
}


#ifdef CONFIG_TLSV12
int tlsv12_key_x_server_params_hash(u16 tls_version, u8 hash_alg,
				    const u8 *client_random,
				    const u8 *server_random,
				    const u8 *server_params,
				    size_t server_params_len, u8 *hash)
{
	size_t hlen;
	struct crypto_hash *ctx;
	enum crypto_hash_alg alg;

	switch (hash_alg) {
	case TLS_HASH_ALG_SHA256:
		alg = CRYPTO_HASH_ALG_SHA256;
		hlen = SHA256_MAC_LEN;
		break;
	case TLS_HASH_ALG_SHA384:
		alg = CRYPTO_HASH_ALG_SHA384;
		hlen = 48;
		break;
	case TLS_HASH_ALG_SHA512:
		alg = CRYPTO_HASH_ALG_SHA512;
		hlen = 64;
		break;
	default:
		return -1;
	}
	ctx = crypto_hash_init(alg, NULL, 0);
	if (ctx == NULL)
		return -1;
	crypto_hash_update(ctx, client_random, TLS_RANDOM_LEN);
	crypto_hash_update(ctx, server_random, TLS_RANDOM_LEN);
	crypto_hash_update(ctx, server_params, server_params_len);
	if (crypto_hash_finish(ctx, hash, &hlen) < 0)
		return -1;

	return hlen;
}
#endif /* CONFIG_TLSV12 */


int tls_key_x_server_params_hash(u16 tls_version, const u8 *client_random,
				 const u8 *server_random,
				 const u8 *server_params,
				 size_t server_params_len, u8 *hash)
{
	u8 *hpos;
	size_t hlen;
	struct crypto_hash *ctx;

	hpos = hash;

	ctx = crypto_hash_init(CRYPTO_HASH_ALG_MD5, NULL, 0);
	if (ctx == NULL)
		return -1;
	crypto_hash_update(ctx, client_random, TLS_RANDOM_LEN);
	crypto_hash_update(ctx, server_random, TLS_RANDOM_LEN);
	crypto_hash_update(ctx, server_params, server_params_len);
	hlen = MD5_MAC_LEN;
	if (crypto_hash_finish(ctx, hash, &hlen) < 0)
		return -1;
	hpos += hlen;

	ctx = crypto_hash_init(CRYPTO_HASH_ALG_SHA1, NULL, 0);
	if (ctx == NULL)
		return -1;
	crypto_hash_update(ctx, client_random, TLS_RANDOM_LEN);
	crypto_hash_update(ctx, server_random, TLS_RANDOM_LEN);
	crypto_hash_update(ctx, server_params, server_params_len);
	hlen = hash + sizeof(hash) - hpos;
	if (crypto_hash_finish(ctx, hpos, &hlen) < 0)
		return -1;
	hpos += hlen;
	return hpos - hash;
}


int tls_verify_signature(u16 tls_version, struct crypto_public_key *pk,
			 const u8 *data, size_t data_len,
			 const u8 *pos, size_t len, u8 *alert)
{
	u8 *buf;
	const u8 *end = pos + len;
	const u8 *decrypted;
	u16 slen;
	size_t buflen;

	if (end - pos < 2) {
		*alert = TLS_ALERT_DECODE_ERROR;
		return -1;
	}
	slen = WPA_GET_BE16(pos);
	pos += 2;
	if (end - pos < slen) {
		*alert = TLS_ALERT_DECODE_ERROR;
		return -1;
	}
	if (end - pos > slen) {
		wpa_hexdump(MSG_MSGDUMP, "Additional data after Signature",
			    pos + slen, end - pos - slen);
		end = pos + slen;
	}

	wpa_hexdump(MSG_MSGDUMP, "TLSv1: Signature", pos, end - pos);
	if (pk == NULL) {
		wpa_printf(MSG_DEBUG, "TLSv1: No public key to verify signature");
		*alert = TLS_ALERT_INTERNAL_ERROR;
		return -1;
	}

	buflen = end - pos;
	buf = os_malloc(end - pos);
	if (buf == NULL) {
		*alert = TLS_ALERT_INTERNAL_ERROR;
		return -1;
	}
	if (crypto_public_key_decrypt_pkcs1(pk, pos, end - pos, buf, &buflen) <
	    0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to decrypt signature");
		os_free(buf);
		*alert = TLS_ALERT_DECRYPT_ERROR;
		return -1;
	}
	decrypted = buf;

	wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: Decrypted Signature",
			decrypted, buflen);

#ifdef CONFIG_TLSV12
	if (tls_version >= TLS_VERSION_1_2) {
		/*
		 * RFC 3447, A.2.4 RSASSA-PKCS1-v1_5
		 *
		 * DigestInfo ::= SEQUENCE {
		 *   digestAlgorithm DigestAlgorithm,
		 *   digest OCTET STRING
		 * }
		 *
		 * SHA-256 OID: sha256WithRSAEncryption ::= {pkcs-1 11}
		 *
		 * DER encoded DigestInfo for SHA256 per RFC 3447:
		 * 30 31 30 0d 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20 ||
		 * H
		 */
		if (buflen >= 19 + 32 &&
		    os_memcmp(buf, "\x30\x31\x30\x0d\x06\x09\x60\x86\x48\x01"
			      "\x65\x03\x04\x02\x01\x05\x00\x04\x20", 19) == 0)
		{
			wpa_printf(MSG_DEBUG, "TLSv1.2: DigestAlgorithm = SHA-256");
			decrypted = buf + 19;
			buflen -= 19;
		} else if (buflen >= 19 + 48 &&
		    os_memcmp(buf, "\x30\x41\x30\x0d\x06\x09\x60\x86\x48\x01"
			      "\x65\x03\x04\x02\x02\x05\x00\x04\x30", 19) == 0)
		{
			wpa_printf(MSG_DEBUG, "TLSv1.2: DigestAlgorithm = SHA-384");
			decrypted = buf + 19;
			buflen -= 19;
		} else if (buflen >= 19 + 64 &&
		    os_memcmp(buf, "\x30\x51\x30\x0d\x06\x09\x60\x86\x48\x01"
			      "\x65\x03\x04\x02\x03\x05\x00\x04\x40", 19) == 0)
		{
			wpa_printf(MSG_DEBUG, "TLSv1.2: DigestAlgorithm = SHA-512");
			decrypted = buf + 19;
			buflen -= 19;

		} else {
			wpa_printf(MSG_DEBUG, "TLSv1.2: Unrecognized DigestInfo");
			os_free(buf);
			*alert = TLS_ALERT_DECRYPT_ERROR;
			return -1;
		}
	}
#endif /* CONFIG_TLSV12 */

	if (buflen != data_len ||
	    os_memcmp_const(decrypted, data, data_len) != 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Invalid Signature in CertificateVerify - did not match calculated hash");
		os_free(buf);
		*alert = TLS_ALERT_DECRYPT_ERROR;
		return -1;
	}

	os_free(buf);

	return 0;
}
