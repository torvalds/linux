/*
 * hostapd / EAP-SIM (RFC 4186)
 * Copyright (c) 2005-2008, Jouni Malinen <j@w1.fi>
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
#include "crypto/random.h"
#include "eap_server/eap_i.h"
#include "eap_common/eap_sim_common.h"
#include "eap_server/eap_sim_db.h"


struct eap_sim_data {
	u8 mk[EAP_SIM_MK_LEN];
	u8 nonce_mt[EAP_SIM_NONCE_MT_LEN];
	u8 nonce_s[EAP_SIM_NONCE_S_LEN];
	u8 k_aut[EAP_SIM_K_AUT_LEN];
	u8 k_encr[EAP_SIM_K_ENCR_LEN];
	u8 msk[EAP_SIM_KEYING_DATA_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 kc[EAP_SIM_MAX_CHAL][EAP_SIM_KC_LEN];
	u8 sres[EAP_SIM_MAX_CHAL][EAP_SIM_SRES_LEN];
	u8 rand[EAP_SIM_MAX_CHAL][GSM_RAND_LEN];
	int num_chal;
	enum {
		START, CHALLENGE, REAUTH, NOTIFICATION, SUCCESS, FAILURE
	} state;
	char *next_pseudonym;
	char *next_reauth_id;
	u16 counter;
	struct eap_sim_reauth *reauth;
	u16 notification;
	int use_result_ind;
};


static const char * eap_sim_state_txt(int state)
{
	switch (state) {
	case START:
		return "START";
	case CHALLENGE:
		return "CHALLENGE";
	case REAUTH:
		return "REAUTH";
	case SUCCESS:
		return "SUCCESS";
	case FAILURE:
		return "FAILURE";
	case NOTIFICATION:
		return "NOTIFICATION";
	default:
		return "Unknown?!";
	}
}


static void eap_sim_state(struct eap_sim_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-SIM: %s -> %s",
		   eap_sim_state_txt(data->state),
		   eap_sim_state_txt(state));
	data->state = state;
}


static void * eap_sim_init(struct eap_sm *sm)
{
	struct eap_sim_data *data;

	if (sm->eap_sim_db_priv == NULL) {
		wpa_printf(MSG_WARNING, "EAP-SIM: eap_sim_db not configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->state = START;

	return data;
}


static void eap_sim_reset(struct eap_sm *sm, void *priv)
{
	struct eap_sim_data *data = priv;
	os_free(data->next_pseudonym);
	os_free(data->next_reauth_id);
	os_free(data);
}


static struct wpabuf * eap_sim_build_start(struct eap_sm *sm,
					   struct eap_sim_data *data, u8 id)
{
	struct eap_sim_msg *msg;
	u8 ver[2];

	wpa_printf(MSG_DEBUG, "EAP-SIM: Generating Start");
	msg = eap_sim_msg_init(EAP_CODE_REQUEST, id, EAP_TYPE_SIM,
			       EAP_SIM_SUBTYPE_START);
	if (eap_sim_db_identity_known(sm->eap_sim_db_priv, sm->identity,
				      sm->identity_len)) {
		wpa_printf(MSG_DEBUG, "   AT_PERMANENT_ID_REQ");
		eap_sim_msg_add(msg, EAP_SIM_AT_PERMANENT_ID_REQ, 0, NULL, 0);
	} else {
		/*
		 * RFC 4186, Chap. 4.2.4 recommends that identity from EAP is
		 * ignored and the SIM/Start is used to request the identity.
		 */
		wpa_printf(MSG_DEBUG, "   AT_ANY_ID_REQ");
		eap_sim_msg_add(msg, EAP_SIM_AT_ANY_ID_REQ, 0, NULL, 0);
	}
	wpa_printf(MSG_DEBUG, "   AT_VERSION_LIST");
	ver[0] = 0;
	ver[1] = EAP_SIM_VERSION;
	eap_sim_msg_add(msg, EAP_SIM_AT_VERSION_LIST, sizeof(ver),
			ver, sizeof(ver));
	return eap_sim_msg_finish(msg, NULL, NULL, 0);
}


