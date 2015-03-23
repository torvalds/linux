/*
 * EAP-WSC common routines for Wi-Fi Protected Setup
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
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
#include "eap_defs.h"
#include "eap_common.h"
#include "wps/wps.h"
#include "eap_wsc_common.h"

struct wpabuf * eap_wsc_build_frag_ack(u8 id, u8 code)
{
	struct wpabuf *msg;

	msg = eap_msg_alloc(EAP_VENDOR_WFA, EAP_VENDOR_TYPE_WSC, 2, code, id);
	if (msg == NULL) {
		wpa_printf(MSG_ERROR, "EAP-WSC: Failed to allocate memory for "
			   "FRAG_ACK");
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-WSC: Send WSC/FRAG_ACK");
	wpabuf_put_u8(msg, WSC_FRAG_ACK); /* Op-Code */
	wpabuf_put_u8(msg, 0); /* Flags */

	return msg;
}
