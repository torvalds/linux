/*
 * TLSv1 client (RFC 2246)
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
#include "crypto/sha1.h"
#include "crypto/tls.h"
#include "tlsv1_common.h"
#include "tlsv1_record.h"
#include "tlsv1_client.h"
#include "tlsv1_client_i.h"

/* TODO:
 * Support for a message fragmented across several records (RFC 2246, 6.2.1)
 */


void tls_alert(struct tlsv1_client *conn, u8 level, u8 description)
{
	conn->alert_level = level;
	conn->alert_description = description;
}


void tlsv1_client_free_dh(struct tlsv1_client *conn)
{
	os_free(conn->dh_p);
	os_free(conn->dh_g);
	os_free(conn->dh_ys);
	conn->dh_p = conn->dh_g = conn->dh_ys = NULL;
}


int tls_derive_pre_master_secret(u8 *pre_master_secret)
{
	WPA_PUT_BE16(pre_master_secret, TLS_VERSION);
	if (os_get_random(pre_master_secret + 2,
			  TLS_PRE_MASTER_SECRET_LEN - 2))
		return -1;
	return 0;
}


int tls_derive_keys(struct tlsv1_client *conn,
		    const u8 *pre_master_secret, size_t pre_master_secret_len)
{
	u8 seed[2 * TLS_RANDOM_LEN];
	u8 key_block[TLS_MAX_KEY_BLOCK_LEN];
	u8 *pos;
	size_t key_block_len;

	if (pre_master_secret) {
		wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: pre_master_secret",
				pre_master_secret, pre_master_secret_len);
		os_memcpy(seed, conn->client_random, TLS_RANDOM_LEN);
		os_memcpy(seed + TLS_RANDOM_LEN, conn->server_random,
			  TLS_RANDOM_LEN);
		if (tls_prf(pre_master_secret, pre_master_secret_len,
			    "master secret", seed, 2 * TLS_RANDOM_LEN,
			    conn->master_secret, TLS_MASTER_SECRET_LEN)) {
			wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive "
				   "master_secret");
			return -1;
		}
		wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: master_secret",
				conn->master_secret, TLS_MASTER_SECRET_LEN);
	}

	os_memcpy(seed, conn->server_random, TLS_RANDOM_LEN);
	os_memcpy(seed + TLS_RANDOM_LEN, conn->client_random, TLS_RANDOM_LEN);
	key_block_len = 2 * (conn->rl.hash_size + conn->rl.key_material_len +
			     conn->rl.iv_size);
	if (tls_prf(conn->master_secret, TLS_MASTER_SECRET_LEN,
		    "key expansion", seed, 2 * TLS_RANDOM_LEN,
		    key_block, key_block_len)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive key_block");
		return -1;
	}
	wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: key_block",
			key_block, key_block_len);

	pos = key_block;

	/* client_write_MAC_secret */
	os_memcpy(conn->rl.write_mac_secret, pos, conn->rl.hash_size);
	pos += conn->rl.hash_size;
	/* server_write_MAC_secret */
	os_memcpy(conn->rl.read_mac_secret, pos, conn->rl.hash_size);
	pos += conn->rl.hash_size;

	/* client_write_key */
	os_memcpy(conn->rl.write_key, pos, conn->rl.key_material_len);
	pos += conn->rl.key_material_len;
	/* server_write_key */
	os_memcpy(conn->rl.read_key, pos, conn->rl.key_material_len);
	pos += conn->rl.key_material_len;

	/* client_write_IV */
	os_memcpy(conn->rl.write_iv, pos, conn->rl.iv_size);
	pos += conn->rl.iv_size;
	/* server_write_IV */
	os_memcpy(conn->rl.read_iv, pos, conn->rl.iv_size);
	pos += conn->rl.iv_size;

	return 0;
}


/**
 * tlsv1_client_handshake - Process TLS handshake
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @in_data: Input data from TLS peer
 * @in_len: Input data length
 * @out_len: Length of the output buffer.
 * @appl_data: Pointer to application data pointer, or %NULL if dropped
 * @appl_data_len: Pointer to variable that is set to appl_data length
 * Returns: Pointer to output data, %NULL on failure
 */
