/*
 * EAP-IKEv2 server (RFC 5106)
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
#include "eap_i.h"
#include "eap_common/eap_ikev2_common.h"
#include "ikev2.h"


struct eap_ikev2_data {
	struct ikev2_initiator_data ikev2;
	enum { MSG, FRAG_ACK, WAIT_FRAG_ACK, DONE, FAIL } state;
	struct wpabuf *in_buf;
	struct wpabuf *out_buf;
	size_t out_used;
	size_t fragment_size;
	int keys_ready;
	u8 keymat[EAP_MSK_LEN + EAP_EMSK_LEN];
	int keymat_ok;
};


static const u8 * eap_ikev2_get_shared_secret(void *ctx, const u8 *IDr,
					      size_t IDr_len,
					      size_t *secret_len)
{
	struct eap_sm *sm = ctx;

	if (IDr == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: No IDr received - default "
			   "to user identity from EAP-Identity");
		IDr = sm->identity;
		IDr_len = sm->identity_len;
	}

	if (eap_user_get(sm, IDr, IDr_len, 0) < 0 || sm->user == NULL ||
	    sm->user->password == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: No user entry found");
		return NULL;
	}

	*secret_len = sm->user->password_len;
	return sm->user->password;
}


static const char * eap_ikev2_state_txt(int state)
{
	switch (state) {
	case MSG:
		return "MSG";
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


static void eap_ikev2_state(struct eap_ikev2_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-IKEV2: %s -> %s",
		   eap_ikev2_state_txt(data->state),
		   eap_ikev2_state_txt(state));
	data->state = state;
}


static void * eap_ikev2_init(struct eap_sm *sm)
{
	struct eap_ikev2_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = MSG;
	data->fragment_size = sm->fragment_size > 0 ? sm->fragment_size :
		IKEV2_FRAGMENT_SIZE;
	data->ikev2.state = SA_INIT;
	data->ikev2.peer_auth = PEER_AUTH_SECRET;
	data->ikev2.key_pad = (u8 *) os_strdup("Key Pad for EAP-IKEv2");
	if (data->ikev2.key_pad == NULL)
		goto failed;
	data->ikev2.key_pad_len = 21;

	/* TODO: make proposals configurable */
	data->ikev2.proposal.proposal_num = 1;
	data->ikev2.proposal.integ = AUTH_HMAC_SHA1_96;
	data->ikev2.proposal.prf = PRF_HMAC_SHA1;
	data->ikev2.proposal.encr = ENCR_AES_CBC;
	data->ikev2.proposal.dh = DH_GROUP2_1024BIT_MODP;

	data->ikev2.IDi = (u8 *) os_strdup("hostapd");
	data->ikev2.IDi_len = 7;

	data->ikev2.get_shared_secret = eap_ikev2_get_shared_secret;
	data->ikev2.cb_ctx = sm;

	return data;

failed:
	ikev2_initiator_deinit(&data->ikev2);
	os_free(data);
	return NULL;
}


static void eap_ikev2_reset(struct eap_sm *sm, void *priv)
{
	struct eap_ikev2_data *data = priv;
	wpabuf_free(data->in_buf);
	wpabuf_free(data->out_buf);
	ikev2_initiator_deinit(&data->ikev2);
	os_free(data);
}


static struct wpabuf * eap_ikev2_build_msg(struct eap_ikev2_data *data, u8 id)
{
	struct wpabuf *req;
	u8 flags;
	size_t send_len, plen, icv_len = 0;

	wpa_printf(MSG_DEBUG, "EAP-IKEV2: Generating Request");

	flags = 0;
	send_len = wpabuf_len(data->out_buf) - data->out_used;
	if (1 + send_len > data->fragment_size) {
		send_len = data->fragment_size - 1;
		flags |= IKEV2_FLAGS_MORE_FRAGMENTS;
		if (data->out_used == 0) {
			flags |= IKEV2_FLAGS_LENGTH_INCLUDED;
			send_len -= 4;
		}
	}

