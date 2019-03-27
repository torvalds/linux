/*
 * EAP-TLS/PEAP/TTLS/FAST server common functions
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_TLS_COMMON_H
#define EAP_TLS_COMMON_H

/**
 * struct eap_ssl_data - TLS data for EAP methods
 */
struct eap_ssl_data {
	/**
	 * conn - TLS connection context data from tls_connection_init()
	 */
	struct tls_connection *conn;

	/**
	 * tls_out - TLS message to be sent out in fragments
	 */
	struct wpabuf *tls_out;

	/**
	 * tls_out_pos - The current position in the outgoing TLS message
	 */
	size_t tls_out_pos;

	/**
	 * tls_out_limit - Maximum fragment size for outgoing TLS messages
	 */
	size_t tls_out_limit;

	/**
	 * tls_in - Received TLS message buffer for re-assembly
	 */
	struct wpabuf *tls_in;

	/**
	 * phase2 - Whether this TLS connection is used in EAP phase 2 (tunnel)
	 */
	int phase2;

	/**
	 * eap - EAP state machine allocated with eap_server_sm_init()
	 */
	struct eap_sm *eap;

	enum { MSG, FRAG_ACK, WAIT_FRAG_ACK } state;
	struct wpabuf tmpbuf;

	/**
	 * tls_v13 - Whether TLS v1.3 or newer is used
	 */
	int tls_v13;
};


/* EAP TLS Flags */
#define EAP_TLS_FLAGS_LENGTH_INCLUDED 0x80
#define EAP_TLS_FLAGS_MORE_FRAGMENTS 0x40
#define EAP_TLS_FLAGS_START 0x20
#define EAP_TLS_VERSION_MASK 0x07

 /* could be up to 128 bytes, but only the first 64 bytes are used */
#define EAP_TLS_KEY_LEN 64

/* dummy type used as a flag for UNAUTH-TLS */
#define EAP_UNAUTH_TLS_TYPE 255
#define EAP_WFA_UNAUTH_TLS_TYPE 254


struct wpabuf * eap_tls_msg_alloc(EapType type, size_t payload_len,
				  u8 code, u8 identifier);
int eap_server_tls_ssl_init(struct eap_sm *sm, struct eap_ssl_data *data,
			    int verify_peer, int eap_type);
void eap_server_tls_ssl_deinit(struct eap_sm *sm, struct eap_ssl_data *data);
u8 * eap_server_tls_derive_key(struct eap_sm *sm, struct eap_ssl_data *data,
			       const char *label, size_t len);
u8 * eap_server_tls_derive_session_id(struct eap_sm *sm,
				      struct eap_ssl_data *data, u8 eap_type,
				      size_t *len);
struct wpabuf * eap_server_tls_build_msg(struct eap_ssl_data *data,
					 int eap_type, int version, u8 id);
struct wpabuf * eap_server_tls_build_ack(u8 id, int eap_type, int version);
int eap_server_tls_phase1(struct eap_sm *sm, struct eap_ssl_data *data);
struct wpabuf * eap_server_tls_encrypt(struct eap_sm *sm,
				       struct eap_ssl_data *data,
				       const struct wpabuf *plain);
int eap_server_tls_process(struct eap_sm *sm, struct eap_ssl_data *data,
			   struct wpabuf *respData, void *priv, int eap_type,
			   int (*proc_version)(struct eap_sm *sm, void *priv,
					       int peer_version),
			   void (*proc_msg)(struct eap_sm *sm, void *priv,
					    const struct wpabuf *respData));

#endif /* EAP_TLS_COMMON_H */
