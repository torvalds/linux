/*
 * hostapd / EAP-MSCHAPv2 (draft-kamath-pppext-eap-mschapv2-00.txt) server
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
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
#include "crypto/ms_funcs.h"
#include "crypto/random.h"
#include "eap_i.h"


struct eap_mschapv2_hdr {
	u8 op_code; /* MSCHAPV2_OP_* */
	u8 mschapv2_id; /* must be changed for challenges, but not for
			 * success/failure */
	u8 ms_length[2]; /* Note: misaligned; length - 5 */
	/* followed by data */
} STRUCT_PACKED;

#define MSCHAPV2_OP_CHALLENGE 1
#define MSCHAPV2_OP_RESPONSE 2
#define MSCHAPV2_OP_SUCCESS 3
#define MSCHAPV2_OP_FAILURE 4
#define MSCHAPV2_OP_CHANGE_PASSWORD 7

#define MSCHAPV2_RESP_LEN 49

#define ERROR_RESTRICTED_LOGON_HOURS 646
#define ERROR_ACCT_DISABLED 647
#define ERROR_PASSWD_EXPIRED 648
#define ERROR_NO_DIALIN_PERMISSION 649
#define ERROR_AUTHENTICATION_FAILURE 691
#define ERROR_CHANGING_PASSWORD 709

#define PASSWD_CHANGE_CHAL_LEN 16
#define MSCHAPV2_KEY_LEN 16


#define CHALLENGE_LEN 16

struct eap_mschapv2_data {
	u8 auth_challenge[CHALLENGE_LEN];
	int auth_challenge_from_tls;
	u8 *peer_challenge;
	u8 auth_response[20];
	enum { CHALLENGE, SUCCESS_REQ, FAILURE_REQ, SUCCESS, FAILURE } state;
	u8 resp_mschapv2_id;
	u8 master_key[16];
	int master_key_valid;
};


static void * eap_mschapv2_init(struct eap_sm *sm)
{
	struct eap_mschapv2_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = CHALLENGE;

	if (sm->auth_challenge) {
		os_memcpy(data->auth_challenge, sm->auth_challenge,
			  CHALLENGE_LEN);
		data->auth_challenge_from_tls = 1;
	}

	if (sm->peer_challenge) {
		data->peer_challenge = os_malloc(CHALLENGE_LEN);
		if (data->peer_challenge == NULL) {
			os_free(data);
			return NULL;
		}
		os_memcpy(data->peer_challenge, sm->peer_challenge,
			  CHALLENGE_LEN);
	}

	return data;
}


static void eap_mschapv2_reset(struct eap_sm *sm, void *priv)
{
	struct eap_mschapv2_data *data = priv;
	if (data == NULL)
		return;

	os_free(data->peer_challenge);
	os_free(data);
}


static struct wpabuf * eap_mschapv2_build_challenge(
	struct eap_sm *sm, struct eap_mschapv2_data *data, u8 id)
{
	struct wpabuf *req;
	struct eap_mschapv2_hdr *ms;
	char *name = "hostapd"; /* TODO: make this configurable */
	size_t ms_len;

	if (!data->auth_challenge_from_tls &&
	    random_get_bytes(data->auth_challenge, CHALLENGE_LEN)) {
		wpa_printf(MSG_ERROR, "EAP-MSCHAPV2: Failed to get random "
			   "data");
		data->state = FAILURE;
		return NULL;
	}

	ms_len = sizeof(*ms) + 1 + CHALLENGE_LEN + os_strlen(name);
	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2, ms_len,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-MSCHAPV2: Failed to allocate memory"
			   " for request");
		data->state = FAILURE;
		return NULL;
	}

	ms = wpabuf_put(req, sizeof(*ms));
	ms->op_code = MSCHAPV2_OP_CHALLENGE;
	ms->mschapv2_id = id;
	WPA_PUT_BE16(ms->ms_length, ms_len);

	wpabuf_put_u8(req, CHALLENGE_LEN);
	if (!data->auth_challenge_from_tls)
		wpabuf_put_data(req, data->auth_challenge, CHALLENGE_LEN);
	else
		wpabuf_put(req, CHALLENGE_LEN);
	wpa_hexdump(MSG_MSGDUMP, "EAP-MSCHAPV2: Challenge",
		    data->auth_challenge, CHALLENGE_LEN);
	wpabuf_put_data(req, name, os_strlen(name));

	return req;
}