u8 * tlsv1_client_handshake(struct tlsv1_client *conn,
			    const u8 *in_data, size_t in_len,
			    size_t *out_len, u8 **appl_data,
			    size_t *appl_data_len)
{
	const u8 *pos, *end;
	u8 *msg = NULL, *in_msg, *in_pos, *in_end, alert, ct;
	size_t in_msg_len;
	int no_appl_data;

	if (conn->state == CLIENT_HELLO) {
		if (in_len)
			return NULL;
		return tls_send_client_hello(conn, out_len);
	}

	if (in_data == NULL || in_len == 0)
		return NULL;

	pos = in_data;
	end = in_data + in_len;
	in_msg = os_malloc(in_len);
	if (in_msg == NULL)
		return NULL;

	/* Each received packet may include multiple records */
	while (pos < end) {
		in_msg_len = in_len;
		if (tlsv1_record_receive(&conn->rl, pos, end - pos,
					 in_msg, &in_msg_len, &alert)) {
			wpa_printf(MSG_DEBUG, "TLSv1: Processing received "
				   "record failed");
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL, alert);
			goto failed;
		}
		ct = pos[0];

		in_pos = in_msg;
		in_end = in_msg + in_msg_len;

		/* Each received record may include multiple messages of the
		 * same ContentType. */
		while (in_pos < in_end) {
			in_msg_len = in_end - in_pos;
			if (tlsv1_client_process_handshake(conn, ct, in_pos,
							   &in_msg_len,
							   appl_data,
							   appl_data_len) < 0)
				goto failed;
			in_pos += in_msg_len;
		}

		pos += TLS_RECORD_HEADER_LEN + WPA_GET_BE16(pos + 3);
	}

	os_free(in_msg);
	in_msg = NULL;

	no_appl_data = appl_data == NULL || *appl_data == NULL;
	msg = tlsv1_client_handshake_write(conn, out_len, no_appl_data);

failed:
	os_free(in_msg);
	if (conn->alert_level) {
		conn->state = FAILED;
		os_free(msg);
		msg = tlsv1_client_send_alert(conn, conn->alert_level,
					      conn->alert_description,
					      out_len);
	} else if (msg == NULL) {
		msg = os_zalloc(1);
		*out_len = 0;
	}

	return msg;
}


/**
 * tlsv1_client_encrypt - Encrypt data into TLS tunnel
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @in_data: Pointer to plaintext data to be encrypted
 * @in_len: Input buffer length
 * @out_data: Pointer to output buffer (encrypted TLS data)
 * @out_len: Maximum out_data length 
 * Returns: Number of bytes written to out_data, -1 on failure
 *
 * This function is used after TLS handshake has been completed successfully to
 * send data in the encrypted tunnel.
 */
int tlsv1_client_encrypt(struct tlsv1_client *conn,
			 const u8 *in_data, size_t in_len,
			 u8 *out_data, size_t out_len)
{
	size_t rlen;

	wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: Plaintext AppData",
			in_data, in_len);

	os_memcpy(out_data + TLS_RECORD_HEADER_LEN, in_data, in_len);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_APPLICATION_DATA,
			      out_data, out_len, in_len, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to create a record");
		tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
			  TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	return rlen;
}


/**
 * tlsv1_client_decrypt - Decrypt data from TLS tunnel
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @in_data: Pointer to input buffer (encrypted TLS data)
 * @in_len: Input buffer length
 * @out_data: Pointer to output buffer (decrypted data from TLS tunnel)
 * @out_len: Maximum out_data length
 * Returns: Number of bytes written to out_data, -1 on failure
 *
 * This function is used after TLS handshake has been completed successfully to
 * receive data from the encrypted tunnel.
 */
