/*
 * EAP peer method: EAP-GPSK (RFC 5433)
 * Copyright (c) 2006-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/random.h"
#include "eap_peer/eap_i.h"
#include "eap_common/eap_gpsk_common.h"

struct eap_gpsk_data {
	enum { GPSK_1, GPSK_3, SUCCESS, FAILURE } state;
	u8 rand_server[EAP_GPSK_RAND_LEN];
	u8 rand_peer[EAP_GPSK_RAND_LEN];
	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 sk[EAP_GPSK_MAX_SK_LEN];
	size_t sk_len;
	u8 pk[EAP_GPSK_MAX_PK_LEN];
	size_t pk_len;
	u8 session_id[128];
	size_t id_len;
	u8 *id_peer;
	size_t id_peer_len;
	u8 *id_server;
	size_t id_server_len;
	int vendor; /* CSuite/Specifier */
	int specifier; /* CSuite/Specifier */
	u8 *psk;
	size_t psk_len;
	u16 forced_cipher; /* force cipher or 0 to allow all supported */
};


static struct wpabuf * eap_gpsk_send_gpsk_2(struct eap_gpsk_data *data,
					    u8 identifier,
					    const u8 *csuite_list,
					    size_t csuite_list_len);
static struct wpabuf * eap_gpsk_send_gpsk_4(struct eap_gpsk_data *data,
					    u8 identifier);


#ifndef CONFIG_NO_STDOUT_DEBUG
static const char * eap_gpsk_state_txt(int state)
{
	switch (state) {
	case GPSK_1:
		return "GPSK-1";
	case GPSK_3:
		return "GPSK-3";
	case SUCCESS:
		return "SUCCESS";
	case FAILURE:
		return "FAILURE";
	default:
		return "?";
	}
}
#endif /* CONFIG_NO_STDOUT_DEBUG */


static void eap_gpsk_state(struct eap_gpsk_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-GPSK: %s -> %s",
		   eap_gpsk_state_txt(data->state),
		   eap_gpsk_state_txt(state));
	data->state = state;
}


static void eap_gpsk_deinit(struct eap_sm *sm, void *priv);


static void * eap_gpsk_init(struct eap_sm *sm)
{
	struct eap_gpsk_data *data;
	const u8 *identity, *password;
	size_t identity_len, password_len;
	const char *phase1;

	password = eap_get_config_password(sm, &password_len);
	if (password == NULL) {
		wpa_printf(MSG_INFO, "EAP-GPSK: No key (password) configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = GPSK_1;

	identity = eap_get_config_identity(sm, &identity_len);
	if (identity) {
		data->id_peer = os_memdup(identity, identity_len);
		if (data->id_peer == NULL) {
			eap_gpsk_deinit(sm, data);
			return NULL;
		}
		data->id_peer_len = identity_len;
	}

	phase1 = eap_get_config_phase1(sm);
	if (phase1) {
		const char *pos;

		pos = os_strstr(phase1, "cipher=");
		if (pos) {
			data->forced_cipher = atoi(pos + 7);
			wpa_printf(MSG_DEBUG, "EAP-GPSK: Forced cipher %u",
				   data->forced_cipher);
		}
	}

	data->psk = os_memdup(password, password_len);
	if (data->psk == NULL) {
		eap_gpsk_deinit(sm, data);
		return NULL;
	}
	data->psk_len = password_len;

	return data;
}


static void eap_gpsk_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_gpsk_data *data = priv;
	os_free(data->id_server);
	os_free(data->id_peer);
	if (data->psk) {
		os_memset(data->psk, 0, data->psk_len);
		os_free(data->psk);
	}
	bin_clear_free(data, sizeof(*data));
}


static const u8 * eap_gpsk_process_id_server(struct eap_gpsk_data *data,
					     const u8 *pos, const u8 *end)
{
	u16 alen;

	if (end - pos < 2) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Too short GPSK-1 packet");
		return NULL;
	}
	alen = WPA_GET_BE16(pos);
	pos += 2;
	if (end - pos < alen) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: ID_Server overflow");
		return NULL;
	}
	os_free(data->id_server);
	data->id_server = os_memdup(pos, alen);
	if (data->id_server == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: No memory for ID_Server");
		return NULL;
	}
	data->id_server_len = alen;
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-GPSK: ID_Server",
			  data->id_server, data->id_server_len);
	pos += alen;

	return pos;
}