static int eap_sim_build_encr(struct eap_sm *sm, struct eap_sim_data *data,
			      struct eap_sim_msg *msg, u16 counter,
			      const u8 *nonce_s)
{
	os_free(data->next_pseudonym);
	data->next_pseudonym =
		eap_sim_db_get_next_pseudonym(sm->eap_sim_db_priv, 0);
	os_free(data->next_reauth_id);
	if (data->counter <= EAP_SIM_MAX_FAST_REAUTHS) {
		data->next_reauth_id =
			eap_sim_db_get_next_reauth_id(sm->eap_sim_db_priv, 0);
	} else {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Max fast re-authentication "
			   "count exceeded - force full authentication");
		data->next_reauth_id = NULL;
	}

	if (data->next_pseudonym == NULL && data->next_reauth_id == NULL &&
	    counter == 0 && nonce_s == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "   AT_IV");
	wpa_printf(MSG_DEBUG, "   AT_ENCR_DATA");
	eap_sim_msg_add_encr_start(msg, EAP_SIM_AT_IV, EAP_SIM_AT_ENCR_DATA);

	if (counter > 0) {
		wpa_printf(MSG_DEBUG, "   *AT_COUNTER (%u)", counter);
		eap_sim_msg_add(msg, EAP_SIM_AT_COUNTER, counter, NULL, 0);
	}

	if (nonce_s) {
		wpa_printf(MSG_DEBUG, "   *AT_NONCE_S");
		eap_sim_msg_add(msg, EAP_SIM_AT_NONCE_S, 0, nonce_s,
				EAP_SIM_NONCE_S_LEN);
	}

	if (data->next_pseudonym) {
		wpa_printf(MSG_DEBUG, "   *AT_NEXT_PSEUDONYM (%s)",
			   data->next_pseudonym);
		eap_sim_msg_add(msg, EAP_SIM_AT_NEXT_PSEUDONYM,
				os_strlen(data->next_pseudonym),
				(u8 *) data->next_pseudonym,
				os_strlen(data->next_pseudonym));
	}

	if (data->next_reauth_id) {
		wpa_printf(MSG_DEBUG, "   *AT_NEXT_REAUTH_ID (%s)",
			   data->next_reauth_id);
		eap_sim_msg_add(msg, EAP_SIM_AT_NEXT_REAUTH_ID,
				os_strlen(data->next_reauth_id),
				(u8 *) data->next_reauth_id,
				os_strlen(data->next_reauth_id));
	}

	if (eap_sim_msg_add_encr_end(msg, data->k_encr, EAP_SIM_AT_PADDING)) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Failed to encrypt "
			   "AT_ENCR_DATA");
		return -1;
	}

	return 0;
}


static struct wpabuf * eap_sim_build_challenge(struct eap_sm *sm,
					       struct eap_sim_data *data,
					       u8 id)
{
	struct eap_sim_msg *msg;

	wpa_printf(MSG_DEBUG, "EAP-SIM: Generating Challenge");
	msg = eap_sim_msg_init(EAP_CODE_REQUEST, id, EAP_TYPE_SIM,
			       EAP_SIM_SUBTYPE_CHALLENGE);
	wpa_printf(MSG_DEBUG, "   AT_RAND");
	eap_sim_msg_add(msg, EAP_SIM_AT_RAND, 0, (u8 *) data->rand,
			data->num_chal * GSM_RAND_LEN);

	if (eap_sim_build_encr(sm, data, msg, 0, NULL)) {
		eap_sim_msg_free(msg);
		return NULL;
	}

	if (sm->eap_sim_aka_result_ind) {
		wpa_printf(MSG_DEBUG, "   AT_RESULT_IND");
		eap_sim_msg_add(msg, EAP_SIM_AT_RESULT_IND, 0, NULL, 0);
	}

	wpa_printf(MSG_DEBUG, "   AT_MAC");
	eap_sim_msg_add_mac(msg, EAP_SIM_AT_MAC);
	return eap_sim_msg_finish(msg, data->k_aut, data->nonce_mt,
				  EAP_SIM_NONCE_MT_LEN);
}


static struct wpabuf * eap_sim_build_reauth(struct eap_sm *sm,
					    struct eap_sim_data *data, u8 id)
{
	struct eap_sim_msg *msg;

	wpa_printf(MSG_DEBUG, "EAP-SIM: Generating Re-authentication");

