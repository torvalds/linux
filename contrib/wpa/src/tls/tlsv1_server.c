/*
 * TLS v1.0/v1.1/v1.2 server (RFC 2246, RFC 4346, RFC 5246)
 * Copyright (c) 2006-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/sha1.h"
#include "crypto/tls.h"
#include "tlsv1_common.h"
#include "tlsv1_record.h"
#include "tlsv1_server.h"
#include "tlsv1_server_i.h"

/* TODO:
 * Support for a message fragmented across several records (RFC 2246, 6.2.1)
 */


void tlsv1_server_log(struct tlsv1_server *conn, const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int buflen;

	va_start(ap, fmt);
	buflen = vsnprintf(NULL, 0, fmt, ap) + 1;
	va_end(ap);

	buf = os_malloc(buflen);
	if (buf == NULL)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, buflen, fmt, ap);
	va_end(ap);

	wpa_printf(MSG_DEBUG, "TLSv1: %s", buf);
	if (conn->log_cb)
		conn->log_cb(conn->log_cb_ctx, buf);

	os_free(buf);
}


void tlsv1_server_alert(struct tlsv1_server *conn, u8 level, u8 description)
{
	conn->alert_level = level;
	conn->alert_description = description;
}


int tlsv1_server_derive_keys(struct tlsv1_server *conn,
			     const u8 *pre_master_secret,
			     size_t pre_master_secret_len)
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
		if (tls_prf(conn->rl.tls_version,
			    pre_master_secret, pre_master_secret_len,
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
	if (tls_prf(conn->rl.tls_version,
		    conn->master_secret, TLS_MASTER_SECRET_LEN,
		    "key expansion", seed, 2 * TLS_RANDOM_LEN,
		    key_block, key_block_len)) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to derive key_block");
		return -1;
	}
	wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: key_block",
			key_block, key_block_len);

	pos = key_block;

	/* client_write_MAC_secret */
	os_memcpy(conn->rl.read_mac_secret, pos, conn->rl.hash_size);
	pos += conn->rl.hash_size;
	/* server_write_MAC_secret */
	os_memcpy(conn->rl.write_mac_secret, pos, conn->rl.hash_size);
	pos += conn->rl.hash_size;

	/* client_write_key */
	os_memcpy(conn->rl.read_key, pos, conn->rl.key_material_len);
	pos += conn->rl.key_material_len;
	/* server_write_key */
	os_memcpy(conn->rl.write_key, pos, conn->rl.key_material_len);
	pos += conn->rl.key_material_len;

	/* client_write_IV */
	os_memcpy(conn->rl.read_iv, pos, conn->rl.iv_size);
	pos += conn->rl.iv_size;
	/* server_write_IV */
	os_memcpy(conn->rl.write_iv, pos, conn->rl.iv_size);
	pos += conn->rl.iv_size;

	return 0;
}


/**
 * tlsv1_server_handshake - Process TLS handshake
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * @in_data: Input data from TLS peer
 * @in_len: Input data length
 * @out_len: Length of the output buffer.
 * Returns: Pointer to output data, %NULL on failure
 */
