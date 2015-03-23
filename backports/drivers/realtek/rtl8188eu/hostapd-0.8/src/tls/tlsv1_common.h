/*
 * TLSv1 common definitions
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

#ifndef TLSV1_COMMON_H
#define TLSV1_COMMON_H

#include "crypto/crypto.h"

#define TLS_VERSION 0x0301 /* TLSv1 */
#define TLS_RANDOM_LEN 32
#define TLS_PRE_MASTER_SECRET_LEN 48
#define TLS_MASTER_SECRET_LEN 48
#define TLS_SESSION_ID_MAX_LEN 32
#define TLS_VERIFY_DATA_LEN 12

/* HandshakeType */
enum {
	TLS_HANDSHAKE_TYPE_HELLO_REQUEST = 0,
	TLS_HANDSHAKE_TYPE_CLIENT_HELLO = 1,
	TLS_HANDSHAKE_TYPE_SERVER_HELLO = 2,
	TLS_HANDSHAKE_TYPE_NEW_SESSION_TICKET = 4 /* RFC 4507 */,
	TLS_HANDSHAKE_TYPE_CERTIFICATE = 11,
	TLS_HANDSHAKE_TYPE_SERVER_KEY_EXCHANGE = 12,
	TLS_HANDSHAKE_TYPE_CERTIFICATE_REQUEST = 13,
	TLS_HANDSHAKE_TYPE_SERVER_HELLO_DONE = 14,
	TLS_HANDSHAKE_TYPE_CERTIFICATE_VERIFY = 15,
	TLS_HANDSHAKE_TYPE_CLIENT_KEY_EXCHANGE = 16,
	TLS_HANDSHAKE_TYPE_FINISHED = 20,
	TLS_HANDSHAKE_TYPE_CERTIFICATE_URL = 21 /* RFC 4366 */,
	TLS_HANDSHAKE_TYPE_CERTIFICATE_STATUS = 22 /* RFC 4366 */
};

/* CipherSuite */
#define TLS_NULL_WITH_NULL_NULL			0x0000 /* RFC 2246 */
#define TLS_RSA_WITH_NULL_MD5			0x0001 /* RFC 2246 */
#define TLS_RSA_WITH_NULL_SHA			0x0002 /* RFC 2246 */
#define TLS_RSA_EXPORT_WITH_RC4_40_MD5		0x0003 /* RFC 2246 */
#define TLS_RSA_WITH_RC4_128_MD5		0x0004 /* RFC 2246 */
#define TLS_RSA_WITH_RC4_128_SHA		0x0005 /* RFC 2246 */
#define TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5	0x0006 /* RFC 2246 */
#define TLS_RSA_WITH_IDEA_CBC_SHA		0x0007 /* RFC 2246 */
#define TLS_RSA_EXPORT_WITH_DES40_CBC_SHA	0x0008 /* RFC 2246 */
#define TLS_RSA_WITH_DES_CBC_SHA		0x0009 /* RFC 2246 */
#define TLS_RSA_WITH_3DES_EDE_CBC_SHA		0x000A /* RFC 2246 */
#define TLS_DH_DSS_EXPORT_WITH_DES40_CBC_SHA	0x000B /* RFC 2246 */
#define TLS_DH_DSS_WITH_DES_CBC_SHA		0x000C /* RFC 2246 */
#define TLS_DH_DSS_WITH_3DES_EDE_CBC_SHA	0x000D /* RFC 2246 */
#define TLS_DH_RSA_EXPORT_WITH_DES40_CBC_SHA	0x000E /* RFC 2246 */
#define TLS_DH_RSA_WITH_DES_CBC_SHA		0x000F /* RFC 2246 */
#define TLS_DH_RSA_WITH_3DES_EDE_CBC_SHA	0x0010 /* RFC 2246 */
#define TLS_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA	0x0011 /* RFC 2246 */
#define TLS_DHE_DSS_WITH_DES_CBC_SHA		0x0012 /* RFC 2246 */
#define TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA	0x0013 /* RFC 2246 */
#define TLS_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA	0x0014 /* RFC 2246 */
#define TLS_DHE_RSA_WITH_DES_CBC_SHA		0x0015 /* RFC 2246 */
#define TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA	0x0016 /* RFC 2246 */
#define TLS_DH_anon_EXPORT_WITH_RC4_40_MD5	0x0017 /* RFC 2246 */
#define TLS_DH_anon_WITH_RC4_128_MD5		0x0018 /* RFC 2246 */
#define TLS_DH_anon_EXPORT_WITH_DES40_CBC_SHA	0x0019 /* RFC 2246 */
#define TLS_DH_anon_WITH_DES_CBC_SHA		0x001A /* RFC 2246 */
#define TLS_DH_anon_WITH_3DES_EDE_CBC_SHA	0x001B /* RFC 2246 */
#define TLS_RSA_WITH_AES_128_CBC_SHA		0x002F /* RFC 3268 */
#define TLS_DH_DSS_WITH_AES_128_CBC_SHA		0x0030 /* RFC 3268 */
#define TLS_DH_RSA_WITH_AES_128_CBC_SHA		0x0031 /* RFC 3268 */
#define TLS_DHE_DSS_WITH_AES_128_CBC_SHA	0x0032 /* RFC 3268 */
#define TLS_DHE_RSA_WITH_AES_128_CBC_SHA	0x0033 /* RFC 3268 */
#define TLS_DH_anon_WITH_AES_128_CBC_SHA	0x0034 /* RFC 3268 */
#define TLS_RSA_WITH_AES_256_CBC_SHA		0x0035 /* RFC 3268 */
#define TLS_DH_DSS_WITH_AES_256_CBC_SHA		0x0036 /* RFC 3268 */
#define TLS_DH_RSA_WITH_AES_256_CBC_SHA		0x0037 /* RFC 3268 */
#define TLS_DHE_DSS_WITH_AES_256_CBC_SHA	0x0038 /* RFC 3268 */
#define TLS_DHE_RSA_WITH_AES_256_CBC_SHA	0x0039 /* RFC 3268 */
#define TLS_DH_anon_WITH_AES_256_CBC_SHA	0x003A /* RFC 3268 */