	if (random_get_bytes(data->nonce_s, EAP_SIM_NONCE_S_LEN))
		return NULL;
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-SIM: NONCE_S",
			data->nonce_s, EAP_SIM_NONCE_S_LEN);

	eap_sim_derive_keys(data->mk, data->k_encr, data->k_aut, data->msk,
			    data->emsk);
	eap_sim_derive_keys_reauth(data->counter, sm->identity,
				   sm->identity_len, data->nonce_s, data->mk,
				   data->msk, data->emsk);

	msg = eap_sim_msg_init(EAP_CODE_REQUEST, id, EAP_TYPE_SIM,
			       EAP_SIM_SUBTYPE_REAUTHENTICATION);

	if (eap_sim_build_encr(sm, data, msg, data->counter, data->nonce_s)) {
		eap_sim_msg_free(msg);
		return NULL;
	}

	if (sm->eap_sim_aka_result_ind) {
		wpa_printf(MSG_DEBUG, "   AT_RESULT_IND");
		eap_sim_msg_add(msg, EAP_SIM_AT_RESULT_IND, 0, NULL, 0);
	}

	wpa_printf(MSG_DEBUG, "   AT_MAC");
	eap_sim_msg_add_mac(msg, EAP_SIM_AT_MAC);
	return eap_sim_msg_finish(msg, data->k_aut, NULL, 0);
}


static struct wpabuf * eap_sim_build_notification(struct eap_sm *sm,
						  struct eap_sim_data *data,
						  u8 id)
{
	struct eap_sim_msg *msg;

	wpa_printf(MSG_DEBUG, "EAP-SIM: Generating Notification");
	msg = eap_sim_msg_init(EAP_CODE_REQUEST, id, EAP_TYPE_SIM,
			       EAP_SIM_SUBTYPE_NOTIFICATION);
	wpa_printf(MSG_DEBUG, "   AT_NOTIFICATION (%d)", data->notification);
	eap_sim_msg_add(msg, EAP_SIM_AT_NOTIFICATION, data->notification,
			NULL, 0);
	if (data->use_result_ind) {
		if (data->reauth) {
			wpa_printf(MSG_DEBUG, "   AT_IV");
			wpa_printf(MSG_DEBUG, "   AT_ENCR_DATA");
			eap_sim_msg_add_encr_start(msg, EAP_SIM_AT_IV,
						   EAP_SIM_AT_ENCR_DATA);
			wpa_printf(MSG_DEBUG, "   *AT_COUNTER (%u)",
				   data->counter);
			eap_sim_msg_add(msg, EAP_SIM_AT_COUNTER, data->counter,
					NULL, 0);

			if (eap_sim_msg_add_encr_end(msg, data->k_encr,
						     EAP_SIM_AT_PADDING)) {
				wpa_printf(MSG_WARNING, "EAP-SIM: Failed to "
					   "encrypt AT_ENCR_DATA");
				eap_sim_msg_free(msg);
				return NULL;
			}
		}

		wpa_printf(MSG_DEBUG, "   AT_MAC");
		eap_sim_msg_add_mac(msg, EAP_SIM_AT_MAC);
	}
	return eap_sim_msg_finish(msg, data->k_aut, NULL, 0);
}


static struct wpabuf * eap_sim_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_sim_data *data = priv;

	switch (data->state) {
	case START:
		return eap_sim_build_start(sm, data, id);
	case CHALLENGE:
		return eap_sim_build_challenge(sm, data, id);
	case REAUTH:
		return eap_sim_build_reauth(sm, data, id);
	case NOTIFICATION:
		return eap_sim_build_notification(sm, data, id);
	default:
		wpa_printf(MSG_DEBUG, "EAP-SIM: Unknown state %d in "
			   "buildReq", data->state);
		break;
	}
	return NULL;
}


