/*
 * EAP server method: EAP-TNC (Trusted Network Connect)
 * Copyright (c) 2007-2010, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_i.h"
#include "tncs.h"


struct eap_tnc_data {
	enum eap_tnc_state {
		START, CONTINUE, RECOMMENDATION, FRAG_ACK, WAIT_FRAG_ACK, DONE,
		FAIL
	} state;
	enum { ALLOW, ISOLATE, NO_ACCESS, NO_RECOMMENDATION } recommendation;
	struct tncs_data *tncs;
	struct wpabuf *in_buf;
	struct wpabuf *out_buf;
	size_t out_used;
	size_t fragment_size;
	unsigned int was_done:1;
	unsigned int was_fail:1;
};


/* EAP-TNC Flags */
#define EAP_TNC_FLAGS_LENGTH_INCLUDED 0x80
#define EAP_TNC_FLAGS_MORE_FRAGMENTS 0x40
#define EAP_TNC_FLAGS_START 0x20
#define EAP_TNC_VERSION_MASK 0x07

#define EAP_TNC_VERSION 1


static const char * eap_tnc_state_txt(enum eap_tnc_state state)
{
	switch (state) {
	case START:
		return "START";
	case CONTINUE:
		return "CONTINUE";
	case RECOMMENDATION:
		return "RECOMMENDATION";
	case FRAG_ACK:
		return "FRAG_ACK";
	case WAIT_FRAG_ACK:
		return "WAIT_FRAG_ACK";
	case DONE:
		return "DONE";
	case FAIL:
		return "FAIL";
	}
	return "??";
}


static void eap_tnc_set_state(struct eap_tnc_data *data,
			      enum eap_tnc_state new_state)
{
	wpa_printf(MSG_DEBUG, "EAP-TNC: %s -> %s",
		   eap_tnc_state_txt(data->state),
		   eap_tnc_state_txt(new_state));
	data->state = new_state;
}


static void * eap_tnc_init(struct eap_sm *sm)
{
	struct eap_tnc_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	eap_tnc_set_state(data, START);
	data->tncs = tncs_init();
	if (data->tncs == NULL) {
		os_free(data);
		return NULL;
	}

	data->fragment_size = sm->fragment_size > 100 ?
		sm->fragment_size - 98 : 1300;

	return data;
}


static void eap_tnc_reset(struct eap_sm *sm, void *priv)
{
	struct eap_tnc_data *data = priv;
	wpabuf_free(data->in_buf);
	wpabuf_free(data->out_buf);
	tncs_deinit(data->tncs);
	os_free(data);
}


static struct wpabuf * eap_tnc_build_start(struct eap_sm *sm,
					   struct eap_tnc_data *data, u8 id)
{
	struct wpabuf *req;

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TNC, 1, EAP_CODE_REQUEST,
			    id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-TNC: Failed to allocate memory for "
			   "request");
		eap_tnc_set_state(data, FAIL);
		return NULL;
	}

	wpabuf_put_u8(req, EAP_TNC_FLAGS_START | EAP_TNC_VERSION);

	eap_tnc_set_state(data, CONTINUE);

	return req;
}


static struct wpabuf * eap_tnc_build(struct eap_sm *sm,
				     struct eap_tnc_data *data)
{
	struct wpabuf *req;
	u8 *rpos, *rpos1;
	size_t rlen;
	char *start_buf, *end_buf;
	size_t start_len, end_len;
	size_t imv_len;

	imv_len = tncs_total_send_len(data->tncs);

	start_buf = tncs_if_tnccs_start(data->tncs);
	if (start_buf == NULL)
		return NULL;
	start_len = os_strlen(start_buf);
	end_buf = tncs_if_tnccs_end();
	if (end_buf == NULL) {
		os_free(start_buf);
		return NULL;
	}
	end_len = os_strlen(end_buf);

	rlen = start_len + imv_len + end_len;
	req = wpabuf_alloc(rlen);
	if (req == NULL) {
		os_free(start_buf);
		os_free(end_buf);
		return NULL;
	}