u8 * tlsv1_server_handshake(struct tlsv1_server *conn,
			    const u8 *in_data, size_t in_len,
			    size_t *out_len)
{
	const u8 *pos, *end;
	u8 *msg = NULL, *in_msg, *in_pos, *in_end, alert, ct;
	size_t in_msg_len;
	int used;

	if (in_data == NULL || in_len == 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: No input data to server");
		return NULL;
	}

	pos = in_data;
	end = in_data + in_len;
	in_msg = os_malloc(in_len);
	if (in_msg == NULL)
		return NULL;

	/* Each received packet may include multiple records */
	while (pos < end) {
		in_msg_len = in_len;
		used = tlsv1_record_receive(&conn->rl, pos, end - pos,
					    in_msg, &in_msg_len, &alert);
		if (used < 0) {
			wpa_printf(MSG_DEBUG, "TLSv1: Processing received "
				   "record failed");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL, alert);
			goto failed;
		}
		if (used == 0) {
			/* need more data */
			wpa_printf(MSG_DEBUG, "TLSv1: Partial processing not "
				   "yet supported");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL, alert);
			goto failed;
		}
		ct = pos[0];

		in_pos = in_msg;
		in_end = in_msg + in_msg_len;

		/* Each received record may include multiple messages of the
		 * same ContentType. */
		while (in_pos < in_end) {
			in_msg_len = in_end - in_pos;
			if (tlsv1_server_process_handshake(conn, ct, in_pos,
							   &in_msg_len) < 0)
				goto failed;
			in_pos += in_msg_len;
		}

		pos += used;
	}

	os_free(in_msg);
	in_msg = NULL;

	msg = tlsv1_server_handshake_write(conn, out_len);

failed:
	os_free(in_msg);
	if (conn->alert_level) {
		if (conn->state == FAILED) {
			/* Avoid alert loops */
			wpa_printf(MSG_DEBUG, "TLSv1: Drop alert loop");
			os_free(msg);
			return NULL;
		}
		conn->state = FAILED;
		os_free(msg);
		msg = tlsv1_server_send_alert(conn, conn->alert_level,
					      conn->alert_description,
					      out_len);
	}

	return msg;
}


/**
 * tlsv1_server_encrypt - Encrypt data into TLS tunnel
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * @in_data: Pointer to plaintext data to be encrypted
 * @in_len: Input buffer length
 * @out_data: Pointer to output buffer (encrypted TLS data)
 * @out_len: Maximum out_data length
 * Returns: Number of bytes written to out_data, -1 on failure
 *
 * This function is used after TLS handshake has been completed successfully to
 * send data in the encrypted tunnel.
 */
int tlsv1_server_encrypt(struct tlsv1_server *conn,
			 const u8 *in_data, size_t in_len,
			 u8 *out_data, size_t out_len)
{
	size_t rlen;

	wpa_hexdump_key(MSG_MSGDUMP, "TLSv1: Plaintext AppData",
			in_data, in_len);

	if (tlsv1_record_send(&conn->rl, TLS_CONTENT_TYPE_APPLICATION_DATA,
			      out_data, out_len, in_data, in_len, &rlen) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to create a record");
		tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
				   TLS_ALERT_INTERNAL_ERROR);
		return -1;
	}

	return rlen;
}


/**
 * tlsv1_server_decrypt - Decrypt data from TLS tunnel
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * @in_data: Pointer to input buffer (encrypted TLS data)
 * @in_len: Input buffer length
 * @out_data: Pointer to output buffer (decrypted data from TLS tunnel)
 * @out_len: Maximum out_data length
 * Returns: Number of bytes written to out_data, -1 on failure
 *
 * This function is used after TLS handshake has been completed successfully to
 * receive data from the encrypted tunnel.
 */
int tlsv1_server_decrypt(struct tlsv1_server *conn,
			 const u8 *in_data, size_t in_len,
			 u8 *out_data, size_t out_len)
{
	const u8 *in_end, *pos;
	int used;
	u8 alert, *out_end, *out_pos, ct;
	size_t olen;

	pos = in_data;
	in_end = in_data + in_len;
	out_pos = out_data;
	out_end = out_data + out_len;

	while (pos < in_end) {
		ct = pos[0];
		olen = out_end - out_pos;
		used = tlsv1_record_receive(&conn->rl, pos, in_end - pos,
					    out_pos, &olen, &alert);
		if (used < 0) {
			tlsv1_server_log(conn, "Record layer processing failed");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL, alert);
			return -1;
		}
		if (used == 0) {
			/* need more data */
			wpa_printf(MSG_DEBUG, "TLSv1: Partial processing not "
				   "yet supported");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL, alert);
			return -1;
		}

		if (ct == TLS_CONTENT_TYPE_ALERT) {
			if (olen < 2) {
				tlsv1_server_log(conn, "Alert underflow");
				tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
						   TLS_ALERT_DECODE_ERROR);
				return -1;
			}
			tlsv1_server_log(conn, "Received alert %d:%d",
					 out_pos[0], out_pos[1]);
			if (out_pos[0] == TLS_ALERT_LEVEL_WARNING) {
				/* Continue processing */
				pos += used;
				continue;
			}

			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   out_pos[1]);
			return -1;
		}

		if (ct != TLS_CONTENT_TYPE_APPLICATION_DATA) {
			tlsv1_server_log(conn, "Unexpected content type 0x%x",
					 pos[0]);
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_UNEXPECTED_MESSAGE);
			return -1;
		}