int tlsv1_client_decrypt(struct tlsv1_client *conn,
			 const u8 *in_data, size_t in_len,
			 u8 *out_data, size_t out_len)
{
	const u8 *in_end, *pos;
	int res;
	u8 alert, *out_end, *out_pos;
	size_t olen;

	pos = in_data;
	in_end = in_data + in_len;
	out_pos = out_data;
	out_end = out_data + out_len;

	while (pos < in_end) {
		if (pos[0] != TLS_CONTENT_TYPE_APPLICATION_DATA) {
			wpa_printf(MSG_DEBUG, "TLSv1: Unexpected content type "
				   "0x%x", pos[0]);
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_UNEXPECTED_MESSAGE);
			return -1;
		}

		olen = out_end - out_pos;
		res = tlsv1_record_receive(&conn->rl, pos, in_end - pos,
					   out_pos, &olen, &alert);
		if (res < 0) {
			wpa_printf(MSG_DEBUG, "TLSv1: Record layer processing "
				   "failed");
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL, alert);
			return -1;
		}
		out_pos += olen;
		if (out_pos > out_end) {
			wpa_printf(MSG_DEBUG, "TLSv1: Buffer not large enough "
				   "for processing the received record");
			tls_alert(conn, TLS_ALERT_LEVEL_FATAL,
				  TLS_ALERT_INTERNAL_ERROR);
			return -1;
		}

		pos += TLS_RECORD_HEADER_LEN + WPA_GET_BE16(pos + 3);
	}

	return out_pos - out_data;
}


/**
 * tlsv1_client_global_init - Initialize TLSv1 client
 * Returns: 0 on success, -1 on failure
 *
 * This function must be called before using any other TLSv1 client functions.
 */
int tlsv1_client_global_init(void)
{
	return crypto_global_init();
}


/**
 * tlsv1_client_global_deinit - Deinitialize TLSv1 client
 *
 * This function can be used to deinitialize the TLSv1 client that was
 * initialized by calling tlsv1_client_global_init(). No TLSv1 client functions
 * can be called after this before calling tlsv1_client_global_init() again.
 */
void tlsv1_client_global_deinit(void)
{
	crypto_global_deinit();
}


/**
 * tlsv1_client_init - Initialize TLSv1 client connection
 * Returns: Pointer to TLSv1 client connection data or %NULL on failure
 */
struct tlsv1_client * tlsv1_client_init(void)
{
	struct tlsv1_client *conn;
	size_t count;
	u16 *suites;

	conn = os_zalloc(sizeof(*conn));
	if (conn == NULL)
		return NULL;

	conn->state = CLIENT_HELLO;

	if (tls_verify_hash_init(&conn->verify) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to initialize verify "
			   "hash");
		os_free(conn);
		return NULL;
	}

	count = 0;
	suites = conn->cipher_suites;
#ifndef CONFIG_CRYPTO_INTERNAL
	suites[count++] = TLS_RSA_WITH_AES_256_CBC_SHA;
#endif /* CONFIG_CRYPTO_INTERNAL */
	suites[count++] = TLS_RSA_WITH_AES_128_CBC_SHA;
	suites[count++] = TLS_RSA_WITH_3DES_EDE_CBC_SHA;
	suites[count++] = TLS_RSA_WITH_RC4_128_SHA;
	suites[count++] = TLS_RSA_WITH_RC4_128_MD5;
	conn->num_cipher_suites = count;

	return conn;
}


/**
 * tlsv1_client_deinit - Deinitialize TLSv1 client connection
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 */
void tlsv1_client_deinit(struct tlsv1_client *conn)
{
	crypto_public_key_free(conn->server_rsa_key);
	tlsv1_record_set_cipher_suite(&conn->rl, TLS_NULL_WITH_NULL_NULL);
	tlsv1_record_change_write_cipher(&conn->rl);
	tlsv1_record_change_read_cipher(&conn->rl);
	tls_verify_hash_free(&conn->verify);
	os_free(conn->client_hello_ext);
	tlsv1_client_free_dh(conn);
	tlsv1_cred_free(conn->cred);
	os_free(conn);
}


/**
 * tlsv1_client_established - Check whether connection has been established
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * Returns: 1 if connection is established, 0 if not
 */
int tlsv1_client_established(struct tlsv1_client *conn)
{
	return conn->state == ESTABLISHED;
}


