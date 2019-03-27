/*
 * TLSv1 Record Protocol
 * Copyright (c) 2006-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TLSV1_RECORD_H
#define TLSV1_RECORD_H

#include "crypto/crypto.h"

#define TLS_MAX_WRITE_MAC_SECRET_LEN 32
#define TLS_MAX_WRITE_KEY_LEN 32
#define TLS_MAX_IV_LEN 16
#define TLS_MAX_KEY_BLOCK_LEN (2 * (TLS_MAX_WRITE_MAC_SECRET_LEN + \
				    TLS_MAX_WRITE_KEY_LEN + TLS_MAX_IV_LEN))

#define TLS_SEQ_NUM_LEN 8
#define TLS_RECORD_HEADER_LEN 5

/* ContentType */
enum {
	TLS_CONTENT_TYPE_CHANGE_CIPHER_SPEC = 20,
	TLS_CONTENT_TYPE_ALERT = 21,
	TLS_CONTENT_TYPE_HANDSHAKE = 22,
	TLS_CONTENT_TYPE_APPLICATION_DATA = 23
};

struct tlsv1_record_layer {
	u16 tls_version;

	u8 write_mac_secret[TLS_MAX_WRITE_MAC_SECRET_LEN];
	u8 read_mac_secret[TLS_MAX_WRITE_MAC_SECRET_LEN];
	u8 write_key[TLS_MAX_WRITE_KEY_LEN];
	u8 read_key[TLS_MAX_WRITE_KEY_LEN];
	u8 write_iv[TLS_MAX_IV_LEN];
	u8 read_iv[TLS_MAX_IV_LEN];

	size_t hash_size;
	size_t key_material_len;
	size_t iv_size; /* also block_size */

	enum crypto_hash_alg hash_alg;
	enum crypto_cipher_alg cipher_alg;

	u8 write_seq_num[TLS_SEQ_NUM_LEN];
	u8 read_seq_num[TLS_SEQ_NUM_LEN];

	u16 cipher_suite;
	u16 write_cipher_suite;
	u16 read_cipher_suite;

	struct crypto_cipher *write_cbc;
	struct crypto_cipher *read_cbc;
};


int tlsv1_record_set_cipher_suite(struct tlsv1_record_layer *rl,
				  u16 cipher_suite);
int tlsv1_record_change_write_cipher(struct tlsv1_record_layer *rl);
int tlsv1_record_change_read_cipher(struct tlsv1_record_layer *rl);
int tlsv1_record_send(struct tlsv1_record_layer *rl, u8 content_type, u8 *buf,
		      size_t buf_size, const u8 *payload, size_t payload_len,
		      size_t *out_len);
int tlsv1_record_receive(struct tlsv1_record_layer *rl,
			 const u8 *in_data, size_t in_len,
			 u8 *out_data, size_t *out_len, u8 *alert);

#endif /* TLSV1_RECORD_H */
