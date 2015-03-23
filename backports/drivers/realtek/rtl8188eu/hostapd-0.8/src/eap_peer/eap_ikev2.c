/*
 * EAP-IKEv2 peer (RFC 5106)
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
	struct ikev2_responder_data ikev2;
	enum { WAIT_START, PROC_MSG, WAIT_FRAG_ACK, DONE, FAIL } state;
	struct wpabuf *in_buf;
	struct wpabuf *out_buf;
	size_t out_used;
	size_t fragment_size;
	int keys_ready;
	u8 keymat[EAP_MSK_LEN + EAP_EMSK_LEN];
	int keymat_ok;
};


static const char * eap_ikev2_state_txt(int state)
{
	switch (state) {
	case WAIT_START:
		return "WAIT_START";
	case PROC_MSG:
		return "PROC_MSG";
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
	const u8 *identity, *password;
	size_t identity_len, password_len;

	identity = eap_get_config_identity(sm, &identity_len);
	if (identity == NULL) {
		wpa_printf(MSG_INFO, "EAP-IKEV2: No identity available");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = WAIT_START;
	data->fragment_size = IKEV2_FRAGMENT_SIZE;
	data->ikev2.state = SA_INIT;
	data->ikev2.peer_auth = PEER_AUTH_SECRET;
	data->ikev2.key_pad = (u8 *) os_strdup("Key Pad for EAP-IKEv2");
	if (data->ikev2.key_pad == NULL)
		goto failed;
	data->ikev2.key_pad_len = 21;
	data->ikev2.IDr = os_malloc(identity_len);
	if (data->ikev2.IDr == NULL)
		goto failed;
	os_memcpy(data->ikev2.IDr, identity, identity_len);
	data->ikev2.IDr_len = identity_len;

	password = eap_get_config_password(sm, &password_len);
	if (password) {
		data->ikev2.shared_secret = os_malloc(password_len);
		if (data->ikev2.shared_secret == NULL)
			goto failed;
		os_memcpy(data->ikev2.shared_secret, password, password_len);
		data->ikev2.shared_secret_len = password_len;
	}

	return data;

failed:
	ikev2_responder_deinit(&data->ikev2);
	os_free(data);
	return NULL;
}


static void eap_ikev2_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_ikev2_data *data = priv;
	wpabuf_free(data->in_buf);
	wpabuf_free(data->out_buf);
	ikev2_responder_deinit(&data->ikev2);
	os_free(data);
}


static int eap_ikev2_peer_keymat(struct eap_ikev2_data *data)
{
	if (eap_ikev2_derive_keymat(
		    data->ikev2.proposal.prf, &data->ikev2.keys,
		    data->ikev2.i_nonce, data->ikev2.i_nonce_len,
		    data->ikev2.r_nonce, data->ikev2.r_nonce_len,
		    data->keymat) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Failed to "
			   "derive key material");
		return -1;
	}
	data->keymat_ok = 1;
	return 0;
}


static struct wpabuf * eap_ikev2_build_msg(struct eap_ikev2_data *data,
					   struct eap_method_ret *ret, u8 id)
{
	struct wpabuf *resp;
	u8 flags;
	size_t send_len, plen, icv_len = 0;

	ret->ignore = FALSE;
	wpa_printf(MSG_DEBUG, "EAP-IKEV2: Generating Response");
	ret->allowNotifications = TRUE;

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
#ifdef CCNS_PL
	/* Some issues figuring out the length of the message if Message Length
	 * field not included?! */
	if (!(flags & IKEV2_FLAGS_LENGTH_INCLUDED))
		flags |= IKEV2_FLAGS_LENGTH_INCLUDED;
#endif /* CCNS_PL */

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
	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_IKEV2, plen,
			     EAP_CODE_RESPONSE, id);
	if (resp == NULL)
		return NULL;

	wpabuf_put_u8(resp, flags); /* Flags */
	if (flags & IKEV2_FLAGS_LENGTH_INCLUDED)
		wpabuf_put_be32(resp, wpabuf_len(data->out_buf));

	wpabuf_put_data(resp, wpabuf_head_u8(data->out_buf) + data->out_used,
			send_len);
	data->out_used += send_len;

	if (flags & IKEV2_FLAGS_ICV_INCLUDED) {
		const u8 *msg = wpabuf_head(resp);
		size_t len = wpabuf_len(resp);
		ikev2_integ_hash(data->ikev2.proposal.integ,
				 data->ikev2.keys.SK_ar,
				 data->ikev2.keys.SK_integ_len,
				 msg, len, wpabuf_put(resp, icv_len));
	}

	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;

	if (data->out_used == wpabuf_len(data->out_buf)) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Sending out %lu bytes "
			   "(message sent completely)",
			   (unsigned long) send_len);
		wpabuf_free(data->out_buf);
		data->out_buf = NULL;
		data->out_used = 0;
		switch (data->ikev2.state) {
		case SA_AUTH:
			/* SA_INIT was sent out, so message have to be
			 * integrity protected from now on. */
			data->keys_ready = 1;
			break;
		case IKEV2_DONE:
			ret->methodState = METHOD_DONE;
			if (data->state == FAIL)
				break;
			ret->decision = DECISION_COND_SUCC;
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Authentication "
				   "completed successfully");
			if (eap_ikev2_peer_keymat(data))
				break;
			eap_ikev2_state(data, DONE);
			break;
		case IKEV2_FAILED:
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Authentication "
				   "failed");
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
			break;
		default:
			break;
		}
	} else {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Sending out %lu bytes "
			   "(%lu more to send)", (unsigned long) send_len,
			   (unsigned long) wpabuf_len(data->out_buf) -
			   data->out_used);
		eap_ikev2_state(data, WAIT_FRAG_ACK);
	}

	return resp;
}