	wpabuf_put_data(req, start_buf, start_len);
	os_free(start_buf);

	rpos1 = wpabuf_put(req, 0);
	rpos = tncs_copy_send_buf(data->tncs, rpos1);
	wpabuf_put(req, rpos - rpos1);

	wpabuf_put_data(req, end_buf, end_len);
	os_free(end_buf);

	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-TNC: Request",
			  wpabuf_head(req), wpabuf_len(req));

	return req;
}


static struct wpabuf * eap_tnc_build_recommendation(struct eap_sm *sm,
						    struct eap_tnc_data *data)
{
	switch (data->recommendation) {
	case ALLOW:
		eap_tnc_set_state(data, DONE);
		break;
	case ISOLATE:
		eap_tnc_set_state(data, FAIL);
		/* TODO: support assignment to a different VLAN */
		break;
	case NO_ACCESS:
		eap_tnc_set_state(data, FAIL);
		break;
	case NO_RECOMMENDATION:
		eap_tnc_set_state(data, DONE);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-TNC: Unknown recommendation");
		return NULL;
	}

	return eap_tnc_build(sm, data);
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


static struct wpabuf * eap_tnc_build_msg(struct eap_tnc_data *data, u8 id)
{
	struct wpabuf *req;
	u8 flags;
	size_t send_len, plen;

	wpa_printf(MSG_DEBUG, "EAP-TNC: Generating Request");

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
	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TNC, plen,
			    EAP_CODE_REQUEST, id);
	if (req == NULL)
		return NULL;

	wpabuf_put_u8(req, flags); /* Flags */
	if (flags & EAP_TNC_FLAGS_LENGTH_INCLUDED)
		wpabuf_put_be32(req, wpabuf_len(data->out_buf));

	wpabuf_put_data(req, wpabuf_head_u8(data->out_buf) + data->out_used,
			send_len);
	data->out_used += send_len;

	if (data->out_used == wpabuf_len(data->out_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Sending out %lu bytes "
			   "(message sent completely)",
			   (unsigned long) send_len);
		wpabuf_free(data->out_buf);
		data->out_buf = NULL;
		data->out_used = 0;
		if (data->was_fail)
			eap_tnc_set_state(data, FAIL);
		else if (data->was_done)
			eap_tnc_set_state(data, DONE);
	} else {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Sending out %lu bytes "
			   "(%lu more to send)", (unsigned long) send_len,
			   (unsigned long) wpabuf_len(data->out_buf) -
			   data->out_used);
		if (data->state == FAIL)
			data->was_fail = 1;
		else if (data->state == DONE)
			data->was_done = 1;
		eap_tnc_set_state(data, WAIT_FRAG_ACK);
	}

	return req;
}


static struct wpabuf * eap_tnc_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_tnc_data *data = priv;

	switch (data->state) {
	case START:
		tncs_init_connection(data->tncs);
		return eap_tnc_build_start(sm, data, id);
	case CONTINUE:
		if (data->out_buf == NULL) {
			data->out_buf = eap_tnc_build(sm, data);
			if (data->out_buf == NULL) {
				wpa_printf(MSG_DEBUG, "EAP-TNC: Failed to "
					   "generate message");
				return NULL;
			}
			data->out_used = 0;
		}
		return eap_tnc_build_msg(data, id);
	case RECOMMENDATION:
		if (data->out_buf == NULL) {
			data->out_buf = eap_tnc_build_recommendation(sm, data);
			if (data->out_buf == NULL) {
				wpa_printf(MSG_DEBUG, "EAP-TNC: Failed to "
					   "generate recommendation message");
				return NULL;
			}
			data->out_used = 0;
		}
		return eap_tnc_build_msg(data, id);
	case WAIT_FRAG_ACK:
		return eap_tnc_build_msg(data, id);
	case FRAG_ACK:
		return eap_tnc_build_frag_ack(id, EAP_CODE_REQUEST);
	case DONE:
	case FAIL:
		return NULL;
	}

	return NULL;
}


