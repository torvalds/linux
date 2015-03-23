/*
 * EAP peer method: EAP-TLS (RFC 2716)
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
#include "crypto/tls.h"
#include "eap_i.h"
#include "eap_tls_common.h"
#include "eap_config.h"


static void eap_tls_deinit(struct eap_sm *sm, void *priv);


struct eap_tls_data {
	struct eap_ssl_data ssl;
	u8 *key_data;
};


static void * eap_tls_init(struct eap_sm *sm)
{
	struct eap_tls_data *data;
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL ||
	    ((sm->init_phase2 ? config->private_key2 : config->private_key)
	     == NULL &&
	     (sm->init_phase2 ? config->engine2 : config->engine) == 0)) {
		wpa_printf(MSG_INFO, "EAP-TLS: Private key not configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	if (eap_peer_tls_ssl_init(sm, &data->ssl, config)) {
		wpa_printf(MSG_INFO, "EAP-TLS: Failed to initialize SSL.");
		eap_tls_deinit(sm, data);
		if (config->engine) {
			wpa_printf(MSG_DEBUG, "EAP-TLS: Requesting Smartcard "
				   "PIN");
			eap_sm_request_pin(sm);
			sm->ignore = TRUE;
		} else if (config->private_key && !config->private_key_passwd)
		{
			wpa_printf(MSG_DEBUG, "EAP-TLS: Requesting private "
				   "key passphrase");
			eap_sm_request_passphrase(sm);
			sm->ignore = TRUE;
		}
		return NULL;
	}

	return data;
}


static void eap_tls_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_tls_data *data = priv;
	if (data == NULL)
		return;
	eap_peer_tls_ssl_deinit(sm, &data->ssl);
	os_free(data->key_data);
	os_free(data);
}


static struct wpabuf * eap_tls_failure(struct eap_sm *sm,
				       struct eap_tls_data *data,
				       struct eap_method_ret *ret, int res,
				       struct wpabuf *resp, u8 id)
{
	wpa_printf(MSG_DEBUG, "EAP-TLS: TLS processing failed");

	ret->methodState = METHOD_DONE;
	ret->decision = DECISION_FAIL;

	if (res == -1) {
		struct eap_peer_config *config = eap_get_config(sm);
		if (config) {
			/*
			 * The TLS handshake failed. So better forget the old
			 * PIN. It may be wrong, we cannot be sure but trying
			 * the wrong one again might block it on the card--so
			 * better ask the user again.
			 */
			os_free(config->pin);
			config->pin = NULL;
		}
	}

	if (resp) {
		/*
		 * This is likely an alert message, so send it instead of just
		 * ACKing the error.
		 */
		return resp;
	}

	return eap_peer_tls_build_ack(id, EAP_TYPE_TLS, 0);
}


static void eap_tls_success(struct eap_sm *sm, struct eap_tls_data *data,
			    struct eap_method_ret *ret)
{
	wpa_printf(MSG_DEBUG, "EAP-TLS: Done");

	ret->methodState = METHOD_DONE;
	ret->decision = DECISION_UNCOND_SUCC;

	os_free(data->key_data);
	data->key_data = eap_peer_tls_derive_key(sm, &data->ssl,
						 "client EAP encryption",
						 EAP_TLS_KEY_LEN +
						 EAP_EMSK_LEN);
	if (data->key_data) {
		wpa_hexdump_key(MSG_DEBUG, "EAP-TLS: Derived key",
				data->key_data, EAP_TLS_KEY_LEN);
		wpa_hexdump_key(MSG_DEBUG, "EAP-TLS: Derived EMSK",
				data->key_data + EAP_TLS_KEY_LEN,
				EAP_EMSK_LEN);
	} else {
		wpa_printf(MSG_INFO, "EAP-TLS: Failed to derive key");
	}
}


static struct wpabuf * eap_tls_process(struct eap_sm *sm, void *priv,
				       struct eap_method_ret *ret,
				       const struct wpabuf *reqData)
{
	size_t left;
	int res;
	struct wpabuf *resp;
	u8 flags, id;
	const u8 *pos;
	struct eap_tls_data *data = priv;

	pos = eap_peer_tls_process_init(sm, &data->ssl, EAP_TYPE_TLS, ret,
					reqData, &left, &flags);
	if (pos == NULL)
		return NULL;
	id = eap_get_id(reqData);

	if (flags & EAP_TLS_FLAGS_START) {
		wpa_printf(MSG_DEBUG, "EAP-TLS: Start");
		left = 0; /* make sure that this frame is empty, even though it
			   * should always be, anyway */
	}

	resp = NULL;
	res = eap_peer_tls_process_helper(sm, &data->ssl, EAP_TYPE_TLS, 0, id,
					  pos, left, &resp);

	if (res < 0) {
		return eap_tls_failure(sm, data, ret, res, resp, id);
	}

	if (tls_connection_established(sm->ssl_ctx, data->ssl.conn))
		eap_tls_success(sm, data, ret);

	if (res == 1) {
		wpabuf_free(resp);
		return eap_peer_tls_build_ack(id, EAP_TYPE_TLS, 0);
	}

	return resp;
}


static Boolean eap_tls_has_reauth_data(struct eap_sm *sm, void *priv)
{
	struct eap_tls_data *data = priv;
	return tls_connection_established(sm->ssl_ctx, data->ssl.conn);
}


static void eap_tls_deinit_for_reauth(struct eap_sm *sm, void *priv)
{
}


static void * eap_tls_init_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_tls_data *data = priv;
	os_free(data->key_data);
	data->key_data = NULL;
	if (eap_peer_tls_reauth_init(sm, &data->ssl)) {
		os_free(data);
		return NULL;
	}
	return priv;
}


static int eap_tls_get_status(struct eap_sm *sm, void *priv, char *buf,
			      size_t buflen, int verbose)
{
	struct eap_tls_data *data = priv;
	return eap_peer_tls_status(sm, &data->ssl, buf, buflen, verbose);
}


static Boolean eap_tls_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_tls_data *data = priv;
	return data->key_data != NULL;
}


static u8 * eap_tls_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_tls_data *data = priv;
	u8 *key;

	if (data->key_data == NULL)
		return NULL;

	key = os_malloc(EAP_TLS_KEY_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_TLS_KEY_LEN;
	os_memcpy(key, data->key_data, EAP_TLS_KEY_LEN);

	return key;
}


static u8 * eap_tls_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_tls_data *data = priv;
	u8 *key;

	if (data->key_data == NULL)
		return NULL;

	key = os_malloc(EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_EMSK_LEN;
	os_memcpy(key, data->key_data + EAP_TLS_KEY_LEN, EAP_EMSK_LEN);

	return key;
}


int eap_peer_tls_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_TLS, "TLS");
	if (eap == NULL)
		return -1;

	eap->init = eap_tls_init;
	eap->deinit = eap_tls_deinit;
	eap->process = eap_tls_process;
	eap->isKeyAvailable = eap_tls_isKeyAvailable;
	eap->getKey = eap_tls_getKey;
	eap->get_status = eap_tls_get_status;
	eap->has_reauth_data = eap_tls_has_reauth_data;
	eap->deinit_for_reauth = eap_tls_deinit_for_reauth;
	eap->init_for_reauth = eap_tls_init_for_reauth;
	eap->get_emsk = eap_tls_get_emsk;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
