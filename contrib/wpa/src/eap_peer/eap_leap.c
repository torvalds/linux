/*
 * EAP peer method: LEAP
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/ms_funcs.h"
#include "crypto/crypto.h"
#include "crypto/random.h"
#include "eap_i.h"

#define LEAP_VERSION 1
#define LEAP_CHALLENGE_LEN 8
#define LEAP_RESPONSE_LEN 24
#define LEAP_KEY_LEN 16


struct eap_leap_data {
	enum {
		LEAP_WAIT_CHALLENGE,
		LEAP_WAIT_SUCCESS,
		LEAP_WAIT_RESPONSE,
		LEAP_DONE
	} state;

	u8 peer_challenge[LEAP_CHALLENGE_LEN];
	u8 peer_response[LEAP_RESPONSE_LEN];

	u8 ap_challenge[LEAP_CHALLENGE_LEN];
	u8 ap_response[LEAP_RESPONSE_LEN];
};


static void * eap_leap_init(struct eap_sm *sm)
{
	struct eap_leap_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = LEAP_WAIT_CHALLENGE;

	sm->leap_done = FALSE;
	return data;
}


static void eap_leap_deinit(struct eap_sm *sm, void *priv)
{
	os_free(priv);
}


static struct wpabuf * eap_leap_process_request(struct eap_sm *sm, void *priv,
						struct eap_method_ret *ret,
						const struct wpabuf *reqData)
{
	struct eap_leap_data *data = priv;
	struct wpabuf *resp;
	const u8 *pos, *challenge, *identity, *password;
	u8 challenge_len, *rpos;
	size_t identity_len, password_len, len;
	int pwhash;

	wpa_printf(MSG_DEBUG, "EAP-LEAP: Processing EAP-Request");

	identity = eap_get_config_identity(sm, &identity_len);
	password = eap_get_config_password2(sm, &password_len, &pwhash);
	if (identity == NULL || password == NULL)
		return NULL;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_LEAP, reqData, &len);
	if (pos == NULL || len < 3) {
		wpa_printf(MSG_INFO, "EAP-LEAP: Invalid EAP-Request frame");
		ret->ignore = TRUE;
		return NULL;
	}

	if (*pos != LEAP_VERSION) {
		wpa_printf(MSG_WARNING, "EAP-LEAP: Unsupported LEAP version "
			   "%d", *pos);
		ret->ignore = TRUE;
		return NULL;
	}
	pos++;

	pos++; /* skip unused byte */

	challenge_len = *pos++;
	if (challenge_len != LEAP_CHALLENGE_LEN || challenge_len > len - 3) {
		wpa_printf(MSG_INFO, "EAP-LEAP: Invalid challenge "
			   "(challenge_len=%d reqDataLen=%lu)",
			   challenge_len, (unsigned long) wpabuf_len(reqData));
		ret->ignore = TRUE;
		return NULL;
	}
	challenge = pos;
	os_memcpy(data->peer_challenge, challenge, LEAP_CHALLENGE_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-LEAP: Challenge from AP",
		    challenge, LEAP_CHALLENGE_LEN);

	wpa_printf(MSG_DEBUG, "EAP-LEAP: Generating Challenge Response");

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_LEAP,
			     3 + LEAP_RESPONSE_LEN + identity_len,
			     EAP_CODE_RESPONSE, eap_get_id(reqData));
	if (resp == NULL)
		return NULL;
	wpabuf_put_u8(resp, LEAP_VERSION);
	wpabuf_put_u8(resp, 0); /* unused */
	wpabuf_put_u8(resp, LEAP_RESPONSE_LEN);
	rpos = wpabuf_put(resp, LEAP_RESPONSE_LEN);
	if ((pwhash && challenge_response(challenge, password, rpos)) ||
	    (!pwhash &&
	     nt_challenge_response(challenge, password, password_len, rpos))) {
		wpa_printf(MSG_DEBUG, "EAP-LEAP: Failed to derive response");
		ret->ignore = TRUE;
		wpabuf_free(resp);
		return NULL;
	}
	os_memcpy(data->peer_response, rpos, LEAP_RESPONSE_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-LEAP: Response",
		    rpos, LEAP_RESPONSE_LEN);
	wpabuf_put_data(resp, identity, identity_len);

	data->state = LEAP_WAIT_SUCCESS;

	return resp;
}