static struct wpabuf * eap_mschapv2_build_success_req(
	struct eap_sm *sm, struct eap_mschapv2_data *data, u8 id)
{
	struct wpabuf *req;
	struct eap_mschapv2_hdr *ms;
	u8 *msg;
	char *message = "OK";
	size_t ms_len;

	ms_len = sizeof(*ms) + 2 + 2 * sizeof(data->auth_response) + 1 + 2 +
		os_strlen(message);
	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2, ms_len,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-MSCHAPV2: Failed to allocate memory"
			   " for request");
		data->state = FAILURE;
		return NULL;
	}

	ms = wpabuf_put(req, sizeof(*ms));
	ms->op_code = MSCHAPV2_OP_SUCCESS;
	ms->mschapv2_id = data->resp_mschapv2_id;
	WPA_PUT_BE16(ms->ms_length, ms_len);
	msg = (u8 *) (ms + 1);

	wpabuf_put_u8(req, 'S');
	wpabuf_put_u8(req, '=');
	wpa_snprintf_hex_uppercase(
		wpabuf_put(req, sizeof(data->auth_response) * 2),
		sizeof(data->auth_response) * 2 + 1,
		data->auth_response, sizeof(data->auth_response));
	wpabuf_put_u8(req, ' ');
	wpabuf_put_u8(req, 'M');
	wpabuf_put_u8(req, '=');
	wpabuf_put_data(req, message, os_strlen(message));

	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-MSCHAPV2: Success Request Message",
			  msg, ms_len - sizeof(*ms));

	return req;
}


static struct wpabuf * eap_mschapv2_build_failure_req(
	struct eap_sm *sm, struct eap_mschapv2_data *data, u8 id)
{
	struct wpabuf *req;
	struct eap_mschapv2_hdr *ms;
	char *message = "E=691 R=0 C=00000000000000000000000000000000 V=3 "
		"M=FAILED";
	size_t ms_len;

	ms_len = sizeof(*ms) + os_strlen(message);
	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2, ms_len,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-MSCHAPV2: Failed to allocate memory"
			   " for request");
		data->state = FAILURE;
		return NULL;
	}

	ms = wpabuf_put(req, sizeof(*ms));
	ms->op_code = MSCHAPV2_OP_FAILURE;
	ms->mschapv2_id = data->resp_mschapv2_id;
	WPA_PUT_BE16(ms->ms_length, ms_len);

	wpabuf_put_data(req, message, os_strlen(message));

	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-MSCHAPV2: Failure Request Message",
			  (u8 *) message, os_strlen(message));

	return req;
}


static struct wpabuf * eap_mschapv2_buildReq(struct eap_sm *sm, void *priv,
					     u8 id)
{
	struct eap_mschapv2_data *data = priv;

	switch (data->state) {
	case CHALLENGE:
		return eap_mschapv2_build_challenge(sm, data, id);
	case SUCCESS_REQ:
		return eap_mschapv2_build_success_req(sm, data, id);
	case FAILURE_REQ:
		return eap_mschapv2_build_failure_req(sm, data, id);
	default:
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Unknown state %d in "
			   "buildReq", data->state);
		break;
	}
	return NULL;
}


static Boolean eap_mschapv2_check(struct eap_sm *sm, void *priv,
				  struct wpabuf *respData)
{
	struct eap_mschapv2_data *data = priv;
	struct eap_mschapv2_hdr *resp;
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2, respData,
			       &len);
	if (pos == NULL || len < 1) {
		wpa_printf(MSG_INFO, "EAP-MSCHAPV2: Invalid frame");
		return TRUE;
	}

	resp = (struct eap_mschapv2_hdr *) pos;
	if (data->state == CHALLENGE &&
	    resp->op_code != MSCHAPV2_OP_RESPONSE) {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Expected Response - "
			   "ignore op %d", resp->op_code);
		return TRUE;
	}

	if (data->state == SUCCESS_REQ &&
	    resp->op_code != MSCHAPV2_OP_SUCCESS &&
	    resp->op_code != MSCHAPV2_OP_FAILURE) {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Expected Success or "
			   "Failure - ignore op %d", resp->op_code);
		return TRUE;
	}

	if (data->state == FAILURE_REQ &&
	    resp->op_code != MSCHAPV2_OP_FAILURE) {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Expected Failure "
			   "- ignore op %d", resp->op_code);
		return TRUE;
	}

	return FALSE;
}


