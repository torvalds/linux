/*
 * EAP peer method: EAP-PSK (RFC 4764)
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * Note: EAP-PSK is an EAP authentication method and as such, completely
 * different from WPA-PSK. This file is not needed for WPA-PSK functionality.
 */

#include "includes.h"

#include "common.h"
#include "crypto/aes_wrap.h"
#include "crypto/random.h"
#include "eap_common/eap_psk_common.h"
#include "eap_i.h"


struct eap_psk_data {
	enum { PSK_INIT, PSK_MAC_SENT, PSK_DONE } state;
	u8 rand_p[EAP_PSK_RAND_LEN];
	u8 ak[EAP_PSK_AK_LEN], kdk[EAP_PSK_KDK_LEN], tek[EAP_PSK_TEK_LEN];
	u8 *id_s, *id_p;
	size_t id_s_len, id_p_len;
	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];
};


static void * eap_psk_init(struct eap_sm *sm)
{
	struct eap_psk_data *data;
	const u8 *identity, *password;
	size_t identity_len, password_len;

	password = eap_get_config_password(sm, &password_len);
	if (!password || password_len != 16) {
		wpa_printf(MSG_INFO, "EAP-PSK: 16-octet pre-shared key not "
			   "configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	if (eap_psk_key_setup(password, data->ak, data->kdk)) {
		os_free(data);
		return NULL;
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: AK", data->ak, EAP_PSK_AK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: KDK", data->kdk, EAP_PSK_KDK_LEN);
	data->state = PSK_INIT;

	identity = eap_get_config_identity(sm, &identity_len);
	if (identity) {
		data->id_p = os_malloc(identity_len);
		if (data->id_p)
			os_memcpy(data->id_p, identity, identity_len);
		data->id_p_len = identity_len;
	}
	if (data->id_p == NULL) {
		wpa_printf(MSG_INFO, "EAP-PSK: could not get own identity");
		os_free(data);
		return NULL;
	}

	return data;
}


static void eap_psk_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_psk_data *data = priv;
	os_free(data->id_s);
	os_free(data->id_p);
	os_free(data);
}


