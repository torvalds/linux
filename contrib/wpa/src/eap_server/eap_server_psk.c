/*
 * hostapd / EAP-PSK (RFC 4764) server
 * Copyright (c) 2005-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * Note: EAP-PSK is an EAP authentication method and as such, completely
 * different from WPA-PSK. This file is not needed for WPA-PSK functionality.
 */

#include "includes.h"

#include "common.h"
#include "crypto/aes_wrap.h"
#include "crypto/random.h"
#include "eap_common/eap_psk_common.h"
#include "eap_server/eap_i.h"


struct eap_psk_data {
	enum { PSK_1, PSK_3, SUCCESS, FAILURE } state;
	u8 rand_s[EAP_PSK_RAND_LEN];
	u8 rand_p[EAP_PSK_RAND_LEN];
	u8 *id_p;
	size_t id_p_len;
	u8 ak[EAP_PSK_AK_LEN], kdk[EAP_PSK_KDK_LEN], tek[EAP_PSK_TEK_LEN];
	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];
};


static void * eap_psk_init(struct eap_sm *sm)
{
	struct eap_psk_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = PSK_1;

	return data;
}


static void eap_psk_reset(struct eap_sm *sm, void *priv)
{
	struct eap_psk_data *data = priv;
	os_free(data->id_p);
	bin_clear_free(data, sizeof(*data));
}


static struct wpabuf * eap_psk_build_1(struct eap_sm *sm,
				       struct eap_psk_data *data, u8 id)
{
	struct wpabuf *req;
	struct eap_psk_hdr_1 *psk;

	wpa_printf(MSG_DEBUG, "EAP-PSK: PSK-1 (sending)");

	if (random_get_bytes(data->rand_s, EAP_PSK_RAND_LEN)) {
		wpa_printf(MSG_ERROR, "EAP-PSK: Failed to get random data");
		data->state = FAILURE;
		return NULL;
	}
	wpa_hexdump(MSG_MSGDUMP, "EAP-PSK: RAND_S (server rand)",
		    data->rand_s, EAP_PSK_RAND_LEN);

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PSK,
			    sizeof(*psk) + sm->server_id_len,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-PSK: Failed to allocate memory "
			   "request");
		data->state = FAILURE;
		return NULL;
	}

	psk = wpabuf_put(req, sizeof(*psk));
	psk->flags = EAP_PSK_FLAGS_SET_T(0); /* T=0 */
	os_memcpy(psk->rand_s, data->rand_s, EAP_PSK_RAND_LEN);
	wpabuf_put_data(req, sm->server_id, sm->server_id_len);

	return req;
}


static struct wpabuf * eap_psk_build_3(struct eap_sm *sm,
				       struct eap_psk_data *data, u8 id)
{
	struct wpabuf *req;
	struct eap_psk_hdr_3 *psk;
	u8 *buf, *pchannel, nonce[16];
	size_t buflen;

	wpa_printf(MSG_DEBUG, "EAP-PSK: PSK-3 (sending)");

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PSK,
			    sizeof(*psk) + 4 + 16 + 1, EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-PSK: Failed to allocate memory "
			   "request");
		data->state = FAILURE;
		return NULL;
	}

	psk = wpabuf_put(req, sizeof(*psk));
	psk->flags = EAP_PSK_FLAGS_SET_T(2); /* T=2 */
	os_memcpy(psk->rand_s, data->rand_s, EAP_PSK_RAND_LEN);

	/* MAC_S = OMAC1-AES-128(AK, ID_S||RAND_P) */
	buflen = sm->server_id_len + EAP_PSK_RAND_LEN;
	buf = os_malloc(buflen);
	if (buf == NULL)
		goto fail;

	os_memcpy(buf, sm->server_id, sm->server_id_len);
	os_memcpy(buf + sm->server_id_len, data->rand_p, EAP_PSK_RAND_LEN);
	if (omac1_aes_128(data->ak, buf, buflen, psk->mac_s)) {
		os_free(buf);
		goto fail;
	}
	os_free(buf);

	if (eap_psk_derive_keys(data->kdk, data->rand_p, data->tek, data->msk,
				data->emsk))
		goto fail;
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: TEK", data->tek, EAP_PSK_TEK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: MSK", data->msk, EAP_MSK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: EMSK", data->emsk, EAP_EMSK_LEN);

	os_memset(nonce, 0, sizeof(nonce));
	pchannel = wpabuf_put(req, 4 + 16 + 1);
	os_memcpy(pchannel, nonce + 12, 4);
	os_memset(pchannel + 4, 0, 16); /* Tag */
	pchannel[4 + 16] = EAP_PSK_R_FLAG_DONE_SUCCESS << 6;
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: PCHANNEL (plaintext)",
		    pchannel, 4 + 16 + 1);
	if (aes_128_eax_encrypt(data->tek, nonce, sizeof(nonce),
				wpabuf_head(req), 22,
				pchannel + 4 + 16, 1, pchannel + 4))
		goto fail;
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: PCHANNEL (encrypted)",
		    pchannel, 4 + 16 + 1);

	return req;

