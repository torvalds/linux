/*
 * EAP-WSC server for Wi-Fi Protected Setup
 * Copyright (c) 2007-2008, Jouni Malinen <j@w1.fi>
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
#include "eloop.h"
#include "eap_i.h"
#include "eap_common/eap_wsc_common.h"
#include "p2p/p2p.h"
#include "wps/wps.h"


struct eap_wsc_data {
	enum { START, MESG, FRAG_ACK, WAIT_FRAG_ACK, DONE, FAIL } state;
	int registrar;
	struct wpabuf *in_buf;
	struct wpabuf *out_buf;
	enum wsc_op_code in_op_code, out_op_code;
	size_t out_used;
	size_t fragment_size;
	struct wps_data *wps;
	int ext_reg_timeout;
};


#ifndef CONFIG_NO_STDOUT_DEBUG
static const char * eap_wsc_state_txt(int state)
{
	switch (state) {
	case START:
		return "START";
	case MESG:
		return "MESG";
	case FRAG_ACK:
		return "FRAG_ACK";
	case WAIT_FRAG_ACK:
		return "WAIT_FRAG_ACK";
	case DONE:
		return "DONE";
	case FAIL:
		return "FAIL";
	default:
		return "?";
	}
}
#endif /* CONFIG_NO_STDOUT_DEBUG */


static void eap_wsc_state(struct eap_wsc_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-WSC: %s -> %s",
		   eap_wsc_state_txt(data->state),
		   eap_wsc_state_txt(state));
	data->state = state;
}


static void eap_wsc_ext_reg_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct eap_sm *sm = eloop_ctx;
	struct eap_wsc_data *data = timeout_ctx;

	if (sm->method_pending != METHOD_PENDING_WAIT)
		return;

	wpa_printf(MSG_DEBUG, "EAP-WSC: Timeout while waiting for an External "
		   "Registrar");
	data->ext_reg_timeout = 1;
	eap_sm_pending_cb(sm);
}


