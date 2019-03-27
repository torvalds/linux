/*
 * EAP peer: EAP-TLS/PEAP/TTLS/FAST common functions
 * Copyright (c) 2004-2009, 2012, Jouni Malinen <j@w1.fi>
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
	 * tls_in_left - Number of remaining bytes in the incoming TLS message
	 */
	size_t tls_in_left;

	/**
	 * tls_in_total - Total number of bytes in the incoming TLS message
	 */
	size_t tls_in_total;

	/**
	 * phase2 - Whether this TLS connection is used in EAP phase 2 (tunnel)
	 */
	int phase2;

	/**
	 * include_tls_length - Whether the TLS length field is included even
	 * if the TLS data is not fragmented
	 */
	int include_tls_length;

	/**
	 * eap - EAP state machine allocated with eap_peer_sm_init()
	 */
	struct eap_sm *eap;

	/**
	 * ssl_ctx - TLS library context to use for the connection
	 */
	void *ssl_ctx;

	/**
	 * eap_type - EAP method used in Phase 1 (EAP_TYPE_TLS/PEAP/TTLS/FAST)
	 */
	u8 eap_type;

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


int eap_peer_tls_ssl_init(struct eap_sm *sm, struct eap_ssl_data *data,
			  struct eap_peer_config *config, u8 eap_type);
void eap_peer_tls_ssl_deinit(struct eap_sm *sm, struct eap_ssl_data *data);
u8 * eap_peer_tls_derive_key(struct eap_sm *sm, struct eap_ssl_data *data,
			     const char *label, size_t len);
u8 * eap_peer_tls_derive_session_id(struct eap_sm *sm,
				    struct eap_ssl_data *data, u8 eap_type,
				    size_t *len);
int eap_peer_tls_process_helper(struct eap_sm *sm, struct eap_ssl_data *data,
				EapType eap_type, int peap_version,
				u8 id, const struct wpabuf *in_data,
				struct wpabuf **out_data);
struct wpabuf * eap_peer_tls_build_ack(u8 id, EapType eap_type,
				       int peap_version);
int eap_peer_tls_reauth_init(struct eap_sm *sm, struct eap_ssl_data *data);
int eap_peer_tls_status(struct eap_sm *sm, struct eap_ssl_data *data,
			char *buf, size_t buflen, int verbose);
const u8 * eap_peer_tls_process_init(struct eap_sm *sm,
				     struct eap_ssl_data *data,
				     EapType eap_type,
				     struct eap_method_ret *ret,
				     const struct wpabuf *reqData,
				     size_t *len, u8 *flags);
void eap_peer_tls_reset_input(struct eap_ssl_data *data);
void eap_peer_tls_reset_output(struct eap_ssl_data *data);
int eap_peer_tls_decrypt(struct eap_sm *sm, struct eap_ssl_data *data,
			 const struct wpabuf *in_data,
			 struct wpabuf **in_decrypted);
int eap_peer_tls_encrypt(struct eap_sm *sm, struct eap_ssl_data *data,
			 EapType eap_type, int peap_version, u8 id,
			 const struct wpabuf *in_data,
			 struct wpabuf **out_data);
int eap_peer_select_phase2_methods(struct eap_peer_config *config,
				   const char *prefix,
				   struct eap_method_type **types,
				   size_t *num_types);
int eap_peer_tls_phase2_nak(struct eap_method_type *types, size_t num_types,
			    struct eap_hdr *hdr, struct wpabuf **resp);

#endif /* EAP_TLS_COMMON_H */