static struct wpabuf * eap_psk_process_1(struct eap_psk_data *data,
					 struct eap_method_ret *ret,
					 const struct wpabuf *reqData)
{
	const struct eap_psk_hdr_1 *hdr1;
	struct eap_psk_hdr_2 *hdr2;
	struct wpabuf *resp;
	u8 *buf, *pos;
	size_t buflen, len;
	const u8 *cpos;

	wpa_printf(MSG_DEBUG, "EAP-PSK: in INIT state");

	cpos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PSK, reqData, &len);
	hdr1 = (const struct eap_psk_hdr_1 *) cpos;
	if (cpos == NULL || len < sizeof(*hdr1)) {
		wpa_printf(MSG_INFO, "EAP-PSK: Invalid first message "
			   "length (%lu; expected %lu or more)",
			   (unsigned long) len,
			   (unsigned long) sizeof(*hdr1));
		ret->ignore = TRUE;
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "EAP-PSK: Flags=0x%x", hdr1->flags);
	if (EAP_PSK_FLAGS_GET_T(hdr1->flags) != 0) {
		wpa_printf(MSG_INFO, "EAP-PSK: Unexpected T=%d (expected 0)",
			   EAP_PSK_FLAGS_GET_T(hdr1->flags));
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: RAND_S", hdr1->rand_s,
		    EAP_PSK_RAND_LEN);
	os_free(data->id_s);
	data->id_s_len = len - sizeof(*hdr1);
	data->id_s = os_malloc(data->id_s_len);
	if (data->id_s == NULL) {
		wpa_printf(MSG_ERROR, "EAP-PSK: Failed to allocate memory for "
			   "ID_S (len=%lu)", (unsigned long) data->id_s_len);
		ret->ignore = TRUE;
		return NULL;
	}
	os_memcpy(data->id_s, (u8 *) (hdr1 + 1), data->id_s_len);
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-PSK: ID_S",
			  data->id_s, data->id_s_len);

	if (random_get_bytes(data->rand_p, EAP_PSK_RAND_LEN)) {
		wpa_printf(MSG_ERROR, "EAP-PSK: Failed to get random data");
		ret->ignore = TRUE;
		return NULL;
	}

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PSK,
			     sizeof(*hdr2) + data->id_p_len, EAP_CODE_RESPONSE,
			     eap_get_id(reqData));
	if (resp == NULL)
		return NULL;
	hdr2 = wpabuf_put(resp, sizeof(*hdr2));
	hdr2->flags = EAP_PSK_FLAGS_SET_T(1); /* T=1 */
	os_memcpy(hdr2->rand_s, hdr1->rand_s, EAP_PSK_RAND_LEN);
	os_memcpy(hdr2->rand_p, data->rand_p, EAP_PSK_RAND_LEN);
	wpabuf_put_data(resp, data->id_p, data->id_p_len);
	/* MAC_P = OMAC1-AES-128(AK, ID_P||ID_S||RAND_S||RAND_P) */
	buflen = data->id_p_len + data->id_s_len + 2 * EAP_PSK_RAND_LEN;
	buf = os_malloc(buflen);
	if (buf == NULL) {
		wpabuf_free(resp);
		return NULL;
	}
	os_memcpy(buf, data->id_p, data->id_p_len);
	pos = buf + data->id_p_len;
	os_memcpy(pos, data->id_s, data->id_s_len);
	pos += data->id_s_len;
	os_memcpy(pos, hdr1->rand_s, EAP_PSK_RAND_LEN);
	pos += EAP_PSK_RAND_LEN;
	os_memcpy(pos, data->rand_p, EAP_PSK_RAND_LEN);
	if (omac1_aes_128(data->ak, buf, buflen, hdr2->mac_p)) {
		os_free(buf);
		wpabuf_free(resp);
		return NULL;
	}
	os_free(buf);
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: RAND_P", hdr2->rand_p,
		    EAP_PSK_RAND_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: MAC_P", hdr2->mac_p, EAP_PSK_MAC_LEN);
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-PSK: ID_P",
			  data->id_p, data->id_p_len);

	data->state = PSK_MAC_SENT;

	return resp;
}


