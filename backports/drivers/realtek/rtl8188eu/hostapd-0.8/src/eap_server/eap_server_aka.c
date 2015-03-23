/*
 * hostapd / EAP-AKA (RFC 4187) and EAP-AKA' (draft-arkko-eap-aka-kdf)
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
#include "crypto/sha256.h"
#include "crypto/crypto.h"
#include "crypto/random.h"
#include "eap_common/eap_sim_common.h"
#include "eap_server/eap_i.h"
#include "eap_server/eap_sim_db.h"


struct eap_aka_data {
	u8 mk[EAP_SIM_MK_LEN];
	u8 nonce_s[EAP_SIM_NONCE_S_LEN];
	u8 k_aut[EAP_AKA_PRIME_K_AUT_LEN];
	u8 k_encr[EAP_SIM_K_ENCR_LEN];
	u8 k_re[EAP_AKA_PRIME_K_RE_LEN]; /* EAP-AKA' only */
	u8 msk[EAP_SIM_KEYING_DATA_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 rand[EAP_AKA_RAND_LEN];
	u8 autn[EAP_AKA_AUTN_LEN];
	u8 ck[EAP_AKA_CK_LEN];
	u8 ik[EAP_AKA_IK_LEN];
	u8 res[EAP_AKA_RES_MAX_LEN];
	size_t res_len;
	enum {
		IDENTITY, CHALLENGE, REAUTH, NOTIFICATION, SUCCESS, FAILURE
	} state;
	char *next_pseudonym;
	char *next_reauth_id;
	u16 counter;
	struct eap_sim_reauth *reauth;
	int auts_reported; /* whether the current AUTS has been reported to the
			    * eap_sim_db */
	u16 notification;
	int use_result_ind;

	struct wpabuf *id_msgs;
	int pending_id;
	u8 eap_method;
	u8 *network_name;
	size_t network_name_len;
	u16 kdf;
};


static void eap_aka_determine_identity(struct eap_sm *sm,
				       struct eap_aka_data *data,
				       int before_identity, int after_reauth);


