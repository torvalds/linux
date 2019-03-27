/*
 * hostapd / EAP-SAKE (RFC 4763) server
 * Copyright (c) 2006-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/random.h"
#include "eap_server/eap_i.h"
#include "eap_common/eap_sake_common.h"


struct eap_sake_data {
	enum { IDENTITY, CHALLENGE, CONFIRM, SUCCESS, FAILURE } state;
	u8 rand_s[EAP_SAKE_RAND_LEN];
	u8 rand_p[EAP_SAKE_RAND_LEN];
	struct {
		u8 auth[EAP_SAKE_TEK_AUTH_LEN];
		u8 cipher[EAP_SAKE_TEK_CIPHER_LEN];
	} tek;
	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 session_id;
	u8 *peerid;
	size_t peerid_len;
};


static const char * eap_sake_state_txt(int state)
{
	switch (state) {
	case IDENTITY:
		return "IDENTITY";
	case CHALLENGE:
		return "CHALLENGE";
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


static void eap_sake_state(struct eap_sake_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-SAKE: %s -> %s",
		   eap_sake_state_txt(data->state),
		   eap_sake_state_txt(state));
	data->state = state;
}


static void * eap_sake_init(struct eap_sm *sm)
{
	struct eap_sake_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = CHALLENGE;

	if (os_get_random(&data->session_id, 1)) {
		wpa_printf(MSG_ERROR, "EAP-SAKE: Failed to get random data");
		os_free(data);
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "EAP-SAKE: Initialized Session ID %d",
		   data->session_id);

	return data;
}


static void eap_sake_reset(struct eap_sm *sm, void *priv)
{
	struct eap_sake_data *data = priv;
	os_free(data->peerid);
	bin_clear_free(data, sizeof(*data));
}


static struct wpabuf * eap_sake_build_msg(struct eap_sake_data *data,
					  u8 id, size_t length, u8 subtype)
{
	struct eap_sake_hdr *sake;
	struct wpabuf *msg;
	size_t plen;

	plen = sizeof(struct eap_sake_hdr) + length;

	msg = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_SAKE, plen,
			    EAP_CODE_REQUEST, id);
	if (msg == NULL) {
		wpa_printf(MSG_ERROR, "EAP-SAKE: Failed to allocate memory "
			   "request");
		return NULL;
	}

	sake = wpabuf_put(msg, sizeof(*sake));
	sake->version = EAP_SAKE_VERSION;
	sake->session_id = data->session_id;
	sake->subtype = subtype;

	return msg;
}


static struct wpabuf * eap_sake_build_identity(struct eap_sm *sm,
					       struct eap_sake_data *data,
					       u8 id)
{
	struct wpabuf *msg;
	size_t plen;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Request/Identity");

	plen = 4;
	plen += 2 + sm->server_id_len;
	msg = eap_sake_build_msg(data, id, plen, EAP_SAKE_SUBTYPE_IDENTITY);
	if (msg == NULL) {
		data->state = FAILURE;
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_PERM_ID_REQ");
	eap_sake_add_attr(msg, EAP_SAKE_AT_PERM_ID_REQ, NULL, 2);

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_SERVERID");
	eap_sake_add_attr(msg, EAP_SAKE_AT_SERVERID,
			  sm->server_id, sm->server_id_len);

	return msg;
}


static struct wpabuf * eap_sake_build_challenge(struct eap_sm *sm,
						struct eap_sake_data *data,
						u8 id)
{
	struct wpabuf *msg;
	size_t plen;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Request/Challenge");

	if (random_get_bytes(data->rand_s, EAP_SAKE_RAND_LEN)) {
		wpa_printf(MSG_ERROR, "EAP-SAKE: Failed to get random data");
		data->state = FAILURE;
		return NULL;
	}
	wpa_hexdump(MSG_MSGDUMP, "EAP-SAKE: RAND_S (server rand)",
		    data->rand_s, EAP_SAKE_RAND_LEN);

	plen = 2 + EAP_SAKE_RAND_LEN + 2 + sm->server_id_len;
	msg = eap_sake_build_msg(data, id, plen, EAP_SAKE_SUBTYPE_CHALLENGE);
	if (msg == NULL) {
		data->state = FAILURE;
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_RAND_S");
	eap_sake_add_attr(msg, EAP_SAKE_AT_RAND_S,
			  data->rand_s, EAP_SAKE_RAND_LEN);

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_SERVERID");
	eap_sake_add_attr(msg, EAP_SAKE_AT_SERVERID,
			  sm->server_id, sm->server_id_len);

	return msg;
}


static struct wpabuf * eap_sake_build_confirm(struct eap_sm *sm,
					      struct eap_sake_data *data,
					      u8 id)
{
	struct wpabuf *msg;
	u8 *mic;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Request/Confirm");

	msg = eap_sake_build_msg(data, id, 2 + EAP_SAKE_MIC_LEN,
				 EAP_SAKE_SUBTYPE_CONFIRM);
	if (msg == NULL) {
		data->state = FAILURE;
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_MIC_S");
	wpabuf_put_u8(msg, EAP_SAKE_AT_MIC_S);
	wpabuf_put_u8(msg, 2 + EAP_SAKE_MIC_LEN);
	mic = wpabuf_put(msg, EAP_SAKE_MIC_LEN);
	if (eap_sake_compute_mic(data->tek.auth, data->rand_s, data->rand_p,
				 sm->server_id, sm->server_id_len,
				 data->peerid, data->peerid_len, 0,
				 wpabuf_head(msg), wpabuf_len(msg), mic, mic))
	{
		wpa_printf(MSG_INFO, "EAP-SAKE: Failed to compute MIC");
		data->state = FAILURE;
		os_free(msg);
		return NULL;
	}

	return msg;
}


static struct wpabuf * eap_sake_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_sake_data *data = priv;

	switch (data->state) {
	case IDENTITY:
		return eap_sake_build_identity(sm, data, id);
	case CHALLENGE:
		return eap_sake_build_challenge(sm, data, id);
	case CONFIRM:
		return eap_sake_build_confirm(sm, data, id);
	default:
		wpa_printf(MSG_DEBUG, "EAP-SAKE: Unknown state %d in buildReq",
			   data->state);
		break;
	}
	return NULL;
}


static Boolean eap_sake_check(struct eap_sm *sm, void *priv,
			      struct wpabuf *respData)
{
	struct eap_sake_data *data = priv;
	struct eap_sake_hdr *resp;
	size_t len;
	u8 version, session_id, subtype;
	const u8 *pos;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_SAKE, respData, &len);
	if (pos == NULL || len < sizeof(struct eap_sake_hdr)) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Invalid frame");
		return TRUE;
	}

	resp = (struct eap_sake_hdr *) pos;
	version = resp->version;
	session_id = resp->session_id;
	subtype = resp->subtype;

	if (version != EAP_SAKE_VERSION) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Unknown version %d", version);
		return TRUE;
	}

	if (session_id != data->session_id) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Session ID mismatch (%d,%d)",
			   session_id, data->session_id);
		return TRUE;
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received frame: subtype=%d", subtype);

	if (data->state == IDENTITY && subtype == EAP_SAKE_SUBTYPE_IDENTITY)
		return FALSE;

	if (data->state == CHALLENGE && subtype == EAP_SAKE_SUBTYPE_CHALLENGE)
		return FALSE;

	if (data->state == CONFIRM && subtype == EAP_SAKE_SUBTYPE_CONFIRM)
		return FALSE;

	if (subtype == EAP_SAKE_SUBTYPE_AUTH_REJECT)
		return FALSE;

	wpa_printf(MSG_INFO, "EAP-SAKE: Unexpected subtype=%d in state=%d",
		   subtype, data->state);

	return TRUE;
}


static void eap_sake_process_identity(struct eap_sm *sm,
				      struct eap_sake_data *data,
				      const struct wpabuf *respData,
				      const u8 *payload, size_t payloadlen)
{
	if (data->state != IDENTITY)
		return;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received Response/Identity");
	/* TODO: update identity and select new user data */
	eap_sake_state(data, CHALLENGE);
}