static struct wpabuf * eap_psk_process_3(struct eap_psk_data *data,
					 struct eap_method_ret *ret,
					 const struct wpabuf *reqData)
{
	const struct eap_psk_hdr_3 *hdr3;
	struct eap_psk_hdr_4 *hdr4;
	struct wpabuf *resp;
	u8 *buf, *rpchannel, nonce[16], *decrypted;
	const u8 *pchannel, *tag, *msg;
	u8 mac[EAP_PSK_MAC_LEN];
	size_t buflen, left, data_len, len, plen;
	int failed = 0;
	const u8 *pos;

	wpa_printf(MSG_DEBUG, "EAP-PSK: in MAC_SENT state");

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PSK,
			       reqData, &len);
	hdr3 = (const struct eap_psk_hdr_3 *) pos;
	if (pos == NULL || len < sizeof(*hdr3)) {
		wpa_printf(MSG_INFO, "EAP-PSK: Invalid third message "
			   "length (%lu; expected %lu or more)",
			   (unsigned long) len,
			   (unsigned long) sizeof(*hdr3));
		ret->ignore = TRUE;
		return NULL;
	}
	left = len - sizeof(*hdr3);
	pchannel = (const u8 *) (hdr3 + 1);
	wpa_printf(MSG_DEBUG, "EAP-PSK: Flags=0x%x", hdr3->flags);
	if (EAP_PSK_FLAGS_GET_T(hdr3->flags) != 2) {
		wpa_printf(MSG_INFO, "EAP-PSK: Unexpected T=%d (expected 2)",
			   EAP_PSK_FLAGS_GET_T(hdr3->flags));
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: RAND_S", hdr3->rand_s,
		    EAP_PSK_RAND_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: MAC_S", hdr3->mac_s, EAP_PSK_MAC_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: PCHANNEL", pchannel, left);

	if (left < 4 + 16 + 1) {
		wpa_printf(MSG_INFO, "EAP-PSK: Too short PCHANNEL data in "
			   "third message (len=%lu, expected 21)",
			   (unsigned long) left);
		ret->ignore = TRUE;
		return NULL;
	}

	/* MAC_S = OMAC1-AES-128(AK, ID_S||RAND_P) */
	buflen = data->id_s_len + EAP_PSK_RAND_LEN;
	buf = os_malloc(buflen);
	if (buf == NULL)
		return NULL;
	os_memcpy(buf, data->id_s, data->id_s_len);
	os_memcpy(buf + data->id_s_len, data->rand_p, EAP_PSK_RAND_LEN);
	if (omac1_aes_128(data->ak, buf, buflen, mac)) {
		os_free(buf);
		return NULL;
	}
	os_free(buf);
	if (os_memcmp(mac, hdr3->mac_s, EAP_PSK_MAC_LEN) != 0) {
		wpa_printf(MSG_WARNING, "EAP-PSK: Invalid MAC_S in third "
			   "message");
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "EAP-PSK: MAC_S verified successfully");

	if (eap_psk_derive_keys(data->kdk, data->rand_p, data->tek,
				data->msk, data->emsk)) {
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		return NULL;
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: TEK", data->tek, EAP_PSK_TEK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: MSK", data->msk, EAP_MSK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PSK: EMSK", data->emsk, EAP_EMSK_LEN);

	os_memset(nonce, 0, 12);
	os_memcpy(nonce + 12, pchannel, 4);
	pchannel += 4;
	left -= 4;

	tag = pchannel;
	pchannel += 16;
	left -= 16;

	msg = pchannel;

	wpa_hexdump(MSG_MSGDUMP, "EAP-PSK: PCHANNEL - nonce",
		    nonce, sizeof(nonce));
	wpa_hexdump(MSG_MSGDUMP, "EAP-PSK: PCHANNEL - hdr",
		    wpabuf_head(reqData), 5);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PSK: PCHANNEL - cipher msg", msg, left);

	decrypted = os_malloc(left);
	if (decrypted == NULL) {
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		return NULL;
	}
	os_memcpy(decrypted, msg, left);

	if (aes_128_eax_decrypt(data->tek, nonce, sizeof(nonce),
				wpabuf_head(reqData),
				sizeof(struct eap_hdr) + 1 +
				sizeof(*hdr3) - EAP_PSK_MAC_LEN, decrypted,
				left, tag)) {
		wpa_printf(MSG_WARNING, "EAP-PSK: PCHANNEL decryption failed");
		os_free(decrypted);
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: Decrypted PCHANNEL message",
		    decrypted, left);

	/* Verify R flag */
	switch (decrypted[0] >> 6) {
	case EAP_PSK_R_FLAG_CONT:
		wpa_printf(MSG_DEBUG, "EAP-PSK: R flag - CONT - unsupported");
		failed = 1;
		break;
	case EAP_PSK_R_FLAG_DONE_SUCCESS:
		wpa_printf(MSG_DEBUG, "EAP-PSK: R flag - DONE_SUCCESS");
		break;
	case EAP_PSK_R_FLAG_DONE_FAILURE:
		wpa_printf(MSG_DEBUG, "EAP-PSK: R flag - DONE_FAILURE");
		wpa_printf(MSG_INFO, "EAP-PSK: Authentication server rejected "
			   "authentication");
		failed = 1;
		break;
	}

	data_len = 1;
	if ((decrypted[0] & EAP_PSK_E_FLAG) && left > 1)
		data_len++;
	plen = sizeof(*hdr4) + 4 + 16 + data_len;
	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PSK, plen,
			     EAP_CODE_RESPONSE, eap_get_id(reqData));
	if (resp == NULL) {
		os_free(decrypted);
		return NULL;
	}
	hdr4 = wpabuf_put(resp, sizeof(*hdr4));
	hdr4->flags = EAP_PSK_FLAGS_SET_T(3); /* T=3 */
	os_memcpy(hdr4->rand_s, hdr3->rand_s, EAP_PSK_RAND_LEN);
	rpchannel = wpabuf_put(resp, 4 + 16 + data_len);

	/* nonce++ */
	inc_byte_array(nonce, sizeof(nonce));
	os_memcpy(rpchannel, nonce + 12, 4);

	if (decrypted[0] & EAP_PSK_E_FLAG) {
		wpa_printf(MSG_DEBUG, "EAP-PSK: Unsupported E (Ext) flag");
		failed = 1;
		rpchannel[4 + 16] = (EAP_PSK_R_FLAG_DONE_FAILURE << 6) |
			EAP_PSK_E_FLAG;
		if (left > 1) {
			/* Add empty EXT_Payload with same EXT_Type */
			rpchannel[4 + 16 + 1] = decrypted[1];
		}
	} else if (failed)
		rpchannel[4 + 16] = EAP_PSK_R_FLAG_DONE_FAILURE << 6;
	else
		rpchannel[4 + 16] = EAP_PSK_R_FLAG_DONE_SUCCESS << 6;

	wpa_hexdump(MSG_DEBUG, "EAP-PSK: reply message (plaintext)",
		    rpchannel + 4 + 16, data_len);
	if (aes_128_eax_encrypt(data->tek, nonce, sizeof(nonce),
				wpabuf_head(resp),
				sizeof(struct eap_hdr) + 1 + sizeof(*hdr4),
				rpchannel + 4 + 16, data_len, rpchannel + 4)) {
		os_free(decrypted);
		wpabuf_free(resp);
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-PSK: reply message (PCHANNEL)",
		    rpchannel, 4 + 16 + data_len);

	wpa_printf(MSG_DEBUG, "EAP-PSK: Completed %ssuccessfully",
		   failed ? "un" : "");
	data->state = PSK_DONE;
	ret->methodState = METHOD_DONE;
	ret->decision = failed ? DECISION_FAIL : DECISION_UNCOND_SUCC;

	os_free(decrypted);

	return resp;
}


