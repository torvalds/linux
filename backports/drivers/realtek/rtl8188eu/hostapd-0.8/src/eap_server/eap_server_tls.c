/*
 * hostapd / EAP-TLS (RFC 2716)
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
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
#include "eap_i.h"
#include "eap_tls_common.h"
#include "crypto/tls.h"


static void eap_tls_reset(struct eap_sm *sm, void *priv);


struct eap_tls_data {
	struct eap_ssl_data ssl;
	enum { START, CONTINUE, SUCCESS, FAILURE } state;
	int established;
};


static const char * eap_tls_state_txt(int state)
{
	switch (state) {
	case START:
		return "START";
	case CONTINUE:
		return "CONTINUE";
	case SUCCESS:
		return "SUCCESS";
	case FAILURE:
		return "FAILURE";
	default:
		return "Unknown?!";
	}
}


static void eap_tls_state(struct eap_tls_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-TLS: %s -> %s",
		   eap_tls_state_txt(data->state),
		   eap_tls_state_txt(state));
	data->state = state;
}


static void * eap_tls_init(struct eap_sm *sm)
{
	struct eap_tls_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = START;

	if (eap_server_tls_ssl_init(sm, &data->ssl, 1)) {
		wpa_printf(MSG_INFO, "EAP-TLS: Failed to initialize SSL.");
		eap_tls_reset(sm, data);
		return NULL;
	}

	return data;
}


static void eap_tls_reset(struct eap_sm *sm, void *priv)
{
	struct eap_tls_data *data = priv;
	if (data == NULL)
		return;
	eap_server_tls_ssl_deinit(sm, &data->ssl);
	os_free(data);
}


static struct wpabuf * eap_tls_build_start(struct eap_sm *sm,
					   struct eap_tls_data *data, u8 id)
{
	struct wpabuf *req;

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TLS, 1, EAP_CODE_REQUEST,
			    id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-TLS: Failed to allocate memory for "
			   "request");
		eap_tls_state(data, FAILURE);
		return NULL;
	}

	wpabuf_put_u8(req, EAP_TLS_FLAGS_START);

	eap_tls_state(data, CONTINUE);

	return req;
}


static struct wpabuf * eap_tls_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_tls_data *data = priv;
	struct wpabuf *res;

	if (data->ssl.state == FRAG_ACK) {
		return eap_server_tls_build_ack(id, EAP_TYPE_TLS, 0);
	}

	if (data->ssl.state == WAIT_FRAG_ACK) {
		res = eap_server_tls_build_msg(&data->ssl, EAP_TYPE_TLS, 0,
					       id);
		goto check_established;
	}

	switch (data->state) {
	case START:
		return eap_tls_build_start(sm, data, id);
	case CONTINUE:
		if (tls_connection_established(sm->ssl_ctx, data->ssl.conn))
			data->established = 1;
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-TLS: %s - unexpected state %d",
			   __func__, data->state);
		return NULL;
	}

	res = eap_server_tls_build_msg(&data->ssl, EAP_TYPE_TLS, 0, id);

check_established:
	if (data->established && data->ssl.state != WAIT_FRAG_ACK) {
		/* TLS handshake has been completed and there are no more
		 * fragments waiting to be sent out. */
		wpa_printf(MSG_DEBUG, "EAP-TLS: Done");
		eap_tls_state(data, SUCCESS);
	}

	return res;
}


static Boolean eap_tls_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_TLS, respData, &len);
	if (pos == NULL || len < 1) {
		wpa_printf(MSG_INFO, "EAP-TLS: Invalid frame");
		return TRUE;
	}

	return FALSE;
}


static void eap_tls_process_msg(struct eap_sm *sm, void *priv,
				const struct wpabuf *respData)
{
	struct eap_tls_data *data = priv;
	if (data->state == SUCCESS && wpabuf_len(data->ssl.tls_in) == 0) {
		wpa_printf(MSG_DEBUG, "EAP-TLS: Client acknowledged final TLS "
			   "handshake message");
		return;
	}
	if (eap_server_tls_phase1(sm, &data->ssl) < 0)
		eap_tls_state(data, FAILURE);
}


static void eap_tls_process(struct eap_sm *sm, void *priv,
			    struct wpabuf *respData)
{
	struct eap_tls_data *data = priv;
	if (eap_server_tls_process(sm, &data->ssl, respData, data,
				   EAP_TYPE_TLS, NULL, eap_tls_process_msg) <
	    0)
		eap_tls_state(data, FAILURE);
}


static Boolean eap_tls_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_tls_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_tls_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_tls_data *data = priv;
	u8 *eapKeyData;

	if (data->state != SUCCESS)
		return NULL;

	eapKeyData = eap_server_tls_derive_key(sm, &data->ssl,
					       "client EAP encryption",
					       EAP_TLS_KEY_LEN);
	if (eapKeyData) {
		*len = EAP_TLS_KEY_LEN;
		wpa_hexdump(MSG_DEBUG, "EAP-TLS: Derived key",
			    eapKeyData, EAP_TLS_KEY_LEN);
	} else {
		wpa_printf(MSG_DEBUG, "EAP-TLS: Failed to derive key");
	}

	return eapKeyData;
}


static u8 * eap_tls_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_tls_data *data = priv;
	u8 *eapKeyData, *emsk;

	if (data->state != SUCCESS)
		return NULL;

	eapKeyData = eap_server_tls_derive_key(sm, &data->ssl,
					       "client EAP encryption",
					       EAP_TLS_KEY_LEN + EAP_EMSK_LEN);
	if (eapKeyData) {
		emsk = os_malloc(EAP_EMSK_LEN);
		if (emsk)
			os_memcpy(emsk, eapKeyData + EAP_TLS_KEY_LEN,
				  EAP_EMSK_LEN);
		os_free(eapKeyData);
	} else
		emsk = NULL;

	if (emsk) {
		*len = EAP_EMSK_LEN;
		wpa_hexdump(MSG_DEBUG, "EAP-TLS: Derived EMSK",
			    emsk, EAP_EMSK_LEN);
	} else {
		wpa_printf(MSG_DEBUG, "EAP-TLS: Failed to derive EMSK");
	}

	return emsk;
}


static Boolean eap_tls_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_tls_data *data = priv;
	return data->state == SUCCESS;
}


int eap_server_tls_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_TLS, "TLS");
	if (eap == NULL)
		return -1;

	eap->init = eap_tls_init;
	eap->reset = eap_tls_reset;
	eap->buildReq = eap_tls_buildReq;
	eap->check = eap_tls_check;
	eap->process = eap_tls_process;
	eap->isDone = eap_tls_isDone;
	eap->getKey = eap_tls_getKey;
	eap->isSuccess = eap_tls_isSuccess;
	eap->get_emsk = eap_tls_get_emsk;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}
