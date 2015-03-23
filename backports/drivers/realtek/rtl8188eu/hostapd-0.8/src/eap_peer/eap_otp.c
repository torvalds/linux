/*
 * EAP peer method: EAP-OTP (RFC 3748)
 * Copyright (c) 2004-2006, Jouni Malinen <j@w1.fi>
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


static void * eap_otp_init(struct eap_sm *sm)
{
	/* No need for private data. However, must return non-NULL to indicate
	 * success. */
	return (void *) 1;
}


static void eap_otp_deinit(struct eap_sm *sm, void *priv)
{
}


static struct wpabuf * eap_otp_process(struct eap_sm *sm, void *priv,
				       struct eap_method_ret *ret,
				       const struct wpabuf *reqData)
{
	struct wpabuf *resp;
	const u8 *pos, *password;
	size_t password_len, len;
	int otp;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_OTP, reqData, &len);
	if (pos == NULL) {
		ret->ignore = TRUE;
		return NULL;
	}
	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-OTP: Request message",
			  pos, len);

	password = eap_get_config_otp(sm, &password_len);
	if (password)
		otp = 1;
	else {
		password = eap_get_config_password(sm, &password_len);
		otp = 0;
	}

	if (password == NULL) {
		wpa_printf(MSG_INFO, "EAP-OTP: Password not configured");
		eap_sm_request_otp(sm, (const char *) pos, len);
		ret->ignore = TRUE;
		return NULL;
	}

	ret->ignore = FALSE;

	ret->methodState = METHOD_DONE;
	ret->decision = DECISION_COND_SUCC;
	ret->allowNotifications = FALSE;

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_OTP, password_len,
			     EAP_CODE_RESPONSE, eap_get_id(reqData));
	if (resp == NULL)
		return NULL;
	wpabuf_put_data(resp, password, password_len);
	wpa_hexdump_ascii_key(MSG_MSGDUMP, "EAP-OTP: Response",
			      password, password_len);

	if (otp) {
		wpa_printf(MSG_DEBUG, "EAP-OTP: Forgetting used password");
		eap_clear_config_otp(sm);
	}

	return resp;
}


int eap_peer_otp_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_OTP, "OTP");
	if (eap == NULL)
		return -1;

	eap->init = eap_otp_init;
	eap->deinit = eap_otp_deinit;
	eap->process = eap_otp_process;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