static Boolean eap_sim_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_sim_data *data = priv;
	const u8 *pos;
	size_t len;
	u8 subtype;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_SIM, respData, &len);
	if (pos == NULL || len < 3) {
		wpa_printf(MSG_INFO, "EAP-SIM: Invalid frame");
		return TRUE;
	}
	subtype = *pos;

	if (subtype == EAP_SIM_SUBTYPE_CLIENT_ERROR)
		return FALSE;

	switch (data->state) {
	case START:
		if (subtype != EAP_SIM_SUBTYPE_START) {
			wpa_printf(MSG_INFO, "EAP-SIM: Unexpected response "
				   "subtype %d", subtype);
			return TRUE;
		}
		break;
	case CHALLENGE:
		if (subtype != EAP_SIM_SUBTYPE_CHALLENGE) {
			wpa_printf(MSG_INFO, "EAP-SIM: Unexpected response "
				   "subtype %d", subtype);
			return TRUE;
		}
		break;
	case REAUTH:
		if (subtype != EAP_SIM_SUBTYPE_REAUTHENTICATION) {
			wpa_printf(MSG_INFO, "EAP-SIM: Unexpected response "
				   "subtype %d", subtype);
			return TRUE;
		}
		break;
	case NOTIFICATION:
		if (subtype != EAP_SIM_SUBTYPE_NOTIFICATION) {
			wpa_printf(MSG_INFO, "EAP-SIM: Unexpected response "
				   "subtype %d", subtype);
			return TRUE;
		}
		break;
	default:
		wpa_printf(MSG_INFO, "EAP-SIM: Unexpected state (%d) for "
			   "processing a response", data->state);
		return TRUE;
	}

	return FALSE;
}


static int eap_sim_supported_ver(struct eap_sim_data *data, int version)
{
	return version == EAP_SIM_VERSION;
}


static void eap_sim_process_start(struct eap_sm *sm,
				  struct eap_sim_data *data,
				  struct wpabuf *respData,
				  struct eap_sim_attrs *attr)
{
	const u8 *identity;
	size_t identity_len;
	u8 ver_list[2];

	wpa_printf(MSG_DEBUG, "EAP-SIM: Receive start response");

	if (attr->identity) {
		os_free(sm->identity);
		sm->identity = os_malloc(attr->identity_len);
		if (sm->identity) {
			os_memcpy(sm->identity, attr->identity,
				  attr->identity_len);
			sm->identity_len = attr->identity_len;
		}
	}

	identity = NULL;
	identity_len = 0;

	if (sm->identity && sm->identity_len > 0 &&
	    sm->identity[0] == EAP_SIM_PERMANENT_PREFIX) {
		identity = sm->identity;
		identity_len = sm->identity_len;
	} else {
		identity = eap_sim_db_get_permanent(sm->eap_sim_db_priv,
						    sm->identity,
						    sm->identity_len,
						    &identity_len);
		if (identity == NULL) {
			data->reauth = eap_sim_db_get_reauth_entry(
				sm->eap_sim_db_priv, sm->identity,
				sm->identity_len);
			if (data->reauth) {
				wpa_printf(MSG_DEBUG, "EAP-SIM: Using fast "
					   "re-authentication");
				identity = data->reauth->identity;
				identity_len = data->reauth->identity_len;
				data->counter = data->reauth->counter;
				os_memcpy(data->mk, data->reauth->mk,
					  EAP_SIM_MK_LEN);
			}
		}
	}

	if (identity == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Could not get proper permanent"
			   " user name");
		eap_sim_state(data, FAILURE);
		return;
	}

	wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM: Identity",
			  identity, identity_len);

	if (data->reauth) {
		eap_sim_state(data, REAUTH);
		return;
	}

	if (attr->nonce_mt == NULL || attr->selected_version < 0) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Start/Response missing "
			   "required attributes");
		eap_sim_state(data, FAILURE);
		return;
	}

	if (!eap_sim_supported_ver(data, attr->selected_version)) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Peer selected unsupported "
			   "version %d", attr->selected_version);
		eap_sim_state(data, FAILURE);
		return;
	}

	data->counter = 0; /* reset re-auth counter since this is full auth */
	data->reauth = NULL;

	data->num_chal = eap_sim_db_get_gsm_triplets(
		sm->eap_sim_db_priv, identity, identity_len,
		EAP_SIM_MAX_CHAL,
		(u8 *) data->rand, (u8 *) data->kc, (u8 *) data->sres, sm);
	if (data->num_chal == EAP_SIM_DB_PENDING) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: GSM authentication triplets "
			   "not yet available - pending request");
		sm->method_pending = METHOD_PENDING_WAIT;
		return;
	}
	if (data->num_chal < 2) {
		wpa_printf(MSG_INFO, "EAP-SIM: Failed to get GSM "
			   "authentication triplets for the peer");
		eap_sim_state(data, FAILURE);
		return;
	}

	identity_len = sm->identity_len;
	while (identity_len > 0 && sm->identity[identity_len - 1] == '\0') {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Workaround - drop last null "
			   "character from identity");
		identity_len--;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM: Identity for MK derivation",
			  sm->identity, identity_len);

	os_memcpy(data->nonce_mt, attr->nonce_mt, EAP_SIM_NONCE_MT_LEN);
	WPA_PUT_BE16(ver_list, EAP_SIM_VERSION);
	eap_sim_derive_mk(sm->identity, identity_len, attr->nonce_mt,
			  attr->selected_version, ver_list, sizeof(ver_list),
			  data->num_chal, (const u8 *) data->kc, data->mk);
	eap_sim_derive_keys(data->mk, data->k_encr, data->k_aut, data->msk,
			    data->emsk);

	eap_sim_state(data, CHALLENGE);
}