static int eap_ikev2_process_icv(struct eap_ikev2_data *data,
				 const struct wpabuf *reqData,
				 u8 flags, const u8 *pos, const u8 **end)
{
	if (flags & IKEV2_FLAGS_ICV_INCLUDED) {
		int icv_len = eap_ikev2_validate_icv(
			data->ikev2.proposal.integ, &data->ikev2.keys, 1,
			reqData, pos, *end);
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
	wpa_printf(MSG_DEBUG, "EAP-IKEV2: Received %lu bytes, waiting "
		   "for %lu bytes more", (unsigned long) len,
		   (unsigned long) wpabuf_tailroom(data->in_buf));

	return 0;
}


static struct wpabuf * eap_ikev2_process_fragment(struct eap_ikev2_data *data,
						  struct eap_method_ret *ret,
						  u8 id, u8 flags,
						  u32 message_length,
						  const u8 *buf, size_t len)
{
	/* Process a fragment that is not the last one of the message */
	if (data->in_buf == NULL && !(flags & IKEV2_FLAGS_LENGTH_INCLUDED)) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: No Message Length field in "
			   "a fragmented packet");
		ret->ignore = TRUE;
		return NULL;
	}

	if (data->in_buf == NULL) {
		/* First fragment of the message */
		data->in_buf = wpabuf_alloc(message_length);
		if (data->in_buf == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: No memory for "
				   "message");
			ret->ignore = TRUE;
			return NULL;
		}
		wpabuf_put_data(data->in_buf, buf, len);
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Received %lu bytes in first "
			   "fragment, waiting for %lu bytes more",
			   (unsigned long) len,
			   (unsigned long) wpabuf_tailroom(data->in_buf));
	}

	return eap_ikev2_build_frag_ack(id, EAP_CODE_RESPONSE);
}


static struct wpabuf * eap_ikev2_process(struct eap_sm *sm, void *priv,
					 struct eap_method_ret *ret,
					 const struct wpabuf *reqData)
{
	struct eap_ikev2_data *data = priv;
	const u8 *start, *pos, *end;
	size_t len;
	u8 flags, id;
	u32 message_length = 0;
	struct wpabuf tmpbuf;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_IKEV2, reqData, &len);
	if (pos == NULL) {
		ret->ignore = TRUE;
		return NULL;
	}

	id = eap_get_id(reqData);

	start = pos;
	end = start + len;

	if (len == 0)
		flags = 0; /* fragment ack */
	else
		flags = *pos++;

	if (eap_ikev2_process_icv(data, reqData, flags, pos, &end) < 0) {
		ret->ignore = TRUE;
		return NULL;
	}

	if (flags & IKEV2_FLAGS_LENGTH_INCLUDED) {
		if (end - pos < 4) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Message underflow");
			ret->ignore = TRUE;
			return NULL;
		}
		message_length = WPA_GET_BE32(pos);
		pos += 4;

		if (message_length < (u32) (end - pos)) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Invalid Message "
				   "Length (%d; %ld remaining in this msg)",
				   message_length, (long) (end - pos));
			ret->ignore = TRUE;
			return NULL;
		}
	}

	wpa_printf(MSG_DEBUG, "EAP-IKEV2: Received packet: Flags 0x%x "
		   "Message Length %u", flags, message_length);

	if (data->state == WAIT_FRAG_ACK) {
#ifdef CCNS_PL
		if (len > 1) /* Empty Flags field included in ACK */
#else /* CCNS_PL */
		if (len != 0)
#endif /* CCNS_PL */
		{
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Unexpected payload "
				   "in WAIT_FRAG_ACK state");
			ret->ignore = TRUE;
			return NULL;
		}
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Fragment acknowledged");
		eap_ikev2_state(data, PROC_MSG);
		return eap_ikev2_build_msg(data, ret, id);
	}

	if (data->in_buf && eap_ikev2_process_cont(data, pos, end - pos) < 0) {
		ret->ignore = TRUE;
		return NULL;
	}
		
	if (flags & IKEV2_FLAGS_MORE_FRAGMENTS) {
		return eap_ikev2_process_fragment(data, ret, id, flags,
						  message_length, pos,
						  end - pos);
	}

	if (data->in_buf == NULL) {
		/* Wrap unfragmented messages as wpabuf without extra copy */
		wpabuf_set(&tmpbuf, pos, end - pos);
		data->in_buf = &tmpbuf;
	}

	if (ikev2_responder_process(&data->ikev2, data->in_buf) < 0) {
		if (data->in_buf == &tmpbuf)
			data->in_buf = NULL;
		eap_ikev2_state(data, FAIL);
		return NULL;
	}

	if (data->in_buf != &tmpbuf)
		wpabuf_free(data->in_buf);
	data->in_buf = NULL;

	if (data->out_buf == NULL) {
		data->out_buf = ikev2_responder_build(&data->ikev2);
		if (data->out_buf == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-IKEV2: Failed to generate "
				   "IKEv2 message");
			return NULL;
		}
		data->out_used = 0;
	}

	eap_ikev2_state(data, PROC_MSG);
	return eap_ikev2_build_msg(data, ret, id);
}


static Boolean eap_ikev2_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_ikev2_data *data = priv;
	return data->state == DONE && data->keymat_ok;
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


int eap_peer_ikev2_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_IKEV2,
				    "IKEV2");
	if (eap == NULL)
		return -1;

	eap->init = eap_ikev2_init;
	eap->deinit = eap_ikev2_deinit;
	eap->process = eap_ikev2_process;
	eap->isKeyAvailable = eap_ikev2_isKeyAvailable;
	eap->getKey = eap_ikev2_getKey;
	eap->get_emsk = eap_ikev2_get_emsk;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
