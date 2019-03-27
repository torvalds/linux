/*
 * EAP peer method: EAP-TNC (Trusted Network Connect)
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_i.h"
#include "eap_config.h"
#include "tncc.h"


struct eap_tnc_data {
	enum { WAIT_START, PROC_MSG, WAIT_FRAG_ACK, DONE, FAIL } state;
	struct tncc_data *tncc;
	struct wpabuf *in_buf;
	struct wpabuf *out_buf;
	size_t out_used;
	size_t fragment_size;
};


/* EAP-TNC Flags */
#define EAP_TNC_FLAGS_LENGTH_INCLUDED 0x80
#define EAP_TNC_FLAGS_MORE_FRAGMENTS 0x40
#define EAP_TNC_FLAGS_START 0x20
#define EAP_TNC_VERSION_MASK 0x07

#define EAP_TNC_VERSION 1


static void * eap_tnc_init(struct eap_sm *sm)
{
	struct eap_tnc_data *data;
	struct eap_peer_config *config = eap_get_config(sm);

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = WAIT_START;
	if (config && config->fragment_size)
		data->fragment_size = config->fragment_size;
	else
		data->fragment_size = 1300;
	data->tncc = tncc_init();
	if (data->tncc == NULL) {
		os_free(data);
		return NULL;
	}

	return data;
}


static void eap_tnc_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_tnc_data *data = priv;

	wpabuf_free(data->in_buf);
	wpabuf_free(data->out_buf);
	tncc_deinit(data->tncc);
	os_free(data);
}


static struct wpabuf * eap_tnc_build_frag_ack(u8 id, u8 code)
{
	struct wpabuf *msg;

	msg = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TNC, 1, code, id);
	if (msg == NULL) {
		wpa_printf(MSG_ERROR, "EAP-TNC: Failed to allocate memory "
			   "for fragment ack");
		return NULL;
	}
	wpabuf_put_u8(msg, EAP_TNC_VERSION); /* Flags */

	wpa_printf(MSG_DEBUG, "EAP-TNC: Send fragment ack");

	return msg;
}


static struct wpabuf * eap_tnc_build_msg(struct eap_tnc_data *data,
					 struct eap_method_ret *ret, u8 id)
{
	struct wpabuf *resp;
	u8 flags;
	size_t send_len, plen;

	ret->ignore = FALSE;
	wpa_printf(MSG_DEBUG, "EAP-TNC: Generating Response");
	ret->allowNotifications = TRUE;

	flags = EAP_TNC_VERSION;
	send_len = wpabuf_len(data->out_buf) - data->out_used;
	if (1 + send_len > data->fragment_size) {
		send_len = data->fragment_size - 1;
		flags |= EAP_TNC_FLAGS_MORE_FRAGMENTS;
		if (data->out_used == 0) {
			flags |= EAP_TNC_FLAGS_LENGTH_INCLUDED;
			send_len -= 4;
		}
	}

	plen = 1 + send_len;
	if (flags & EAP_TNC_FLAGS_LENGTH_INCLUDED)
		plen += 4;
	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TNC, plen,
			     EAP_CODE_RESPONSE, id);
	if (resp == NULL)
		return NULL;

	wpabuf_put_u8(resp, flags); /* Flags */
	if (flags & EAP_TNC_FLAGS_LENGTH_INCLUDED)
		wpabuf_put_be32(resp, wpabuf_len(data->out_buf));

	wpabuf_put_data(resp, wpabuf_head_u8(data->out_buf) + data->out_used,
			send_len);
	data->out_used += send_len;

	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;

	if (data->out_used == wpabuf_len(data->out_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Sending out %lu bytes "
			   "(message sent completely)",
			   (unsigned long) send_len);
		wpabuf_free(data->out_buf);
		data->out_buf = NULL;
		data->out_used = 0;
	} else {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Sending out %lu bytes "
			   "(%lu more to send)", (unsigned long) send_len,
			   (unsigned long) wpabuf_len(data->out_buf) -
			   data->out_used);
		data->state = WAIT_FRAG_ACK;
	}

	return resp;
}