static Boolean eap_tnc_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_tnc_data *data = priv;
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_TNC, respData,
			       &len);
	if (pos == NULL) {
		wpa_printf(MSG_INFO, "EAP-TNC: Invalid frame");
		return TRUE;
	}

	if (len == 0 && data->state != WAIT_FRAG_ACK) {
		wpa_printf(MSG_INFO, "EAP-TNC: Invalid frame (empty)");
		return TRUE;
	}

	if (len == 0)
		return FALSE; /* Fragment ACK does not include flags */

	if ((*pos & EAP_TNC_VERSION_MASK) != EAP_TNC_VERSION) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Unsupported version %d",
			   *pos & EAP_TNC_VERSION_MASK);
		return TRUE;
	}

	if (*pos & EAP_TNC_FLAGS_START) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Peer used Start flag");
		return TRUE;
	}

	return FALSE;
}


static void tncs_process(struct eap_tnc_data *data, struct wpabuf *inbuf)
{
	enum tncs_process_res res;

	res = tncs_process_if_tnccs(data->tncs, wpabuf_head(inbuf),
				    wpabuf_len(inbuf));
	switch (res) {
	case TNCCS_RECOMMENDATION_ALLOW:
		wpa_printf(MSG_DEBUG, "EAP-TNC: TNCS allowed access");
		eap_tnc_set_state(data, RECOMMENDATION);
		data->recommendation = ALLOW;
		break;
	case TNCCS_RECOMMENDATION_NO_RECOMMENDATION:
		wpa_printf(MSG_DEBUG, "EAP-TNC: TNCS has no recommendation");
		eap_tnc_set_state(data, RECOMMENDATION);
		data->recommendation = NO_RECOMMENDATION;
		break;
	case TNCCS_RECOMMENDATION_ISOLATE:
		wpa_printf(MSG_DEBUG, "EAP-TNC: TNCS requested isolation");
		eap_tnc_set_state(data, RECOMMENDATION);
		data->recommendation = ISOLATE;
		break;
	case TNCCS_RECOMMENDATION_NO_ACCESS:
		wpa_printf(MSG_DEBUG, "EAP-TNC: TNCS rejected access");
		eap_tnc_set_state(data, RECOMMENDATION);
		data->recommendation = NO_ACCESS;
		break;
	case TNCCS_PROCESS_ERROR:
		wpa_printf(MSG_DEBUG, "EAP-TNC: TNCS processing error");
		eap_tnc_set_state(data, FAIL);
		break;
	default:
		break;
	}
}


static int eap_tnc_process_cont(struct eap_tnc_data *data,
				const u8 *buf, size_t len)
{
	/* Process continuation of a pending message */
	if (len > wpabuf_tailroom(data->in_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Fragment overflow");
		eap_tnc_set_state(data, FAIL);
		return -1;
	}

	wpabuf_put_data(data->in_buf, buf, len);
	wpa_printf(MSG_DEBUG, "EAP-TNC: Received %lu bytes, waiting for %lu "
		   "bytes more", (unsigned long) len,
		   (unsigned long) wpabuf_tailroom(data->in_buf));

	return 0;
}


static int eap_tnc_process_fragment(struct eap_tnc_data *data,
				    u8 flags, u32 message_length,
				    const u8 *buf, size_t len)
{
	/* Process a fragment that is not the last one of the message */
	if (data->in_buf == NULL && !(flags & EAP_TNC_FLAGS_LENGTH_INCLUDED)) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: No Message Length field in a "
			   "fragmented packet");
		return -1;
	}

	if (data->in_buf == NULL) {
		/* First fragment of the message */
		data->in_buf = wpabuf_alloc(message_length);
		if (data->in_buf == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: No memory for "
				   "message");
			return -1;
		}
		wpabuf_put_data(data->in_buf, buf, len);
		wpa_printf(MSG_DEBUG, "EAP-TNC: Received %lu bytes in first "
			   "fragment, waiting for %lu bytes more",
			   (unsigned long) len,
			   (unsigned long) wpabuf_tailroom(data->in_buf));
	}

	return 0;
}


