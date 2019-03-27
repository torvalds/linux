/*
 * EAP peer method: EAP-SAKE (RFC 4763)
 * Copyright (c) 2006-2008, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/random.h"
#include "eap_peer/eap_i.h"
#include "eap_common/eap_sake_common.h"

struct eap_sake_data {
	enum { IDENTITY, CHALLENGE, CONFIRM, SUCCESS, FAILURE } state;
	u8 root_secret_a[EAP_SAKE_ROOT_SECRET_LEN];
	u8 root_secret_b[EAP_SAKE_ROOT_SECRET_LEN];
	u8 rand_s[EAP_SAKE_RAND_LEN];
	u8 rand_p[EAP_SAKE_RAND_LEN];
	struct {
		u8 auth[EAP_SAKE_TEK_AUTH_LEN];
		u8 cipher[EAP_SAKE_TEK_CIPHER_LEN];
	} tek;
	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 session_id;
	int session_id_set;
	u8 *peerid;
	size_t peerid_len;
	u8 *serverid;
	size_t serverid_len;
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


static void eap_sake_deinit(struct eap_sm *sm, void *priv);


static void * eap_sake_init(struct eap_sm *sm)
{
	struct eap_sake_data *data;
	const u8 *identity, *password;
	size_t identity_len, password_len;

	password = eap_get_config_password(sm, &password_len);
	if (!password || password_len != 2 * EAP_SAKE_ROOT_SECRET_LEN) {
		wpa_printf(MSG_INFO, "EAP-SAKE: No key of correct length "
			   "configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = IDENTITY;

	identity = eap_get_config_identity(sm, &identity_len);
	if (identity) {
		data->peerid = os_memdup(identity, identity_len);
		if (data->peerid == NULL) {
			eap_sake_deinit(sm, data);
			return NULL;
		}
		data->peerid_len = identity_len;
	}

	os_memcpy(data->root_secret_a, password, EAP_SAKE_ROOT_SECRET_LEN);
	os_memcpy(data->root_secret_b,
		  password + EAP_SAKE_ROOT_SECRET_LEN,
		  EAP_SAKE_ROOT_SECRET_LEN);

	return data;
}


static void eap_sake_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_sake_data *data = priv;
	os_free(data->serverid);
	os_free(data->peerid);
	bin_clear_free(data, sizeof(*data));
}


static struct wpabuf * eap_sake_build_msg(struct eap_sake_data *data,
					  int id, size_t length, u8 subtype)
{
	struct eap_sake_hdr *sake;
	struct wpabuf *msg;
	size_t plen;

	plen = length + sizeof(struct eap_sake_hdr);

	msg = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_SAKE, plen,
			    EAP_CODE_RESPONSE, id);
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


static struct wpabuf * eap_sake_process_identity(struct eap_sm *sm,
						 struct eap_sake_data *data,
						 struct eap_method_ret *ret,
						 u8 id,
						 const u8 *payload,
						 size_t payload_len)
{
	struct eap_sake_parse_attr attr;
	struct wpabuf *resp;

	if (data->state != IDENTITY) {
		ret->ignore = TRUE;
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received Request/Identity");

	if (eap_sake_parse_attributes(payload, payload_len, &attr))
		return NULL;

	if (!attr.perm_id_req && !attr.any_id_req) {
		wpa_printf(MSG_INFO, "EAP-SAKE: No AT_PERM_ID_REQ or "
			   "AT_ANY_ID_REQ in Request/Identity");
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Sending Response/Identity");

	resp = eap_sake_build_msg(data, id, 2 + data->peerid_len,
				  EAP_SAKE_SUBTYPE_IDENTITY);
	if (resp == NULL)
		return NULL;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_PEERID");
	eap_sake_add_attr(resp, EAP_SAKE_AT_PEERID,
			  data->peerid, data->peerid_len);

	eap_sake_state(data, CHALLENGE);

	return resp;
}


static struct wpabuf * eap_sake_process_challenge(struct eap_sm *sm,
						  struct eap_sake_data *data,
						  struct eap_method_ret *ret,
						  u8 id,
						  const u8 *payload,
						  size_t payload_len)
{
	struct eap_sake_parse_attr attr;
	struct wpabuf *resp;
	u8 *rpos;
	size_t rlen;

	if (data->state != IDENTITY && data->state != CHALLENGE) {
		wpa_printf(MSG_DEBUG, "EAP-SAKE: Request/Challenge received "
			   "in unexpected state (%d)", data->state);
		ret->ignore = TRUE;
		return NULL;
	}
	if (data->state == IDENTITY)
		eap_sake_state(data, CHALLENGE);

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received Request/Challenge");

	if (eap_sake_parse_attributes(payload, payload_len, &attr))
		return NULL;

	if (!attr.rand_s) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Request/Challenge did not "
			   "include AT_RAND_S");
		return NULL;
	}

	os_memcpy(data->rand_s, attr.rand_s, EAP_SAKE_RAND_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-SAKE: RAND_S (server rand)",
		    data->rand_s, EAP_SAKE_RAND_LEN);

	if (random_get_bytes(data->rand_p, EAP_SAKE_RAND_LEN)) {
		wpa_printf(MSG_ERROR, "EAP-SAKE: Failed to get random data");
		return NULL;
	}
	wpa_hexdump(MSG_MSGDUMP, "EAP-SAKE: RAND_P (peer rand)",
		    data->rand_p, EAP_SAKE_RAND_LEN);

	os_free(data->serverid);
	data->serverid = NULL;
	data->serverid_len = 0;
	if (attr.serverid) {
		wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-SAKE: SERVERID",
				  attr.serverid, attr.serverid_len);
		data->serverid = os_memdup(attr.serverid, attr.serverid_len);
		if (data->serverid == NULL)
			return NULL;
		data->serverid_len = attr.serverid_len;
	}

	eap_sake_derive_keys(data->root_secret_a, data->root_secret_b,
			     data->rand_s, data->rand_p,
			     (u8 *) &data->tek, data->msk, data->emsk);

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Sending Response/Challenge");

	rlen = 2 + EAP_SAKE_RAND_LEN + 2 + EAP_SAKE_MIC_LEN;
	if (data->peerid)
		rlen += 2 + data->peerid_len;
	resp = eap_sake_build_msg(data, id, rlen, EAP_SAKE_SUBTYPE_CHALLENGE);
	if (resp == NULL)
		return NULL;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_RAND_P");
	eap_sake_add_attr(resp, EAP_SAKE_AT_RAND_P,
			  data->rand_p, EAP_SAKE_RAND_LEN);

	if (data->peerid) {
		wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_PEERID");
		eap_sake_add_attr(resp, EAP_SAKE_AT_PEERID,
				  data->peerid, data->peerid_len);
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_MIC_P");
	wpabuf_put_u8(resp, EAP_SAKE_AT_MIC_P);
	wpabuf_put_u8(resp, 2 + EAP_SAKE_MIC_LEN);
	rpos = wpabuf_put(resp, EAP_SAKE_MIC_LEN);
	if (eap_sake_compute_mic(data->tek.auth, data->rand_s, data->rand_p,
				 data->serverid, data->serverid_len,
				 data->peerid, data->peerid_len, 1,
				 wpabuf_head(resp), wpabuf_len(resp), rpos,
				 rpos)) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Failed to compute MIC");
		wpabuf_free(resp);
		return NULL;
	}

	eap_sake_state(data, CONFIRM);

	return resp;
}


static struct wpabuf * eap_sake_process_confirm(struct eap_sm *sm,
						struct eap_sake_data *data,
						struct eap_method_ret *ret,
						u8 id,
						const struct wpabuf *reqData,
						const u8 *payload,
						size_t payload_len)
{
	struct eap_sake_parse_attr attr;
	u8 mic_s[EAP_SAKE_MIC_LEN];
	struct wpabuf *resp;
	u8 *rpos;

	if (data->state != CONFIRM) {
		ret->ignore = TRUE;
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received Request/Confirm");

	if (eap_sake_parse_attributes(payload, payload_len, &attr))
		return NULL;

	if (!attr.mic_s) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Request/Confirm did not "
			   "include AT_MIC_S");
		return NULL;
	}

	if (eap_sake_compute_mic(data->tek.auth, data->rand_s, data->rand_p,
				 data->serverid, data->serverid_len,
				 data->peerid, data->peerid_len, 0,
				 wpabuf_head(reqData), wpabuf_len(reqData),
				 attr.mic_s, mic_s)) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Failed to compute MIC");
		eap_sake_state(data, FAILURE);
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		ret->allowNotifications = FALSE;
		wpa_printf(MSG_DEBUG, "EAP-SAKE: Sending Response/Auth-Reject");
		return eap_sake_build_msg(data, id, 0,
					  EAP_SAKE_SUBTYPE_AUTH_REJECT);
	}
	if (os_memcmp_const(attr.mic_s, mic_s, EAP_SAKE_MIC_LEN) != 0) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Incorrect AT_MIC_S");
		eap_sake_state(data, FAILURE);
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		ret->allowNotifications = FALSE;
		wpa_printf(MSG_DEBUG, "EAP-SAKE: Sending "
			   "Response/Auth-Reject");
		return eap_sake_build_msg(data, id, 0,
					  EAP_SAKE_SUBTYPE_AUTH_REJECT);
	}

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Sending Response/Confirm");

	resp = eap_sake_build_msg(data, id, 2 + EAP_SAKE_MIC_LEN,
				  EAP_SAKE_SUBTYPE_CONFIRM);
	if (resp == NULL)
		return NULL;

	wpa_printf(MSG_DEBUG, "EAP-SAKE: * AT_MIC_P");
	wpabuf_put_u8(resp, EAP_SAKE_AT_MIC_P);
	wpabuf_put_u8(resp, 2 + EAP_SAKE_MIC_LEN);
	rpos = wpabuf_put(resp, EAP_SAKE_MIC_LEN);
	if (eap_sake_compute_mic(data->tek.auth, data->rand_s, data->rand_p,
				 data->serverid, data->serverid_len,
				 data->peerid, data->peerid_len, 1,
				 wpabuf_head(resp), wpabuf_len(resp), rpos,
				 rpos)) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Failed to compute MIC");
		wpabuf_free(resp);
		return NULL;
	}

	eap_sake_state(data, SUCCESS);
	ret->methodState = METHOD_DONE;
	ret->decision = DECISION_UNCOND_SUCC;
	ret->allowNotifications = FALSE;

	return resp;
}


static struct wpabuf * eap_sake_process(struct eap_sm *sm, void *priv,
					struct eap_method_ret *ret,
					const struct wpabuf *reqData)
{
	struct eap_sake_data *data = priv;
	const struct eap_sake_hdr *req;
	struct wpabuf *resp;
	const u8 *pos, *end;
	size_t len;
	u8 subtype, session_id, id;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_SAKE, reqData, &len);
	if (pos == NULL || len < sizeof(struct eap_sake_hdr)) {
		ret->ignore = TRUE;
		return NULL;
	}

	req = (const struct eap_sake_hdr *) pos;
	end = pos + len;
	id = eap_get_id(reqData);
	subtype = req->subtype;
	session_id = req->session_id;
	pos = (const u8 *) (req + 1);

	wpa_printf(MSG_DEBUG, "EAP-SAKE: Received frame: subtype %d "
		   "session_id %d", subtype, session_id);
	wpa_hexdump(MSG_DEBUG, "EAP-SAKE: Received attributes",
		    pos, end - pos);

	if (data->session_id_set && data->session_id != session_id) {
		wpa_printf(MSG_INFO, "EAP-SAKE: Session ID mismatch (%d,%d)",
			   session_id, data->session_id);
		ret->ignore = TRUE;
		return NULL;
	}
	data->session_id = session_id;
	data->session_id_set = 1;

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = TRUE;

	switch (subtype) {
	case EAP_SAKE_SUBTYPE_IDENTITY:
		resp = eap_sake_process_identity(sm, data, ret, id,
						 pos, end - pos);
		break;
	case EAP_SAKE_SUBTYPE_CHALLENGE:
		resp = eap_sake_process_challenge(sm, data, ret, id,
						  pos, end - pos);
		break;
	case EAP_SAKE_SUBTYPE_CONFIRM:
		resp = eap_sake_process_confirm(sm, data, ret, id, reqData,
						pos, end - pos);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-SAKE: Ignoring message with "
			   "unknown subtype %d", subtype);
		ret->ignore = TRUE;
		return NULL;
	}

	if (ret->methodState == METHOD_DONE)
		ret->allowNotifications = FALSE;

	return resp;
}


static Boolean eap_sake_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_sake_data *data = priv;
	return data->state == SUCCESS;
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


int eap_peer_sake_register(void)
{
	struct eap_method *eap;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_SAKE, "SAKE");
	if (eap == NULL)
		return -1;

	eap->init = eap_sake_init;
	eap->deinit = eap_sake_deinit;
	eap->process = eap_sake_process;
	eap->isKeyAvailable = eap_sake_isKeyAvailable;
	eap->getKey = eap_sake_getKey;
	eap->getSessionId = eap_sake_get_session_id;
	eap->get_emsk = eap_sake_get_emsk;

	return eap_peer_method_register(eap);
}