/* CompressionMethod */
#define TLS_COMPRESSION_NULL 0

/* AlertLevel */
#define TLS_ALERT_LEVEL_WARNING 1
#define TLS_ALERT_LEVEL_FATAL 2

/* AlertDescription */
#define TLS_ALERT_CLOSE_NOTIFY			0
#define TLS_ALERT_UNEXPECTED_MESSAGE		10
#define TLS_ALERT_BAD_RECORD_MAC		20
#define TLS_ALERT_DECRYPTION_FAILED		21
#define TLS_ALERT_RECORD_OVERFLOW		22
#define TLS_ALERT_DECOMPRESSION_FAILURE		30
#define TLS_ALERT_HANDSHAKE_FAILURE		40
#define TLS_ALERT_BAD_CERTIFICATE		42
#define TLS_ALERT_UNSUPPORTED_CERTIFICATE	43
#define TLS_ALERT_CERTIFICATE_REVOKED		44
#define TLS_ALERT_CERTIFICATE_EXPIRED		45
#define TLS_ALERT_CERTIFICATE_UNKNOWN		46
#define TLS_ALERT_ILLEGAL_PARAMETER		47
#define TLS_ALERT_UNKNOWN_CA			48
#define TLS_ALERT_ACCESS_DENIED			49
#define TLS_ALERT_DECODE_ERROR			50
#define TLS_ALERT_DECRYPT_ERROR			51
#define TLS_ALERT_EXPORT_RESTRICTION		60
#define TLS_ALERT_PROTOCOL_VERSION		70
#define TLS_ALERT_INSUFFICIENT_SECURITY		71
#define TLS_ALERT_INTERNAL_ERROR		80
#define TLS_ALERT_USER_CANCELED			90
#define TLS_ALERT_NO_RENEGOTIATION		100
#define TLS_ALERT_UNSUPPORTED_EXTENSION		110 /* RFC 4366 */
#define TLS_ALERT_CERTIFICATE_UNOBTAINABLE	111 /* RFC 4366 */
#define TLS_ALERT_UNRECOGNIZED_NAME		112 /* RFC 4366 */
#define TLS_ALERT_BAD_CERTIFICATE_STATUS_RESPONSE	113 /* RFC 4366 */
#define TLS_ALERT_BAD_CERTIFICATE_HASH_VALUE	114 /* RFC 4366 */