#ifdef CONFIG_TESTING_OPTIONS
		if ((conn->test_flags &
		     (TLS_BREAK_VERIFY_DATA | TLS_BREAK_SRV_KEY_X_HASH |
		      TLS_BREAK_SRV_KEY_X_SIGNATURE)) &&
		    !conn->test_failure_reported) {
			tlsv1_server_log(conn, "TEST-FAILURE: Client ApplData received after invalid handshake");
			conn->test_failure_reported = 1;
		}
#endif /* CONFIG_TESTING_OPTIONS */

		out_pos += olen;
		if (out_pos > out_end) {
			wpa_printf(MSG_DEBUG, "TLSv1: Buffer not large enough "
				   "for processing the received record");
			tlsv1_server_alert(conn, TLS_ALERT_LEVEL_FATAL,
					   TLS_ALERT_INTERNAL_ERROR);
			return -1;
		}

		pos += used;
	}

	return out_pos - out_data;
}


/**
 * tlsv1_server_global_init - Initialize TLSv1 server
 * Returns: 0 on success, -1 on failure
 *
 * This function must be called before using any other TLSv1 server functions.
 */
int tlsv1_server_global_init(void)
{
	return crypto_global_init();
}


/**
 * tlsv1_server_global_deinit - Deinitialize TLSv1 server
 *
 * This function can be used to deinitialize the TLSv1 server that was
 * initialized by calling tlsv1_server_global_init(). No TLSv1 server functions
 * can be called after this before calling tlsv1_server_global_init() again.
 */
void tlsv1_server_global_deinit(void)
{
	crypto_global_deinit();
}


/**
 * tlsv1_server_init - Initialize TLSv1 server connection
 * @cred: Pointer to server credentials from tlsv1_server_cred_alloc()
 * Returns: Pointer to TLSv1 server connection data or %NULL on failure
 */
struct tlsv1_server * tlsv1_server_init(struct tlsv1_credentials *cred)
{
	struct tlsv1_server *conn;
	size_t count;
	u16 *suites;

	conn = os_zalloc(sizeof(*conn));
	if (conn == NULL)
		return NULL;

	conn->cred = cred;

	conn->state = CLIENT_HELLO;

	if (tls_verify_hash_init(&conn->verify) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to initialize verify "
			   "hash");
		os_free(conn);
		return NULL;
	}

	count = 0;
	suites = conn->cipher_suites;
	suites[count++] = TLS_DHE_RSA_WITH_AES_256_CBC_SHA256;
	suites[count++] = TLS_RSA_WITH_AES_256_CBC_SHA256;
	suites[count++] = TLS_DHE_RSA_WITH_AES_256_CBC_SHA;
	suites[count++] = TLS_RSA_WITH_AES_256_CBC_SHA;
	suites[count++] = TLS_DHE_RSA_WITH_AES_128_CBC_SHA256;
	suites[count++] = TLS_RSA_WITH_AES_128_CBC_SHA256;
	suites[count++] = TLS_DHE_RSA_WITH_AES_128_CBC_SHA;
	suites[count++] = TLS_RSA_WITH_AES_128_CBC_SHA;
	suites[count++] = TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA;
	suites[count++] = TLS_RSA_WITH_3DES_EDE_CBC_SHA;
	suites[count++] = TLS_RSA_WITH_RC4_128_SHA;
	suites[count++] = TLS_RSA_WITH_RC4_128_MD5;
	conn->num_cipher_suites = count;

	return conn;
}