static struct wpabuf * eap_leap_process_success(struct eap_sm *sm, void *priv,
						struct eap_method_ret *ret,
						const struct wpabuf *reqData)
{
	struct eap_leap_data *data = priv;
	struct wpabuf *resp;
	u8 *pos;
	const u8 *identity;
	size_t identity_len;

	wpa_printf(MSG_DEBUG, "EAP-LEAP: Processing EAP-Success");

	identity = eap_get_config_identity(sm, &identity_len);
	if (identity == NULL)
		return NULL;

	if (data->state != LEAP_WAIT_SUCCESS) {
		wpa_printf(MSG_INFO, "EAP-LEAP: EAP-Success received in "
			   "unexpected state (%d) - ignored", data->state);
		ret->ignore = TRUE;
		return NULL;
	}

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_LEAP,
			     3 + LEAP_CHALLENGE_LEN + identity_len,
			     EAP_CODE_REQUEST, eap_get_id(reqData));
	if (resp == NULL)
		return NULL;
	wpabuf_put_u8(resp, LEAP_VERSION);
	wpabuf_put_u8(resp, 0); /* unused */
	wpabuf_put_u8(resp, LEAP_CHALLENGE_LEN);
	pos = wpabuf_put(resp, LEAP_CHALLENGE_LEN);
	if (random_get_bytes(pos, LEAP_CHALLENGE_LEN)) {
		wpa_printf(MSG_WARNING, "EAP-LEAP: Failed to read random data "
			   "for challenge");
		wpabuf_free(resp);
		ret->ignore = TRUE;
		return NULL;
	}
	os_memcpy(data->ap_challenge, pos, LEAP_CHALLENGE_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-LEAP: Challenge to AP/AS", pos,
		    LEAP_CHALLENGE_LEN);
	wpabuf_put_data(resp, identity, identity_len);

	data->state = LEAP_WAIT_RESPONSE;

	return resp;
}


static struct wpabuf * eap_leap_process_response(struct eap_sm *sm, void *priv,
						 struct eap_method_ret *ret,
						 const struct wpabuf *reqData)
{
	struct eap_leap_data *data = priv;
	const u8 *pos, *password;
	u8 response_len, pw_hash[16], pw_hash_hash[16],
		expected[LEAP_RESPONSE_LEN];
	size_t password_len, len;
	int pwhash;

	wpa_printf(MSG_DEBUG, "EAP-LEAP: Processing EAP-Response");

	password = eap_get_config_password2(sm, &password_len, &pwhash);
	if (password == NULL)
		return NULL;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_LEAP, reqData, &len);
	if (pos == NULL || len < 3) {
		wpa_printf(MSG_INFO, "EAP-LEAP: Invalid EAP-Response frame");
		ret->ignore = TRUE;
		return NULL;
	}

	if (*pos != LEAP_VERSION) {
		wpa_printf(MSG_WARNING, "EAP-LEAP: Unsupported LEAP version "
			   "%d", *pos);
		ret->ignore = TRUE;
		return NULL;
	}
	pos++;

	pos++; /* skip unused byte */

	response_len = *pos++;
	if (response_len != LEAP_RESPONSE_LEN || response_len > len - 3) {
		wpa_printf(MSG_INFO, "EAP-LEAP: Invalid response "
			   "(response_len=%d reqDataLen=%lu)",
			   response_len, (unsigned long) wpabuf_len(reqData));
		ret->ignore = TRUE;
		return NULL;
	}

	wpa_hexdump(MSG_DEBUG, "EAP-LEAP: Response from AP",
		    pos, LEAP_RESPONSE_LEN);
	os_memcpy(data->ap_response, pos, LEAP_RESPONSE_LEN);

	if (pwhash) {
		if (hash_nt_password_hash(password, pw_hash_hash)) {
			ret->ignore = TRUE;
			return NULL;
		}
	} else {
		if (nt_password_hash(password, password_len, pw_hash) ||
		    hash_nt_password_hash(pw_hash, pw_hash_hash)) {
			ret->ignore = TRUE;
			return NULL;
		}
	}
	if (challenge_response(data->ap_challenge, pw_hash_hash, expected)) {
		ret->ignore = TRUE;
		return NULL;
	}

	ret->methodState = METHOD_DONE;
	ret->allowNotifications = FALSE;

	if (os_memcmp_const(pos, expected, LEAP_RESPONSE_LEN) != 0) {
		wpa_printf(MSG_WARNING, "EAP-LEAP: AP sent an invalid "
			   "response - authentication failed");
		wpa_hexdump(MSG_DEBUG, "EAP-LEAP: Expected response from AP",
			    expected, LEAP_RESPONSE_LEN);
		ret->decision = DECISION_FAIL;
		return NULL;
	}

	ret->decision = DECISION_UNCOND_SUCC;

	/* LEAP is somewhat odd method since it sends EAP-Success in the middle
	 * of the authentication. Use special variable to transit EAP state
	 * machine to SUCCESS state. */
	sm->leap_done = TRUE;
	data->state = LEAP_DONE;

	/* No more authentication messages expected; AP will send EAPOL-Key
	 * frames if encryption is enabled. */
	return NULL;
}


