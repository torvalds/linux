/*
 * hostapd / Test method for vendor specific (expanded) EAP type
 * Copyright (c) 2005-2007, Jouni Malinen <j@w1.fi>
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


#define EAP_VENDOR_ID 0xfffefd
#define EAP_VENDOR_TYPE 0xfcfbfaf9


struct eap_vendor_test_data {
	enum { INIT, CONFIRM, SUCCESS, FAILURE } state;
};


static const char * eap_vendor_test_state_txt(int state)
{
	switch (state) {
	case INIT:
		return "INIT";
	case CONFIRM:
		return "CONFIRM";
	case SUCCESS:
		return "SUCCESS";
	case FAILURE:
		return "FAILURE";
	default:
		return "?";
	}
}


static void eap_vendor_test_state(struct eap_vendor_test_data *data,
				  int state)
{
	wpa_printf(MSG_DEBUG, "EAP-VENDOR-TEST: %s -> %s",
		   eap_vendor_test_state_txt(data->state),
		   eap_vendor_test_state_txt(state));
	data->state = state;
}


static void * eap_vendor_test_init(struct eap_sm *sm)
{
	struct eap_vendor_test_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = INIT;

	return data;
}


static void eap_vendor_test_reset(struct eap_sm *sm, void *priv)
{
	struct eap_vendor_test_data *data = priv;
	os_free(data);
}


static struct wpabuf * eap_vendor_test_buildReq(struct eap_sm *sm, void *priv,
						u8 id)
{
	struct eap_vendor_test_data *data = priv;
	struct wpabuf *req;

	req = eap_msg_alloc(EAP_VENDOR_ID, EAP_VENDOR_TYPE, 1,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-VENDOR-TEST: Failed to allocate "
			   "memory for request");
		return NULL;
	}

	wpabuf_put_u8(req, data->state == INIT ? 1 : 3);

	return req;
}


static Boolean eap_vendor_test_check(struct eap_sm *sm, void *priv,
				     struct wpabuf *respData)
{
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_ID, EAP_VENDOR_TYPE, respData, &len);
	if (pos == NULL || len < 1) {
		wpa_printf(MSG_INFO, "EAP-VENDOR-TEST: Invalid frame");
		return TRUE;
	}

	return FALSE;
}


static void eap_vendor_test_process(struct eap_sm *sm, void *priv,
				    struct wpabuf *respData)
{
	struct eap_vendor_test_data *data = priv;
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_ID, EAP_VENDOR_TYPE, respData, &len);
	if (pos == NULL || len < 1)
		return;

	if (data->state == INIT) {
		if (*pos == 2)
			eap_vendor_test_state(data, CONFIRM);
		else
			eap_vendor_test_state(data, FAILURE);
	} else if (data->state == CONFIRM) {
		if (*pos == 4)
			eap_vendor_test_state(data, SUCCESS);
		else
			eap_vendor_test_state(data, FAILURE);
	} else
		eap_vendor_test_state(data, FAILURE);
}


static Boolean eap_vendor_test_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_vendor_test_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_vendor_test_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_vendor_test_data *data = priv;
	u8 *key;
	const int key_len = 64;

	if (data->state != SUCCESS)
		return NULL;

	key = os_malloc(key_len);
	if (key == NULL)
		return NULL;

	os_memset(key, 0x11, key_len / 2);
	os_memset(key + key_len / 2, 0x22, key_len / 2);
	*len = key_len;

	return key;
}


static Boolean eap_vendor_test_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_vendor_test_data *data = priv;
	return data->state == SUCCESS;
}


int eap_server_vendor_test_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_ID, EAP_VENDOR_TYPE,
				      "VENDOR-TEST");
	if (eap == NULL)
		return -1;

	eap->init = eap_vendor_test_init;
	eap->reset = eap_vendor_test_reset;
	eap->buildReq = eap_vendor_test_buildReq;
	eap->check = eap_vendor_test_check;
	eap->process = eap_vendor_test_process;
	eap->isDone = eap_vendor_test_isDone;
	eap->getKey = eap_vendor_test_getKey;
	eap->isSuccess = eap_vendor_test_isSuccess;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}