static void * eap_wsc_init(struct eap_sm *sm)
{
	struct eap_wsc_data *data;
	int registrar;
	struct wps_config cfg;

	if (sm->identity && sm->identity_len == WSC_ID_REGISTRAR_LEN &&
	    os_memcmp(sm->identity, WSC_ID_REGISTRAR, WSC_ID_REGISTRAR_LEN) ==
	    0)
		registrar = 0; /* Supplicant is Registrar */
	else if (sm->identity && sm->identity_len == WSC_ID_ENROLLEE_LEN &&
		 os_memcmp(sm->identity, WSC_ID_ENROLLEE, WSC_ID_ENROLLEE_LEN)
		 == 0)
		registrar = 1; /* Supplicant is Enrollee */
	else {
		wpa_hexdump_ascii(MSG_INFO, "EAP-WSC: Unexpected identity",
				  sm->identity, sm->identity_len);
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = registrar ? START : MESG;
	data->registrar = registrar;

	os_memset(&cfg, 0, sizeof(cfg));
	cfg.wps = sm->wps;
	cfg.registrar = registrar;
	if (registrar) {
		if (sm->wps == NULL || sm->wps->registrar == NULL) {
			wpa_printf(MSG_INFO, "EAP-WSC: WPS Registrar not "
				   "initialized");
			os_free(data);
			return NULL;
		}
	} else {
		if (sm->user == NULL || sm->user->password == NULL) {
			/*
			 * In theory, this should not really be needed, but
			 * Windows 7 uses Registrar mode to probe AP's WPS
			 * capabilities before trying to use Enrollee and fails
			 * if the AP does not allow that probing to happen..
			 */
			wpa_printf(MSG_DEBUG, "EAP-WSC: No AP PIN (password) "
				   "configured for Enrollee functionality - "
				   "allow for probing capabilities (M1)");
		} else {
			cfg.pin = sm->user->password;
			cfg.pin_len = sm->user->password_len;
		}
	}
	cfg.assoc_wps_ie = sm->assoc_wps_ie;
	cfg.peer_addr = sm->peer_addr;
#ifdef CONFIG_P2P
	if (sm->assoc_p2p_ie) {
		wpa_printf(MSG_DEBUG, "EAP-WSC: Prefer PSK format for P2P "
			   "client");
		cfg.use_psk_key = 1;
		cfg.p2p_dev_addr = p2p_get_go_dev_addr(sm->assoc_p2p_ie);
	}
#endif /* CONFIG_P2P */
	data->wps = wps_init(&cfg);
	if (data->wps == NULL) {
		os_free(data);
		return NULL;
	}
	data->fragment_size = sm->fragment_size > 0 ? sm->fragment_size :
		WSC_FRAGMENT_SIZE;

	return data;
}


static void eap_wsc_reset(struct eap_sm *sm, void *priv)
{
	struct eap_wsc_data *data = priv;
	eloop_cancel_timeout(eap_wsc_ext_reg_timeout, sm, data);
	wpabuf_free(data->in_buf);
	wpabuf_free(data->out_buf);
	wps_deinit(data->wps);
	os_free(data);
}


static struct wpabuf * eap_wsc_build_start(struct eap_sm *sm,
					   struct eap_wsc_data *data, u8 id)
{
	struct wpabuf *req;

	req = eap_msg_alloc(EAP_VENDOR_WFA, EAP_VENDOR_TYPE_WSC, 2,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-WSC: Failed to allocate memory for "
			   "request");
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-WSC: Send WSC/Start");
	wpabuf_put_u8(req, WSC_Start); /* Op-Code */
	wpabuf_put_u8(req, 0); /* Flags */

	return req;
}


static struct wpabuf * eap_wsc_build_msg(struct eap_wsc_data *data, u8 id)
{
	struct wpabuf *req;
	u8 flags;
	size_t send_len, plen;

	flags = 0;
	send_len = wpabuf_len(data->out_buf) - data->out_used;
	if (2 + send_len > data->fragment_size) {
		send_len = data->fragment_size - 2;
		flags |= WSC_FLAGS_MF;
		if (data->out_used == 0) {
			flags |= WSC_FLAGS_LF;
			send_len -= 2;
		}
	}
	plen = 2 + send_len;
	if (flags & WSC_FLAGS_LF)
		plen += 2;
	req = eap_msg_alloc(EAP_VENDOR_WFA, EAP_VENDOR_TYPE_WSC, plen,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-WSC: Failed to allocate memory for "
			   "request");
		return NULL;
	}

	wpabuf_put_u8(req, data->out_op_code); /* Op-Code */
	wpabuf_put_u8(req, flags); /* Flags */
	if (flags & WSC_FLAGS_LF)
		wpabuf_put_be16(req, wpabuf_len(data->out_buf));

	wpabuf_put_data(req, wpabuf_head_u8(data->out_buf) + data->out_used,
			send_len);
	data->out_used += send_len;

	if (data->out_used == wpabuf_len(data->out_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-WSC: Sending out %lu bytes "
			   "(message sent completely)",
			   (unsigned long) send_len);
		wpabuf_free(data->out_buf);
		data->out_buf = NULL;
		data->out_used = 0;
		eap_wsc_state(data, MESG);
	} else {
		wpa_printf(MSG_DEBUG, "EAP-WSC: Sending out %lu bytes "
			   "(%lu more to send)", (unsigned long) send_len,
			   (unsigned long) wpabuf_len(data->out_buf) -
			   data->out_used);
		eap_wsc_state(data, WAIT_FRAG_ACK);
	}

	return req;
}