static void tlsv1_server_clear_data(struct tlsv1_server *conn)
{
	tlsv1_record_set_cipher_suite(&conn->rl, TLS_NULL_WITH_NULL_NULL);
	tlsv1_record_change_write_cipher(&conn->rl);
	tlsv1_record_change_read_cipher(&conn->rl);
	tls_verify_hash_free(&conn->verify);

	crypto_public_key_free(conn->client_rsa_key);
	conn->client_rsa_key = NULL;

	os_free(conn->session_ticket);
	conn->session_ticket = NULL;
	conn->session_ticket_len = 0;
	conn->use_session_ticket = 0;

	os_free(conn->dh_secret);
	conn->dh_secret = NULL;
	conn->dh_secret_len = 0;
}


/**
 * tlsv1_server_deinit - Deinitialize TLSv1 server connection
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 */
void tlsv1_server_deinit(struct tlsv1_server *conn)
{
	tlsv1_server_clear_data(conn);
	os_free(conn);
}


/**
 * tlsv1_server_established - Check whether connection has been established
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * Returns: 1 if connection is established, 0 if not
 */
int tlsv1_server_established(struct tlsv1_server *conn)
{
	return conn->state == ESTABLISHED;
}


/**
 * tlsv1_server_prf - Use TLS-PRF to derive keying material
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * @label: Label (e.g., description of the key) for PRF
 * @server_random_first: seed is 0 = client_random|server_random,
 * 1 = server_random|client_random
 * @out: Buffer for output data from TLS-PRF
 * @out_len: Length of the output buffer
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_server_prf(struct tlsv1_server *conn, const char *label,
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

	return tls_prf(conn->rl.tls_version,
		       conn->master_secret, TLS_MASTER_SECRET_LEN,
		       label, seed, 2 * TLS_RANDOM_LEN, out, out_len);
}


/**
 * tlsv1_server_get_cipher - Get current cipher name
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * @buf: Buffer for the cipher name
 * @buflen: buf size
 * Returns: 0 on success, -1 on failure
 *
 * Get the name of the currently used cipher.
 */
int tlsv1_server_get_cipher(struct tlsv1_server *conn, char *buf,
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
	case TLS_DHE_RSA_WITH_DES_CBC_SHA:
		cipher = "DHE-RSA-DES-CBC-SHA";
		break;
	case TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA:
		cipher = "DHE-RSA-DES-CBC3-SHA";
		break;
	case TLS_DH_anon_WITH_RC4_128_MD5:
		cipher = "ADH-RC4-MD5";
		break;
	case TLS_DH_anon_WITH_DES_CBC_SHA:
		cipher = "ADH-DES-SHA";
		break;
	case TLS_DH_anon_WITH_3DES_EDE_CBC_SHA:
		cipher = "ADH-DES-CBC3-SHA";
		break;
	case TLS_RSA_WITH_AES_128_CBC_SHA:
		cipher = "AES-128-SHA";
		break;
	case TLS_DHE_RSA_WITH_AES_128_CBC_SHA:
		cipher = "DHE-RSA-AES-128-SHA";
		break;
	case TLS_DH_anon_WITH_AES_128_CBC_SHA:
		cipher = "ADH-AES-128-SHA";
		break;
	case TLS_RSA_WITH_AES_256_CBC_SHA:
		cipher = "AES-256-SHA";
		break;
	case TLS_DHE_RSA_WITH_AES_256_CBC_SHA:
		cipher = "DHE-RSA-AES-256-SHA";
		break;
	case TLS_DH_anon_WITH_AES_256_CBC_SHA:
		cipher = "ADH-AES-256-SHA";
		break;
	case TLS_RSA_WITH_AES_128_CBC_SHA256:
		cipher = "AES-128-SHA256";
		break;
	case TLS_RSA_WITH_AES_256_CBC_SHA256:
		cipher = "AES-256-SHA256";
		break;
	case TLS_DHE_RSA_WITH_AES_128_CBC_SHA256:
		cipher = "DHE-RSA-AES-128-SHA256";
		break;
	case TLS_DHE_RSA_WITH_AES_256_CBC_SHA256:
		cipher = "DHE-RSA-AES-256-SHA256";
		break;
	case TLS_DH_anon_WITH_AES_128_CBC_SHA256:
		cipher = "ADH-AES-128-SHA256";
		break;
	case TLS_DH_anon_WITH_AES_256_CBC_SHA256:
		cipher = "ADH-AES-256-SHA256";
		break;
	default:
		return -1;
	}

	if (os_strlcpy(buf, cipher, buflen) >= buflen)
		return -1;
	return 0;
}