static int eap_tnc_process_cont(struct eap_tnc_data *data,
				const u8 *buf, size_t len)
{
	/* Process continuation of a pending message */
	if (len > wpabuf_tailroom(data->in_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Fragment overflow");
		data->state = FAIL;
		return -1;
	}

	wpabuf_put_data(data->in_buf, buf, len);
	wpa_printf(MSG_DEBUG, "EAP-TNC: Received %lu bytes, waiting for "
		   "%lu bytes more", (unsigned long) len,
		   (unsigned long) wpabuf_tailroom(data->in_buf));

	return 0;
}


static struct wpabuf * eap_tnc_process_fragment(struct eap_tnc_data *data,
						struct eap_method_ret *ret,
						u8 id, u8 flags,
						u32 message_length,
						const u8 *buf, size_t len)
{
	/* Process a fragment that is not the last one of the message */
	if (data->in_buf == NULL && !(flags & EAP_TNC_FLAGS_LENGTH_INCLUDED)) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: No Message Length field in a "
			   "fragmented packet");
		ret->ignore = TRUE;
		return NULL;
	}

	if (data->in_buf == NULL) {
		/* First fragment of the message */
		data->in_buf = wpabuf_alloc(message_length);
		if (data->in_buf == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: No memory for "
				   "message");
			ret->ignore = TRUE;
			return NULL;
		}
		wpabuf_put_data(data->in_buf, buf, len);
		wpa_printf(MSG_DEBUG, "EAP-TNC: Received %lu bytes in first "
			   "fragment, waiting for %lu bytes more",
			   (unsigned long) len,
			   (unsigned long) wpabuf_tailroom(data->in_buf));
	}

	return eap_tnc_build_frag_ack(id, EAP_CODE_RESPONSE);
}