static const char * eap_aka_state_txt(int state)
{
	switch (state) {
	case IDENTITY:
		return "IDENTITY";
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


static void eap_aka_state(struct eap_aka_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-AKA: %s -> %s",
		   eap_aka_state_txt(data->state),
		   eap_aka_state_txt(state));
	data->state = state;
}


static void * eap_aka_init(struct eap_sm *sm)
{
	struct eap_aka_data *data;

	if (sm->eap_sim_db_priv == NULL) {
		wpa_printf(MSG_WARNING, "EAP-AKA: eap_sim_db not configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	data->eap_method = EAP_TYPE_AKA;

	data->state = IDENTITY;
	eap_aka_determine_identity(sm, data, 1, 0);
	data->pending_id = -1;

	return data;
}


#ifdef EAP_SERVER_AKA_PRIME
static void * eap_aka_prime_init(struct eap_sm *sm)
{
	struct eap_aka_data *data;
	/* TODO: make ANID configurable; see 3GPP TS 24.302 */
	char *network_name = "WLAN";

	if (sm->eap_sim_db_priv == NULL) {
		wpa_printf(MSG_WARNING, "EAP-AKA: eap_sim_db not configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	data->eap_method = EAP_TYPE_AKA_PRIME;
	data->network_name = os_malloc(os_strlen(network_name));
	if (data->network_name == NULL) {
		os_free(data);
		return NULL;
	}

	data->network_name_len = os_strlen(network_name);
	os_memcpy(data->network_name, network_name, data->network_name_len);

	data->state = IDENTITY;
	eap_aka_determine_identity(sm, data, 1, 0);
	data->pending_id = -1;

	return data;
}
#endif /* EAP_SERVER_AKA_PRIME */


static void eap_aka_reset(struct eap_sm *sm, void *priv)
{
	struct eap_aka_data *data = priv;
	os_free(data->next_pseudonym);
	os_free(data->next_reauth_id);
	wpabuf_free(data->id_msgs);
	os_free(data->network_name);
	os_free(data);
}


static int eap_aka_add_id_msg(struct eap_aka_data *data,
			      const struct wpabuf *msg)
{
	if (msg == NULL)
		return -1;

	if (data->id_msgs == NULL) {
		data->id_msgs = wpabuf_dup(msg);
		return data->id_msgs == NULL ? -1 : 0;
	}

	if (wpabuf_resize(&data->id_msgs, wpabuf_len(msg)) < 0)
		return -1;
	wpabuf_put_buf(data->id_msgs, msg);

	return 0;
}


static void eap_aka_add_checkcode(struct eap_aka_data *data,
				  struct eap_sim_msg *msg)
{
	const u8 *addr;
	size_t len;
	u8 hash[SHA256_MAC_LEN];

	wpa_printf(MSG_DEBUG, "   AT_CHECKCODE");

	if (data->id_msgs == NULL) {
		/*
		 * No EAP-AKA/Identity packets were exchanged - send empty
		 * checkcode.
		 */
		eap_sim_msg_add(msg, EAP_SIM_AT_CHECKCODE, 0, NULL, 0);
		return;
	}

	/* Checkcode is SHA1 hash over all EAP-AKA/Identity packets. */
	addr = wpabuf_head(data->id_msgs);
	len = wpabuf_len(data->id_msgs);
	wpa_hexdump(MSG_MSGDUMP, "EAP-AKA: AT_CHECKCODE data", addr, len);
	if (data->eap_method == EAP_TYPE_AKA_PRIME)
		sha256_vector(1, &addr, &len, hash);
	else
		sha1_vector(1, &addr, &len, hash);

	eap_sim_msg_add(msg, EAP_SIM_AT_CHECKCODE, 0, hash,
			data->eap_method == EAP_TYPE_AKA_PRIME ?
			EAP_AKA_PRIME_CHECKCODE_LEN : EAP_AKA_CHECKCODE_LEN);
}


static int eap_aka_verify_checkcode(struct eap_aka_data *data,
				    const u8 *checkcode, size_t checkcode_len)
{
	const u8 *addr;
	size_t len;
	u8 hash[SHA256_MAC_LEN];
	size_t hash_len;

	if (checkcode == NULL)
		return -1;

	if (data->id_msgs == NULL) {
		if (checkcode_len != 0) {
			wpa_printf(MSG_DEBUG, "EAP-AKA: Checkcode from peer "
				   "indicates that AKA/Identity messages were "
				   "used, but they were not");
			return -1;
		}
		return 0;
	}

	hash_len = data->eap_method == EAP_TYPE_AKA_PRIME ?
		EAP_AKA_PRIME_CHECKCODE_LEN : EAP_AKA_CHECKCODE_LEN;

	if (checkcode_len != hash_len) {
		wpa_printf(MSG_DEBUG, "EAP-AKA: Checkcode from peer indicates "
			   "that AKA/Identity message were not used, but they "
			   "were");
		return -1;
	}

	/* Checkcode is SHA1 hash over all EAP-AKA/Identity packets. */
	addr = wpabuf_head(data->id_msgs);
	len = wpabuf_len(data->id_msgs);
	if (data->eap_method == EAP_TYPE_AKA_PRIME)
		sha256_vector(1, &addr, &len, hash);
	else
		sha1_vector(1, &addr, &len, hash);

	if (os_memcmp(hash, checkcode, hash_len) != 0) {
		wpa_printf(MSG_DEBUG, "EAP-AKA: Mismatch in AT_CHECKCODE");
		return -1;
	}

	return 0;
}


static struct wpabuf * eap_aka_build_identity(struct eap_sm *sm,
					      struct eap_aka_data *data, u8 id)
{
	struct eap_sim_msg *msg;
	struct wpabuf *buf;

	wpa_printf(MSG_DEBUG, "EAP-AKA: Generating Identity");
	msg = eap_sim_msg_init(EAP_CODE_REQUEST, id, data->eap_method,
			       EAP_AKA_SUBTYPE_IDENTITY);
	if (eap_sim_db_identity_known(sm->eap_sim_db_priv, sm->identity,
				      sm->identity_len)) {
		wpa_printf(MSG_DEBUG, "   AT_PERMANENT_ID_REQ");
		eap_sim_msg_add(msg, EAP_SIM_AT_PERMANENT_ID_REQ, 0, NULL, 0);
	} else {
		/*
		 * RFC 4187, Chap. 4.1.4 recommends that identity from EAP is
		 * ignored and the AKA/Identity is used to request the
		 * identity.
		 */
		wpa_printf(MSG_DEBUG, "   AT_ANY_ID_REQ");
		eap_sim_msg_add(msg, EAP_SIM_AT_ANY_ID_REQ, 0, NULL, 0);
	}
	buf = eap_sim_msg_finish(msg, NULL, NULL, 0);
	if (eap_aka_add_id_msg(data, buf) < 0) {
		wpabuf_free(buf);
		return NULL;
	}
	data->pending_id = id;
	return buf;
}


static int eap_aka_build_encr(struct eap_sm *sm, struct eap_aka_data *data,
			      struct eap_sim_msg *msg, u16 counter,
			      const u8 *nonce_s)
{
	os_free(data->next_pseudonym);
	data->next_pseudonym =
		eap_sim_db_get_next_pseudonym(sm->eap_sim_db_priv, 1);
	os_free(data->next_reauth_id);
	if (data->counter <= EAP_AKA_MAX_FAST_REAUTHS) {
		data->next_reauth_id =
			eap_sim_db_get_next_reauth_id(sm->eap_sim_db_priv, 1);
	} else {
		wpa_printf(MSG_DEBUG, "EAP-AKA: Max fast re-authentication "
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
		wpa_printf(MSG_WARNING, "EAP-AKA: Failed to encrypt "
			   "AT_ENCR_DATA");
		return -1;
	}

	return 0;
}


static struct wpabuf * eap_aka_build_challenge(struct eap_sm *sm,
					       struct eap_aka_data *data,
					       u8 id)
{
	struct eap_sim_msg *msg;

	wpa_printf(MSG_DEBUG, "EAP-AKA: Generating Challenge");
	msg = eap_sim_msg_init(EAP_CODE_REQUEST, id, data->eap_method,
			       EAP_AKA_SUBTYPE_CHALLENGE);
	wpa_printf(MSG_DEBUG, "   AT_RAND");
	eap_sim_msg_add(msg, EAP_SIM_AT_RAND, 0, data->rand, EAP_AKA_RAND_LEN);
	wpa_printf(MSG_DEBUG, "   AT_AUTN");
	eap_sim_msg_add(msg, EAP_SIM_AT_AUTN, 0, data->autn, EAP_AKA_AUTN_LEN);
	if (data->eap_method == EAP_TYPE_AKA_PRIME) {
		if (data->kdf) {
			/* Add the selected KDF into the beginning */
			wpa_printf(MSG_DEBUG, "   AT_KDF");
			eap_sim_msg_add(msg, EAP_SIM_AT_KDF, data->kdf,
					NULL, 0);
		}
		wpa_printf(MSG_DEBUG, "   AT_KDF");
		eap_sim_msg_add(msg, EAP_SIM_AT_KDF, EAP_AKA_PRIME_KDF,
				NULL, 0);
		wpa_printf(MSG_DEBUG, "   AT_KDF_INPUT");
		eap_sim_msg_add(msg, EAP_SIM_AT_KDF_INPUT,
				data->network_name_len,
				data->network_name, data->network_name_len);
	}

	if (eap_aka_build_encr(sm, data, msg, 0, NULL)) {
		eap_sim_msg_free(msg);
		return NULL;
	}

	eap_aka_add_checkcode(data, msg);

	if (sm->eap_sim_aka_result_ind) {
		wpa_printf(MSG_DEBUG, "   AT_RESULT_IND");
		eap_sim_msg_add(msg, EAP_SIM_AT_RESULT_IND, 0, NULL, 0);
	}

#ifdef EAP_SERVER_AKA_PRIME
	if (data->eap_method == EAP_TYPE_AKA) {
		u16 flags = 0;
		int i;
		int aka_prime_preferred = 0;

		i = 0;
		while (sm->user && i < EAP_MAX_METHODS &&
		       (sm->user->methods[i].vendor != EAP_VENDOR_IETF ||
			sm->user->methods[i].method != EAP_TYPE_NONE)) {
			if (sm->user->methods[i].vendor == EAP_VENDOR_IETF) {
				if (sm->user->methods[i].method ==
				    EAP_TYPE_AKA)
					break;
				if (sm->user->methods[i].method ==
				    EAP_TYPE_AKA_PRIME) {
					aka_prime_preferred = 1;
					break;
				}
			}
			i++;
		}

		if (aka_prime_preferred)
			flags |= EAP_AKA_BIDDING_FLAG_D;
		eap_sim_msg_add(msg, EAP_SIM_AT_BIDDING, flags, NULL, 0);
	}
#endif /* EAP_SERVER_AKA_PRIME */

	wpa_printf(MSG_DEBUG, "   AT_MAC");
	eap_sim_msg_add_mac(msg, EAP_SIM_AT_MAC);
	return eap_sim_msg_finish(msg, data->k_aut, NULL, 0);
}


static struct wpabuf * eap_aka_build_reauth(struct eap_sm *sm,
					    struct eap_aka_data *data, u8 id)
{
	struct eap_sim_msg *msg;

	wpa_printf(MSG_DEBUG, "EAP-AKA: Generating Re-authentication");

	if (random_get_bytes(data->nonce_s, EAP_SIM_NONCE_S_LEN))
		return NULL;
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-AKA: NONCE_S",
			data->nonce_s, EAP_SIM_NONCE_S_LEN);

	if (data->eap_method == EAP_TYPE_AKA_PRIME) {
		eap_aka_prime_derive_keys_reauth(data->k_re, data->counter,
						 sm->identity,
						 sm->identity_len,
						 data->nonce_s,
						 data->msk, data->emsk);
	} else {
		eap_sim_derive_keys(data->mk, data->k_encr, data->k_aut,
				    data->msk, data->emsk);
		eap_sim_derive_keys_reauth(data->counter, sm->identity,
					   sm->identity_len, data->nonce_s,
					   data->mk, data->msk, data->emsk);
	}

	msg = eap_sim_msg_init(EAP_CODE_REQUEST, id, data->eap_method,
			       EAP_AKA_SUBTYPE_REAUTHENTICATION);

	if (eap_aka_build_encr(sm, data, msg, data->counter, data->nonce_s)) {
		eap_sim_msg_free(msg);
		return NULL;
	}

	eap_aka_add_checkcode(data, msg);

	if (sm->eap_sim_aka_result_ind) {
		wpa_printf(MSG_DEBUG, "   AT_RESULT_IND");
		eap_sim_msg_add(msg, EAP_SIM_AT_RESULT_IND, 0, NULL, 0);
	}

	wpa_printf(MSG_DEBUG, "   AT_MAC");
	eap_sim_msg_add_mac(msg, EAP_SIM_AT_MAC);
	return eap_sim_msg_finish(msg, data->k_aut, NULL, 0);
}


static struct wpabuf * eap_aka_build_notification(struct eap_sm *sm,
						  struct eap_aka_data *data,
						  u8 id)
{
	struct eap_sim_msg *msg;

	wpa_printf(MSG_DEBUG, "EAP-AKA: Generating Notification");
	msg = eap_sim_msg_init(EAP_CODE_REQUEST, id, data->eap_method,
			       EAP_AKA_SUBTYPE_NOTIFICATION);
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
				wpa_printf(MSG_WARNING, "EAP-AKA: Failed to "
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


static struct wpabuf * eap_aka_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_aka_data *data = priv;

	data->auts_reported = 0;
	switch (data->state) {
	case IDENTITY:
		return eap_aka_build_identity(sm, data, id);
	case CHALLENGE:
		return eap_aka_build_challenge(sm, data, id);
	case REAUTH:
		return eap_aka_build_reauth(sm, data, id);
	case NOTIFICATION:
		return eap_aka_build_notification(sm, data, id);
	default:
		wpa_printf(MSG_DEBUG, "EAP-AKA: Unknown state %d in "
			   "buildReq", data->state);
		break;
	}
	return NULL;
}


static Boolean eap_aka_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_aka_data *data = priv;
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, data->eap_method, respData,
			       &len);
	if (pos == NULL || len < 3) {
		wpa_printf(MSG_INFO, "EAP-AKA: Invalid frame");
		return TRUE;
	}

	return FALSE;
}


static Boolean eap_aka_subtype_ok(struct eap_aka_data *data, u8 subtype)
{
	if (subtype == EAP_AKA_SUBTYPE_CLIENT_ERROR ||
	    subtype == EAP_AKA_SUBTYPE_AUTHENTICATION_REJECT)
		return FALSE;

	switch (data->state) {
	case IDENTITY:
		if (subtype != EAP_AKA_SUBTYPE_IDENTITY) {
			wpa_printf(MSG_INFO, "EAP-AKA: Unexpected response "
				   "subtype %d", subtype);
			return TRUE;
		}
		break;
	case CHALLENGE:
		if (subtype != EAP_AKA_SUBTYPE_CHALLENGE &&
		    subtype != EAP_AKA_SUBTYPE_SYNCHRONIZATION_FAILURE) {
			wpa_printf(MSG_INFO, "EAP-AKA: Unexpected response "
				   "subtype %d", subtype);
			return TRUE;
		}
		break;
	case REAUTH:
		if (subtype != EAP_AKA_SUBTYPE_REAUTHENTICATION) {
			wpa_printf(MSG_INFO, "EAP-AKA: Unexpected response "
				   "subtype %d", subtype);
			return TRUE;
		}
		break;
	case NOTIFICATION:
		if (subtype != EAP_AKA_SUBTYPE_NOTIFICATION) {
			wpa_printf(MSG_INFO, "EAP-AKA: Unexpected response "
				   "subtype %d", subtype);
			return TRUE;
		}
		break;
	default:
		wpa_printf(MSG_INFO, "EAP-AKA: Unexpected state (%d) for "
			   "processing a response", data->state);
		return TRUE;
	}

	return FALSE;
}


static void eap_aka_determine_identity(struct eap_sm *sm,
				       struct eap_aka_data *data,
				       int before_identity, int after_reauth)
{
	const u8 *identity;
	size_t identity_len;
	int res;

	identity = NULL;
	identity_len = 0;

	if (after_reauth && data->reauth) {
		identity = data->reauth->identity;
		identity_len = data->reauth->identity_len;
	} else if (sm->identity && sm->identity_len > 0 &&
		   sm->identity[0] == EAP_AKA_PERMANENT_PREFIX) {
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
			if (data->reauth &&
			    data->reauth->aka_prime !=
			    (data->eap_method == EAP_TYPE_AKA_PRIME)) {
				wpa_printf(MSG_DEBUG, "EAP-AKA: Reauth data "
					   "was for different AKA version");
				data->reauth = NULL;
			}
			if (data->reauth) {
				wpa_printf(MSG_DEBUG, "EAP-AKA: Using fast "
					   "re-authentication");
				identity = data->reauth->identity;
				identity_len = data->reauth->identity_len;
				data->counter = data->reauth->counter;
				if (data->eap_method == EAP_TYPE_AKA_PRIME) {
					os_memcpy(data->k_encr,
						  data->reauth->k_encr,
						  EAP_SIM_K_ENCR_LEN);
					os_memcpy(data->k_aut,
						  data->reauth->k_aut,
						  EAP_AKA_PRIME_K_AUT_LEN);
					os_memcpy(data->k_re,
						  data->reauth->k_re,
						  EAP_AKA_PRIME_K_RE_LEN);
				} else {
					os_memcpy(data->mk, data->reauth->mk,
						  EAP_SIM_MK_LEN);
				}
			}
		}
	}

	if (identity == NULL ||
	    eap_sim_db_identity_known(sm->eap_sim_db_priv, sm->identity,
				      sm->identity_len) < 0) {
		if (before_identity) {
			wpa_printf(MSG_DEBUG, "EAP-AKA: Permanent user name "
				   "not known - send AKA-Identity request");
			eap_aka_state(data, IDENTITY);
			return;
		} else {
			wpa_printf(MSG_DEBUG, "EAP-AKA: Unknown whether the "
				   "permanent user name is known; try to use "
				   "it");
			/* eap_sim_db_get_aka_auth() will report failure, if
			 * this identity is not known. */
		}
	}

	wpa_hexdump_ascii(MSG_DEBUG, "EAP-AKA: Identity",
			  identity, identity_len);

	if (!after_reauth && data->reauth) {
		eap_aka_state(data, REAUTH);
		return;
	}

	res = eap_sim_db_get_aka_auth(sm->eap_sim_db_priv, identity,
				      identity_len, data->rand, data->autn,
				      data->ik, data->ck, data->res,
				      &data->res_len, sm);
	if (res == EAP_SIM_DB_PENDING) {
		wpa_printf(MSG_DEBUG, "EAP-AKA: AKA authentication data "
			   "not yet available - pending request");
		sm->method_pending = METHOD_PENDING_WAIT;
		return;
	}

#ifdef EAP_SERVER_AKA_PRIME
	if (data->eap_method == EAP_TYPE_AKA_PRIME) {
		/* Note: AUTN = (SQN ^ AK) || AMF || MAC which gives us the
		 * needed 6-octet SQN ^AK for CK',IK' derivation */
		eap_aka_prime_derive_ck_ik_prime(data->ck, data->ik,
						 data->autn,
						 data->network_name,
						 data->network_name_len);
	}
#endif /* EAP_SERVER_AKA_PRIME */

	data->reauth = NULL;
	data->counter = 0; /* reset re-auth counter since this is full auth */

	if (res != 0) {
		wpa_printf(MSG_INFO, "EAP-AKA: Failed to get AKA "
			   "authentication data for the peer");
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}
	if (sm->method_pending == METHOD_PENDING_WAIT) {
		wpa_printf(MSG_DEBUG, "EAP-AKA: AKA authentication data "
			   "available - abort pending wait");
		sm->method_pending = METHOD_PENDING_NONE;
	}

	identity_len = sm->identity_len;
	while (identity_len > 0 && sm->identity[identity_len - 1] == '\0') {
		wpa_printf(MSG_DEBUG, "EAP-AKA: Workaround - drop last null "
			   "character from identity");
		identity_len--;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-AKA: Identity for MK derivation",
			  sm->identity, identity_len);

	if (data->eap_method == EAP_TYPE_AKA_PRIME) {
		eap_aka_prime_derive_keys(identity, identity_len, data->ik,
					  data->ck, data->k_encr, data->k_aut,
					  data->k_re, data->msk, data->emsk);
	} else {
		eap_aka_derive_mk(sm->identity, identity_len, data->ik,
				  data->ck, data->mk);
		eap_sim_derive_keys(data->mk, data->k_encr, data->k_aut,
				    data->msk, data->emsk);
	}

	eap_aka_state(data, CHALLENGE);
}


static void eap_aka_process_identity(struct eap_sm *sm,
				     struct eap_aka_data *data,
				     struct wpabuf *respData,
				     struct eap_sim_attrs *attr)
{
	wpa_printf(MSG_DEBUG, "EAP-AKA: Processing Identity");

	if (attr->mac || attr->iv || attr->encr_data) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Unexpected attribute "
			   "received in EAP-Response/AKA-Identity");
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}

	if (attr->identity) {
		os_free(sm->identity);
		sm->identity = os_malloc(attr->identity_len);
		if (sm->identity) {
			os_memcpy(sm->identity, attr->identity,
				  attr->identity_len);
			sm->identity_len = attr->identity_len;
		}
	}

	eap_aka_determine_identity(sm, data, 0, 0);
	if (eap_get_id(respData) == data->pending_id) {
		data->pending_id = -1;
		eap_aka_add_id_msg(data, respData);
	}
}


static int eap_aka_verify_mac(struct eap_aka_data *data,
			      const struct wpabuf *req,
			      const u8 *mac, const u8 *extra,
			      size_t extra_len)
{
	if (data->eap_method == EAP_TYPE_AKA_PRIME)
		return eap_sim_verify_mac_sha256(data->k_aut, req, mac, extra,
						 extra_len);
	return eap_sim_verify_mac(data->k_aut, req, mac, extra, extra_len);
}


static void eap_aka_process_challenge(struct eap_sm *sm,
				      struct eap_aka_data *data,
				      struct wpabuf *respData,
				      struct eap_sim_attrs *attr)
{
	const u8 *identity;
	size_t identity_len;

	wpa_printf(MSG_DEBUG, "EAP-AKA: Processing Challenge");

#ifdef EAP_SERVER_AKA_PRIME
#if 0
	/* KDF negotiation; to be enabled only after more than one KDF is
	 * supported */
	if (data->eap_method == EAP_TYPE_AKA_PRIME &&
	    attr->kdf_count == 1 && attr->mac == NULL) {
		if (attr->kdf[0] != EAP_AKA_PRIME_KDF) {
			wpa_printf(MSG_WARNING, "EAP-AKA': Peer selected "
				   "unknown KDF");
			data->notification =
				EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
			eap_aka_state(data, NOTIFICATION);
			return;
		}

		data->kdf = attr->kdf[0];

		/* Allow negotiation to continue with the selected KDF by
		 * sending another Challenge message */
		wpa_printf(MSG_DEBUG, "EAP-AKA': KDF %d selected", data->kdf);
		return;
	}
#endif
#endif /* EAP_SERVER_AKA_PRIME */

	if (attr->checkcode &&
	    eap_aka_verify_checkcode(data, attr->checkcode,
				     attr->checkcode_len)) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Invalid AT_CHECKCODE in the "
			   "message");
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}
	if (attr->mac == NULL ||
	    eap_aka_verify_mac(data, respData, attr->mac, NULL, 0)) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Challenge message "
			   "did not include valid AT_MAC");
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}

	/*
	 * AT_RES is padded, so verify that there is enough room for RES and
	 * that the RES length in bits matches with the expected RES.
	 */
	if (attr->res == NULL || attr->res_len < data->res_len ||
	    attr->res_len_bits != data->res_len * 8 ||
	    os_memcmp(attr->res, data->res, data->res_len) != 0) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Challenge message did not "
			   "include valid AT_RES (attr len=%lu, res len=%lu "
			   "bits, expected %lu bits)",
			   (unsigned long) attr->res_len,
			   (unsigned long) attr->res_len_bits,
			   (unsigned long) data->res_len * 8);
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}

	wpa_printf(MSG_DEBUG, "EAP-AKA: Challenge response includes the "
		   "correct AT_MAC");
	if (sm->eap_sim_aka_result_ind && attr->result_ind) {
		data->use_result_ind = 1;
		data->notification = EAP_SIM_SUCCESS;
		eap_aka_state(data, NOTIFICATION);
	} else
		eap_aka_state(data, SUCCESS);

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
		if (data->eap_method == EAP_TYPE_AKA_PRIME) {
#ifdef EAP_SERVER_AKA_PRIME
			eap_sim_db_add_reauth_prime(sm->eap_sim_db_priv,
						    identity,
						    identity_len,
						    data->next_reauth_id,
						    data->counter + 1,
						    data->k_encr, data->k_aut,
						    data->k_re);
#endif /* EAP_SERVER_AKA_PRIME */
		} else {
			eap_sim_db_add_reauth(sm->eap_sim_db_priv, identity,
					      identity_len,
					      data->next_reauth_id,
					      data->counter + 1,
					      data->mk);
		}
		data->next_reauth_id = NULL;
	}
}


