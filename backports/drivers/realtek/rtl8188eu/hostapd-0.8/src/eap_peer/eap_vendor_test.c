/*
 * EAP peer method: Test method for vendor specific (expanded) EAP type
 * Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file implements a vendor specific test method using EAP expanded types.
 * This is only for test use and must not be used for authentication since no
 * security is provided.
 */

#include "includes.h"

#include "common.h"
#include "eap_i.h"
#ifdef TEST_PENDING_REQUEST
#include "eloop.h"
#endif /* TEST_PENDING_REQUEST */


#define EAP_VENDOR_ID 0xfffefd
#define EAP_VENDOR_TYPE 0xfcfbfaf9


/* #define TEST_PENDING_REQUEST */

struct eap_vendor_test_data {
	enum { INIT, CONFIRM, SUCCESS } state;
	int first_try;
};


static void * eap_vendor_test_init(struct eap_sm *sm)
{
	struct eap_vendor_test_data *data;
	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = INIT;
	data->first_try = 1;
	return data;
}


static void eap_vendor_test_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_vendor_test_data *data = priv;
	os_free(data);
}


#ifdef TEST_PENDING_REQUEST
static void eap_vendor_ready(void *eloop_ctx, void *timeout_ctx)
{
	struct eap_sm *sm = eloop_ctx;
	wpa_printf(MSG_DEBUG, "EAP-VENDOR-TEST: Ready to re-process pending "
		   "request");
	eap_notify_pending(sm);
}
#endif /* TEST_PENDING_REQUEST */


static struct wpabuf * eap_vendor_test_process(struct eap_sm *sm, void *priv,
					       struct eap_method_ret *ret,
					       const struct wpabuf *reqData)
{
	struct eap_vendor_test_data *data = priv;
	struct wpabuf *resp;
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_ID, EAP_VENDOR_TYPE, reqData, &len);
	if (pos == NULL || len < 1) {
		ret->ignore = TRUE;
		return NULL;
	}

	if (data->state == INIT && *pos != 1) {
		wpa_printf(MSG_DEBUG, "EAP-VENDOR-TEST: Unexpected message "
			   "%d in INIT state", *pos);
		ret->ignore = TRUE;
		return NULL;
	}

	if (data->state == CONFIRM && *pos != 3) {
		wpa_printf(MSG_DEBUG, "EAP-VENDOR-TEST: Unexpected message "
			   "%d in CONFIRM state", *pos);
		ret->ignore = TRUE;
		return NULL;
	}

	if (data->state == SUCCESS) {
		wpa_printf(MSG_DEBUG, "EAP-VENDOR-TEST: Unexpected message "
			   "in SUCCESS state");
		ret->ignore = TRUE;
		return NULL;
	}

	if (data->state == CONFIRM) {
#ifdef TEST_PENDING_REQUEST
		if (data->first_try) {
			data->first_try = 0;
			wpa_printf(MSG_DEBUG, "EAP-VENDOR-TEST: Testing "
				   "pending request");
			ret->ignore = TRUE;
			eloop_register_timeout(1, 0, eap_vendor_ready, sm,
					       NULL);
			return NULL;
		}
#endif /* TEST_PENDING_REQUEST */
	}

	ret->ignore = FALSE;

	wpa_printf(MSG_DEBUG, "EAP-VENDOR-TEST: Generating Response");
	ret->allowNotifications = TRUE;

	resp = eap_msg_alloc(EAP_VENDOR_ID, EAP_VENDOR_TYPE, 1,
			     EAP_CODE_RESPONSE, eap_get_id(reqData));
	if (resp == NULL)
		return NULL;

	if (data->state == INIT) {
		wpabuf_put_u8(resp, 2);
		data->state = CONFIRM;
		ret->methodState = METHOD_CONT;
		ret->decision = DECISION_FAIL;
	} else {
		wpabuf_put_u8(resp, 4);
		data->state = SUCCESS;
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_UNCOND_SUCC;
	}

	return resp;
}


static Boolean eap_vendor_test_isKeyAvailable(struct eap_sm *sm, void *priv)
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


int eap_peer_vendor_test_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_ID, EAP_VENDOR_TYPE,
				    "VENDOR-TEST");
	if (eap == NULL)
		return -1;

	eap->init = eap_vendor_test_init;
	eap->deinit = eap_vendor_test_deinit;
	eap->process = eap_vendor_test_process;
	eap->isKeyAvailable = eap_vendor_test_isKeyAvailable;
	eap->getKey = eap_vendor_test_getKey;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