static void eap_sim_process_challenge(struct eap_sm *sm,
				      struct eap_sim_data *data,
				      struct wpabuf *respData,
				      struct eap_sim_attrs *attr)
{
	const u8 *identity;
	size_t identity_len;

	if (attr->mac == NULL ||
	    eap_sim_verify_mac(data->k_aut, respData, attr->mac,
			       (u8 *) data->sres,
			       data->num_chal * EAP_SIM_SRES_LEN)) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Challenge message "
			   "did not include valid AT_MAC");
		eap_sim_state(data, FAILURE);
		return;
	}

	wpa_printf(MSG_DEBUG, "EAP-SIM: Challenge response includes the "
		   "correct AT_MAC");
	if (sm->eap_sim_aka_result_ind && attr->result_ind) {
		data->use_result_ind = 1;
		data->notification = EAP_SIM_SUCCESS;
		eap_sim_state(data, NOTIFICATION);
	} else
		eap_sim_state(data, SUCCESS);

	identity = eap_sim_db_get_permanent(sm->eap_sim_db_priv, sm->identity,
					    sm->identity_len, &identity_len);
	if (identity == NULL) {
		identity = sm->identity;
		identity_len = sm->identity_len;
	}

	if (data->next_pseudonym) {
		eap_sim_db_add_pseudonym(sm->eap_sim_db_priv, identity,
					 identity_len,
					 data->next_pseudonym);
		data->next_pseudonym = NULL;
	}
	if (data->next_reauth_id) {
		eap_sim_db_add_reauth(sm->eap_sim_db_priv, identity,
				      identity_len,
				      data->next_reauth_id, data->counter + 1,
				      data->mk);
		data->next_reauth_id = NULL;
	}
}


static void eap_sim_process_reauth(struct eap_sm *sm,
				   struct eap_sim_data *data,
				   struct wpabuf *respData,
				   struct eap_sim_attrs *attr)
{
	struct eap_sim_attrs eattr;
	u8 *decrypted = NULL;
	const u8 *identity, *id2;
	size_t identity_len, id2_len;

	if (attr->mac == NULL ||
	    eap_sim_verify_mac(data->k_aut, respData, attr->mac, data->nonce_s,
			       EAP_SIM_NONCE_S_LEN)) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Re-authentication message "
			   "did not include valid AT_MAC");
		goto fail;
	}

	if (attr->encr_data == NULL || attr->iv == NULL) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Reauthentication "
			   "message did not include encrypted data");
		goto fail;
	}

	decrypted = eap_sim_parse_encr(data->k_encr, attr->encr_data,
				       attr->encr_data_len, attr->iv, &eattr,
				       0);
	if (decrypted == NULL) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Failed to parse encrypted "
			   "data from reauthentication message");
		goto fail;
	}

	if (eattr.counter != data->counter) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Re-authentication message "
			   "used incorrect counter %u, expected %u",
			   eattr.counter, data->counter);
		goto fail;
	}
	os_free(decrypted);
	decrypted = NULL;

	wpa_printf(MSG_DEBUG, "EAP-SIM: Re-authentication response includes "
		   "the correct AT_MAC");
	if (sm->eap_sim_aka_result_ind && attr->result_ind) {
		data->use_result_ind = 1;
		data->notification = EAP_SIM_SUCCESS;
		eap_sim_state(data, NOTIFICATION);
	} else
		eap_sim_state(data, SUCCESS);

	if (data->reauth) {
		identity = data->reauth->identity;
		identity_len = data->reauth->identity_len;
	} else {
		identity = sm->identity;
		identity_len = sm->identity_len;
	}

	id2 = eap_sim_db_get_permanent(sm->eap_sim_db_priv, identity,
				       identity_len, &id2_len);
	if (id2) {
		identity = id2;
		identity_len = id2_len;
	}

	if (data->next_pseudonym) {
		eap_sim_db_add_pseudonym(sm->eap_sim_db_priv, identity,
					 identity_len, data->next_pseudonym);
		data->next_pseudonym = NULL;
	}
	if (data->next_reauth_id) {
		eap_sim_db_add_reauth(sm->eap_sim_db_priv, identity,
				      identity_len, data->next_reauth_id,
				      data->counter + 1, data->mk);
		data->next_reauth_id = NULL;
	} else {
		eap_sim_db_remove_reauth(sm->eap_sim_db_priv, data->reauth);
		data->reauth = NULL;
	}

	return;