static void eap_aka_process_sync_failure(struct eap_sm *sm,
					 struct eap_aka_data *data,
					 struct wpabuf *respData,
					 struct eap_sim_attrs *attr)
{
	wpa_printf(MSG_DEBUG, "EAP-AKA: Processing Synchronization-Failure");

	if (attr->auts == NULL) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Synchronization-Failure "
			   "message did not include valid AT_AUTS");
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}

	/* Avoid re-reporting AUTS when processing pending EAP packet by
	 * maintaining a local flag stating whether this AUTS has already been
	 * reported. */
	if (!data->auts_reported &&
	    eap_sim_db_resynchronize(sm->eap_sim_db_priv, sm->identity,
				     sm->identity_len, attr->auts,
				     data->rand)) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Resynchronization failed");
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}
	data->auts_reported = 1;

	/* Try again after resynchronization */
	eap_aka_determine_identity(sm, data, 0, 0);
}


static void eap_aka_process_reauth(struct eap_sm *sm,
				   struct eap_aka_data *data,
				   struct wpabuf *respData,
				   struct eap_sim_attrs *attr)
{
	struct eap_sim_attrs eattr;
	u8 *decrypted = NULL;
	const u8 *identity, *id2;
	size_t identity_len, id2_len;

	wpa_printf(MSG_DEBUG, "EAP-AKA: Processing Reauthentication");

