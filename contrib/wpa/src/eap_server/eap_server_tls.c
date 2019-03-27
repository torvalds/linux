/*
 * hostapd / EAP-TLS (RFC 2716)
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
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
	u8 eap_type;
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
	if (state == FAILURE)
		tls_connection_remove_session(data->ssl.conn);
}


static void eap_tls_valid_session(struct eap_sm *sm, struct eap_tls_data *data)
{
	struct wpabuf *buf;

	if (!sm->tls_session_lifetime)
		return;

	buf = wpabuf_alloc(1);
	if (!buf)
		return;
	wpabuf_put_u8(buf, data->eap_type);
	tls_connection_set_success_data(data->ssl.conn, buf);
}


static void * eap_tls_init(struct eap_sm *sm)
{
	struct eap_tls_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = START;

	if (eap_server_tls_ssl_init(sm, &data->ssl, 1, EAP_TYPE_TLS)) {
		wpa_printf(MSG_INFO, "EAP-TLS: Failed to initialize SSL.");
		eap_tls_reset(sm, data);
		return NULL;
	}

	data->eap_type = EAP_TYPE_TLS;

	return data;
}


#ifdef EAP_SERVER_UNAUTH_TLS
static void * eap_unauth_tls_init(struct eap_sm *sm)
{
	struct eap_tls_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = START;

	if (eap_server_tls_ssl_init(sm, &data->ssl, 0, EAP_UNAUTH_TLS_TYPE)) {
		wpa_printf(MSG_INFO, "EAP-TLS: Failed to initialize SSL.");
		eap_tls_reset(sm, data);
		return NULL;
	}

	data->eap_type = EAP_UNAUTH_TLS_TYPE;
	return data;
}
#endif /* EAP_SERVER_UNAUTH_TLS */


#ifdef CONFIG_HS20
static void * eap_wfa_unauth_tls_init(struct eap_sm *sm)
{
	struct eap_tls_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = START;

	if (eap_server_tls_ssl_init(sm, &data->ssl, 0,
				    EAP_WFA_UNAUTH_TLS_TYPE)) {
		wpa_printf(MSG_INFO, "EAP-TLS: Failed to initialize SSL.");
		eap_tls_reset(sm, data);
		return NULL;
	}

	data->eap_type = EAP_WFA_UNAUTH_TLS_TYPE;
	return data;
}
#endif /* CONFIG_HS20 */


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

	req = eap_tls_msg_alloc(data->eap_type, 1, EAP_CODE_REQUEST, id);
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
		return eap_server_tls_build_ack(id, data->eap_type, 0);
	}

	if (data->ssl.state == WAIT_FRAG_ACK) {
		res = eap_server_tls_build_msg(&data->ssl, data->eap_type, 0,
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

	res = eap_server_tls_build_msg(&data->ssl, data->eap_type, 0, id);

check_established:
	if (data->established && data->ssl.state != WAIT_FRAG_ACK) {
		/* TLS handshake has been completed and there are no more
		 * fragments waiting to be sent out. */
		wpa_printf(MSG_DEBUG, "EAP-TLS: Done");
		eap_tls_state(data, SUCCESS);
		eap_tls_valid_session(sm, data);
	}

	return res;
}


static Boolean eap_tls_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_tls_data *data = priv;
	const u8 *pos;
	size_t len;

	if (data->eap_type == EAP_UNAUTH_TLS_TYPE)
		pos = eap_hdr_validate(EAP_VENDOR_UNAUTH_TLS,
				       EAP_VENDOR_TYPE_UNAUTH_TLS, respData,
				       &len);
	else if (data->eap_type == EAP_WFA_UNAUTH_TLS_TYPE)
		pos = eap_hdr_validate(EAP_VENDOR_WFA_NEW,
				       EAP_VENDOR_WFA_UNAUTH_TLS, respData,
				       &len);
	else
		pos = eap_hdr_validate(EAP_VENDOR_IETF, data->eap_type,
				       respData, &len);
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
	const struct wpabuf *buf;
	const u8 *pos;

	if (eap_server_tls_process(sm, &data->ssl, respData, data,
				   data->eap_type, NULL, eap_tls_process_msg) <
	    0) {
		eap_tls_state(data, FAILURE);
		return;
	}

	if (!tls_connection_established(sm->ssl_ctx, data->ssl.conn) ||
	    !tls_connection_resumed(sm->ssl_ctx, data->ssl.conn))
		return;

	buf = tls_connection_get_success_data(data->ssl.conn);
	if (!buf || wpabuf_len(buf) < 1) {
		wpa_printf(MSG_DEBUG,
			   "EAP-TLS: No success data in resumed session - reject attempt");
		eap_tls_state(data, FAILURE);
		return;
	}

	pos = wpabuf_head(buf);
	if (*pos != data->eap_type) {
		wpa_printf(MSG_DEBUG,
			   "EAP-TLS: Resumed session for another EAP type (%u) - reject attempt",
			   *pos);
		eap_tls_state(data, FAILURE);
		return;
	}

	wpa_printf(MSG_DEBUG,
		   "EAP-TLS: Resuming previous session");
	eap_tls_state(data, SUCCESS);
	tls_connection_set_success_data_resumed(data->ssl.conn);
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
	const char *label;

	if (data->state != SUCCESS)
		return NULL;

	if (data->ssl.tls_v13)
		label = "EXPORTER_EAP_TLS_Key_Material";
	else
		label = "client EAP encryption";
	eapKeyData = eap_server_tls_derive_key(sm, &data->ssl, label,
					       EAP_TLS_KEY_LEN + EAP_EMSK_LEN);
	if (eapKeyData) {
		*len = EAP_TLS_KEY_LEN;
		wpa_hexdump(MSG_DEBUG, "EAP-TLS: Derived key",
			    eapKeyData, EAP_TLS_KEY_LEN);
		os_memset(eapKeyData + EAP_TLS_KEY_LEN, 0, EAP_EMSK_LEN);
	} else {
		wpa_printf(MSG_DEBUG, "EAP-TLS: Failed to derive key");
	}

	return eapKeyData;
}


