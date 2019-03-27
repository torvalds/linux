/*
 * hostapd / EAP-PAX (RFC 4746) server
 * Copyright (c) 2005-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/random.h"
#include "eap_server/eap_i.h"
#include "eap_common/eap_pax_common.h"

/*
 * Note: only PAX_STD subprotocol is currently supported
 *
 * TODO: Add support with PAX_SEC with the mandatory to implement ciphersuite
 * (HMAC_SHA1_128, IANA DH Group 14 (2048 bits), RSA-PKCS1-V1_5) and
 * recommended ciphersuite (HMAC_SHA256_128, IANA DH Group 15 (3072 bits),
 * RSAES-OAEP).
 */

struct eap_pax_data {
	enum { PAX_STD_1, PAX_STD_3, SUCCESS, FAILURE } state;
	u8 mac_id;
	union {
		u8 e[2 * EAP_PAX_RAND_LEN];
		struct {
			u8 x[EAP_PAX_RAND_LEN]; /* server rand */
			u8 y[EAP_PAX_RAND_LEN]; /* client rand */
		} r;
	} rand;
	u8 ak[EAP_PAX_AK_LEN];
	u8 mk[EAP_PAX_MK_LEN];
	u8 ck[EAP_PAX_CK_LEN];
	u8 ick[EAP_PAX_ICK_LEN];
	u8 mid[EAP_PAX_MID_LEN];
	int keys_set;
	char *cid;
	size_t cid_len;
};


static void * eap_pax_init(struct eap_sm *sm)
{
	struct eap_pax_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = PAX_STD_1;
	/*
	 * TODO: make this configurable once EAP_PAX_HMAC_SHA256_128 is
	 * supported
	 */
	data->mac_id = EAP_PAX_MAC_HMAC_SHA1_128;

	return data;
}


static void eap_pax_reset(struct eap_sm *sm, void *priv)
{
	struct eap_pax_data *data = priv;
	os_free(data->cid);
	bin_clear_free(data, sizeof(*data));
}


static struct wpabuf * eap_pax_build_std_1(struct eap_sm *sm,
					   struct eap_pax_data *data, u8 id)
{
	struct wpabuf *req;
	struct eap_pax_hdr *pax;
	u8 *pos;

	wpa_printf(MSG_DEBUG, "EAP-PAX: PAX_STD-1 (sending)");

	if (random_get_bytes(data->rand.r.x, EAP_PAX_RAND_LEN)) {
		wpa_printf(MSG_ERROR, "EAP-PAX: Failed to get random data");
		data->state = FAILURE;
		return NULL;
	}

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PAX,
			    sizeof(*pax) + 2 + EAP_PAX_RAND_LEN +
			    EAP_PAX_ICV_LEN, EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-PAX: Failed to allocate memory "
			   "request");
		data->state = FAILURE;
		return NULL;
	}

	pax = wpabuf_put(req, sizeof(*pax));
	pax->op_code = EAP_PAX_OP_STD_1;
	pax->flags = 0;
	pax->mac_id = data->mac_id;
	pax->dh_group_id = EAP_PAX_DH_GROUP_NONE;
	pax->public_key_id = EAP_PAX_PUBLIC_KEY_NONE;

	wpabuf_put_be16(req, EAP_PAX_RAND_LEN);
	wpabuf_put_data(req, data->rand.r.x, EAP_PAX_RAND_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: A = X (server rand)",
		    data->rand.r.x, EAP_PAX_RAND_LEN);

	pos = wpabuf_put(req, EAP_PAX_MAC_LEN);
	eap_pax_mac(data->mac_id, (u8 *) "", 0,
		    wpabuf_mhead(req), wpabuf_len(req) - EAP_PAX_ICV_LEN,
		    NULL, 0, NULL, 0, pos);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: ICV", pos, EAP_PAX_ICV_LEN);

	return req;
}


static struct wpabuf * eap_pax_build_std_3(struct eap_sm *sm,
					   struct eap_pax_data *data, u8 id)
{
	struct wpabuf *req;
	struct eap_pax_hdr *pax;
	u8 *pos;