static const u8 * eap_gpsk_process_rand_server(struct eap_gpsk_data *data,
					       const u8 *pos, const u8 *end)
{
	if (pos == NULL)
		return NULL;

	if (end - pos < EAP_GPSK_RAND_LEN) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: RAND_Server overflow");
		return NULL;
	}
	os_memcpy(data->rand_server, pos, EAP_GPSK_RAND_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-GPSK: RAND_Server",
		    data->rand_server, EAP_GPSK_RAND_LEN);
	pos += EAP_GPSK_RAND_LEN;

	return pos;
}


static int eap_gpsk_select_csuite(struct eap_sm *sm,
				  struct eap_gpsk_data *data,
				  const u8 *csuite_list,
				  size_t csuite_list_len)
{
	struct eap_gpsk_csuite *csuite;
	int i, count;

	count = csuite_list_len / sizeof(struct eap_gpsk_csuite);
	data->vendor = EAP_GPSK_VENDOR_IETF;
	data->specifier = EAP_GPSK_CIPHER_RESERVED;
	csuite = (struct eap_gpsk_csuite *) csuite_list;
	for (i = 0; i < count; i++) {
		int vendor, specifier;
		vendor = WPA_GET_BE32(csuite->vendor);
		specifier = WPA_GET_BE16(csuite->specifier);
		wpa_printf(MSG_DEBUG, "EAP-GPSK: CSuite[%d]: %d:%d",
			   i, vendor, specifier);
		if (data->vendor == EAP_GPSK_VENDOR_IETF &&
		    data->specifier == EAP_GPSK_CIPHER_RESERVED &&
		    eap_gpsk_supported_ciphersuite(vendor, specifier) &&
		    (!data->forced_cipher || data->forced_cipher == specifier))
		{
			data->vendor = vendor;
			data->specifier = specifier;
		}
		csuite++;
	}
	if (data->vendor == EAP_GPSK_VENDOR_IETF &&
	    data->specifier == EAP_GPSK_CIPHER_RESERVED) {
		wpa_msg(sm->msg_ctx, MSG_INFO, "EAP-GPSK: No supported "
			"ciphersuite found");
		return -1;
	}
	wpa_printf(MSG_DEBUG, "EAP-GPSK: Selected ciphersuite %d:%d",
		   data->vendor, data->specifier);

	return 0;
}


static const u8 * eap_gpsk_process_csuite_list(struct eap_sm *sm,
					       struct eap_gpsk_data *data,
					       const u8 **list,
					       size_t *list_len,
					       const u8 *pos, const u8 *end)
{
	size_t len;

	if (pos == NULL)
		return NULL;

	if (end - pos < 2) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Too short GPSK-1 packet");
		return NULL;
	}
	len = WPA_GET_BE16(pos);
	pos += 2;
	if (len > (size_t) (end - pos)) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: CSuite_List overflow");
		return NULL;
	}
	if (len == 0 || (len % sizeof(struct eap_gpsk_csuite))) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Invalid CSuite_List len %lu",
			   (unsigned long) len);
		return NULL;
	}

	if (eap_gpsk_select_csuite(sm, data, pos, len) < 0)
		return NULL;

	*list = pos;
	*list_len = len;
	pos += len;

	return pos;
}