static void eap_mschapv2_process_response(struct eap_sm *sm,
					  struct eap_mschapv2_data *data,
					  struct wpabuf *respData)
{
	struct eap_mschapv2_hdr *resp;
	const u8 *pos, *end, *peer_challenge, *nt_response, *name;
	u8 flags;
	size_t len, name_len, i;
	u8 expected[24];
	const u8 *username, *user;
	size_t username_len, user_len;
	int res;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2, respData,
			       &len);
	if (pos == NULL || len < 1)
		return; /* Should not happen - frame already validated */

	end = pos + len;
	resp = (struct eap_mschapv2_hdr *) pos;
	pos = (u8 *) (resp + 1);

	if (len < sizeof(*resp) + 1 + 49 ||
	    resp->op_code != MSCHAPV2_OP_RESPONSE ||
	    pos[0] != 49) {
		wpa_hexdump_buf(MSG_DEBUG, "EAP-MSCHAPV2: Invalid response",
				respData);
		data->state = FAILURE;
		return;
	}
	data->resp_mschapv2_id = resp->mschapv2_id;
	pos++;
	peer_challenge = pos;
	pos += 16 + 8;
	nt_response = pos;
	pos += 24;
	flags = *pos++;
	name = pos;
	name_len = end - name;

	if (data->peer_challenge) {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Using pre-configured "
			   "Peer-Challenge");
		peer_challenge = data->peer_challenge;
	}
	wpa_hexdump(MSG_MSGDUMP, "EAP-MSCHAPV2: Peer-Challenge",
		    peer_challenge, 16);
	wpa_hexdump(MSG_MSGDUMP, "EAP-MSCHAPV2: NT-Response", nt_response, 24);
	wpa_printf(MSG_MSGDUMP, "EAP-MSCHAPV2: Flags 0x%x", flags);
	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-MSCHAPV2: Name", name, name_len);

	/* MSCHAPv2 does not include optional domain name in the
	 * challenge-response calculation, so remove domain prefix
	 * (if present). */
	username = sm->identity;
	username_len = sm->identity_len;
	for (i = 0; i < username_len; i++) {
		if (username[i] == '\\') {
			username_len -= i + 1;
			username += i + 1;
			break;
		}
	}

	user = name;
	user_len = name_len;
	for (i = 0; i < user_len; i++) {
		if (user[i] == '\\') {
			user_len -= i + 1;
			user += i + 1;
			break;
		}
	}

	if (username_len != user_len ||
	    os_memcmp(username, user, username_len) != 0) {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Mismatch in user names");
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-MSCHAPV2: Expected user "
				  "name", username, username_len);
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-MSCHAPV2: Received user "
				  "name", user, user_len);
		data->state = FAILURE;
		return;
	}

	wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-MSCHAPV2: User name",
			  username, username_len);

	if (sm->user->password_hash) {
		res = generate_nt_response_pwhash(data->auth_challenge,
						  peer_challenge,
						  username, username_len,
						  sm->user->password,
						  expected);
	} else {
		res = generate_nt_response(data->auth_challenge,
					   peer_challenge,
					   username, username_len,
					   sm->user->password,
					   sm->user->password_len,
					   expected);
	}
	if (res) {
		data->state = FAILURE;
		return;
	}

	if (os_memcmp(nt_response, expected, 24) == 0) {
		const u8 *pw_hash;
		u8 pw_hash_buf[16], pw_hash_hash[16];

		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Correct NT-Response");
		data->state = SUCCESS_REQ;

		/* Authenticator response is not really needed yet, but
		 * calculate it here so that peer_challenge and username need
		 * not be saved. */
		if (sm->user->password_hash) {
			pw_hash = sm->user->password;
		} else {
			nt_password_hash(sm->user->password,
					 sm->user->password_len,
					 pw_hash_buf);
			pw_hash = pw_hash_buf;
		}
		generate_authenticator_response_pwhash(
			pw_hash, peer_challenge, data->auth_challenge,
			username, username_len, nt_response,
			data->auth_response);

		hash_nt_password_hash(pw_hash, pw_hash_hash);
		get_master_key(pw_hash_hash, nt_response, data->master_key);
		data->master_key_valid = 1;
		wpa_hexdump_key(MSG_DEBUG, "EAP-MSCHAPV2: Derived Master Key",
				data->master_key, MSCHAPV2_KEY_LEN);
	} else {
		wpa_hexdump(MSG_MSGDUMP, "EAP-MSCHAPV2: Expected NT-Response",
			    expected, 24);
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Invalid NT-Response");
		data->state = FAILURE_REQ;
	}
}


