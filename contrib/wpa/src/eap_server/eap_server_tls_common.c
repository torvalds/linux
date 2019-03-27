/*
 * EAP-TLS/PEAP/TTLS/FAST server common functions
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/sha1.h"
#include "crypto/tls.h"
#include "eap_i.h"
#include "eap_tls_common.h"


static void eap_server_tls_free_in_buf(struct eap_ssl_data *data);


struct wpabuf * eap_tls_msg_alloc(EapType type, size_t payload_len,
				  u8 code, u8 identifier)
{
	if (type == EAP_UNAUTH_TLS_TYPE)
		return eap_msg_alloc(EAP_VENDOR_UNAUTH_TLS,
				     EAP_VENDOR_TYPE_UNAUTH_TLS, payload_len,
				     code, identifier);
	else if (type == EAP_WFA_UNAUTH_TLS_TYPE)
		return eap_msg_alloc(EAP_VENDOR_WFA_NEW,
				     EAP_VENDOR_WFA_UNAUTH_TLS, payload_len,
				     code, identifier);
	return eap_msg_alloc(EAP_VENDOR_IETF, type, payload_len, code,
			     identifier);
}


#ifdef CONFIG_TLS_INTERNAL
static void eap_server_tls_log_cb(void *ctx, const char *msg)
{
	struct eap_sm *sm = ctx;
	eap_log_msg(sm, "TLS: %s", msg);
}
#endif /* CONFIG_TLS_INTERNAL */


int eap_server_tls_ssl_init(struct eap_sm *sm, struct eap_ssl_data *data,
			    int verify_peer, int eap_type)
{
	u8 session_ctx[8];
	unsigned int flags = sm->tls_flags;

	if (sm->ssl_ctx == NULL) {
		wpa_printf(MSG_ERROR, "TLS context not initialized - cannot use TLS-based EAP method");
		return -1;
	}

	data->eap = sm;
	data->phase2 = sm->init_phase2;

	data->conn = tls_connection_init(sm->ssl_ctx);
	if (data->conn == NULL) {
		wpa_printf(MSG_INFO, "SSL: Failed to initialize new TLS "
			   "connection");
		return -1;
	}

#ifdef CONFIG_TLS_INTERNAL
	tls_connection_set_log_cb(data->conn, eap_server_tls_log_cb, sm);
#ifdef CONFIG_TESTING_OPTIONS
	tls_connection_set_test_flags(data->conn, sm->tls_test_flags);
#endif /* CONFIG_TESTING_OPTIONS */
#endif /* CONFIG_TLS_INTERNAL */

	if (eap_type != EAP_TYPE_FAST)
		flags |= TLS_CONN_DISABLE_SESSION_TICKET;
	os_memcpy(session_ctx, "hostapd", 7);
	session_ctx[7] = (u8) eap_type;
	if (tls_connection_set_verify(sm->ssl_ctx, data->conn, verify_peer,
				      flags, session_ctx,
				      sizeof(session_ctx))) {
		wpa_printf(MSG_INFO, "SSL: Failed to configure verification "
			   "of TLS peer certificate");
		tls_connection_deinit(sm->ssl_ctx, data->conn);
		data->conn = NULL;
		return -1;
	}

	data->tls_out_limit = sm->fragment_size > 0 ? sm->fragment_size : 1398;
	if (data->phase2) {
		/* Limit the fragment size in the inner TLS authentication
		 * since the outer authentication with EAP-PEAP does not yet
		 * support fragmentation */
		if (data->tls_out_limit > 100)
			data->tls_out_limit -= 100;
	}
	return 0;
}


void eap_server_tls_ssl_deinit(struct eap_sm *sm, struct eap_ssl_data *data)
{
	tls_connection_deinit(sm->ssl_ctx, data->conn);
	eap_server_tls_free_in_buf(data);
	wpabuf_free(data->tls_out);
	data->tls_out = NULL;
}


u8 * eap_server_tls_derive_key(struct eap_sm *sm, struct eap_ssl_data *data,
			       const char *label, size_t len)
{
	u8 *out;

	out = os_malloc(len);
	if (out == NULL)
		return NULL;

	if (tls_connection_export_key(sm->ssl_ctx, data->conn, label, out,
				      len)) {
		os_free(out);
		return NULL;
	}

	return out;
}