static struct wpabuf * eap_wsc_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_wsc_data *data = priv;

	switch (data->state) {
	case START:
		return eap_wsc_build_start(sm, data, id);
	case MESG:
		if (data->out_buf == NULL) {
			data->out_buf = wps_get_msg(data->wps,
						    &data->out_op_code);
			if (data->out_buf == NULL) {
				wpa_printf(MSG_DEBUG, "EAP-WSC: Failed to "
					   "receive message from WPS");
				return NULL;
			}
			data->out_used = 0;
		}
		/* pass through */
	case WAIT_FRAG_ACK:
		return eap_wsc_build_msg(data, id);
	case FRAG_ACK:
		return eap_wsc_build_frag_ack(id, EAP_CODE_REQUEST);
	default:
		wpa_printf(MSG_DEBUG, "EAP-WSC: Unexpected state %d in "
			   "buildReq", data->state);
		return NULL;
	}
}


static Boolean eap_wsc_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_WFA, EAP_VENDOR_TYPE_WSC,
			       respData, &len);
	if (pos == NULL || len < 2) {
		wpa_printf(MSG_INFO, "EAP-WSC: Invalid frame");
		return TRUE;
	}

	return FALSE;
}


static int eap_wsc_process_cont(struct eap_wsc_data *data,
				const u8 *buf, size_t len, u8 op_code)
{
	/* Process continuation of a pending message */
	if (op_code != data->in_op_code) {
		wpa_printf(MSG_DEBUG, "EAP-WSC: Unexpected Op-Code %d in "
			   "fragment (expected %d)",
			   op_code, data->in_op_code);
		eap_wsc_state(data, FAIL);
		return -1;
	}

	if (len > wpabuf_tailroom(data->in_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-WSC: Fragment overflow");
		eap_wsc_state(data, FAIL);
		return -1;
	}

	wpabuf_put_data(data->in_buf, buf, len);
	wpa_printf(MSG_DEBUG, "EAP-WSC: Received %lu bytes, waiting for %lu "
		   "bytes more", (unsigned long) len,
		   (unsigned long) wpabuf_tailroom(data->in_buf));

	return 0;
}


static int eap_wsc_process_fragment(struct eap_wsc_data *data,
				    u8 flags, u8 op_code, u16 message_length,
				    const u8 *buf, size_t len)
{
	/* Process a fragment that is not the last one of the message */
	if (data->in_buf == NULL && !(flags & WSC_FLAGS_LF)) {
		wpa_printf(MSG_DEBUG, "EAP-WSC: No Message Length "
			   "field in a fragmented packet");
		return -1;
	}

	if (data->in_buf == NULL) {
		/* First fragment of the message */
		data->in_buf = wpabuf_alloc(message_length);
		if (data->in_buf == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-WSC: No memory for "
				   "message");
			return -1;
		}
		data->in_op_code = op_code;
		wpabuf_put_data(data->in_buf, buf, len);
		wpa_printf(MSG_DEBUG, "EAP-WSC: Received %lu bytes in "
			   "first fragment, waiting for %lu bytes more",
			   (unsigned long) len,
			   (unsigned long) wpabuf_tailroom(data->in_buf));
	}

	return 0;
}


static void eap_wsc_process(struct eap_sm *sm, void *priv,
			    struct wpabuf *respData)
{
	struct eap_wsc_data *data = priv;
	const u8 *start, *pos, *end;
	size_t len;
	u8 op_code, flags;
	u16 message_length = 0;
	enum wps_process_res res;
	struct wpabuf tmpbuf;

	eloop_cancel_timeout(eap_wsc_ext_reg_timeout, sm, data);
	if (data->ext_reg_timeout) {
		eap_wsc_state(data, FAIL);
		return;
	}

	pos = eap_hdr_validate(EAP_VENDOR_WFA, EAP_VENDOR_TYPE_WSC,
			       respData, &len);
	if (pos == NULL || len < 2)
		return; /* Should not happen; message already verified */

	start = pos;
	end = start + len;

	op_code = *pos++;
	flags = *pos++;
	if (flags & WSC_FLAGS_LF) {
		if (end - pos < 2) {
			wpa_printf(MSG_DEBUG, "EAP-WSC: Message underflow");
			return;
		}
		message_length = WPA_GET_BE16(pos);
		pos += 2;

		if (message_length < end - pos) {
			wpa_printf(MSG_DEBUG, "EAP-WSC: Invalid Message "
				   "Length");
			return;
		}
	}