/* ChangeCipherSpec */
enum {
	TLS_CHANGE_CIPHER_SPEC = 1
};

/* TLS Extensions */
#define TLS_EXT_SERVER_NAME			0 /* RFC 4366 */
#define TLS_EXT_MAX_FRAGMENT_LENGTH		1 /* RFC 4366 */
#define TLS_EXT_CLIENT_CERTIFICATE_URL		2 /* RFC 4366 */
#define TLS_EXT_TRUSTED_CA_KEYS			3 /* RFC 4366 */
#define TLS_EXT_TRUNCATED_HMAC			4 /* RFC 4366 */
#define TLS_EXT_STATUS_REQUEST			5 /* RFC 4366 */
#define TLS_EXT_SESSION_TICKET			35 /* RFC 4507 */

#define TLS_EXT_PAC_OPAQUE TLS_EXT_SESSION_TICKET /* EAP-FAST terminology */


typedef enum {
	TLS_KEY_X_NULL,
	TLS_KEY_X_RSA,
	TLS_KEY_X_RSA_EXPORT,
	TLS_KEY_X_DH_DSS_EXPORT,
	TLS_KEY_X_DH_DSS,
	TLS_KEY_X_DH_RSA_EXPORT,
	TLS_KEY_X_DH_RSA,
	TLS_KEY_X_DHE_DSS_EXPORT,
	TLS_KEY_X_DHE_DSS,
	TLS_KEY_X_DHE_RSA_EXPORT,
	TLS_KEY_X_DHE_RSA,
	TLS_KEY_X_DH_anon_EXPORT,
	TLS_KEY_X_DH_anon
} tls_key_exchange;

typedef enum {
	TLS_CIPHER_NULL,
	TLS_CIPHER_RC4_40,
	TLS_CIPHER_RC4_128,
	TLS_CIPHER_RC2_CBC_40,
	TLS_CIPHER_IDEA_CBC,
	TLS_CIPHER_DES40_CBC,
	TLS_CIPHER_DES_CBC,
	TLS_CIPHER_3DES_EDE_CBC,
	TLS_CIPHER_AES_128_CBC,
	TLS_CIPHER_AES_256_CBC
} tls_cipher;

typedef enum {
	TLS_HASH_NULL,
	TLS_HASH_MD5,
	TLS_HASH_SHA
} tls_hash;

struct tls_cipher_suite {
	u16 suite;
	tls_key_exchange key_exchange;
	tls_cipher cipher;
	tls_hash hash;
};

typedef enum {
	TLS_CIPHER_STREAM,
	TLS_CIPHER_BLOCK
} tls_cipher_type;

struct tls_cipher_data {
	tls_cipher cipher;
	tls_cipher_type type;
	size_t key_material;
	size_t expanded_key_material;
	size_t block_size; /* also iv_size */
	enum crypto_cipher_alg alg;
};


struct tls_verify_hash {
	struct crypto_hash *md5_client;
	struct crypto_hash *sha1_client;
	struct crypto_hash *md5_server;
	struct crypto_hash *sha1_server;
	struct crypto_hash *md5_cert;
	struct crypto_hash *sha1_cert;
};


const struct tls_cipher_suite * tls_get_cipher_suite(u16 suite);
const struct tls_cipher_data * tls_get_cipher_data(tls_cipher cipher);
int tls_server_key_exchange_allowed(tls_cipher cipher);
int tls_parse_cert(const u8 *buf, size_t len, struct crypto_public_key **pk);
int tls_verify_hash_init(struct tls_verify_hash *verify);
void tls_verify_hash_add(struct tls_verify_hash *verify, const u8 *buf,
			 size_t len);
void tls_verify_hash_free(struct tls_verify_hash *verify);

#endif /* TLSV1_COMMON_H */