/**
 * tlsv1_client_prf - Use TLS-PRF to derive keying material
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @label: Label (e.g., description of the key) for PRF
 * @server_random_first: seed is 0 = client_random|server_random,
 * 1 = server_random|client_random
 * @out: Buffer for output data from TLS-PRF
 * @out_len: Length of the output buffer
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_client_prf(struct tlsv1_client *conn, const char *label,
		     int server_random_first, u8 *out, size_t out_len)
{
	u8 seed[2 * TLS_RANDOM_LEN];

	if (conn->state != ESTABLISHED)
		return -1;

	if (server_random_first) {
		os_memcpy(seed, conn->server_random, TLS_RANDOM_LEN);
		os_memcpy(seed + TLS_RANDOM_LEN, conn->client_random,
			  TLS_RANDOM_LEN);
	} else {
		os_memcpy(seed, conn->client_random, TLS_RANDOM_LEN);
		os_memcpy(seed + TLS_RANDOM_LEN, conn->server_random,
			  TLS_RANDOM_LEN);
	}

	return tls_prf(conn->master_secret, TLS_MASTER_SECRET_LEN,
		       label, seed, 2 * TLS_RANDOM_LEN, out, out_len);
}


/**
 * tlsv1_client_get_cipher - Get current cipher name
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @buf: Buffer for the cipher name
 * @buflen: buf size
 * Returns: 0 on success, -1 on failure
 *
 * Get the name of the currently used cipher.
 */
int tlsv1_client_get_cipher(struct tlsv1_client *conn, char *buf,
			    size_t buflen)
{
	char *cipher;

	switch (conn->rl.cipher_suite) {
	case TLS_RSA_WITH_RC4_128_MD5:
		cipher = "RC4-MD5";
		break;
	case TLS_RSA_WITH_RC4_128_SHA:
		cipher = "RC4-SHA";
		break;
	case TLS_RSA_WITH_DES_CBC_SHA:
		cipher = "DES-CBC-SHA";
		break;
	case TLS_RSA_WITH_3DES_EDE_CBC_SHA:
		cipher = "DES-CBC3-SHA";
		break;
	case TLS_DH_anon_WITH_AES_128_CBC_SHA:
		cipher = "ADH-AES-128-SHA";
		break;
	case TLS_RSA_WITH_AES_256_CBC_SHA:
		cipher = "AES-256-SHA";
		break;
	case TLS_RSA_WITH_AES_128_CBC_SHA:
		cipher = "AES-128-SHA";
		break;
	default:
		return -1;
	}

	if (os_strlcpy(buf, cipher, buflen) >= buflen)
		return -1;
	return 0;
}


/**
 * tlsv1_client_shutdown - Shutdown TLS connection
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_client_shutdown(struct tlsv1_client *conn)
{
	conn->state = CLIENT_HELLO;

	if (tls_verify_hash_init(&conn->verify) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to re-initialize verify "
			   "hash");
		return -1;
	}

	tlsv1_record_set_cipher_suite(&conn->rl, TLS_NULL_WITH_NULL_NULL);
	tlsv1_record_change_write_cipher(&conn->rl);
	tlsv1_record_change_read_cipher(&conn->rl);

	conn->certificate_requested = 0;
	crypto_public_key_free(conn->server_rsa_key);
	conn->server_rsa_key = NULL;
	conn->session_resumed = 0;

	return 0;
}


/**
 * tlsv1_client_resumed - Was session resumption used
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * Returns: 1 if current session used session resumption, 0 if not
 */
int tlsv1_client_resumed(struct tlsv1_client *conn)
{
	return !!conn->session_resumed;
}