/**
 * tlsv1_server_shutdown - Shutdown TLS connection
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_server_shutdown(struct tlsv1_server *conn)
{
	conn->state = CLIENT_HELLO;

	if (tls_verify_hash_init(&conn->verify) < 0) {
		wpa_printf(MSG_DEBUG, "TLSv1: Failed to re-initialize verify "
			   "hash");
		return -1;
	}

	tlsv1_server_clear_data(conn);

	return 0;
}


/**
 * tlsv1_server_resumed - Was session resumption used
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * Returns: 1 if current session used session resumption, 0 if not
 */
int tlsv1_server_resumed(struct tlsv1_server *conn)
{
	return 0;
}


/**
 * tlsv1_server_get_random - Get random data from TLS connection
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * @keys: Structure of random data (filled on success)
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_server_get_random(struct tlsv1_server *conn, struct tls_random *keys)
{
	os_memset(keys, 0, sizeof(*keys));
	if (conn->state == CLIENT_HELLO)
		return -1;

	keys->client_random = conn->client_random;
	keys->client_random_len = TLS_RANDOM_LEN;

	if (conn->state != SERVER_HELLO) {
		keys->server_random = conn->server_random;
		keys->server_random_len = TLS_RANDOM_LEN;
	}

	return 0;
}


/**
 * tlsv1_server_get_keyblock_size - Get TLS key_block size
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * Returns: Size of the key_block for the negotiated cipher suite or -1 on
 * failure
 */
int tlsv1_server_get_keyblock_size(struct tlsv1_server *conn)
{
	if (conn->state == CLIENT_HELLO || conn->state == SERVER_HELLO)
		return -1;

	return 2 * (conn->rl.hash_size + conn->rl.key_material_len +
		    conn->rl.iv_size);
}


/**
 * tlsv1_server_set_cipher_list - Configure acceptable cipher suites
 * @conn: TLSv1 server connection data from tlsv1_server_init()
 * @ciphers: Zero (TLS_CIPHER_NONE) terminated list of allowed ciphers
 * (TLS_CIPHER_*).
 * Returns: 0 on success, -1 on failure
 */
int tlsv1_server_set_cipher_list(struct tlsv1_server *conn, u8 *ciphers)
{
	size_t count;
	u16 *suites;

	/* TODO: implement proper configuration of cipher suites */
	if (ciphers[0] == TLS_CIPHER_ANON_DH_AES128_SHA) {
		count = 0;
		suites = conn->cipher_suites;
		suites[count++] = TLS_RSA_WITH_AES_256_CBC_SHA;
		suites[count++] = TLS_RSA_WITH_AES_128_CBC_SHA;
		suites[count++] = TLS_RSA_WITH_3DES_EDE_CBC_SHA;
		suites[count++] = TLS_RSA_WITH_RC4_128_SHA;
		suites[count++] = TLS_RSA_WITH_RC4_128_MD5;
		suites[count++] = TLS_DH_anon_WITH_AES_256_CBC_SHA;
		suites[count++] = TLS_DH_anon_WITH_AES_128_CBC_SHA;
		suites[count++] = TLS_DH_anon_WITH_3DES_EDE_CBC_SHA;
		suites[count++] = TLS_DH_anon_WITH_RC4_128_MD5;
		suites[count++] = TLS_DH_anon_WITH_DES_CBC_SHA;
		conn->num_cipher_suites = count;
	}

	return 0;
}