fail:
	eap_sim_state(data, FAILURE);
	eap_sim_db_remove_reauth(sm->eap_sim_db_priv, data->reauth);
	data->reauth = NULL;
	os_free(decrypted);
}


static void eap_sim_process_client_error(struct eap_sm *sm,
					 struct eap_sim_data *data,
					 struct wpabuf *respData,
					 struct eap_sim_attrs *attr)
{
	wpa_printf(MSG_DEBUG, "EAP-SIM: Client reported error %d",
		   attr->client_error_code);
	if (data->notification == EAP_SIM_SUCCESS && data->use_result_ind)
		eap_sim_state(data, SUCCESS);
	else
		eap_sim_state(data, FAILURE);
}


static void eap_sim_process_notification(struct eap_sm *sm,
					 struct eap_sim_data *data,
					 struct wpabuf *respData,
					 struct eap_sim_attrs *attr)
{
	wpa_printf(MSG_DEBUG, "EAP-SIM: Client replied to notification");
	if (data->notification == EAP_SIM_SUCCESS && data->use_result_ind)
		eap_sim_state(data, SUCCESS);
	else
		eap_sim_state(data, FAILURE);
}


static void eap_sim_process(struct eap_sm *sm, void *priv,
			    struct wpabuf *respData)
{
	struct eap_sim_data *data = priv;
	const u8 *pos, *end;
	u8 subtype;
	size_t len;
	struct eap_sim_attrs attr;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_SIM, respData, &len);
	if (pos == NULL || len < 3)
		return;

	end = pos + len;
	subtype = *pos;
	pos += 3;

	if (eap_sim_parse_attr(pos, end, &attr, 0, 0)) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Failed to parse attributes");
		eap_sim_state(data, FAILURE);
		return;
	}

	if (subtype == EAP_SIM_SUBTYPE_CLIENT_ERROR) {
		eap_sim_process_client_error(sm, data, respData, &attr);
		return;
	}

	switch (data->state) {
	case START:
		eap_sim_process_start(sm, data, respData, &attr);
		break;
	case CHALLENGE:
		eap_sim_process_challenge(sm, data, respData, &attr);
		break;
	case REAUTH:
		eap_sim_process_reauth(sm, data, respData, &attr);
		break;
	case NOTIFICATION:
		eap_sim_process_notification(sm, data, respData, &attr);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-SIM: Unknown state %d in "
			   "process", data->state);
		break;
	}
}


static Boolean eap_sim_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_sim_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_sim_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_sim_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_malloc(EAP_SIM_KEYING_DATA_LEN);
	if (key == NULL)
		return NULL;
	os_memcpy(key, data->msk, EAP_SIM_KEYING_DATA_LEN);
	*len = EAP_SIM_KEYING_DATA_LEN;
	return key;
}


static u8 * eap_sim_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_sim_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_malloc(EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;
	os_memcpy(key, data->emsk, EAP_EMSK_LEN);
	*len = EAP_EMSK_LEN;
	return key;
}


static Boolean eap_sim_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_sim_data *data = priv;
	return data->state == SUCCESS;
}


int eap_server_sim_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_SIM, "SIM");
	if (eap == NULL)
		return -1;

	eap->init = eap_sim_init;
	eap->reset = eap_sim_reset;
	eap->buildReq = eap_sim_buildReq;
	eap->check = eap_sim_check;
	eap->process = eap_sim_process;
	eap->isDone = eap_sim_isDone;
	eap->getKey = eap_sim_getKey;
	eap->isSuccess = eap_sim_isSuccess;
	eap->get_emsk = eap_sim_get_emsk;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}
