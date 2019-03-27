/*
 * TLSv1 client - internal structures
 * Copyright (c) 2006-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef TLSV1_CLIENT_I_H
#define TLSV1_CLIENT_I_H

struct tlsv1_client {
	enum {
		CLIENT_HELLO, SERVER_HELLO, SERVER_CERTIFICATE,
		SERVER_KEY_EXCHANGE, SERVER_CERTIFICATE_REQUEST,
		SERVER_HELLO_DONE, CLIENT_KEY_EXCHANGE, CHANGE_CIPHER_SPEC,
		SERVER_CHANGE_CIPHER_SPEC, SERVER_FINISHED, ACK_FINISHED,
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

	unsigned int flags; /* TLS_CONN_* bitfield */

	unsigned int certificate_requested:1;
	unsigned int session_resumed:1;
	unsigned int session_ticket_included:1;
	unsigned int use_session_ticket:1;
	unsigned int cert_in_cb:1;
	unsigned int ocsp_resp_received:1;

	struct crypto_public_key *server_rsa_key;

	struct tls_verify_hash verify;

#define MAX_CIPHER_COUNT 30
	u16 cipher_suites[MAX_CIPHER_COUNT];
	size_t num_cipher_suites;

	u16 prev_cipher_suite;

	u8 *client_hello_ext;
	size_t client_hello_ext_len;

	/* The prime modulus used for Diffie-Hellman */
	u8 *dh_p;
	size_t dh_p_len;
	/* The generator used for Diffie-Hellman */
	u8 *dh_g;
	size_t dh_g_len;
	/* The server's Diffie-Hellman public value */
	u8 *dh_ys;
	size_t dh_ys_len;

	struct tlsv1_credentials *cred;

	tlsv1_client_session_ticket_cb session_ticket_cb;
	void *session_ticket_cb_ctx;

	struct wpabuf *partial_input;

	void (*event_cb)(void *ctx, enum tls_event ev,
			 union tls_event_data *data);
	void *cb_ctx;

	struct x509_certificate *server_cert;
};


void tls_alert(struct tlsv1_client *conn, u8 level, u8 description);
void tlsv1_client_free_dh(struct tlsv1_client *conn);
int tls_derive_pre_master_secret(u8 *pre_master_secret);
int tls_derive_keys(struct tlsv1_client *conn,
		    const u8 *pre_master_secret, size_t pre_master_secret_len);
u8 * tls_send_client_hello(struct tlsv1_client *conn, size_t *out_len);
u8 * tlsv1_client_send_alert(struct tlsv1_client *conn, u8 level,
			     u8 description, size_t *out_len);
u8 * tlsv1_client_handshake_write(struct tlsv1_client *conn, size_t *out_len,
				  int no_appl_data);
int tlsv1_client_process_handshake(struct tlsv1_client *conn, u8 ct,
				   const u8 *buf, size_t *len,
				   u8 **out_data, size_t *out_len);

enum tls_ocsp_result {
	TLS_OCSP_NO_RESPONSE, TLS_OCSP_INVALID, TLS_OCSP_GOOD, TLS_OCSP_REVOKED
};

enum tls_ocsp_result tls_process_ocsp_response(struct tlsv1_client *conn,
					       const u8 *resp, size_t len);

#endif /* TLSV1_CLIENT_I_H */