fail:
	wpabuf_free(req);
	data->state = FAILURE;
	return NULL;
}


static struct wpabuf * eap_psk_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_psk_data *data = priv;

	switch (data->state) {
	case PSK_1:
		return eap_psk_build_1(sm, data, id);
	case PSK_3:
		return eap_psk_build_3(sm, data, id);
	default:
		wpa_printf(MSG_DEBUG, "EAP-PSK: Unknown state %d in buildReq",
			   data->state);
		break;
	}
	return NULL;
}


static Boolean eap_psk_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_psk_data *data = priv;
	size_t len;
	u8 t;
	const u8 *pos;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PSK, respData, &len);
	if (pos == NULL || len < 1) {
		wpa_printf(MSG_INFO, "EAP-PSK: Invalid frame");
		return TRUE;
	}
	t = EAP_PSK_FLAGS_GET_T(*pos);

	wpa_printf(MSG_DEBUG, "EAP-PSK: received frame: T=%d", t);

	if (data->state == PSK_1 && t != 1) {
		wpa_printf(MSG_DEBUG, "EAP-PSK: Expected PSK-2 - "
			   "ignore T=%d", t);
		return TRUE;
	}

	if (data->state == PSK_3 && t != 3) {
		wpa_printf(MSG_DEBUG, "EAP-PSK: Expected PSK-4 - "
			   "ignore T=%d", t);
		return TRUE;
	}

	if ((t == 1 && len < sizeof(struct eap_psk_hdr_2)) ||
	    (t == 3 && len < sizeof(struct eap_psk_hdr_4))) {
		wpa_printf(MSG_DEBUG, "EAP-PSK: Too short frame");
		return TRUE;
	}

	return FALSE;
}


static void eap_psk_process_2(struct eap_sm *sm,
			      struct eap_psk_data *data,
			      struct wpabuf *respData)
{
	const struct eap_psk_hdr_2 *resp;
	u8 *pos, mac[EAP_PSK_MAC_LEN], *buf;
	size_t left, buflen;
	int i;
	const u8 *cpos;

	if (data->state != PSK_1)
		return;

	wpa_printf(MSG_DEBUG, "EAP-PSK: Received PSK-2");