/**
 * tlsv1_client_hello_ext - Set TLS extension for ClientHello
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @ext_type: Extension type
 * @data: Extension payload (%NULL to remove extension)
 * @data_len: Extension payload length
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_client_hello_ext(struct tlsv1_client *conn, int ext_type,
			   const u8 *data, size_t data_len)
{
	u8 *pos;

	conn->session_ticket_included = 0;
	os_free(conn->client_hello_ext);
	conn->client_hello_ext = NULL;
	conn->client_hello_ext_len = 0;

	if (data == NULL || data_len == 0)
		return 0;

	pos = conn->client_hello_ext = os_malloc(6 + data_len);
	if (pos == NULL)
		return -1;

	WPA_PUT_BE16(pos, 4 + data_len);
	pos += 2;
	WPA_PUT_BE16(pos, ext_type);
	pos += 2;
	WPA_PUT_BE16(pos, data_len);
	pos += 2;
	os_memcpy(pos, data, data_len);
	conn->client_hello_ext_len = 6 + data_len;

	if (ext_type == TLS_EXT_PAC_OPAQUE) {
		conn->session_ticket_included = 1;
		wpa_printf(MSG_DEBUG, "TLSv1: Using session ticket");
	}

	return 0;
}


/**
 * tlsv1_client_get_keys - Get master key and random data from TLS connection
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @keys: Structure of key/random data (filled on success)
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_client_get_keys(struct tlsv1_client *conn, struct tls_keys *keys)
{
	os_memset(keys, 0, sizeof(*keys));
	if (conn->state == CLIENT_HELLO)
		return -1;

	keys->client_random = conn->client_random;
	keys->client_random_len = TLS_RANDOM_LEN;

	if (conn->state != SERVER_HELLO) {
		keys->server_random = conn->server_random;
		keys->server_random_len = TLS_RANDOM_LEN;
		keys->master_key = conn->master_secret;
		keys->master_key_len = TLS_MASTER_SECRET_LEN;
	}

	return 0;
}


/**
 * tlsv1_client_get_keyblock_size - Get TLS key_block size
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * Returns: Size of the key_block for the negotiated cipher suite or -1 on
 * failure
 */
int tlsv1_client_get_keyblock_size(struct tlsv1_client *conn)
{
	if (conn->state == CLIENT_HELLO || conn->state == SERVER_HELLO)
		return -1;

	return 2 * (conn->rl.hash_size + conn->rl.key_material_len +
		    conn->rl.iv_size);
}


/**
 * tlsv1_client_set_cipher_list - Configure acceptable cipher suites
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @ciphers: Zero (TLS_CIPHER_NONE) terminated list of allowed ciphers
 * (TLS_CIPHER_*).
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_client_set_cipher_list(struct tlsv1_client *conn, u8 *ciphers)
{
	size_t count;
	u16 *suites;

	/* TODO: implement proper configuration of cipher suites */
	if (ciphers[0] == TLS_CIPHER_ANON_DH_AES128_SHA) {
		count = 0;
		suites = conn->cipher_suites;
#ifndef CONFIG_CRYPTO_INTERNAL
		suites[count++] = TLS_DH_anon_WITH_AES_256_CBC_SHA;
#endif /* CONFIG_CRYPTO_INTERNAL */
		suites[count++] = TLS_DH_anon_WITH_AES_128_CBC_SHA;
		suites[count++] = TLS_DH_anon_WITH_3DES_EDE_CBC_SHA;
		suites[count++] = TLS_DH_anon_WITH_RC4_128_MD5;
		suites[count++] = TLS_DH_anon_WITH_DES_CBC_SHA;

		/*
		 * Cisco AP (at least 350 and 1200 series) local authentication
		 * server does not know how to search cipher suites from the
		 * list and seem to require that the last entry in the list is
		 * the one that it wants to use. However, TLS specification
		 * requires the list to be in the client preference order. As a
		 * workaround, add anon-DH AES-128-SHA1 again at the end of the
		 * list to allow the Cisco code to find it.
		 */
		suites[count++] = TLS_DH_anon_WITH_AES_128_CBC_SHA;
		conn->num_cipher_suites = count;
	}

	return 0;
}


/**
 * tlsv1_client_set_cred - Set client credentials
 * @conn: TLSv1 client connection data from tlsv1_client_init()
 * @cred: Credentials from tlsv1_cred_alloc()
 * Returns: 0 on success, -1 on failure
 *
 * On success, the client takes ownership of the credentials block and caller
 * must not free it. On failure, caller is responsible for freeing the
 * credential block.
 */
int tlsv1_client_set_cred(struct tlsv1_client *conn,
			  struct tlsv1_credentials *cred)
{
	tlsv1_cred_free(conn->cred);
	conn->cred = cred;
	return 0;
}


void tlsv1_client_set_session_ticket_cb(struct tlsv1_client *conn,
					tlsv1_client_session_ticket_cb cb,
					void *ctx)
{
	wpa_printf(MSG_DEBUG, "TLSv1: SessionTicket callback set %p (ctx %p)",
		   cb, ctx);
	conn->session_ticket_cb = cb;
	conn->session_ticket_cb_ctx = ctx;
}