	if (attr->mac == NULL ||
	    eap_aka_verify_mac(data, respData, attr->mac, data->nonce_s,
			       EAP_SIM_NONCE_S_LEN)) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Re-authentication message "
			   "did not include valid AT_MAC");
		goto fail;
	}

	if (attr->encr_data == NULL || attr->iv == NULL) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Reauthentication "
			   "message did not include encrypted data");
		goto fail;
	}

	decrypted = eap_sim_parse_encr(data->k_encr, attr->encr_data,
				       attr->encr_data_len, attr->iv, &eattr,
				       0);
	if (decrypted == NULL) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Failed to parse encrypted "
			   "data from reauthentication message");
		goto fail;
	}

	if (eattr.counter != data->counter) {
		wpa_printf(MSG_WARNING, "EAP-AKA: Re-authentication message "
			   "used incorrect counter %u, expected %u",
			   eattr.counter, data->counter);
		goto fail;
	}
	os_free(decrypted);
	decrypted = NULL;

	wpa_printf(MSG_DEBUG, "EAP-AKA: Re-authentication response includes "
		   "the correct AT_MAC");

	if (eattr.counter_too_small) {
		wpa_printf(MSG_DEBUG, "EAP-AKA: Re-authentication response "
			   "included AT_COUNTER_TOO_SMALL - starting full "
			   "authentication");
		eap_aka_determine_identity(sm, data, 0, 1);
		return;
	}

	if (sm->eap_sim_aka_result_ind && attr->result_ind) {
		data->use_result_ind = 1;
		data->notification = EAP_SIM_SUCCESS;
		eap_aka_state(data, NOTIFICATION);
	} else
		eap_aka_state(data, SUCCESS);

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
		if (data->eap_method == EAP_TYPE_AKA_PRIME) {
#ifdef EAP_SERVER_AKA_PRIME
			eap_sim_db_add_reauth_prime(sm->eap_sim_db_priv,
						    identity,
						    identity_len,
						    data->next_reauth_id,
						    data->counter + 1,
						    data->k_encr, data->k_aut,
						    data->k_re);
#endif /* EAP_SERVER_AKA_PRIME */
		} else {
			eap_sim_db_add_reauth(sm->eap_sim_db_priv, identity,
					      identity_len,
					      data->next_reauth_id,
					      data->counter + 1,
					      data->mk);
		}
		data->next_reauth_id = NULL;
	} else {
		eap_sim_db_remove_reauth(sm->eap_sim_db_priv, data->reauth);
		data->reauth = NULL;
	}

	return;