static struct wpabuf * eap_tnc_process(struct eap_sm *sm, void *priv,
				       struct eap_method_ret *ret,
				       const struct wpabuf *reqData)
{
	struct eap_tnc_data *data = priv;
	struct wpabuf *resp;
	const u8 *pos, *end;
	u8 *rpos, *rpos1;
	size_t len, rlen;
	size_t imc_len;
	char *start_buf, *end_buf;
	size_t start_len, end_len;
	int tncs_done = 0;
	u8 flags, id;
	u32 message_length = 0;
	struct wpabuf tmpbuf;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_TNC, reqData, &len);
	if (pos == NULL) {
		wpa_printf(MSG_INFO, "EAP-TNC: Invalid frame (pos=%p len=%lu)",
			   pos, (unsigned long) len);
		ret->ignore = TRUE;
		return NULL;
	}

	id = eap_get_id(reqData);

	end = pos + len;

	if (len == 0)
		flags = 0; /* fragment ack */
	else
		flags = *pos++;

	if (len > 0 && (flags & EAP_TNC_VERSION_MASK) != EAP_TNC_VERSION) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Unsupported version %d",
			   flags & EAP_TNC_VERSION_MASK);
		ret->ignore = TRUE;
		return NULL;
	}

	if (flags & EAP_TNC_FLAGS_LENGTH_INCLUDED) {
		if (end - pos < 4) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: Message underflow");
			ret->ignore = TRUE;
			return NULL;
		}
		message_length = WPA_GET_BE32(pos);
		pos += 4;

		if (message_length < (u32) (end - pos) ||
		    message_length > 75000) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: Invalid Message "
				   "Length (%d; %ld remaining in this msg)",
				   message_length, (long) (end - pos));
			ret->ignore = TRUE;
			return NULL;
		}
	}

	wpa_printf(MSG_DEBUG, "EAP-TNC: Received packet: Flags 0x%x "
		   "Message Length %u", flags, message_length);

	if (data->state == WAIT_FRAG_ACK) {
		if (len > 1) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: Unexpected payload in "
				   "WAIT_FRAG_ACK state");
			ret->ignore = TRUE;
			return NULL;
		}
		wpa_printf(MSG_DEBUG, "EAP-TNC: Fragment acknowledged");
		data->state = PROC_MSG;
		return eap_tnc_build_msg(data, ret, id);
	}

	if (data->in_buf && eap_tnc_process_cont(data, pos, end - pos) < 0) {
		ret->ignore = TRUE;
		return NULL;
	}
		
	if (flags & EAP_TNC_FLAGS_MORE_FRAGMENTS) {
		return eap_tnc_process_fragment(data, ret, id, flags,
						message_length, pos,
						end - pos);
	}

	if (data->in_buf == NULL) {
		/* Wrap unfragmented messages as wpabuf without extra copy */
		wpabuf_set(&tmpbuf, pos, end - pos);
		data->in_buf = &tmpbuf;
	}

	if (data->state == WAIT_START) {
		if (!(flags & EAP_TNC_FLAGS_START)) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: Server did not use "
				   "start flag in the first message");
			ret->ignore = TRUE;
			goto fail;
		}

		tncc_init_connection(data->tncc);

		data->state = PROC_MSG;
	} else {
		enum tncc_process_res res;

		if (flags & EAP_TNC_FLAGS_START) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: Server used start "
				   "flag again");
			ret->ignore = TRUE;
			goto fail;
		}

		res = tncc_process_if_tnccs(data->tncc,
					    wpabuf_head(data->in_buf),
					    wpabuf_len(data->in_buf));
		switch (res) {
		case TNCCS_PROCESS_ERROR:
			ret->ignore = TRUE;
			goto fail;
		case TNCCS_PROCESS_OK_NO_RECOMMENDATION:
		case TNCCS_RECOMMENDATION_ERROR:
			wpa_printf(MSG_DEBUG, "EAP-TNC: No "
				   "TNCCS-Recommendation received");
			break;
		case TNCCS_RECOMMENDATION_ALLOW:
			wpa_msg(sm->msg_ctx, MSG_INFO,
				"TNC: Recommendation = allow");
			tncs_done = 1;
			break;
		case TNCCS_RECOMMENDATION_NONE:
			wpa_msg(sm->msg_ctx, MSG_INFO,
				"TNC: Recommendation = none");
			tncs_done = 1;
			break;
		case TNCCS_RECOMMENDATION_ISOLATE:
			wpa_msg(sm->msg_ctx, MSG_INFO,
				"TNC: Recommendation = isolate");
			tncs_done = 1;
			break;
		}
	}

	if (data->in_buf != &tmpbuf)
		wpabuf_free(data->in_buf);
	data->in_buf = NULL;

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_UNCOND_SUCC;
	ret->allowNotifications = TRUE;

	if (tncs_done) {
		resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TNC, 1,
				     EAP_CODE_RESPONSE, eap_get_id(reqData));
		if (resp == NULL)
			return NULL;

		wpabuf_put_u8(resp, EAP_TNC_VERSION);
		wpa_printf(MSG_DEBUG, "EAP-TNC: TNCS done - reply with an "
			   "empty ACK message");
		return resp;
	}

	imc_len = tncc_total_send_len(data->tncc);

	start_buf = tncc_if_tnccs_start(data->tncc);
	if (start_buf == NULL)
		return NULL;
	start_len = os_strlen(start_buf);
	end_buf = tncc_if_tnccs_end();
	if (end_buf == NULL) {
		os_free(start_buf);
		return NULL;
	}
	end_len = os_strlen(end_buf);

	rlen = start_len + imc_len + end_len;
	resp = wpabuf_alloc(rlen);
	if (resp == NULL) {
		os_free(start_buf);
		os_free(end_buf);
		return NULL;
	}

	wpabuf_put_data(resp, start_buf, start_len);
	os_free(start_buf);

	rpos1 = wpabuf_put(resp, 0);
	rpos = tncc_copy_send_buf(data->tncc, rpos1);
	wpabuf_put(resp, rpos - rpos1);

	wpabuf_put_data(resp, end_buf, end_len);
	os_free(end_buf);

	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-TNC: Response",
			  wpabuf_head(resp), wpabuf_len(resp));

	data->out_buf = resp;
	data->state = PROC_MSG;
	return eap_tnc_build_msg(data, ret, id);

fail:
	if (data->in_buf == &tmpbuf)
		data->in_buf = NULL;
	return NULL;
}


int eap_peer_tnc_register(void)
{
	struct eap_method *eap;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_TNC, "TNC");
	if (eap == NULL)
		return -1;

	eap->init = eap_tnc_init;
	eap->deinit = eap_tnc_deinit;
	eap->process = eap_tnc_process;

	return eap_peer_method_register(eap);
}