	plen = 1 + send_len;
	if (flags & IKEV2_FLAGS_LENGTH_INCLUDED)
		plen += 4;
	if (data->keys_ready) {
		const struct ikev2_integ_alg *integ;
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Add Integrity Checksum "
			   "Data");
		flags |= IKEV2_FLAGS_ICV_INCLUDED;
		integ = ikev2_get_integ(data->ikev2.proposal.integ);
		if (integ == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Unknown INTEG "
				   "transform / cannot generate ICV");
			return NULL;
		}
		icv_len = integ->hash_len;

		plen += icv_len;
	}
	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_IKEV2, plen,
			    EAP_CODE_REQUEST, id);
	if (req == NULL)
		return NULL;

	wpabuf_put_u8(req, flags); /* Flags */
	if (flags & IKEV2_FLAGS_LENGTH_INCLUDED)
		wpabuf_put_be32(req, wpabuf_len(data->out_buf));

	wpabuf_put_data(req, wpabuf_head_u8(data->out_buf) + data->out_used,
			send_len);
	data->out_used += send_len;

	if (flags & IKEV2_FLAGS_ICV_INCLUDED) {
		const u8 *msg = wpabuf_head(req);
		size_t len = wpabuf_len(req);
		ikev2_integ_hash(data->ikev2.proposal.integ,
				 data->ikev2.keys.SK_ai,
				 data->ikev2.keys.SK_integ_len,
				 msg, len, wpabuf_put(req, icv_len));
	}

	if (data->out_used == wpabuf_len(data->out_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Sending out %lu bytes "
			   "(message sent completely)",
			   (unsigned long) send_len);
		wpabuf_free(data->out_buf);
		data->out_buf = NULL;
		data->out_used = 0;
	} else {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Sending out %lu bytes "
			   "(%lu more to send)", (unsigned long) send_len,
			   (unsigned long) wpabuf_len(data->out_buf) -
			   data->out_used);
		eap_ikev2_state(data, WAIT_FRAG_ACK);
	}

	return req;
}


static struct wpabuf * eap_ikev2_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_ikev2_data *data = priv;

	switch (data->state) {
	case MSG:
		if (data->out_buf == NULL) {
			data->out_buf = ikev2_initiator_build(&data->ikev2);
			if (data->out_buf == NULL) {
				wpa_printf(MSG_DEBUG, "EAP-IKEV2: Failed to "
					   "generate IKEv2 message");
				return NULL;
			}
			data->out_used = 0;
		}
		/* pass through */
	case WAIT_FRAG_ACK:
		return eap_ikev2_build_msg(data, id);
	case FRAG_ACK:
		return eap_ikev2_build_frag_ack(id, EAP_CODE_REQUEST);
	default:
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Unexpected state %d in "
			   "buildReq", data->state);
		return NULL;
	}
}


static Boolean eap_ikev2_check(struct eap_sm *sm, void *priv,
			       struct wpabuf *respData)
{
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_IKEV2, respData,
			       &len);
	if (pos == NULL) {
		wpa_printf(MSG_INFO, "EAP-IKEV2: Invalid frame");
		return TRUE;
	}

	return FALSE;
}


static int eap_ikev2_process_icv(struct eap_ikev2_data *data,
				 const struct wpabuf *respData,
				 u8 flags, const u8 *pos, const u8 **end)
{
	if (flags & IKEV2_FLAGS_ICV_INCLUDED) {
		int icv_len = eap_ikev2_validate_icv(
			data->ikev2.proposal.integ, &data->ikev2.keys, 0,
			respData, pos, *end);
		if (icv_len < 0)
			return -1;
		/* Hide Integrity Checksum Data from further processing */
		*end -= icv_len;
	} else if (data->keys_ready) {
		wpa_printf(MSG_INFO, "EAP-IKEV2: The message should have "
			   "included integrity checksum");
		return -1;
	}

	return 0;
}


static int eap_ikev2_process_cont(struct eap_ikev2_data *data,
				  const u8 *buf, size_t len)
{
	/* Process continuation of a pending message */
	if (len > wpabuf_tailroom(data->in_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Fragment overflow");
		eap_ikev2_state(data, FAIL);
		return -1;
	}

	wpabuf_put_data(data->in_buf, buf, len);
	wpa_printf(MSG_DEBUG, "EAP-IKEV2: Received %lu bytes, waiting for %lu "
		   "bytes more", (unsigned long) len,
		   (unsigned long) wpabuf_tailroom(data->in_buf));

	return 0;
}


static int eap_ikev2_process_fragment(struct eap_ikev2_data *data,
				      u8 flags, u32 message_length,
				      const u8 *buf, size_t len)
{
	/* Process a fragment that is not the last one of the message */
	if (data->in_buf == NULL && !(flags & IKEV2_FLAGS_LENGTH_INCLUDED)) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: No Message Length field in "
			   "a fragmented packet");
		return -1;
	}

	if (data->in_buf == NULL) {
		/* First fragment of the message */
		data->in_buf = wpabuf_alloc(message_length);
		if (data->in_buf == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: No memory for "
				   "message");
			return -1;
		}
		wpabuf_put_data(data->in_buf, buf, len);
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Received %lu bytes in first "
			   "fragment, waiting for %lu bytes more",
			   (unsigned long) len,
			   (unsigned long) wpabuf_tailroom(data->in_buf));
	}

	return 0;
}


static int eap_ikev2_server_keymat(struct eap_ikev2_data *data)
{
	if (eap_ikev2_derive_keymat(
		    data->ikev2.proposal.prf, &data->ikev2.keys,
		    data->ikev2.i_nonce, data->ikev2.i_nonce_len,
		    data->ikev2.r_nonce, data->ikev2.r_nonce_len,
		    data->keymat) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Failed to derive "
			   "key material");
		return -1;
	}
	data->keymat_ok = 1;
	return 0;
}