fail:
	data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
	eap_aka_state(data, NOTIFICATION);
	eap_sim_db_remove_reauth(sm->eap_sim_db_priv, data->reauth);
	data->reauth = NULL;
	os_free(decrypted);
}


static void eap_aka_process_client_error(struct eap_sm *sm,
					 struct eap_aka_data *data,
					 struct wpabuf *respData,
					 struct eap_sim_attrs *attr)
{
	wpa_printf(MSG_DEBUG, "EAP-AKA: Client reported error %d",
		   attr->client_error_code);
	if (data->notification == EAP_SIM_SUCCESS && data->use_result_ind)
		eap_aka_state(data, SUCCESS);
	else
		eap_aka_state(data, FAILURE);
}


static void eap_aka_process_authentication_reject(
	struct eap_sm *sm, struct eap_aka_data *data,
	struct wpabuf *respData, struct eap_sim_attrs *attr)
{
	wpa_printf(MSG_DEBUG, "EAP-AKA: Client rejected authentication");
	eap_aka_state(data, FAILURE);
}


static void eap_aka_process_notification(struct eap_sm *sm,
					 struct eap_aka_data *data,
					 struct wpabuf *respData,
					 struct eap_sim_attrs *attr)
{
	wpa_printf(MSG_DEBUG, "EAP-AKA: Client replied to notification");
	if (data->notification == EAP_SIM_SUCCESS && data->use_result_ind)
		eap_aka_state(data, SUCCESS);
	else
		eap_aka_state(data, FAILURE);
}