	wpa_printf(MSG_DEBUG, "EAP-PAX: PAX_STD-3 (sending)");

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PAX,
			    sizeof(*pax) + 2 + EAP_PAX_MAC_LEN +
			    EAP_PAX_ICV_LEN, EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-PAX: Failed to allocate memory "
			   "request");
		data->state = FAILURE;
		return NULL;
	}

	pax = wpabuf_put(req, sizeof(*pax));
	pax->op_code = EAP_PAX_OP_STD_3;
	pax->flags = 0;
	pax->mac_id = data->mac_id;
	pax->dh_group_id = EAP_PAX_DH_GROUP_NONE;
	pax->public_key_id = EAP_PAX_PUBLIC_KEY_NONE;

	wpabuf_put_be16(req, EAP_PAX_MAC_LEN);
	pos = wpabuf_put(req, EAP_PAX_MAC_LEN);
	eap_pax_mac(data->mac_id, data->ck, EAP_PAX_CK_LEN,
		    data->rand.r.y, EAP_PAX_RAND_LEN,
		    (u8 *) data->cid, data->cid_len, NULL, 0, pos);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: MAC_CK(B, CID)",
		    pos, EAP_PAX_MAC_LEN);

	/* Optional ADE could be added here, if needed */

	pos = wpabuf_put(req, EAP_PAX_MAC_LEN);
	eap_pax_mac(data->mac_id, data->ick, EAP_PAX_ICK_LEN,
		    wpabuf_mhead(req), wpabuf_len(req) - EAP_PAX_ICV_LEN,
		    NULL, 0, NULL, 0, pos);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: ICV", pos, EAP_PAX_ICV_LEN);

	return req;
}


static struct wpabuf * eap_pax_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_pax_data *data = priv;

	switch (data->state) {
	case PAX_STD_1:
		return eap_pax_build_std_1(sm, data, id);
	case PAX_STD_3:
		return eap_pax_build_std_3(sm, data, id);
	default:
		wpa_printf(MSG_DEBUG, "EAP-PAX: Unknown state %d in buildReq",
			   data->state);
		break;
	}
	return NULL;
}


