/*
 * TLSv1 server - internal structures
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

#ifndef TLSV1_SERVER_I_H
#define TLSV1_SERVER_I_H

struct tlsv1_server {
	enum {
		CLIENT_HELLO, SERVER_HELLO, SERVER_CERTIFICATE,
		SERVER_KEY_EXCHANGE, SERVER_CERTIFICATE_REQUEST,
		SERVER_HELLO_DONE, CLIENT_CERTIFICATE, CLIENT_KEY_EXCHANGE,
		CERTIFICATE_VERIFY, CHANGE_CIPHER_SPEC, CLIENT_FINISHED,
		SERVER_CHANGE_CIPHER_SPEC, SERVER_FINISHED,
		ESTABLISHED, FAILED
	} state;

	struct tlsv1_record_layer rl;

	u8 session_id[TLS_SESSION_ID_MAX_LEN];
	size_t session_id_len;
	u8 client_random[TLS_RANDOM_LEN];
	u8 server_random[TLS_RANDOM_LEN];
	u8 master_secret[TLS_MASTER_SECRET_LEN];

	u8 alert_level;
	u8 alert_description;

	struct crypto_public_key *client_rsa_key;

	struct tls_verify_hash verify;

#define MAX_CIPHER_COUNT 30
	u16 cipher_suites[MAX_CIPHER_COUNT];
	size_t num_cipher_suites;

	u16 cipher_suite;

	struct tlsv1_credentials *cred;

	int verify_peer;
	u16 client_version;

	u8 *session_ticket;
	size_t session_ticket_len;

	tlsv1_server_session_ticket_cb session_ticket_cb;
	void *session_ticket_cb_ctx;

	int use_session_ticket;

	u8 *dh_secret;
	size_t dh_secret_len;
};


void tlsv1_server_alert(struct tlsv1_server *conn, u8 level, u8 description);
int tlsv1_server_derive_keys(struct tlsv1_server *conn,
			     const u8 *pre_master_secret,
			     size_t pre_master_secret_len);
u8 * tlsv1_server_handshake_write(struct tlsv1_server *conn, size_t *out_len);
u8 * tlsv1_server_send_alert(struct tlsv1_server *conn, u8 level,
			     u8 description, size_t *out_len);
int tlsv1_server_process_handshake(struct tlsv1_server *conn, u8 ct,
				   const u8 *buf, size_t *len);

#endif /* TLSV1_SERVER_I_H */