static struct wpabuf * eap_gpsk_process_gpsk_1(struct eap_sm *sm,
					       struct eap_gpsk_data *data,
					       struct eap_method_ret *ret,
					       u8 identifier,
					       const u8 *payload,
					       size_t payload_len)
{
	size_t csuite_list_len;
	const u8 *csuite_list, *pos, *end;
	struct wpabuf *resp;

	if (data->state != GPSK_1) {
		ret->ignore = TRUE;
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-GPSK: Received Request/GPSK-1");

	end = payload + payload_len;

	pos = eap_gpsk_process_id_server(data, payload, end);
	pos = eap_gpsk_process_rand_server(data, pos, end);
	pos = eap_gpsk_process_csuite_list(sm, data, &csuite_list,
					   &csuite_list_len, pos, end);
	if (pos == NULL) {
		ret->methodState = METHOD_DONE;
		eap_gpsk_state(data, FAILURE);
		return NULL;
	}

	resp = eap_gpsk_send_gpsk_2(data, identifier,
				    csuite_list, csuite_list_len);
	if (resp == NULL)
		return NULL;

	eap_gpsk_state(data, GPSK_3);

	return resp;
}


static struct wpabuf * eap_gpsk_send_gpsk_2(struct eap_gpsk_data *data,
					    u8 identifier,
					    const u8 *csuite_list,
					    size_t csuite_list_len)
{
	struct wpabuf *resp;
	size_t len, miclen;
	u8 *rpos, *start;
	struct eap_gpsk_csuite *csuite;

	wpa_printf(MSG_DEBUG, "EAP-GPSK: Sending Response/GPSK-2");

	miclen = eap_gpsk_mic_len(data->vendor, data->specifier);
	len = 1 + 2 + data->id_peer_len + 2 + data->id_server_len +
		2 * EAP_GPSK_RAND_LEN + 2 + csuite_list_len +
		sizeof(struct eap_gpsk_csuite) + 2 + miclen;

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_GPSK, len,
			     EAP_CODE_RESPONSE, identifier);
	if (resp == NULL)
		return NULL;

	wpabuf_put_u8(resp, EAP_GPSK_OPCODE_GPSK_2);
	start = wpabuf_put(resp, 0);

	wpa_hexdump_ascii(MSG_DEBUG, "EAP-GPSK: ID_Peer",
			  data->id_peer, data->id_peer_len);
	wpabuf_put_be16(resp, data->id_peer_len);
	wpabuf_put_data(resp, data->id_peer, data->id_peer_len);

	wpabuf_put_be16(resp, data->id_server_len);
	wpabuf_put_data(resp, data->id_server, data->id_server_len);

	if (random_get_bytes(data->rand_peer, EAP_GPSK_RAND_LEN)) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Failed to get random data "
			   "for RAND_Peer");
		eap_gpsk_state(data, FAILURE);
		wpabuf_free(resp);
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-GPSK: RAND_Peer",
		    data->rand_peer, EAP_GPSK_RAND_LEN);
	wpabuf_put_data(resp, data->rand_peer, EAP_GPSK_RAND_LEN);
	wpabuf_put_data(resp, data->rand_server, EAP_GPSK_RAND_LEN);

	wpabuf_put_be16(resp, csuite_list_len);
	wpabuf_put_data(resp, csuite_list, csuite_list_len);

	csuite = wpabuf_put(resp, sizeof(*csuite));
	WPA_PUT_BE32(csuite->vendor, data->vendor);
	WPA_PUT_BE16(csuite->specifier, data->specifier);

	if (eap_gpsk_derive_keys(data->psk, data->psk_len,
				 data->vendor, data->specifier,
				 data->rand_peer, data->rand_server,
				 data->id_peer, data->id_peer_len,
				 data->id_server, data->id_server_len,
				 data->msk, data->emsk,
				 data->sk, &data->sk_len,
				 data->pk, &data->pk_len) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Failed to derive keys");
		eap_gpsk_state(data, FAILURE);
		wpabuf_free(resp);
		return NULL;
	}

	if (eap_gpsk_derive_session_id(data->psk, data->psk_len,
				       data->vendor, data->specifier,
				       data->rand_peer, data->rand_server,
				       data->id_peer, data->id_peer_len,
				       data->id_server, data->id_server_len,
				       EAP_TYPE_GPSK,
				       data->session_id, &data->id_len) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Failed to derive Session-Id");
		eap_gpsk_state(data, FAILURE);
		wpabuf_free(resp);
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-GPSK: Derived Session-Id",
		    data->session_id, data->id_len);

	/* No PD_Payload_1 */
	wpabuf_put_be16(resp, 0);

	rpos = wpabuf_put(resp, miclen);
	if (eap_gpsk_compute_mic(data->sk, data->sk_len, data->vendor,
				 data->specifier, start, rpos - start, rpos) <
	    0) {
		eap_gpsk_state(data, FAILURE);
		wpabuf_free(resp);
		return NULL;
	}

	return resp;
}


static const u8 * eap_gpsk_validate_rand(struct eap_gpsk_data *data,
					 const u8 *pos, const u8 *end)
{
	if (end - pos < EAP_GPSK_RAND_LEN) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Message too short for "
			   "RAND_Peer");
		return NULL;
	}
	if (os_memcmp(pos, data->rand_peer, EAP_GPSK_RAND_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: RAND_Peer in GPSK-2 and "
			   "GPSK-3 did not match");
		wpa_hexdump(MSG_DEBUG, "EAP-GPSK: RAND_Peer in GPSK-2",
			    data->rand_peer, EAP_GPSK_RAND_LEN);
		wpa_hexdump(MSG_DEBUG, "EAP-GPSK: RAND_Peer in GPSK-3",
			    pos, EAP_GPSK_RAND_LEN);
		return NULL;
	}
	pos += EAP_GPSK_RAND_LEN;

	if (end - pos < EAP_GPSK_RAND_LEN) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Message too short for "
			   "RAND_Server");
		return NULL;
	}
	if (os_memcmp(pos, data->rand_server, EAP_GPSK_RAND_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: RAND_Server in GPSK-1 and "
			   "GPSK-3 did not match");
		wpa_hexdump(MSG_DEBUG, "EAP-GPSK: RAND_Server in GPSK-1",
			    data->rand_server, EAP_GPSK_RAND_LEN);
		wpa_hexdump(MSG_DEBUG, "EAP-GPSK: RAND_Server in GPSK-3",
			    pos, EAP_GPSK_RAND_LEN);
		return NULL;
	}
	pos += EAP_GPSK_RAND_LEN;

	return pos;
}