static Boolean eap_pax_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_pax_data *data = priv;
	struct eap_pax_hdr *resp;
	const u8 *pos;
	size_t len, mlen;
	u8 icvbuf[EAP_PAX_ICV_LEN], *icv;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PAX, respData, &len);
	if (pos == NULL || len < sizeof(*resp)) {
		wpa_printf(MSG_INFO, "EAP-PAX: Invalid frame");
		return TRUE;
	}

	mlen = sizeof(struct eap_hdr) + 1 + len;
	resp = (struct eap_pax_hdr *) pos;

	wpa_printf(MSG_DEBUG, "EAP-PAX: received frame: op_code 0x%x "
		   "flags 0x%x mac_id 0x%x dh_group_id 0x%x "
		   "public_key_id 0x%x",
		   resp->op_code, resp->flags, resp->mac_id, resp->dh_group_id,
		   resp->public_key_id);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: received payload",
		    (u8 *) (resp + 1), len - sizeof(*resp) - EAP_PAX_ICV_LEN);

	if (data->state == PAX_STD_1 &&
	    resp->op_code != EAP_PAX_OP_STD_2) {
		wpa_printf(MSG_DEBUG, "EAP-PAX: Expected PAX_STD-2 - "
			   "ignore op %d", resp->op_code);
		return TRUE;
	}

	if (data->state == PAX_STD_3 &&
	    resp->op_code != EAP_PAX_OP_ACK) {
		wpa_printf(MSG_DEBUG, "EAP-PAX: Expected PAX-ACK - "
			   "ignore op %d", resp->op_code);
		return TRUE;
	}

	if (resp->op_code != EAP_PAX_OP_STD_2 &&
	    resp->op_code != EAP_PAX_OP_ACK) {
		wpa_printf(MSG_DEBUG, "EAP-PAX: Unknown op_code 0x%x",
			   resp->op_code);
	}

	if (data->mac_id != resp->mac_id) {
		wpa_printf(MSG_DEBUG, "EAP-PAX: Expected MAC ID 0x%x, "
			   "received 0x%x", data->mac_id, resp->mac_id);
		return TRUE;
	}

	if (resp->dh_group_id != EAP_PAX_DH_GROUP_NONE) {
		wpa_printf(MSG_INFO, "EAP-PAX: Expected DH Group ID 0x%x, "
			   "received 0x%x", EAP_PAX_DH_GROUP_NONE,
			   resp->dh_group_id);
		return TRUE;
	}

	if (resp->public_key_id != EAP_PAX_PUBLIC_KEY_NONE) {
		wpa_printf(MSG_INFO, "EAP-PAX: Expected Public Key ID 0x%x, "
			   "received 0x%x", EAP_PAX_PUBLIC_KEY_NONE,
			   resp->public_key_id);
		return TRUE;
	}

	if (resp->flags & EAP_PAX_FLAGS_MF) {
		/* TODO: add support for reassembling fragments */
		wpa_printf(MSG_INFO, "EAP-PAX: fragmentation not supported");
		return TRUE;
	}

	if (resp->flags & EAP_PAX_FLAGS_CE) {
		wpa_printf(MSG_INFO, "EAP-PAX: Unexpected CE flag");
		return TRUE;
	}

	if (data->keys_set) {
		if (len - sizeof(*resp) < EAP_PAX_ICV_LEN) {
			wpa_printf(MSG_INFO, "EAP-PAX: No ICV in the packet");
			return TRUE;
		}
		icv = wpabuf_mhead_u8(respData) + mlen - EAP_PAX_ICV_LEN;
		wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: ICV", icv, EAP_PAX_ICV_LEN);
		eap_pax_mac(data->mac_id, data->ick, EAP_PAX_ICK_LEN,
			    wpabuf_mhead(respData),
			    wpabuf_len(respData) - EAP_PAX_ICV_LEN,
			    NULL, 0, NULL, 0, icvbuf);
		if (os_memcmp_const(icvbuf, icv, EAP_PAX_ICV_LEN) != 0) {
			wpa_printf(MSG_INFO, "EAP-PAX: Invalid ICV");
			wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: Expected ICV",
				    icvbuf, EAP_PAX_ICV_LEN);
			return TRUE;
		}
	}

	return FALSE;
}


static void eap_pax_process_std_2(struct eap_sm *sm,
				  struct eap_pax_data *data,
				  struct wpabuf *respData)
{
	struct eap_pax_hdr *resp;
	u8 mac[EAP_PAX_MAC_LEN], icvbuf[EAP_PAX_ICV_LEN];
	const u8 *pos;
	size_t len, left, cid_len;
	int i;

	if (data->state != PAX_STD_1)
		return;