static u8 * eap_tls_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_tls_data *data = priv;
	u8 *eapKeyData, *emsk;
	const char *label;

	if (data->state != SUCCESS)
		return NULL;

	if (data->ssl.tls_v13)
		label = "EXPORTER_EAP_TLS_Key_Material";
	else
		label = "client EAP encryption";
	eapKeyData = eap_server_tls_derive_key(sm, &data->ssl, label,
					       EAP_TLS_KEY_LEN + EAP_EMSK_LEN);
	if (eapKeyData) {
		emsk = os_malloc(EAP_EMSK_LEN);
		if (emsk)
			os_memcpy(emsk, eapKeyData + EAP_TLS_KEY_LEN,
				  EAP_EMSK_LEN);
		bin_clear_free(eapKeyData, EAP_TLS_KEY_LEN + EAP_EMSK_LEN);
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


static u8 * eap_tls_get_session_id(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_tls_data *data = priv;

	if (data->state != SUCCESS)
		return NULL;

	return eap_server_tls_derive_session_id(sm, &data->ssl, EAP_TYPE_TLS,
						len);
}


int eap_server_tls_register(void)
{
	struct eap_method *eap;

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
	eap->getSessionId = eap_tls_get_session_id;

	return eap_server_method_register(eap);
}


#ifdef EAP_SERVER_UNAUTH_TLS
int eap_server_unauth_tls_register(void)
{
	struct eap_method *eap;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_UNAUTH_TLS,
				      EAP_VENDOR_TYPE_UNAUTH_TLS,
				      "UNAUTH-TLS");
	if (eap == NULL)
		return -1;

	eap->init = eap_unauth_tls_init;
	eap->reset = eap_tls_reset;
	eap->buildReq = eap_tls_buildReq;
	eap->check = eap_tls_check;
	eap->process = eap_tls_process;
	eap->isDone = eap_tls_isDone;
	eap->getKey = eap_tls_getKey;
	eap->isSuccess = eap_tls_isSuccess;
	eap->get_emsk = eap_tls_get_emsk;

	return eap_server_method_register(eap);
}
#endif /* EAP_SERVER_UNAUTH_TLS */


#ifdef CONFIG_HS20
int eap_server_wfa_unauth_tls_register(void)
{
	struct eap_method *eap;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_WFA_NEW,
				      EAP_VENDOR_WFA_UNAUTH_TLS,
				      "WFA-UNAUTH-TLS");
	if (eap == NULL)
		return -1;

	eap->init = eap_wfa_unauth_tls_init;
	eap->reset = eap_tls_reset;
	eap->buildReq = eap_tls_buildReq;
	eap->check = eap_tls_check;
	eap->process = eap_tls_process;
	eap->isDone = eap_tls_isDone;
	eap->getKey = eap_tls_getKey;
	eap->isSuccess = eap_tls_isSuccess;
	eap->get_emsk = eap_tls_get_emsk;

	return eap_server_method_register(eap);
}
#endif /* CONFIG_HS20 */