static void eap_ikev2_process(struct eap_sm *sm, void *priv,
			      struct wpabuf *respData)
{
	struct eap_ikev2_data *data = priv;
	const u8 *start, *pos, *end;
	size_t len;
	u8 flags;
	u32 message_length = 0;
	struct wpabuf tmpbuf;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_IKEV2, respData,
			       &len);
	if (pos == NULL)
		return; /* Should not happen; message already verified */

	start = pos;
	end = start + len;

	if (len == 0) {
		/* fragment ack */
		flags = 0;
	} else
		flags = *pos++;

	if (eap_ikev2_process_icv(data, respData, flags, pos, &end) < 0) {
		eap_ikev2_state(data, FAIL);
		return;
	}

	if (flags & IKEV2_FLAGS_LENGTH_INCLUDED) {
		if (end - pos < 4) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Message underflow");
			eap_ikev2_state(data, FAIL);
			return;
		}
		message_length = WPA_GET_BE32(pos);
		pos += 4;

		if (message_length < (u32) (end - pos)) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Invalid Message "
				   "Length (%d; %ld remaining in this msg)",
				   message_length, (long) (end - pos));
			eap_ikev2_state(data, FAIL);
			return;
		}
	}
	wpa_printf(MSG_DEBUG, "EAP-IKEV2: Received packet: Flags 0x%x "
		   "Message Length %u", flags, message_length);

	if (data->state == WAIT_FRAG_ACK) {
		if (len != 0) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Unexpected payload "
				   "in WAIT_FRAG_ACK state");
			eap_ikev2_state(data, FAIL);
			return;
		}
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Fragment acknowledged");
		eap_ikev2_state(data, MSG);
		return;
	}

	if (data->in_buf && eap_ikev2_process_cont(data, pos, end - pos) < 0) {
		eap_ikev2_state(data, FAIL);
		return;
	}
		
	if (flags & IKEV2_FLAGS_MORE_FRAGMENTS) {
		if (eap_ikev2_process_fragment(data, flags, message_length,
					       pos, end - pos) < 0)
			eap_ikev2_state(data, FAIL);
		else
			eap_ikev2_state(data, FRAG_ACK);
		return;
	} else if (data->state == FRAG_ACK) {
		wpa_printf(MSG_DEBUG, "EAP-TNC: All fragments received");
		data->state = MSG;
	}

	if (data->in_buf == NULL) {
		/* Wrap unfragmented messages as wpabuf without extra copy */
		wpabuf_set(&tmpbuf, pos, end - pos);
		data->in_buf = &tmpbuf;
	}

	if (ikev2_initiator_process(&data->ikev2, data->in_buf) < 0) {
		if (data->in_buf == &tmpbuf)
			data->in_buf = NULL;
		eap_ikev2_state(data, FAIL);
		return;
	}

	switch (data->ikev2.state) {
	case SA_AUTH:
		/* SA_INIT was sent out, so message have to be
		 * integrity protected from now on. */
		data->keys_ready = 1;
		break;
	case IKEV2_DONE:
		if (data->state == FAIL)
			break;
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Authentication completed "
			   "successfully");
		if (eap_ikev2_server_keymat(data))
			break;
		eap_ikev2_state(data, DONE);
		break;
	default:
		break;
	}

	if (data->in_buf != &tmpbuf)
		wpabuf_free(data->in_buf);
	data->in_buf = NULL;
}


static Boolean eap_ikev2_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_ikev2_data *data = priv;
	return data->state == DONE || data->state == FAIL;
}


static Boolean eap_ikev2_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_ikev2_data *data = priv;
	return data->state == DONE && data->ikev2.state == IKEV2_DONE &&
		data->keymat_ok;
}


static u8 * eap_ikev2_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_ikev2_data *data = priv;
	u8 *key;

	if (data->state != DONE || !data->keymat_ok)
		return NULL;

	key = os_malloc(EAP_MSK_LEN);
	if (key) {
		os_memcpy(key, data->keymat, EAP_MSK_LEN);
		*len = EAP_MSK_LEN;
	}

	return key;
}


static u8 * eap_ikev2_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_ikev2_data *data = priv;
	u8 *key;

	if (data->state != DONE || !data->keymat_ok)
		return NULL;

	key = os_malloc(EAP_EMSK_LEN);
	if (key) {
		os_memcpy(key, data->keymat + EAP_MSK_LEN, EAP_EMSK_LEN);
		*len = EAP_EMSK_LEN;
	}

	return key;
}


int eap_server_ikev2_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_IKEV2,
				      "IKEV2");
	if (eap == NULL)
		return -1;

	eap->init = eap_ikev2_init;
	eap->reset = eap_ikev2_reset;
	eap->buildReq = eap_ikev2_buildReq;
	eap->check = eap_ikev2_check;
	eap->process = eap_ikev2_process;
	eap->isDone = eap_ikev2_isDone;
	eap->getKey = eap_ikev2_getKey;
	eap->isSuccess = eap_ikev2_isSuccess;
	eap->get_emsk = eap_ikev2_get_emsk;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}