int tlsv1_server_set_verify(struct tlsv1_server *conn, int verify_peer)
{
	conn->verify_peer = verify_peer;
	return 0;
}


void tlsv1_server_set_session_ticket_cb(struct tlsv1_server *conn,
					tlsv1_server_session_ticket_cb cb,
					void *ctx)
{
	wpa_printf(MSG_DEBUG, "TLSv1: SessionTicket callback set %p (ctx %p)",
		   cb, ctx);
	conn->session_ticket_cb = cb;
	conn->session_ticket_cb_ctx = ctx;
}


void tlsv1_server_set_log_cb(struct tlsv1_server *conn,
			     void (*cb)(void *ctx, const char *msg), void *ctx)
{
	conn->log_cb = cb;
	conn->log_cb_ctx = ctx;
}


#ifdef CONFIG_TESTING_OPTIONS
void tlsv1_server_set_test_flags(struct tlsv1_server *conn, u32 flags)
{
	conn->test_flags = flags;
}


static const u8 test_tls_prime15[1] = {
	15
};

static const u8 test_tls_prime511b[64] = {
	0x50, 0xfb, 0xf1, 0xae, 0x01, 0xf1, 0xfe, 0xe6,
	0xe1, 0xae, 0xdc, 0x1e, 0xbe, 0xfb, 0x9e, 0x58,
	0x9a, 0xd7, 0x54, 0x9d, 0x6b, 0xb3, 0x78, 0xe2,
	0x39, 0x7f, 0x30, 0x01, 0x25, 0xa1, 0xf9, 0x7c,
	0x55, 0x0e, 0xa1, 0x15, 0xcc, 0x36, 0x34, 0xbb,
	0x6c, 0x8b, 0x64, 0x45, 0x15, 0x7f, 0xd3, 0xe7,
	0x31, 0xc8, 0x8e, 0x56, 0x8e, 0x95, 0xdc, 0xea,
	0x9e, 0xdf, 0xf7, 0x56, 0xdd, 0xb0, 0x34, 0xdb
};

static const u8 test_tls_prime767b[96] = {
	0x4c, 0xdc, 0xb8, 0x21, 0x20, 0x9d, 0xe8, 0xa3,
	0x53, 0xd9, 0x1c, 0x18, 0xc1, 0x3a, 0x58, 0x67,
	0xa7, 0x85, 0xf9, 0x28, 0x9b, 0xce, 0xc0, 0xd1,
	0x05, 0x84, 0x61, 0x97, 0xb2, 0x86, 0x1c, 0xd0,
	0xd1, 0x96, 0x23, 0x29, 0x8c, 0xc5, 0x30, 0x68,
	0x3e, 0xf9, 0x05, 0xba, 0x60, 0xeb, 0xdb, 0xee,
	0x2d, 0xdf, 0x84, 0x65, 0x49, 0x87, 0x90, 0x2a,
	0xc9, 0x8e, 0x34, 0x63, 0x6d, 0x9a, 0x2d, 0x32,
	0x1c, 0x46, 0xd5, 0x4e, 0x20, 0x20, 0x90, 0xac,
	0xd5, 0x48, 0x79, 0x99, 0x0c, 0xe6, 0xed, 0xbf,
	0x79, 0xc2, 0x47, 0x50, 0x95, 0x38, 0x38, 0xbc,
	0xde, 0xb0, 0xd2, 0xe8, 0x97, 0xcb, 0x22, 0xbb
};