static void eap_tnc_process(struct eap_sm *sm, void *priv,
			    struct wpabuf *respData)
{
	struct eap_tnc_data *data = priv;
	const u8 *pos, *end;
	size_t len;
	u8 flags;
	u32 message_length = 0;
	struct wpabuf tmpbuf;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_TNC, respData, &len);
	if (pos == NULL)
		return; /* Should not happen; message already verified */

	end = pos + len;

	if (len == 1 && (data->state == DONE || data->state == FAIL)) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: Peer acknowledged the last "
			   "message");
		return;
	}

	if (len == 0) {
		/* fragment ack */
		flags = 0;
	} else
		flags = *pos++;

	if (flags & EAP_TNC_FLAGS_LENGTH_INCLUDED) {
		if (end - pos < 4) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: Message underflow");
			eap_tnc_set_state(data, FAIL);
			return;
		}
		message_length = WPA_GET_BE32(pos);
		pos += 4;

		if (message_length < (u32) (end - pos) ||
		    message_length > 75000) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: Invalid Message "
				   "Length (%d; %ld remaining in this msg)",
				   message_length, (long) (end - pos));
			eap_tnc_set_state(data, FAIL);
			return;
		}
	}
	wpa_printf(MSG_DEBUG, "EAP-TNC: Received packet: Flags 0x%x "
		   "Message Length %u", flags, message_length);

	if (data->state == WAIT_FRAG_ACK) {
		if (len > 1) {
			wpa_printf(MSG_DEBUG, "EAP-TNC: Unexpected payload "
				   "in WAIT_FRAG_ACK state");
			eap_tnc_set_state(data, FAIL);
			return;
		}
		wpa_printf(MSG_DEBUG, "EAP-TNC: Fragment acknowledged");
		eap_tnc_set_state(data, CONTINUE);
		return;
	}

	if (data->in_buf && eap_tnc_process_cont(data, pos, end - pos) < 0) {
		eap_tnc_set_state(data, FAIL);
		return;
	}
		
	if (flags & EAP_TNC_FLAGS_MORE_FRAGMENTS) {
		if (eap_tnc_process_fragment(data, flags, message_length,
					     pos, end - pos) < 0)
			eap_tnc_set_state(data, FAIL);
		else
			eap_tnc_set_state(data, FRAG_ACK);
		return;
	} else if (data->state == FRAG_ACK) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: All fragments received");
		eap_tnc_set_state(data, CONTINUE);
	}

	if (data->in_buf == NULL) {
		/* Wrap unfragmented messages as wpabuf without extra copy */
		wpabuf_set(&tmpbuf, pos, end - pos);
		data->in_buf = &tmpbuf;
	}

	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-TNC: Received payload",
			  wpabuf_head(data->in_buf), wpabuf_len(data->in_buf));
	tncs_process(data, data->in_buf);

	if (data->in_buf != &tmpbuf)
		wpabuf_free(data->in_buf);
	data->in_buf = NULL;
}


static Boolean eap_tnc_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_tnc_data *data = priv;
	return data->state == DONE || data->state == FAIL;
}


static Boolean eap_tnc_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_tnc_data *data = priv;
	return data->state == DONE;
}


int eap_server_tnc_register(void)
{
	struct eap_method *eap;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_TNC, "TNC");
	if (eap == NULL)
		return -1;

	eap->init = eap_tnc_init;
	eap->reset = eap_tnc_reset;
	eap->buildReq = eap_tnc_buildReq;
	eap->check = eap_tnc_check;
	eap->process = eap_tnc_process;
	eap->isDone = eap_tnc_isDone;
	eap->isSuccess = eap_tnc_isSuccess;

	return eap_server_method_register(eap);
}