static void eap_aka_process(struct eap_sm *sm, void *priv,
			    struct wpabuf *respData)
{
	struct eap_aka_data *data = priv;
	const u8 *pos, *end;
	u8 subtype;
	size_t len;
	struct eap_sim_attrs attr;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, data->eap_method, respData,
			       &len);
	if (pos == NULL || len < 3)
		return;

	end = pos + len;
	subtype = *pos;
	pos += 3;

	if (eap_aka_subtype_ok(data, subtype)) {
		wpa_printf(MSG_DEBUG, "EAP-AKA: Unrecognized or unexpected "
			   "EAP-AKA Subtype in EAP Response");
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}

	if (eap_sim_parse_attr(pos, end, &attr,
			       data->eap_method == EAP_TYPE_AKA_PRIME ? 2 : 1,
			       0)) {
		wpa_printf(MSG_DEBUG, "EAP-AKA: Failed to parse attributes");
		data->notification = EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH;
		eap_aka_state(data, NOTIFICATION);
		return;
	}

	if (subtype == EAP_AKA_SUBTYPE_CLIENT_ERROR) {
		eap_aka_process_client_error(sm, data, respData, &attr);
		return;
	}

	if (subtype == EAP_AKA_SUBTYPE_AUTHENTICATION_REJECT) {
		eap_aka_process_authentication_reject(sm, data, respData,
						      &attr);
		return;
	}

	switch (data->state) {
	case IDENTITY:
		eap_aka_process_identity(sm, data, respData, &attr);
		break;
	case CHALLENGE:
		if (subtype == EAP_AKA_SUBTYPE_SYNCHRONIZATION_FAILURE) {
			eap_aka_process_sync_failure(sm, data, respData,
						     &attr);
		} else {
			eap_aka_process_challenge(sm, data, respData, &attr);
		}
		break;
	case REAUTH:
		eap_aka_process_reauth(sm, data, respData, &attr);
		break;
	case NOTIFICATION:
		eap_aka_process_notification(sm, data, respData, &attr);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-AKA: Unknown state %d in "
			   "process", data->state);
		break;
	}
}