static const u8 test_tls_prime58[128] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x03, 0xc1, 0xba, 0xc8, 0x25, 0xbe, 0x2d, 0xf3
};

static const u8 test_tls_non_prime[] = {
	/*
	 * This is not a prime and the value has the following factors:
	 * 13736783488716579923 * 16254860191773456563 * 18229434976173670763 *
	 * 11112313018289079419 * 10260802278580253339 * 12394009491575311499 *
	 * 12419059668711064739 * 14317973192687985827 * 10498605410533203179 *
	 * 16338688760390249003 * 11128963991123878883 * 12990532258280301419 *
	 * 3
	 */
	0x0C, 0x8C, 0x36, 0x9C, 0x6F, 0x71, 0x2E, 0xA7,
	0xAB, 0x32, 0xD3, 0x0F, 0x68, 0x3D, 0xB2, 0x6D,
	0x81, 0xDD, 0xC4, 0x84, 0x0D, 0x9C, 0x6E, 0x36,
	0x29, 0x70, 0xF3, 0x1E, 0x9A, 0x42, 0x0B, 0x67,
	0x82, 0x6B, 0xB1, 0xF2, 0xAF, 0x55, 0x28, 0xE7,
	0xDB, 0x67, 0x6C, 0xF7, 0x6B, 0xAC, 0xAC, 0xE5,
	0xF7, 0x9F, 0xD4, 0x63, 0x55, 0x70, 0x32, 0x7C,
	0x70, 0xFB, 0xAF, 0xB8, 0xEB, 0x37, 0xCF, 0x3F,
	0xFE, 0x94, 0x73, 0xF9, 0x7A, 0xC7, 0x12, 0x2E,
	0x9B, 0xB4, 0x7D, 0x08, 0x60, 0x83, 0x43, 0x52,
	0x83, 0x1E, 0xA5, 0xFC, 0xFA, 0x87, 0x12, 0xF4,
	0x64, 0xE2, 0xCE, 0x71, 0x17, 0x72, 0xB6, 0xAB
};

#endif /* CONFIG_TESTING_OPTIONS */


void tlsv1_server_get_dh_p(struct tlsv1_server *conn, const u8 **dh_p,
			   size_t *dh_p_len)
{
	*dh_p = conn->cred->dh_p;
	*dh_p_len = conn->cred->dh_p_len;

#ifdef CONFIG_TESTING_OPTIONS
	if (conn->test_flags & TLS_DHE_PRIME_511B) {
		tlsv1_server_log(conn, "TESTING: Use short 511-bit prime with DHE");
		*dh_p = test_tls_prime511b;
		*dh_p_len = sizeof(test_tls_prime511b);
	} else if (conn->test_flags & TLS_DHE_PRIME_767B) {
		tlsv1_server_log(conn, "TESTING: Use short 767-bit prime with DHE");
		*dh_p = test_tls_prime767b;
		*dh_p_len = sizeof(test_tls_prime767b);
	} else if (conn->test_flags & TLS_DHE_PRIME_15) {
		tlsv1_server_log(conn, "TESTING: Use bogus 15 \"prime\" with DHE");
		*dh_p = test_tls_prime15;
		*dh_p_len = sizeof(test_tls_prime15);
	} else if (conn->test_flags & TLS_DHE_PRIME_58B) {
		tlsv1_server_log(conn, "TESTING: Use short 58-bit prime in long container with DHE");
		*dh_p = test_tls_prime58;
		*dh_p_len = sizeof(test_tls_prime58);
	} else if (conn->test_flags & TLS_DHE_NON_PRIME) {
		tlsv1_server_log(conn, "TESTING: Use claim non-prime as the DHE prime");
		*dh_p = test_tls_non_prime;
		*dh_p_len = sizeof(test_tls_non_prime);
	}
#endif /* CONFIG_TESTING_OPTIONS */
}