	wpa_printf(MSG_DEBUG, "EAP-PAX: Received PAX_STD-2");

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PAX, respData, &len);
	if (pos == NULL || len < sizeof(*resp) + EAP_PAX_ICV_LEN)
		return;

	resp = (struct eap_pax_hdr *) pos;
	pos = (u8 *) (resp + 1);
	left = len - sizeof(*resp);

	if (left < 2 + EAP_PAX_RAND_LEN ||
	    WPA_GET_BE16(pos) != EAP_PAX_RAND_LEN) {
		wpa_printf(MSG_INFO, "EAP-PAX: Too short PAX_STD-2 (B)");
		return;
	}
	pos += 2;
	left -= 2;
	os_memcpy(data->rand.r.y, pos, EAP_PAX_RAND_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: Y (client rand)",
		    data->rand.r.y, EAP_PAX_RAND_LEN);
	pos += EAP_PAX_RAND_LEN;
	left -= EAP_PAX_RAND_LEN;

	if (left < 2 || (size_t) 2 + WPA_GET_BE16(pos) > left) {
		wpa_printf(MSG_INFO, "EAP-PAX: Too short PAX_STD-2 (CID)");
		return;
	}
	cid_len = WPA_GET_BE16(pos);
	if (cid_len > 1500) {
		wpa_printf(MSG_INFO, "EAP-PAX: Too long CID");
		return;
	}
	data->cid_len = cid_len;
	os_free(data->cid);
	data->cid = os_memdup(pos + 2, data->cid_len);
	if (data->cid == NULL) {
		wpa_printf(MSG_INFO, "EAP-PAX: Failed to allocate memory for "
			   "CID");
		return;
	}
	pos += 2 + data->cid_len;
	left -= 2 + data->cid_len;
	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-PAX: CID",
			  (u8 *) data->cid, data->cid_len);

	if (left < 2 + EAP_PAX_MAC_LEN ||
	    WPA_GET_BE16(pos) != EAP_PAX_MAC_LEN) {
		wpa_printf(MSG_INFO, "EAP-PAX: Too short PAX_STD-2 (MAC_CK)");
		return;
	}
	pos += 2;
	left -= 2;
	wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: MAC_CK(A, B, CID)",
		    pos, EAP_PAX_MAC_LEN);

	if (eap_user_get(sm, (u8 *) data->cid, data->cid_len, 0) < 0) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-PAX: unknown CID",
				  (u8 *) data->cid, data->cid_len);
		data->state = FAILURE;
		return;
	}

	for (i = 0;
	     i < EAP_MAX_METHODS &&
		     (sm->user->methods[i].vendor != EAP_VENDOR_IETF ||
		      sm->user->methods[i].method != EAP_TYPE_NONE);
	     i++) {
		if (sm->user->methods[i].vendor == EAP_VENDOR_IETF &&
		    sm->user->methods[i].method == EAP_TYPE_PAX)
			break;
	}

	if (i >= EAP_MAX_METHODS ||
	    sm->user->methods[i].vendor != EAP_VENDOR_IETF ||
	    sm->user->methods[i].method != EAP_TYPE_PAX) {
		wpa_hexdump_ascii(MSG_DEBUG,
				  "EAP-PAX: EAP-PAX not enabled for CID",
				  (u8 *) data->cid, data->cid_len);
		data->state = FAILURE;
		return;
	}

	if (sm->user->password == NULL ||
	    sm->user->password_len != EAP_PAX_AK_LEN) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-PAX: invalid password in "
				  "user database for CID",
				  (u8 *) data->cid, data->cid_len);
		data->state = FAILURE;
		return;
	}
	os_memcpy(data->ak, sm->user->password, EAP_PAX_AK_LEN);

	if (eap_pax_initial_key_derivation(data->mac_id, data->ak,
					   data->rand.e, data->mk, data->ck,
					   data->ick, data->mid) < 0) {
		wpa_printf(MSG_INFO, "EAP-PAX: Failed to complete initial "
			   "key derivation");
		data->state = FAILURE;
		return;
	}
	data->keys_set = 1;

	eap_pax_mac(data->mac_id, data->ck, EAP_PAX_CK_LEN,
		    data->rand.r.x, EAP_PAX_RAND_LEN,
		    data->rand.r.y, EAP_PAX_RAND_LEN,
		    (u8 *) data->cid, data->cid_len, mac);
	if (os_memcmp_const(mac, pos, EAP_PAX_MAC_LEN) != 0) {
		wpa_printf(MSG_INFO, "EAP-PAX: Invalid MAC_CK(A, B, CID) in "
			   "PAX_STD-2");
		wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: Expected MAC_CK(A, B, CID)",
			    mac, EAP_PAX_MAC_LEN);
		data->state = FAILURE;
		return;
	}

	pos += EAP_PAX_MAC_LEN;
	left -= EAP_PAX_MAC_LEN;

	if (left < EAP_PAX_ICV_LEN) {
		wpa_printf(MSG_INFO, "EAP-PAX: Too short ICV (%lu) in "
			   "PAX_STD-2", (unsigned long) left);
		return;
	}
	wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: ICV", pos, EAP_PAX_ICV_LEN);
	eap_pax_mac(data->mac_id, data->ick, EAP_PAX_ICK_LEN,
		    wpabuf_head(respData),
		    wpabuf_len(respData) - EAP_PAX_ICV_LEN, NULL, 0, NULL, 0,
		    icvbuf);
	if (os_memcmp_const(icvbuf, pos, EAP_PAX_ICV_LEN) != 0) {
		wpa_printf(MSG_INFO, "EAP-PAX: Invalid ICV in PAX_STD-2");
		wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: Expected ICV",
			    icvbuf, EAP_PAX_ICV_LEN);
		return;
	}
	pos += EAP_PAX_ICV_LEN;
	left -= EAP_PAX_ICV_LEN;

	if (left > 0) {
		wpa_hexdump(MSG_MSGDUMP, "EAP-PAX: ignored extra payload",
			    pos, left);
	}

	data->state = PAX_STD_3;
}