static const u8 * eap_gpsk_validate_id_server(struct eap_gpsk_data *data,
					      const u8 *pos, const u8 *end)
{
	size_t len;

	if (pos == NULL)
		return NULL;

	if (end - pos < (int) 2) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Message too short for "
			   "length(ID_Server)");
		return NULL;
	}

	len = WPA_GET_BE16(pos);
	pos += 2;

	if (end - pos < (int) len) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Message too short for "
			   "ID_Server");
		return NULL;
	}

	if (len != data->id_server_len ||
	    os_memcmp(pos, data->id_server, len) != 0) {
		wpa_printf(MSG_INFO, "EAP-GPSK: ID_Server did not match with "
			   "the one used in GPSK-1");
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-GPSK: ID_Server in GPSK-1",
				  data->id_server, data->id_server_len);
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-GPSK: ID_Server in GPSK-3",
				  pos, len);
		return NULL;
	}

	pos += len;

	return pos;
}


static const u8 * eap_gpsk_validate_csuite(struct eap_gpsk_data *data,
					   const u8 *pos, const u8 *end)
{
	int vendor, specifier;
	const struct eap_gpsk_csuite *csuite;

	if (pos == NULL)
		return NULL;

	if (end - pos < (int) sizeof(*csuite)) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Message too short for "
			   "CSuite_Sel");
		return NULL;
	}
	csuite = (const struct eap_gpsk_csuite *) pos;
	vendor = WPA_GET_BE32(csuite->vendor);
	specifier = WPA_GET_BE16(csuite->specifier);
	pos += sizeof(*csuite);
	if (vendor != data->vendor || specifier != data->specifier) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: CSuite_Sel (%d:%d) does not "
			   "match with the one sent in GPSK-2 (%d:%d)",
			   vendor, specifier, data->vendor, data->specifier);
		return NULL;
	}

	return pos;
}


static const u8 * eap_gpsk_validate_pd_payload_2(struct eap_gpsk_data *data,
						 const u8 *pos, const u8 *end)
{
	u16 alen;

	if (pos == NULL)
		return NULL;

	if (end - pos < 2) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Message too short for "
			   "PD_Payload_2 length");
		return NULL;
	}
	alen = WPA_GET_BE16(pos);
	pos += 2;
	if (end - pos < alen) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Message too short for "
			   "%d-octet PD_Payload_2", alen);
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-GPSK: PD_Payload_2", pos, alen);
	pos += alen;

	return pos;
}


static const u8 * eap_gpsk_validate_gpsk_3_mic(struct eap_gpsk_data *data,
					       const u8 *payload,
					       const u8 *pos, const u8 *end)
{
	size_t miclen;
	u8 mic[EAP_GPSK_MAX_MIC_LEN];

	if (pos == NULL)
		return NULL;

	miclen = eap_gpsk_mic_len(data->vendor, data->specifier);
	if (end - pos < (int) miclen) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Message too short for MIC "
			   "(left=%lu miclen=%lu)",
			   (unsigned long) (end - pos),
			   (unsigned long) miclen);
		return NULL;
	}
	if (eap_gpsk_compute_mic(data->sk, data->sk_len, data->vendor,
				 data->specifier, payload, pos - payload, mic)
	    < 0) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Failed to compute MIC");
		return NULL;
	}
	if (os_memcmp_const(mic, pos, miclen) != 0) {
		wpa_printf(MSG_INFO, "EAP-GPSK: Incorrect MIC in GPSK-3");
		wpa_hexdump(MSG_DEBUG, "EAP-GPSK: Received MIC", pos, miclen);
		wpa_hexdump(MSG_DEBUG, "EAP-GPSK: Computed MIC", mic, miclen);
		return NULL;
	}
	pos += miclen;

	return pos;
}