/**
 * eap_server_tls_derive_session_id - Derive a Session-Id based on TLS data
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @data: Data for TLS processing
 * @eap_type: EAP method used in Phase 1 (EAP_TYPE_TLS/PEAP/TTLS/FAST)
 * @len: Pointer to length of the session ID generated
 * Returns: Pointer to allocated Session-Id on success or %NULL on failure
 *
 * This function derive the Session-Id based on the TLS session data
 * (client/server random and method type).
 *
 * The caller is responsible for freeing the returned buffer.
 */
u8 * eap_server_tls_derive_session_id(struct eap_sm *sm,
				      struct eap_ssl_data *data, u8 eap_type,
				      size_t *len)
{
	struct tls_random keys;
	u8 *out;

	if (eap_type == EAP_TYPE_TLS && data->tls_v13) {
		*len = 64;
		return eap_server_tls_derive_key(sm, data,
						 "EXPORTER_EAP_TLS_Session-Id",
						 64);
	}

	if (tls_connection_get_random(sm->ssl_ctx, data->conn, &keys))
		return NULL;

	if (keys.client_random == NULL || keys.server_random == NULL)
		return NULL;

	*len = 1 + keys.client_random_len + keys.server_random_len;
	out = os_malloc(*len);
	if (out == NULL)
		return NULL;

	/* Session-Id = EAP type || client.random || server.random */
	out[0] = eap_type;
	os_memcpy(out + 1, keys.client_random, keys.client_random_len);
	os_memcpy(out + 1 + keys.client_random_len, keys.server_random,
		  keys.server_random_len);

	return out;
}


struct wpabuf * eap_server_tls_build_msg(struct eap_ssl_data *data,
					 int eap_type, int version, u8 id)
{
	struct wpabuf *req;
	u8 flags;
	size_t send_len, plen;

	wpa_printf(MSG_DEBUG, "SSL: Generating Request");
	if (data->tls_out == NULL) {
		wpa_printf(MSG_ERROR, "SSL: tls_out NULL in %s", __func__);
		return NULL;
	}

	flags = version;
	send_len = wpabuf_len(data->tls_out) - data->tls_out_pos;
	if (1 + send_len > data->tls_out_limit) {
		send_len = data->tls_out_limit - 1;
		flags |= EAP_TLS_FLAGS_MORE_FRAGMENTS;
		if (data->tls_out_pos == 0) {
			flags |= EAP_TLS_FLAGS_LENGTH_INCLUDED;
			send_len -= 4;
		}
	}

	plen = 1 + send_len;
	if (flags & EAP_TLS_FLAGS_LENGTH_INCLUDED)
		plen += 4;

	req = eap_tls_msg_alloc(eap_type, plen, EAP_CODE_REQUEST, id);
	if (req == NULL)
		return NULL;

	wpabuf_put_u8(req, flags); /* Flags */
	if (flags & EAP_TLS_FLAGS_LENGTH_INCLUDED)
		wpabuf_put_be32(req, wpabuf_len(data->tls_out));

	wpabuf_put_data(req, wpabuf_head_u8(data->tls_out) + data->tls_out_pos,
			send_len);
	data->tls_out_pos += send_len;

	if (data->tls_out_pos == wpabuf_len(data->tls_out)) {
		wpa_printf(MSG_DEBUG, "SSL: Sending out %lu bytes "
			   "(message sent completely)",
			   (unsigned long) send_len);
		wpabuf_free(data->tls_out);
		data->tls_out = NULL;
		data->tls_out_pos = 0;
		data->state = MSG;
	} else {
		wpa_printf(MSG_DEBUG, "SSL: Sending out %lu bytes "
			   "(%lu more to send)", (unsigned long) send_len,
			   (unsigned long) wpabuf_len(data->tls_out) -
			   data->tls_out_pos);
		data->state = WAIT_FRAG_ACK;
	}

	return req;
}


struct wpabuf * eap_server_tls_build_ack(u8 id, int eap_type, int version)
{
	struct wpabuf *req;

	req = eap_tls_msg_alloc(eap_type, 1, EAP_CODE_REQUEST, id);
	if (req == NULL)
		return NULL;
	wpa_printf(MSG_DEBUG, "SSL: Building ACK");
	wpabuf_put_u8(req, version); /* Flags */
	return req;
}