	cpos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PSK, respData,
				&left);
	if (cpos == NULL || left < sizeof(*resp)) {
		wpa_printf(MSG_INFO, "EAP-PSK: Invalid frame");
		return;
	}
	resp = (const struct eap_psk_hdr_2 *) cpos;
	cpos = (const u8 *) (resp + 1);
	left -= sizeof(*resp);

	os_free(data->id_p);
	data->id_p = os_memdup(cpos, left);
	if (data->id_p == NULL) {
		wpa_printf(MSG_INFO, "EAP-PSK: Failed to allocate memory for "
			   "ID_P");
		return;
	}
	data->id_p_len = left;
	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-PSK: ID_P",
			  data->id_p, data->id_p_len);

	if (eap_user_get(sm, data->id_p, data->id_p_len, 0) < 0) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-PSK: unknown ID_P",
				  data->id_p, data->id_p_len);
		data->state = FAILURE;
		return;
	}

	for (i = 0;
	     i < EAP_MAX_METHODS &&
		     (sm->user->methods[i].vendor != EAP_VENDOR_IETF ||
		      sm->user->methods[i].method != EAP_TYPE_NONE);
	     i++) {
		if (sm->user->methods[i].vendor == EAP_VENDOR_IETF &&
		    sm->user->methods[i].method == EAP_TYPE_PSK)
			break;
	}

	if (i >= EAP_MAX_METHODS ||
	    sm->user->methods[i].vendor != EAP_VENDOR_IETF ||
	    sm->user->methods[i].method != EAP_TYPE_PSK) {
		wpa_hexdump_ascii(MSG_DEBUG,
				  "EAP-PSK: EAP-PSK not enabled for ID_P",
				  data->id_p, data->id_p_len);
		data->state = FAILURE;
		return;
	}

	if (sm->user->password == NULL ||
	    sm->user->password_len != EAP_PSK_PSK_LEN) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-PSK: invalid password in "
				  "user database for ID_P",
				  data->id_p, data->id_p_len);
		data->state = FAILURE;
		return;
	}
	if (eap_psk_key_setup(sm->user->password, data->ak, data->kdk)) {
		data->state = FAILURE;
		return;
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: AK", data->ak, EAP_PSK_AK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: KDK", data->kdk, EAP_PSK_KDK_LEN);

	wpa_hexdump(MSG_MSGDUMP, "EAP-PSK: RAND_P (client rand)",
		    resp->rand_p, EAP_PSK_RAND_LEN);
	os_memcpy(data->rand_p, resp->rand_p, EAP_PSK_RAND_LEN);

	/* MAC_P = OMAC1-AES-128(AK, ID_P||ID_S||RAND_S||RAND_P) */
	buflen = data->id_p_len + sm->server_id_len + 2 * EAP_PSK_RAND_LEN;
	buf = os_malloc(buflen);
	if (buf == NULL) {
		data->state = FAILURE;
		return;
	}
	os_memcpy(buf, data->id_p, data->id_p_len);
	pos = buf + data->id_p_len;
	os_memcpy(pos, sm->server_id, sm->server_id_len);
	pos += sm->server_id_len;
	os_memcpy(pos, data->rand_s, EAP_PSK_RAND_LEN);
	pos += EAP_PSK_RAND_LEN;
	os_memcpy(pos, data->rand_p, EAP_PSK_RAND_LEN);
	if (omac1_aes_128(data->ak, buf, buflen, mac)) {
		os_free(buf);
		data->state = FAILURE;
		return;
	}
	os_free(buf);
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: MAC_P", resp->mac_p, EAP_PSK_MAC_LEN);
	if (os_memcmp_const(mac, resp->mac_p, EAP_PSK_MAC_LEN) != 0) {
		wpa_printf(MSG_INFO, "EAP-PSK: Invalid MAC_P");
		wpa_hexdump(MSG_MSGDUMP, "EAP-PSK: Expected MAC_P",
			    mac, EAP_PSK_MAC_LEN);
		data->state = FAILURE;
		return;
	}

	data->state = PSK_3;
}


static void eap_psk_process_4(struct eap_sm *sm,
			      struct eap_psk_data *data,
			      struct wpabuf *respData)
{
	const struct eap_psk_hdr_4 *resp;
	u8 *decrypted, nonce[16];
	size_t left;
	const u8 *pos, *tag;

	if (data->state != PSK_3)
		return;