	wpa_printf(MSG_DEBUG, "EAP-WSC: Received packet: Op-Code %d "
		   "Flags 0x%x Message Length %d",
		   op_code, flags, message_length);

	if (data->state == WAIT_FRAG_ACK) {
		if (op_code != WSC_FRAG_ACK) {
			wpa_printf(MSG_DEBUG, "EAP-WSC: Unexpected Op-Code %d "
				   "in WAIT_FRAG_ACK state", op_code);
			eap_wsc_state(data, FAIL);
			return;
		}
		wpa_printf(MSG_DEBUG, "EAP-WSC: Fragment acknowledged");
		eap_wsc_state(data, MESG);
		return;
	}

	if (op_code != WSC_ACK && op_code != WSC_NACK && op_code != WSC_MSG &&
	    op_code != WSC_Done) {
		wpa_printf(MSG_DEBUG, "EAP-WSC: Unexpected Op-Code %d",
			   op_code);
		eap_wsc_state(data, FAIL);
		return;
	}

	if (data->in_buf &&
	    eap_wsc_process_cont(data, pos, end - pos, op_code) < 0) {
		eap_wsc_state(data, FAIL);
		return;
	}

	if (flags & WSC_FLAGS_MF) {
		if (eap_wsc_process_fragment(data, flags, op_code,
					     message_length, pos, end - pos) <
		    0)
			eap_wsc_state(data, FAIL);
		else
			eap_wsc_state(data, FRAG_ACK);
		return;
	}

	if (data->in_buf == NULL) {
		/* Wrap unfragmented messages as wpabuf without extra copy */
		wpabuf_set(&tmpbuf, pos, end - pos);
		data->in_buf = &tmpbuf;
	}

	res = wps_process_msg(data->wps, op_code, data->in_buf);
	switch (res) {
	case WPS_DONE:
		wpa_printf(MSG_DEBUG, "EAP-WSC: WPS processing completed "
			   "successfully - report EAP failure");
		eap_wsc_state(data, FAIL);
		break;
	case WPS_CONTINUE:
		eap_wsc_state(data, MESG);
		break;
	case WPS_FAILURE:
		wpa_printf(MSG_DEBUG, "EAP-WSC: WPS processing failed");
		eap_wsc_state(data, FAIL);
		break;
	case WPS_PENDING:
		eap_wsc_state(data, MESG);
		sm->method_pending = METHOD_PENDING_WAIT;
		eloop_cancel_timeout(eap_wsc_ext_reg_timeout, sm, data);
		eloop_register_timeout(5, 0, eap_wsc_ext_reg_timeout,
				       sm, data);
		break;
	}

	if (data->in_buf != &tmpbuf)
		wpabuf_free(data->in_buf);
	data->in_buf = NULL;
}


static Boolean eap_wsc_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_wsc_data *data = priv;
	return data->state == FAIL;
}


static Boolean eap_wsc_isSuccess(struct eap_sm *sm, void *priv)
{
	/* EAP-WSC will always result in EAP-Failure */
	return FALSE;
}


static int eap_wsc_getTimeout(struct eap_sm *sm, void *priv)
{
	/* Recommended retransmit times: retransmit timeout 5 seconds,
	 * per-message timeout 15 seconds, i.e., 3 tries. */
	sm->MaxRetrans = 2; /* total 3 attempts */
	return 5;
}


int eap_server_wsc_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_WFA, EAP_VENDOR_TYPE_WSC,
				      "WSC");
	if (eap == NULL)
		return -1;

	eap->init = eap_wsc_init;
	eap->reset = eap_wsc_reset;
	eap->buildReq = eap_wsc_buildReq;
	eap->check = eap_wsc_check;
	eap->process = eap_wsc_process;
	eap->isDone = eap_wsc_isDone;
	eap->isSuccess = eap_wsc_isSuccess;
	eap->getTimeout = eap_wsc_getTimeout;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}