static void eap_mschapv2_process_success_resp(struct eap_sm *sm,
					      struct eap_mschapv2_data *data,
					      struct wpabuf *respData)
{
	struct eap_mschapv2_hdr *resp;
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2, respData,
			       &len);
	if (pos == NULL || len < 1)
		return; /* Should not happen - frame already validated */

	resp = (struct eap_mschapv2_hdr *) pos;

	if (resp->op_code == MSCHAPV2_OP_SUCCESS) {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Received Success Response"
			   " - authentication completed successfully");
		data->state = SUCCESS;
	} else {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Did not receive Success "
			   "Response - peer rejected authentication");
		data->state = FAILURE;
	}
}


static void eap_mschapv2_process_failure_resp(struct eap_sm *sm,
					      struct eap_mschapv2_data *data,
					      struct wpabuf *respData)
{
	struct eap_mschapv2_hdr *resp;
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2, respData,
			       &len);
	if (pos == NULL || len < 1)
		return; /* Should not happen - frame already validated */

	resp = (struct eap_mschapv2_hdr *) pos;

	if (resp->op_code == MSCHAPV2_OP_FAILURE) {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Received Failure Response"
			   " - authentication failed");
	} else {
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Did not receive Failure "
			   "Response - authentication failed");
	}

	data->state = FAILURE;
}


static void eap_mschapv2_process(struct eap_sm *sm, void *priv,
				 struct wpabuf *respData)
{
	struct eap_mschapv2_data *data = priv;

	if (sm->user == NULL || sm->user->password == NULL) {
		wpa_printf(MSG_INFO, "EAP-MSCHAPV2: Password not configured");
		data->state = FAILURE;
		return;
	}

	switch (data->state) {
	case CHALLENGE:
		eap_mschapv2_process_response(sm, data, respData);
		break;
	case SUCCESS_REQ:
		eap_mschapv2_process_success_resp(sm, data, respData);
		break;
	case FAILURE_REQ:
		eap_mschapv2_process_failure_resp(sm, data, respData);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-MSCHAPV2: Unknown state %d in "
			   "process", data->state);
		break;
	}
}


static Boolean eap_mschapv2_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_mschapv2_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_mschapv2_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_mschapv2_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS || !data->master_key_valid)
		return NULL;

	*len = 2 * MSCHAPV2_KEY_LEN;
	key = os_malloc(*len);
	if (key == NULL)
		return NULL;
	/* MSK = server MS-MPPE-Recv-Key | MS-MPPE-Send-Key */
	get_asymetric_start_key(data->master_key, key, MSCHAPV2_KEY_LEN, 0, 1);
	get_asymetric_start_key(data->master_key, key + MSCHAPV2_KEY_LEN,
				MSCHAPV2_KEY_LEN, 1, 1);
	wpa_hexdump_key(MSG_DEBUG, "EAP-MSCHAPV2: Derived key", key, *len);

	return key;
}


static Boolean eap_mschapv2_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_mschapv2_data *data = priv;
	return data->state == SUCCESS;
}


int eap_server_mschapv2_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_MSCHAPV2,
				      "MSCHAPV2");
	if (eap == NULL)
		return -1;

	eap->init = eap_mschapv2_init;
	eap->reset = eap_mschapv2_reset;
	eap->buildReq = eap_mschapv2_buildReq;
	eap->check = eap_mschapv2_check;
	eap->process = eap_mschapv2_process;
	eap->isDone = eap_mschapv2_isDone;
	eap->getKey = eap_mschapv2_getKey;
	eap->isSuccess = eap_mschapv2_isSuccess;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}