static struct wpabuf * eap_psk_process(struct eap_sm *sm, void *priv,
				       struct eap_method_ret *ret,
				       const struct wpabuf *reqData)
{
	struct eap_psk_data *data = priv;
	const u8 *pos;
	struct wpabuf *resp = NULL;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PSK, reqData, &len);
	if (pos == NULL) {
		ret->ignore = TRUE;
		return NULL;
	}

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = TRUE;

	switch (data->state) {
	case PSK_INIT:
		resp = eap_psk_process_1(data, ret, reqData);
		break;
	case PSK_MAC_SENT:
		resp = eap_psk_process_3(data, ret, reqData);
		break;
	case PSK_DONE:
		wpa_printf(MSG_DEBUG, "EAP-PSK: in DONE state - ignore "
			   "unexpected message");
		ret->ignore = TRUE;
		return NULL;
	}

	if (ret->methodState == METHOD_DONE) {
		ret->allowNotifications = FALSE;
	}

	return resp;
}


static Boolean eap_psk_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_psk_data *data = priv;
	return data->state == PSK_DONE;
}


static u8 * eap_psk_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_psk_data *data = priv;
	u8 *key;

	if (data->state != PSK_DONE)
		return NULL;

	key = os_malloc(EAP_MSK_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_MSK_LEN;
	os_memcpy(key, data->msk, EAP_MSK_LEN);

	return key;
}


static u8 * eap_psk_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_psk_data *data = priv;
	u8 *key;

	if (data->state != PSK_DONE)
		return NULL;

	key = os_malloc(EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_EMSK_LEN;
	os_memcpy(key, data->emsk, EAP_EMSK_LEN);

	return key;
}


int eap_peer_psk_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_PSK, "PSK");
	if (eap == NULL)
		return -1;

	eap->init = eap_psk_init;
	eap->deinit = eap_psk_deinit;
	eap->process = eap_psk_process;
	eap->isKeyAvailable = eap_psk_isKeyAvailable;
	eap->getKey = eap_psk_getKey;
	eap->get_emsk = eap_psk_get_emsk;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