static struct wpabuf * eap_leap_process(struct eap_sm *sm, void *priv,
					struct eap_method_ret *ret,
					const struct wpabuf *reqData)
{
	const struct eap_hdr *eap;
	size_t password_len;
	const u8 *password;

	password = eap_get_config_password(sm, &password_len);
	if (password == NULL) {
		wpa_printf(MSG_INFO, "EAP-LEAP: Password not configured");
		eap_sm_request_password(sm);
		ret->ignore = TRUE;
		return NULL;
	}

	/*
	 * LEAP needs to be able to handle EAP-Success frame which does not
	 * include Type field. Consequently, eap_hdr_validate() cannot be used
	 * here. This validation will be done separately for EAP-Request and
	 * EAP-Response frames.
	 */
	eap = wpabuf_head(reqData);
	if (wpabuf_len(reqData) < sizeof(*eap) ||
	    be_to_host16(eap->length) > wpabuf_len(reqData)) {
		wpa_printf(MSG_INFO, "EAP-LEAP: Invalid frame");
		ret->ignore = TRUE;
		return NULL;
	}

	ret->ignore = FALSE;
	ret->allowNotifications = TRUE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;

	sm->leap_done = FALSE;

	switch (eap->code) {
	case EAP_CODE_REQUEST:
		return eap_leap_process_request(sm, priv, ret, reqData);
	case EAP_CODE_SUCCESS:
		return eap_leap_process_success(sm, priv, ret, reqData);
	case EAP_CODE_RESPONSE:
		return eap_leap_process_response(sm, priv, ret, reqData);
	default:
		wpa_printf(MSG_INFO, "EAP-LEAP: Unexpected EAP code (%d) - "
			   "ignored", eap->code);
		ret->ignore = TRUE;
		return NULL;
	}
}


static Boolean eap_leap_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_leap_data *data = priv;
	return data->state == LEAP_DONE;
}


static u8 * eap_leap_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_leap_data *data = priv;
	u8 *key, pw_hash_hash[16], pw_hash[16];
	const u8 *addr[5], *password;
	size_t elen[5], password_len;
	int pwhash;

	if (data->state != LEAP_DONE)
		return NULL;

	password = eap_get_config_password2(sm, &password_len, &pwhash);
	if (password == NULL)
		return NULL;

	key = os_malloc(LEAP_KEY_LEN);
	if (key == NULL)
		return NULL;

	if (pwhash) {
		if (hash_nt_password_hash(password, pw_hash_hash)) {
			os_free(key);
			return NULL;
		}
	} else {
		if (nt_password_hash(password, password_len, pw_hash) ||
		    hash_nt_password_hash(pw_hash, pw_hash_hash)) {
			os_free(key);
			return NULL;
		}
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-LEAP: pw_hash_hash",
			pw_hash_hash, 16);
	wpa_hexdump(MSG_DEBUG, "EAP-LEAP: peer_challenge",
		    data->peer_challenge, LEAP_CHALLENGE_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-LEAP: peer_response",
		    data->peer_response, LEAP_RESPONSE_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-LEAP: ap_challenge",
		    data->ap_challenge, LEAP_CHALLENGE_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-LEAP: ap_response",
		    data->ap_response, LEAP_RESPONSE_LEN);

	addr[0] = pw_hash_hash;
	elen[0] = 16;
	addr[1] = data->ap_challenge;
	elen[1] = LEAP_CHALLENGE_LEN;
	addr[2] = data->ap_response;
	elen[2] = LEAP_RESPONSE_LEN;
	addr[3] = data->peer_challenge;
	elen[3] = LEAP_CHALLENGE_LEN;
	addr[4] = data->peer_response;
	elen[4] = LEAP_RESPONSE_LEN;
	md5_vector(5, addr, elen, key);
	wpa_hexdump_key(MSG_DEBUG, "EAP-LEAP: master key", key, LEAP_KEY_LEN);
	*len = LEAP_KEY_LEN;

	os_memset(pw_hash, 0, sizeof(pw_hash));
	os_memset(pw_hash_hash, 0, sizeof(pw_hash_hash));

	return key;
}


int eap_peer_leap_register(void)
{
	struct eap_method *eap;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_LEAP, "LEAP");
	if (eap == NULL)
		return -1;

	eap->init = eap_leap_init;
	eap->deinit = eap_leap_deinit;
	eap->process = eap_leap_process;
	eap->isKeyAvailable = eap_leap_isKeyAvailable;
	eap->getKey = eap_leap_getKey;

	return eap_peer_method_register(eap);
}