static struct wpabuf * eap_gpsk_process_gpsk_3(struct eap_sm *sm,
					       struct eap_gpsk_data *data,
					       struct eap_method_ret *ret,
					       u8 identifier,
					       const u8 *payload,
					       size_t payload_len)
{
	struct wpabuf *resp;
	const u8 *pos, *end;

	if (data->state != GPSK_3) {
		ret->ignore = TRUE;
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-GPSK: Received Request/GPSK-3");

	end = payload + payload_len;

	pos = eap_gpsk_validate_rand(data, payload, end);
	pos = eap_gpsk_validate_id_server(data, pos, end);
	pos = eap_gpsk_validate_csuite(data, pos, end);
	pos = eap_gpsk_validate_pd_payload_2(data, pos, end);
	pos = eap_gpsk_validate_gpsk_3_mic(data, payload, pos, end);

	if (pos == NULL) {
		eap_gpsk_state(data, FAILURE);
		return NULL;
	}
	if (pos != end) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Ignored %lu bytes of extra "
			   "data in the end of GPSK-2",
			   (unsigned long) (end - pos));
	}

	resp = eap_gpsk_send_gpsk_4(data, identifier);
	if (resp == NULL)
		return NULL;

	eap_gpsk_state(data, SUCCESS);
	ret->methodState = METHOD_DONE;
	ret->decision = DECISION_UNCOND_SUCC;

	return resp;
}


static struct wpabuf * eap_gpsk_send_gpsk_4(struct eap_gpsk_data *data,
					    u8 identifier)
{
	struct wpabuf *resp;
	u8 *rpos, *start;
	size_t mlen;

	wpa_printf(MSG_DEBUG, "EAP-GPSK: Sending Response/GPSK-4");

	mlen = eap_gpsk_mic_len(data->vendor, data->specifier);

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_GPSK, 1 + 2 + mlen,
			     EAP_CODE_RESPONSE, identifier);
	if (resp == NULL)
		return NULL;

	wpabuf_put_u8(resp, EAP_GPSK_OPCODE_GPSK_4);
	start = wpabuf_put(resp, 0);

	/* No PD_Payload_3 */
	wpabuf_put_be16(resp, 0);

	rpos = wpabuf_put(resp, mlen);
	if (eap_gpsk_compute_mic(data->sk, data->sk_len, data->vendor,
				 data->specifier, start, rpos - start, rpos) <
	    0) {
		eap_gpsk_state(data, FAILURE);
		wpabuf_free(resp);
		return NULL;
	}

	return resp;
}


static struct wpabuf * eap_gpsk_process(struct eap_sm *sm, void *priv,
					struct eap_method_ret *ret,
					const struct wpabuf *reqData)
{
	struct eap_gpsk_data *data = priv;
	struct wpabuf *resp;
	const u8 *pos;
	size_t len;
	u8 opcode, id;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_GPSK, reqData, &len);
	if (pos == NULL || len < 1) {
		ret->ignore = TRUE;
		return NULL;
	}

	id = eap_get_id(reqData);
	opcode = *pos++;
	len--;
	wpa_printf(MSG_DEBUG, "EAP-GPSK: Received frame: opcode %d", opcode);

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = FALSE;

	switch (opcode) {
	case EAP_GPSK_OPCODE_GPSK_1:
		resp = eap_gpsk_process_gpsk_1(sm, data, ret, id, pos, len);
		break;
	case EAP_GPSK_OPCODE_GPSK_3:
		resp = eap_gpsk_process_gpsk_3(sm, data, ret, id, pos, len);
		break;
	default:
		wpa_printf(MSG_DEBUG,
			   "EAP-GPSK: Ignoring message with unknown opcode %d",
			   opcode);
		ret->ignore = TRUE;
		return NULL;
	}

	return resp;
}


static Boolean eap_gpsk_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_gpsk_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_gpsk_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_gpsk_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->msk, EAP_MSK_LEN);
	if (key == NULL)
		return NULL;
	*len = EAP_MSK_LEN;

	return key;
}


static u8 * eap_gpsk_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_gpsk_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->emsk, EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;
	*len = EAP_EMSK_LEN;

	return key;
}


static u8 * eap_gpsk_get_session_id(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_gpsk_data *data = priv;
	u8 *sid;

	if (data->state != SUCCESS)
		return NULL;

	sid = os_memdup(data->session_id, data->id_len);
	if (sid == NULL)
		return NULL;
	*len = data->id_len;

	return sid;
}


int eap_peer_gpsk_register(void)
{
	struct eap_method *eap;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_GPSK, "GPSK");
	if (eap == NULL)
		return -1;

	eap->init = eap_gpsk_init;
	eap->deinit = eap_gpsk_deinit;
	eap->process = eap_gpsk_process;
	eap->isKeyAvailable = eap_gpsk_isKeyAvailable;
	eap->getKey = eap_gpsk_getKey;
	eap->get_emsk = eap_gpsk_get_emsk;
	eap->getSessionId = eap_gpsk_get_session_id;

	return eap_peer_method_register(eap);
}