static int eap_server_tls_process_cont(struct eap_ssl_data *data,
				       const u8 *buf, size_t len)
{
	/* Process continuation of a pending message */
	if (len > wpabuf_tailroom(data->tls_in)) {
		wpa_printf(MSG_DEBUG, "SSL: Fragment overflow");
		return -1;
	}

	wpabuf_put_data(data->tls_in, buf, len);
	wpa_printf(MSG_DEBUG, "SSL: Received %lu bytes, waiting for %lu "
		   "bytes more", (unsigned long) len,
		   (unsigned long) wpabuf_tailroom(data->tls_in));

	return 0;
}


static int eap_server_tls_process_fragment(struct eap_ssl_data *data,
					   u8 flags, u32 message_length,
					   const u8 *buf, size_t len)
{
	/* Process a fragment that is not the last one of the message */
	if (data->tls_in == NULL && !(flags & EAP_TLS_FLAGS_LENGTH_INCLUDED)) {
		wpa_printf(MSG_DEBUG, "SSL: No Message Length field in a "
			   "fragmented packet");
		return -1;
	}

	if (data->tls_in == NULL) {
		/* First fragment of the message */

		/* Limit length to avoid rogue peers from causing large
		 * memory allocations. */
		if (message_length > 65536) {
			wpa_printf(MSG_INFO, "SSL: Too long TLS fragment (size"
				   " over 64 kB)");
			return -1;
		}

		if (len > message_length) {
			wpa_printf(MSG_INFO, "SSL: Too much data (%d bytes) in "
				   "first fragment of frame (TLS Message "
				   "Length %d bytes)",
				   (int) len, (int) message_length);
			return -1;
		}

		data->tls_in = wpabuf_alloc(message_length);
		if (data->tls_in == NULL) {
			wpa_printf(MSG_DEBUG, "SSL: No memory for message");
			return -1;
		}
		wpabuf_put_data(data->tls_in, buf, len);
		wpa_printf(MSG_DEBUG, "SSL: Received %lu bytes in first "
			   "fragment, waiting for %lu bytes more",
			   (unsigned long) len,
			   (unsigned long) wpabuf_tailroom(data->tls_in));
	}

	return 0;
}


int eap_server_tls_phase1(struct eap_sm *sm, struct eap_ssl_data *data)
{
	char buf[20];

	if (data->tls_out) {
		/* This should not happen.. */
		wpa_printf(MSG_INFO, "SSL: pending tls_out data when "
			   "processing new message");
		wpabuf_free(data->tls_out);
		WPA_ASSERT(data->tls_out == NULL);
	}

	data->tls_out = tls_connection_server_handshake(sm->ssl_ctx,
							data->conn,
							data->tls_in, NULL);
	if (data->tls_out == NULL) {
		wpa_printf(MSG_INFO, "SSL: TLS processing failed");
		return -1;
	}
	if (tls_connection_get_failed(sm->ssl_ctx, data->conn)) {
		/* TLS processing has failed - return error */
		wpa_printf(MSG_DEBUG, "SSL: Failed - tls_out available to "
			   "report error");
		return -1;
	}

	if (tls_get_version(sm->ssl_ctx, data->conn, buf, sizeof(buf)) == 0) {
		wpa_printf(MSG_DEBUG, "SSL: Using TLS version %s", buf);
		data->tls_v13 = os_strcmp(buf, "TLSv1.3") == 0;
	}

	if (!sm->serial_num &&
	    tls_connection_established(sm->ssl_ctx, data->conn))
		sm->serial_num = tls_connection_peer_serial_num(sm->ssl_ctx,
								data->conn);

	return 0;
}