static void eap_pax_process_ack(struct eap_sm *sm,
				struct eap_pax_data *data,
				struct wpabuf *respData)
{
	if (data->state != PAX_STD_3)
		return;

	wpa_printf(MSG_DEBUG, "EAP-PAX: Received PAX-ACK - authentication "
		   "completed successfully");
	data->state = SUCCESS;
}


static void eap_pax_process(struct eap_sm *sm, void *priv,
			    struct wpabuf *respData)
{
	struct eap_pax_data *data = priv;
	struct eap_pax_hdr *resp;
	const u8 *pos;
	size_t len;

	if (sm->user == NULL || sm->user->password == NULL) {
		wpa_printf(MSG_INFO, "EAP-PAX: Plaintext password not "
			   "configured");
		data->state = FAILURE;
		return;
	}

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PAX, respData, &len);
	if (pos == NULL || len < sizeof(*resp))
		return;

	resp = (struct eap_pax_hdr *) pos;

	switch (resp->op_code) {
	case EAP_PAX_OP_STD_2:
		eap_pax_process_std_2(sm, data, respData);
		break;
	case EAP_PAX_OP_ACK:
		eap_pax_process_ack(sm, data, respData);
		break;
	}
}


static Boolean eap_pax_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_pax_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_pax_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pax_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_malloc(EAP_MSK_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_MSK_LEN;
	eap_pax_kdf(data->mac_id, data->mk, EAP_PAX_MK_LEN,
		    "Master Session Key", data->rand.e, 2 * EAP_PAX_RAND_LEN,
		    EAP_MSK_LEN, key);

	return key;
}


static u8 * eap_pax_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pax_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_malloc(EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_EMSK_LEN;
	eap_pax_kdf(data->mac_id, data->mk, EAP_PAX_MK_LEN,
		    "Extended Master Session Key",
		    data->rand.e, 2 * EAP_PAX_RAND_LEN,
		    EAP_EMSK_LEN, key);

	return key;
}


static Boolean eap_pax_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_pax_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_pax_get_session_id(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pax_data *data = priv;
	u8 *sid;

	if (data->state != SUCCESS)
		return NULL;

	sid = os_malloc(1 + EAP_PAX_MID_LEN);
	if (sid == NULL)
		return NULL;

	*len = 1 + EAP_PAX_MID_LEN;
	sid[0] = EAP_TYPE_PAX;
	os_memcpy(sid + 1, data->mid, EAP_PAX_MID_LEN);

	return sid;
}


int eap_server_pax_register(void)
{
	struct eap_method *eap;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_PAX, "PAX");
	if (eap == NULL)
		return -1;

	eap->init = eap_pax_init;
	eap->reset = eap_pax_reset;
	eap->buildReq = eap_pax_buildReq;
	eap->check = eap_pax_check;
	eap->process = eap_pax_process;
	eap->isDone = eap_pax_isDone;
	eap->getKey = eap_pax_getKey;
	eap->isSuccess = eap_pax_isSuccess;
	eap->get_emsk = eap_pax_get_emsk;
	eap->getSessionId = eap_pax_get_session_id;

	return eap_server_method_register(eap);
}