	wpa_printf(MSG_DEBUG, "EAP-PSK: Received PSK-4");

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PSK, respData, &left);
	if (pos == NULL || left < sizeof(*resp)) {
		wpa_printf(MSG_INFO, "EAP-PSK: Invalid frame");
		return;
	}
	resp = (const struct eap_psk_hdr_4 *) pos;
	pos = (const u8 *) (resp + 1);
	left -= sizeof(*resp);

	wpa_hexdump(MSG_MSGDUMP, "EAP-PSK: Encrypted PCHANNEL", pos, left);

	if (left < 4 + 16 + 1) {
		wpa_printf(MSG_INFO, "EAP-PSK: Too short PCHANNEL data in "
			   "PSK-4 (len=%lu, expected 21)",
			   (unsigned long) left);
		return;
	}

	if (pos[0] == 0 && pos[1] == 0 && pos[2] == 0 && pos[3] == 0) {
		wpa_printf(MSG_DEBUG, "EAP-PSK: Nonce did not increase");
		return;
	}

	os_memset(nonce, 0, 12);
	os_memcpy(nonce + 12, pos, 4);
	pos += 4;
	left -= 4;
	tag = pos;
	pos += 16;
	left -= 16;

	decrypted = os_memdup(pos, left);
	if (decrypted == NULL)
		return;

	if (aes_128_eax_decrypt(data->tek, nonce, sizeof(nonce),
				wpabuf_head(respData), 22, decrypted, left,
				tag)) {
		wpa_printf(MSG_WARNING, "EAP-PSK: PCHANNEL decryption failed");
		os_free(decrypted);
		data->state = FAILURE;
		return;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: Decrypted PCHANNEL message",
		    decrypted, left);

	/* Verify R flag */
	switch (decrypted[0] >> 6) {
	case EAP_PSK_R_FLAG_CONT:
		wpa_printf(MSG_DEBUG, "EAP-PSK: R flag - CONT - unsupported");
		data->state = FAILURE;
		break;
	case EAP_PSK_R_FLAG_DONE_SUCCESS:
		wpa_printf(MSG_DEBUG, "EAP-PSK: R flag - DONE_SUCCESS");
		data->state = SUCCESS;
		break;
	case EAP_PSK_R_FLAG_DONE_FAILURE:
		wpa_printf(MSG_DEBUG, "EAP-PSK: R flag - DONE_FAILURE");
		data->state = FAILURE;
		break;
	}
	os_free(decrypted);
}


static void eap_psk_process(struct eap_sm *sm, void *priv,
			    struct wpabuf *respData)
{
	struct eap_psk_data *data = priv;
	const u8 *pos;
	size_t len;

	if (sm->user == NULL || sm->user->password == NULL) {
		wpa_printf(MSG_INFO, "EAP-PSK: Plaintext password not "
			   "configured");
		data->state = FAILURE;
		return;
	}

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PSK, respData, &len);
	if (pos == NULL || len < 1)
		return;

	switch (EAP_PSK_FLAGS_GET_T(*pos)) {
	case 1:
		eap_psk_process_2(sm, data, respData);
		break;
	case 3:
		eap_psk_process_4(sm, data, respData);
		break;
	}
}


static Boolean eap_psk_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_psk_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_psk_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_psk_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->msk, EAP_MSK_LEN);
	if (key == NULL)
		return NULL;
	*len = EAP_MSK_LEN;

	return key;
}


static u8 * eap_psk_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_psk_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->emsk, EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;
	*len = EAP_EMSK_LEN;

	return key;
}


static Boolean eap_psk_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_psk_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_psk_get_session_id(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_psk_data *data = priv;
	u8 *id;

	if (data->state != SUCCESS)
		return NULL;

	*len = 1 + 2 * EAP_PSK_RAND_LEN;
	id = os_malloc(*len);
	if (id == NULL)
		return NULL;

	id[0] = EAP_TYPE_PSK;
	os_memcpy(id + 1, data->rand_p, EAP_PSK_RAND_LEN);
	os_memcpy(id + 1 + EAP_PSK_RAND_LEN, data->rand_s, EAP_PSK_RAND_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: Derived Session-Id", id, *len);

	return id;
}


int eap_server_psk_register(void)
{
	struct eap_method *eap;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_PSK, "PSK");
	if (eap == NULL)
		return -1;

	eap->init = eap_psk_init;
	eap->reset = eap_psk_reset;
	eap->buildReq = eap_psk_buildReq;
	eap->check = eap_psk_check;
	eap->process = eap_psk_process;
	eap->isDone = eap_psk_isDone;
	eap->getKey = eap_psk_getKey;
	eap->isSuccess = eap_psk_isSuccess;
	eap->get_emsk = eap_psk_get_emsk;
	eap->getSessionId = eap_psk_get_session_id;

	return eap_server_method_register(eap);
}