static int eap_server_tls_reassemble(struct eap_ssl_data *data, u8 flags,
				     const u8 **pos, size_t *left)
{
	unsigned int tls_msg_len = 0;
	const u8 *end = *pos + *left;

	if (flags & EAP_TLS_FLAGS_LENGTH_INCLUDED) {
		if (*left < 4) {
			wpa_printf(MSG_INFO, "SSL: Short frame with TLS "
				   "length");
			return -1;
		}
		tls_msg_len = WPA_GET_BE32(*pos);
		wpa_printf(MSG_DEBUG, "SSL: TLS Message Length: %d",
			   tls_msg_len);
		*pos += 4;
		*left -= 4;

		if (*left > tls_msg_len) {
			wpa_printf(MSG_INFO, "SSL: TLS Message Length (%d "
				   "bytes) smaller than this fragment (%d "
				   "bytes)", (int) tls_msg_len, (int) *left);
			return -1;
		}
	}

	wpa_printf(MSG_DEBUG, "SSL: Received packet: Flags 0x%x "
		   "Message Length %u", flags, tls_msg_len);

	if (data->state == WAIT_FRAG_ACK) {
		if (*left != 0) {
			wpa_printf(MSG_DEBUG, "SSL: Unexpected payload in "
				   "WAIT_FRAG_ACK state");
			return -1;
		}
		wpa_printf(MSG_DEBUG, "SSL: Fragment acknowledged");
		return 1;
	}

	if (data->tls_in &&
	    eap_server_tls_process_cont(data, *pos, end - *pos) < 0)
		return -1;

	if (flags & EAP_TLS_FLAGS_MORE_FRAGMENTS) {
		if (eap_server_tls_process_fragment(data, flags, tls_msg_len,
						    *pos, end - *pos) < 0)
			return -1;

		data->state = FRAG_ACK;
		return 1;
	}

	if (data->state == FRAG_ACK) {
		wpa_printf(MSG_DEBUG, "SSL: All fragments received");
		data->state = MSG;
	}

	if (data->tls_in == NULL) {
		/* Wrap unfragmented messages as wpabuf without extra copy */
		wpabuf_set(&data->tmpbuf, *pos, end - *pos);
		data->tls_in = &data->tmpbuf;
	}

	return 0;
}


static void eap_server_tls_free_in_buf(struct eap_ssl_data *data)
{
	if (data->tls_in != &data->tmpbuf)
		wpabuf_free(data->tls_in);
	data->tls_in = NULL;
}


struct wpabuf * eap_server_tls_encrypt(struct eap_sm *sm,
				       struct eap_ssl_data *data,
				       const struct wpabuf *plain)
{
	struct wpabuf *buf;

	buf = tls_connection_encrypt(sm->ssl_ctx, data->conn,
				     plain);
	if (buf == NULL) {
		wpa_printf(MSG_INFO, "SSL: Failed to encrypt Phase 2 data");
		return NULL;
	}

	return buf;
}


int eap_server_tls_process(struct eap_sm *sm, struct eap_ssl_data *data,
			   struct wpabuf *respData, void *priv, int eap_type,
			   int (*proc_version)(struct eap_sm *sm, void *priv,
					       int peer_version),
			   void (*proc_msg)(struct eap_sm *sm, void *priv,
					    const struct wpabuf *respData))
{
	const u8 *pos;
	u8 flags;
	size_t left;
	int ret, res = 0;

	if (eap_type == EAP_UNAUTH_TLS_TYPE)
		pos = eap_hdr_validate(EAP_VENDOR_UNAUTH_TLS,
				       EAP_VENDOR_TYPE_UNAUTH_TLS, respData,
				       &left);
	else if (eap_type == EAP_WFA_UNAUTH_TLS_TYPE)
		pos = eap_hdr_validate(EAP_VENDOR_WFA_NEW,
				       EAP_VENDOR_WFA_UNAUTH_TLS, respData,
				       &left);
	else
		pos = eap_hdr_validate(EAP_VENDOR_IETF, eap_type, respData,
				       &left);
	if (pos == NULL || left < 1)
		return 0; /* Should not happen - frame already validated */
	flags = *pos++;
	left--;
	wpa_printf(MSG_DEBUG, "SSL: Received packet(len=%lu) - Flags 0x%02x",
		   (unsigned long) wpabuf_len(respData), flags);

	if (proc_version &&
	    proc_version(sm, priv, flags & EAP_TLS_VERSION_MASK) < 0)
		return -1;

	ret = eap_server_tls_reassemble(data, flags, &pos, &left);
	if (ret < 0) {
		res = -1;
		goto done;
	} else if (ret == 1)
		return 0;

	if (proc_msg)
		proc_msg(sm, priv, respData);

	if (tls_connection_get_write_alerts(sm->ssl_ctx, data->conn) > 1) {
		wpa_printf(MSG_INFO, "SSL: Locally detected fatal error in "
			   "TLS processing");
		res = -1;
	}

done:
	eap_server_tls_free_in_buf(data);

	return res;
}