static Boolean eap_aka_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_aka_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_aka_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_aka_data *data = priv;
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


static u8 * eap_aka_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_aka_data *data = priv;
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


static Boolean eap_aka_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_aka_data *data = priv;
	return data->state == SUCCESS;
}


int eap_server_aka_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_AKA, "AKA");
	if (eap == NULL)
		return -1;

	eap->init = eap_aka_init;
	eap->reset = eap_aka_reset;
	eap->buildReq = eap_aka_buildReq;
	eap->check = eap_aka_check;
	eap->process = eap_aka_process;
	eap->isDone = eap_aka_isDone;
	eap->getKey = eap_aka_getKey;
	eap->isSuccess = eap_aka_isSuccess;
	eap->get_emsk = eap_aka_get_emsk;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}


#ifdef EAP_SERVER_AKA_PRIME
int eap_server_aka_prime_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_AKA_PRIME,
				      "AKA'");
	if (eap == NULL)
		return -1;

	eap->init = eap_aka_prime_init;
	eap->reset = eap_aka_reset;
	eap->buildReq = eap_aka_buildReq;
	eap->check = eap_aka_check;
	eap->process = eap_aka_process;
	eap->isDone = eap_aka_isDone;
	eap->getKey = eap_aka_getKey;
	eap->isSuccess = eap_aka_isSuccess;
	eap->get_emsk = eap_aka_get_emsk;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);

	return ret;
}
#endif /* EAP_SERVER_AKA_PRIME */