static void eap_sake_process_challenge(struct eap_sm *sm,
				       struct eap_sake_data *data,
				       const struct wpabuf *respData,
				       const u8 *payload, size_t payloadlen)
{
	struct eap_sake_parse_attr attr;
	u8 mic_p[EAP_SAKE_MIC_LEN];

	if (data->state != CHALLENGE)
		return;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received Response/Challenge");

	if (eap_sake_parse_attributes(payload, payloadlen, &attr))
		return;

	if (!attr.rand_p || !attr.mic_p) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Response/Challenge did not "
			   "include AT_RAND_P or AT_MIC_P");
		return;
	}

	os_memcpy(data->rand_p, attr.rand_p, EAP_SAKE_RAND_LEN);

	os_free(data->peerid);
	data->peerid = NULL;
	data->peerid_len = 0;
	if (attr.peerid) {
		data->peerid = os_memdup(attr.peerid, attr.peerid_len);
		if (data->peerid == NULL)
			return;
		data->peerid_len = attr.peerid_len;
	}

	if (sm->user == NULL || sm->user->password == NULL ||
	    sm->user->password_len != 2 * EAP_SAKE_ROOT_SECRET_LEN) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Plaintext password with "
			   "%d-byte key not configured",
			   2 * EAP_SAKE_ROOT_SECRET_LEN);
		data->state = FAILURE;
		return;
	}
	eap_sake_derive_keys(sm->user->password,
			     sm->user->password + EAP_SAKE_ROOT_SECRET_LEN,
			     data->rand_s, data->rand_p,
			     (u8 *) &data->tek, data->msk, data->emsk);

	eap_sake_compute_mic(data->tek.auth, data->rand_s, data->rand_p,
			     sm->server_id, sm->server_id_len,
			     data->peerid, data->peerid_len, 1,
			     wpabuf_head(respData), wpabuf_len(respData),
			     attr.mic_p, mic_p);
	if (os_memcmp_const(attr.mic_p, mic_p, EAP_SAKE_MIC_LEN) != 0) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Incorrect AT_MIC_P");
		eap_sake_state(data, FAILURE);
		return;
	}

	eap_sake_state(data, CONFIRM);
}


static void eap_sake_process_confirm(struct eap_sm *sm,
				     struct eap_sake_data *data,
				     const struct wpabuf *respData,
				     const u8 *payload, size_t payloadlen)
{
	struct eap_sake_parse_attr attr;
	u8 mic_p[EAP_SAKE_MIC_LEN];

	if (data->state != CONFIRM)
		return;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received Response/Confirm");

	if (eap_sake_parse_attributes(payload, payloadlen, &attr))
		return;

	if (!attr.mic_p) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Response/Confirm did not "
			   "include AT_MIC_P");
		return;
	}

	eap_sake_compute_mic(data->tek.auth, data->rand_s, data->rand_p,
			     sm->server_id, sm->server_id_len,
			     data->peerid, data->peerid_len, 1,
			     wpabuf_head(respData), wpabuf_len(respData),
			     attr.mic_p, mic_p);
	if (os_memcmp_const(attr.mic_p, mic_p, EAP_SAKE_MIC_LEN) != 0) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Incorrect AT_MIC_P");
		eap_sake_state(data, FAILURE);
	} else
		eap_sake_state(data, SUCCESS);
}


static void eap_sake_process_auth_reject(struct eap_sm *sm,
					 struct eap_sake_data *data,
					 const struct wpabuf *respData,
					 const u8 *payload, size_t payloadlen)
{
	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received Response/Auth-Reject");
	eap_sake_state(data, FAILURE);
}


static void eap_sake_process(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_sake_data *data = priv;
	struct eap_sake_hdr *resp;
	u8 subtype;
	size_t len;
	const u8 *pos, *end;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_SAKE, respData, &len);
	if (pos == NULL || len < sizeof(struct eap_sake_hdr))
		return;

	resp = (struct eap_sake_hdr *) pos;
	end = pos + len;
	subtype = resp->subtype;
	pos = (u8 *) (resp + 1);

	wpa_hexdump(MSG_DEBUG, "EAP-SAKE: Received attributes",
		    pos, end - pos);

	switch (subtype) {
	case EAP_SAKE_SUBTYPE_IDENTITY:
		eap_sake_process_identity(sm, data, respData, pos, end - pos);
		break;
	case EAP_SAKE_SUBTYPE_CHALLENGE:
		eap_sake_process_challenge(sm, data, respData, pos, end - pos);
		break;
	case EAP_SAKE_SUBTYPE_CONFIRM:
		eap_sake_process_confirm(sm, data, respData, pos, end - pos);
		break;
	case EAP_SAKE_SUBTYPE_AUTH_REJECT:
		eap_sake_process_auth_reject(sm, data, respData, pos,
					     end - pos);
		break;
	}
}


static Boolean eap_sake_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_sake_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_sake_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_sake_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->msk, EAP_MSK_LEN);
	if (key == NULL)
		return NULL;
	*len = EAP_MSK_LEN;

	return key;
}


static u8 * eap_sake_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_sake_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->emsk, EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;
	*len = EAP_EMSK_LEN;

	return key;
}


static Boolean eap_sake_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_sake_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_sake_get_session_id(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_sake_data *data = priv;
	u8 *id;

	if (data->state != SUCCESS)
		return NULL;

	*len = 1 + 2 * EAP_SAKE_RAND_LEN;
	id = os_malloc(*len);
	if (id == NULL)
		return NULL;

	id[0] = EAP_TYPE_SAKE;
	os_memcpy(id + 1, data->rand_s, EAP_SAKE_RAND_LEN);
	os_memcpy(id + 1 + EAP_SAKE_RAND_LEN, data->rand_s, EAP_SAKE_RAND_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-SAKE: Derived Session-Id", id, *len);

	return id;
}


int eap_server_sake_register(void)
{
	struct eap_method *eap;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_SAKE, "SAKE");
	if (eap == NULL)
		return -1;

	eap->init = eap_sake_init;
	eap->reset = eap_sake_reset;
	eap->buildReq = eap_sake_buildReq;
	eap->check = eap_sake_check;
	eap->process = eap_sake_process;
	eap->isDone = eap_sake_isDone;
	eap->getKey = eap_sake_getKey;
	eap->isSuccess = eap_sake_isSuccess;
	eap->get_emsk = eap_sake_get_emsk;
	eap->getSessionId = eap_sake_get_session_id;

	return eap_server_method_register(eap);
}
