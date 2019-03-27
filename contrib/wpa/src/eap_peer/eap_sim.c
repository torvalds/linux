/*
 * EAP peer method: EAP-SIM (RFC 4186)
 * Copyright (c) 2004-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "pcsc_funcs.h"
#include "crypto/milenage.h"
#include "crypto/random.h"
#include "eap_peer/eap_i.h"
#include "eap_config.h"
#include "eap_common/eap_sim_common.h"


struct eap_sim_data {
	u8 *ver_list;
	size_t ver_list_len;
	int selected_version;
	size_t min_num_chal, num_chal;

	u8 kc[3][EAP_SIM_KC_LEN];
	u8 sres[3][EAP_SIM_SRES_LEN];
	u8 nonce_mt[EAP_SIM_NONCE_MT_LEN], nonce_s[EAP_SIM_NONCE_S_LEN];
	u8 mk[EAP_SIM_MK_LEN];
	u8 k_aut[EAP_SIM_K_AUT_LEN];
	u8 k_encr[EAP_SIM_K_ENCR_LEN];
	u8 msk[EAP_SIM_KEYING_DATA_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 rand[3][GSM_RAND_LEN];

	int num_id_req, num_notification;
	u8 *pseudonym;
	size_t pseudonym_len;
	u8 *reauth_id;
	size_t reauth_id_len;
	int reauth;
	unsigned int counter, counter_too_small;
	u8 *last_eap_identity;
	size_t last_eap_identity_len;
	enum {
		CONTINUE, RESULT_SUCCESS, SUCCESS, FAILURE
	} state;
	int result_ind, use_result_ind;
	int use_pseudonym;
	int error_code;
};


#ifndef CONFIG_NO_STDOUT_DEBUG
static const char * eap_sim_state_txt(int state)
{
	switch (state) {
	case CONTINUE:
		return "CONTINUE";
	case RESULT_SUCCESS:
		return "RESULT_SUCCESS";
	case SUCCESS:
		return "SUCCESS";
	case FAILURE:
		return "FAILURE";
	default:
		return "?";
	}
}
#endif /* CONFIG_NO_STDOUT_DEBUG */


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
	struct eap_peer_config *config = eap_get_config(sm);

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	if (random_get_bytes(data->nonce_mt, EAP_SIM_NONCE_MT_LEN)) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Failed to get random data "
			   "for NONCE_MT");
		os_free(data);
		return NULL;
	}

	/* Zero is a valid error code, so we need to initialize */
	data->error_code = NO_EAP_METHOD_ERROR;

	data->min_num_chal = 2;
	if (config && config->phase1) {
		char *pos = os_strstr(config->phase1, "sim_min_num_chal=");
		if (pos) {
			data->min_num_chal = atoi(pos + 17);
			if (data->min_num_chal < 2 || data->min_num_chal > 3) {
				wpa_printf(MSG_WARNING, "EAP-SIM: Invalid "
					   "sim_min_num_chal configuration "
					   "(%lu, expected 2 or 3)",
					   (unsigned long) data->min_num_chal);
				os_free(data);
				return NULL;
			}
			wpa_printf(MSG_DEBUG, "EAP-SIM: Set minimum number of "
				   "challenges to %lu",
				   (unsigned long) data->min_num_chal);
		}

		data->result_ind = os_strstr(config->phase1, "result_ind=1") !=
			NULL;
	}

	data->use_pseudonym = !sm->init_phase2;
	if (config && config->anonymous_identity && data->use_pseudonym) {
		data->pseudonym = os_malloc(config->anonymous_identity_len);
		if (data->pseudonym) {
			os_memcpy(data->pseudonym, config->anonymous_identity,
				  config->anonymous_identity_len);
			data->pseudonym_len = config->anonymous_identity_len;
		}
	}

	eap_sim_state(data, CONTINUE);

	return data;
}


static void eap_sim_clear_keys(struct eap_sim_data *data, int reauth)
{
	if (!reauth) {
		os_memset(data->mk, 0, EAP_SIM_MK_LEN);
		os_memset(data->k_aut, 0, EAP_SIM_K_AUT_LEN);
		os_memset(data->k_encr, 0, EAP_SIM_K_ENCR_LEN);
	}
	os_memset(data->kc, 0, 3 * EAP_SIM_KC_LEN);
	os_memset(data->sres, 0, 3 * EAP_SIM_SRES_LEN);
	os_memset(data->msk, 0, EAP_SIM_KEYING_DATA_LEN);
	os_memset(data->emsk, 0, EAP_EMSK_LEN);
}


static void eap_sim_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_sim_data *data = priv;
	if (data) {
		os_free(data->ver_list);
		os_free(data->pseudonym);
		os_free(data->reauth_id);
		os_free(data->last_eap_identity);
		eap_sim_clear_keys(data, 0);
		os_free(data);
	}
}


static int eap_sim_ext_sim_req(struct eap_sm *sm, struct eap_sim_data *data)
{
	char req[200], *pos, *end;
	size_t i;

	wpa_printf(MSG_DEBUG, "EAP-SIM: Use external SIM processing");
	pos = req;
	end = pos + sizeof(req);
	pos += os_snprintf(pos, end - pos, "GSM-AUTH");
	for (i = 0; i < data->num_chal; i++) {
		pos += os_snprintf(pos, end - pos, ":");
		pos += wpa_snprintf_hex(pos, end - pos, data->rand[i],
					GSM_RAND_LEN);
	}

	eap_sm_request_sim(sm, req);
	return 1;
}


static int eap_sim_ext_sim_result(struct eap_sm *sm, struct eap_sim_data *data,
				  struct eap_peer_config *conf)
{
	char *resp, *pos;
	size_t i;

	wpa_printf(MSG_DEBUG,
		   "EAP-SIM: Use result from external SIM processing");

	resp = conf->external_sim_resp;
	conf->external_sim_resp = NULL;

	if (os_strncmp(resp, "GSM-AUTH:", 9) != 0) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Unrecognized external SIM processing response");
		os_free(resp);
		return -1;
	}

	pos = resp + 9;
	for (i = 0; i < data->num_chal; i++) {
		wpa_hexdump(MSG_DEBUG, "EAP-SIM: RAND",
			    data->rand[i], GSM_RAND_LEN);

		if (hexstr2bin(pos, data->kc[i], EAP_SIM_KC_LEN) < 0)
			goto invalid;
		wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: Kc",
				data->kc[i], EAP_SIM_KC_LEN);
		pos += EAP_SIM_KC_LEN * 2;
		if (*pos != ':')
			goto invalid;
		pos++;

		if (hexstr2bin(pos, data->sres[i], EAP_SIM_SRES_LEN) < 0)
			goto invalid;
		wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: SRES",
				data->sres[i], EAP_SIM_SRES_LEN);
		pos += EAP_SIM_SRES_LEN * 2;
		if (i + 1 < data->num_chal) {
			if (*pos != ':')
				goto invalid;
			pos++;
		}
	}

	os_free(resp);
	return 0;

invalid:
	wpa_printf(MSG_DEBUG, "EAP-SIM: Invalid external SIM processing GSM-AUTH response");
	os_free(resp);
	return -1;
}


static int eap_sim_gsm_auth(struct eap_sm *sm, struct eap_sim_data *data)
{
	struct eap_peer_config *conf;

	wpa_printf(MSG_DEBUG, "EAP-SIM: GSM authentication algorithm");

	conf = eap_get_config(sm);
	if (conf == NULL)
		return -1;

	if (sm->external_sim) {
		if (conf->external_sim_resp)
			return eap_sim_ext_sim_result(sm, data, conf);
		else
			return eap_sim_ext_sim_req(sm, data);
	}

#ifdef PCSC_FUNCS
	if (conf->pcsc) {
		if (scard_gsm_auth(sm->scard_ctx, data->rand[0],
				   data->sres[0], data->kc[0]) ||
		    scard_gsm_auth(sm->scard_ctx, data->rand[1],
				   data->sres[1], data->kc[1]) ||
		    (data->num_chal > 2 &&
		     scard_gsm_auth(sm->scard_ctx, data->rand[2],
				    data->sres[2], data->kc[2]))) {
			wpa_printf(MSG_DEBUG, "EAP-SIM: GSM SIM "
				   "authentication could not be completed");
			return -1;
		}
		return 0;
	}
#endif /* PCSC_FUNCS */

#ifdef CONFIG_SIM_SIMULATOR
	if (conf->password) {
		u8 opc[16], k[16];
		const char *pos;
		size_t i;
		wpa_printf(MSG_DEBUG, "EAP-SIM: Use internal GSM-Milenage "
			   "implementation for authentication");
		if (conf->password_len < 65) {
			wpa_printf(MSG_DEBUG, "EAP-SIM: invalid GSM-Milenage "
				   "password");
			return -1;
		}
		pos = (const char *) conf->password;
		if (hexstr2bin(pos, k, 16))
			return -1;
		pos += 32;
		if (*pos != ':')
			return -1;
		pos++;

		if (hexstr2bin(pos, opc, 16))
			return -1;

		for (i = 0; i < data->num_chal; i++) {
			if (gsm_milenage(opc, k, data->rand[i],
					 data->sres[i], data->kc[i])) {
				wpa_printf(MSG_DEBUG, "EAP-SIM: "
					   "GSM-Milenage authentication "
					   "could not be completed");
				return -1;
			}
			wpa_hexdump(MSG_DEBUG, "EAP-SIM: RAND",
				    data->rand[i], GSM_RAND_LEN);
			wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: SRES",
					data->sres[i], EAP_SIM_SRES_LEN);
			wpa_hexdump_key(MSG_DEBUG, "EAP-SIM: Kc",
					data->kc[i], EAP_SIM_KC_LEN);
		}
		return 0;
	}
#endif /* CONFIG_SIM_SIMULATOR */

#ifdef CONFIG_SIM_HARDCODED
	/* These hardcoded Kc and SRES values are used for testing. RAND to
	 * KC/SREC mapping is very bogus as far as real authentication is
	 * concerned, but it is quite useful for cases where the AS is rotating
	 * the order of pre-configured values. */
	{
		size_t i;

		wpa_printf(MSG_DEBUG, "EAP-SIM: Use hardcoded Kc and SRES "
			   "values for testing");

		for (i = 0; i < data->num_chal; i++) {
			if (data->rand[i][0] == 0xaa) {
				os_memcpy(data->kc[i],
					  "\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7",
					  EAP_SIM_KC_LEN);
				os_memcpy(data->sres[i], "\xd1\xd2\xd3\xd4",
					  EAP_SIM_SRES_LEN);
			} else if (data->rand[i][0] == 0xbb) {
				os_memcpy(data->kc[i],
					  "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7",
					  EAP_SIM_KC_LEN);
				os_memcpy(data->sres[i], "\xe1\xe2\xe3\xe4",
					  EAP_SIM_SRES_LEN);
			} else {
				os_memcpy(data->kc[i],
					  "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7",
					  EAP_SIM_KC_LEN);
				os_memcpy(data->sres[i], "\xf1\xf2\xf3\xf4",
					  EAP_SIM_SRES_LEN);
			}
		}
	}

	return 0;

#else /* CONFIG_SIM_HARDCODED */

	wpa_printf(MSG_DEBUG, "EAP-SIM: No GSM authentication algorithm "
		   "enabled");
	return -1;

#endif /* CONFIG_SIM_HARDCODED */
}


static int eap_sim_supported_ver(int version)
{
	return version == EAP_SIM_VERSION;
}


#define CLEAR_PSEUDONYM	0x01
#define CLEAR_REAUTH_ID	0x02
#define CLEAR_EAP_ID	0x04

static void eap_sim_clear_identities(struct eap_sm *sm,
				     struct eap_sim_data *data, int id)
{
	if ((id & CLEAR_PSEUDONYM) && data->pseudonym) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: forgetting old pseudonym");
		os_free(data->pseudonym);
		data->pseudonym = NULL;
		data->pseudonym_len = 0;
		if (data->use_pseudonym)
			eap_set_anon_id(sm, NULL, 0);
	}
	if ((id & CLEAR_REAUTH_ID) && data->reauth_id) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: forgetting old reauth_id");
		os_free(data->reauth_id);
		data->reauth_id = NULL;
		data->reauth_id_len = 0;
	}
	if ((id & CLEAR_EAP_ID) && data->last_eap_identity) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: forgetting old eap_id");
		os_free(data->last_eap_identity);
		data->last_eap_identity = NULL;
		data->last_eap_identity_len = 0;
	}
}


static int eap_sim_learn_ids(struct eap_sm *sm, struct eap_sim_data *data,
			     struct eap_sim_attrs *attr)
{
	if (attr->next_pseudonym) {
		const u8 *identity = NULL;
		size_t identity_len = 0;
		const u8 *realm = NULL;
		size_t realm_len = 0;

		wpa_hexdump_ascii(MSG_DEBUG,
				  "EAP-SIM: (encr) AT_NEXT_PSEUDONYM",
				  attr->next_pseudonym,
				  attr->next_pseudonym_len);
		os_free(data->pseudonym);
		/* Look for the realm of the permanent identity */
		identity = eap_get_config_identity(sm, &identity_len);
		if (identity) {
			for (realm = identity, realm_len = identity_len;
			     realm_len > 0; realm_len--, realm++) {
				if (*realm == '@')
					break;
			}
		}
		data->pseudonym = os_malloc(attr->next_pseudonym_len +
					    realm_len);
		if (data->pseudonym == NULL) {
			wpa_printf(MSG_INFO, "EAP-SIM: (encr) No memory for "
				   "next pseudonym");
			data->pseudonym_len = 0;
			return -1;
		}
		os_memcpy(data->pseudonym, attr->next_pseudonym,
			  attr->next_pseudonym_len);
		if (realm_len) {
			os_memcpy(data->pseudonym + attr->next_pseudonym_len,
				  realm, realm_len);
		}
		data->pseudonym_len = attr->next_pseudonym_len + realm_len;
		if (data->use_pseudonym)
			eap_set_anon_id(sm, data->pseudonym,
					data->pseudonym_len);
	}

	if (attr->next_reauth_id) {
		os_free(data->reauth_id);
		data->reauth_id = os_memdup(attr->next_reauth_id,
					    attr->next_reauth_id_len);
		if (data->reauth_id == NULL) {
			wpa_printf(MSG_INFO, "EAP-SIM: (encr) No memory for "
				   "next reauth_id");
			data->reauth_id_len = 0;
			return -1;
		}
		data->reauth_id_len = attr->next_reauth_id_len;
		wpa_hexdump_ascii(MSG_DEBUG,
				  "EAP-SIM: (encr) AT_NEXT_REAUTH_ID",
				  data->reauth_id,
				  data->reauth_id_len);
	}

	return 0;
}


static struct wpabuf * eap_sim_client_error(struct eap_sim_data *data, u8 id,
					    int err)
{
	struct eap_sim_msg *msg;

	eap_sim_state(data, FAILURE);
	data->num_id_req = 0;
	data->num_notification = 0;

	wpa_printf(MSG_DEBUG, "EAP-SIM: Send Client-Error (error code %d)",
		   err);
	msg = eap_sim_msg_init(EAP_CODE_RESPONSE, id, EAP_TYPE_SIM,
			       EAP_SIM_SUBTYPE_CLIENT_ERROR);
	eap_sim_msg_add(msg, EAP_SIM_AT_CLIENT_ERROR_CODE, err, NULL, 0);
	return eap_sim_msg_finish(msg, EAP_TYPE_SIM, NULL, NULL, 0);
}


static struct wpabuf * eap_sim_response_start(struct eap_sm *sm,
					      struct eap_sim_data *data, u8 id,
					      enum eap_sim_id_req id_req)
{
	const u8 *identity = NULL;
	size_t identity_len = 0;
	struct eap_sim_msg *msg;

	data->reauth = 0;
	if (id_req == ANY_ID && data->reauth_id) {
		identity = data->reauth_id;
		identity_len = data->reauth_id_len;
		data->reauth = 1;
	} else if ((id_req == ANY_ID || id_req == FULLAUTH_ID) &&
		   data->pseudonym) {
		identity = data->pseudonym;
		identity_len = data->pseudonym_len;
		eap_sim_clear_identities(sm, data, CLEAR_REAUTH_ID);
	} else if (id_req != NO_ID_REQ) {
		identity = eap_get_config_identity(sm, &identity_len);
		if (identity) {
			eap_sim_clear_identities(sm, data, CLEAR_PSEUDONYM |
						 CLEAR_REAUTH_ID);
		}
	}
	if (id_req != NO_ID_REQ)
		eap_sim_clear_identities(sm, data, CLEAR_EAP_ID);

	wpa_printf(MSG_DEBUG, "Generating EAP-SIM Start (id=%d)", id);
	msg = eap_sim_msg_init(EAP_CODE_RESPONSE, id,
			       EAP_TYPE_SIM, EAP_SIM_SUBTYPE_START);
	if (!data->reauth) {
		wpa_hexdump(MSG_DEBUG, "   AT_NONCE_MT",
			    data->nonce_mt, EAP_SIM_NONCE_MT_LEN);
		eap_sim_msg_add(msg, EAP_SIM_AT_NONCE_MT, 0,
				data->nonce_mt, EAP_SIM_NONCE_MT_LEN);
		wpa_printf(MSG_DEBUG, "   AT_SELECTED_VERSION %d",
			   data->selected_version);
		eap_sim_msg_add(msg, EAP_SIM_AT_SELECTED_VERSION,
				data->selected_version, NULL, 0);
	}

	if (identity) {
		wpa_hexdump_ascii(MSG_DEBUG, "   AT_IDENTITY",
				  identity, identity_len);
		eap_sim_msg_add(msg, EAP_SIM_AT_IDENTITY, identity_len,
				identity, identity_len);
	}

	return eap_sim_msg_finish(msg, EAP_TYPE_SIM, NULL, NULL, 0);
}


static struct wpabuf * eap_sim_response_challenge(struct eap_sim_data *data,
						  u8 id)
{
	struct eap_sim_msg *msg;

	wpa_printf(MSG_DEBUG, "Generating EAP-SIM Challenge (id=%d)", id);
	msg = eap_sim_msg_init(EAP_CODE_RESPONSE, id, EAP_TYPE_SIM,
			       EAP_SIM_SUBTYPE_CHALLENGE);
	if (data->use_result_ind) {
		wpa_printf(MSG_DEBUG, "   AT_RESULT_IND");
		eap_sim_msg_add(msg, EAP_SIM_AT_RESULT_IND, 0, NULL, 0);
	}
	wpa_printf(MSG_DEBUG, "   AT_MAC");
	eap_sim_msg_add_mac(msg, EAP_SIM_AT_MAC);
	return eap_sim_msg_finish(msg, EAP_TYPE_SIM, data->k_aut,
				  (u8 *) data->sres,
				  data->num_chal * EAP_SIM_SRES_LEN);
}


static struct wpabuf * eap_sim_response_reauth(struct eap_sim_data *data,
					       u8 id, int counter_too_small,
					       const u8 *nonce_s)
{
	struct eap_sim_msg *msg;
	unsigned int counter;

	wpa_printf(MSG_DEBUG, "Generating EAP-SIM Reauthentication (id=%d)",
		   id);
	msg = eap_sim_msg_init(EAP_CODE_RESPONSE, id, EAP_TYPE_SIM,
			       EAP_SIM_SUBTYPE_REAUTHENTICATION);
	wpa_printf(MSG_DEBUG, "   AT_IV");
	wpa_printf(MSG_DEBUG, "   AT_ENCR_DATA");
	eap_sim_msg_add_encr_start(msg, EAP_SIM_AT_IV, EAP_SIM_AT_ENCR_DATA);

	if (counter_too_small) {
		wpa_printf(MSG_DEBUG, "   *AT_COUNTER_TOO_SMALL");
		eap_sim_msg_add(msg, EAP_SIM_AT_COUNTER_TOO_SMALL, 0, NULL, 0);
		counter = data->counter_too_small;
	} else
		counter = data->counter;

	wpa_printf(MSG_DEBUG, "   *AT_COUNTER %d", counter);
	eap_sim_msg_add(msg, EAP_SIM_AT_COUNTER, counter, NULL, 0);

	if (eap_sim_msg_add_encr_end(msg, data->k_encr, EAP_SIM_AT_PADDING)) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Failed to encrypt "
			   "AT_ENCR_DATA");
		eap_sim_msg_free(msg);
		return NULL;
	}
	if (data->use_result_ind) {
		wpa_printf(MSG_DEBUG, "   AT_RESULT_IND");
		eap_sim_msg_add(msg, EAP_SIM_AT_RESULT_IND, 0, NULL, 0);
	}
	wpa_printf(MSG_DEBUG, "   AT_MAC");
	eap_sim_msg_add_mac(msg, EAP_SIM_AT_MAC);
	return eap_sim_msg_finish(msg, EAP_TYPE_SIM, data->k_aut, nonce_s,
				  EAP_SIM_NONCE_S_LEN);
}


static struct wpabuf * eap_sim_response_notification(struct eap_sim_data *data,
						     u8 id, u16 notification)
{
	struct eap_sim_msg *msg;
	u8 *k_aut = (notification & 0x4000) == 0 ? data->k_aut : NULL;

	wpa_printf(MSG_DEBUG, "Generating EAP-SIM Notification (id=%d)", id);
	msg = eap_sim_msg_init(EAP_CODE_RESPONSE, id,
			       EAP_TYPE_SIM, EAP_SIM_SUBTYPE_NOTIFICATION);
	if (k_aut && data->reauth) {
		wpa_printf(MSG_DEBUG, "   AT_IV");
		wpa_printf(MSG_DEBUG, "   AT_ENCR_DATA");
		eap_sim_msg_add_encr_start(msg, EAP_SIM_AT_IV,
					   EAP_SIM_AT_ENCR_DATA);
		wpa_printf(MSG_DEBUG, "   *AT_COUNTER %d", data->counter);
		eap_sim_msg_add(msg, EAP_SIM_AT_COUNTER, data->counter,
				NULL, 0);
		if (eap_sim_msg_add_encr_end(msg, data->k_encr,
					     EAP_SIM_AT_PADDING)) {
			wpa_printf(MSG_WARNING, "EAP-SIM: Failed to encrypt "
				   "AT_ENCR_DATA");
			eap_sim_msg_free(msg);
			return NULL;
		}
	}
	if (k_aut) {
		wpa_printf(MSG_DEBUG, "   AT_MAC");
		eap_sim_msg_add_mac(msg, EAP_SIM_AT_MAC);
	}
	return eap_sim_msg_finish(msg, EAP_TYPE_SIM, k_aut, (u8 *) "", 0);
}


static struct wpabuf * eap_sim_process_start(struct eap_sm *sm,
					     struct eap_sim_data *data, u8 id,
					     struct eap_sim_attrs *attr)
{
	int selected_version = -1, id_error;
	size_t i;
	u8 *pos;

	wpa_printf(MSG_DEBUG, "EAP-SIM: subtype Start");
	if (attr->version_list == NULL) {
		wpa_printf(MSG_INFO, "EAP-SIM: No AT_VERSION_LIST in "
			   "SIM/Start");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNSUPPORTED_VERSION);
	}

	os_free(data->ver_list);
	data->ver_list = os_memdup(attr->version_list, attr->version_list_len);
	if (data->ver_list == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Failed to allocate "
			   "memory for version list");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}
	data->ver_list_len = attr->version_list_len;
	pos = data->ver_list;
	for (i = 0; i < data->ver_list_len / 2; i++) {
		int ver = pos[0] * 256 + pos[1];
		pos += 2;
		if (eap_sim_supported_ver(ver)) {
			selected_version = ver;
			break;
		}
	}
	if (selected_version < 0) {
		wpa_printf(MSG_INFO, "EAP-SIM: Could not find a supported "
			   "version");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNSUPPORTED_VERSION);
	}
	wpa_printf(MSG_DEBUG, "EAP-SIM: Selected Version %d",
		   selected_version);
	data->selected_version = selected_version;

	id_error = 0;
	switch (attr->id_req) {
	case NO_ID_REQ:
		break;
	case ANY_ID:
		if (data->num_id_req > 0)
			id_error++;
		data->num_id_req++;
		break;
	case FULLAUTH_ID:
		if (data->num_id_req > 1)
			id_error++;
		data->num_id_req++;
		break;
	case PERMANENT_ID:
		if (data->num_id_req > 2)
			id_error++;
		data->num_id_req++;
		break;
	}
	if (id_error) {
		wpa_printf(MSG_INFO, "EAP-SIM: Too many ID requests "
			   "used within one authentication");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	return eap_sim_response_start(sm, data, id, attr->id_req);
}


static struct wpabuf * eap_sim_process_challenge(struct eap_sm *sm,
						 struct eap_sim_data *data,
						 u8 id,
						 const struct wpabuf *reqData,
						 struct eap_sim_attrs *attr)
{
	const u8 *identity;
	size_t identity_len;
	struct eap_sim_attrs eattr;
	int res;

	wpa_printf(MSG_DEBUG, "EAP-SIM: subtype Challenge");
	data->reauth = 0;
	if (!attr->mac || !attr->rand) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Challenge message "
			   "did not include%s%s",
			   !attr->mac ? " AT_MAC" : "",
			   !attr->rand ? " AT_RAND" : "");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	wpa_printf(MSG_DEBUG, "EAP-SIM: %lu challenges",
		   (unsigned long) attr->num_chal);
	if (attr->num_chal < data->min_num_chal) {
		wpa_printf(MSG_INFO, "EAP-SIM: Insufficient number of "
			   "challenges (%lu)", (unsigned long) attr->num_chal);
		return eap_sim_client_error(data, id,
					    EAP_SIM_INSUFFICIENT_NUM_OF_CHAL);
	}
	if (attr->num_chal > 3) {
		wpa_printf(MSG_INFO, "EAP-SIM: Too many challenges "
			   "(%lu)", (unsigned long) attr->num_chal);
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	/* Verify that RANDs are different */
	if (os_memcmp(attr->rand, attr->rand + GSM_RAND_LEN,
		   GSM_RAND_LEN) == 0 ||
	    (attr->num_chal > 2 &&
	     (os_memcmp(attr->rand, attr->rand + 2 * GSM_RAND_LEN,
			GSM_RAND_LEN) == 0 ||
	      os_memcmp(attr->rand + GSM_RAND_LEN,
			attr->rand + 2 * GSM_RAND_LEN,
			GSM_RAND_LEN) == 0))) {
		wpa_printf(MSG_INFO, "EAP-SIM: Same RAND used multiple times");
		return eap_sim_client_error(data, id,
					    EAP_SIM_RAND_NOT_FRESH);
	}

	os_memcpy(data->rand, attr->rand, attr->num_chal * GSM_RAND_LEN);
	data->num_chal = attr->num_chal;

	res = eap_sim_gsm_auth(sm, data);
	if (res > 0) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Wait for external SIM processing");
		return NULL;
	}
	if (res) {
		wpa_printf(MSG_WARNING, "EAP-SIM: GSM authentication failed");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}
	if (data->last_eap_identity) {
		identity = data->last_eap_identity;
		identity_len = data->last_eap_identity_len;
	} else if (data->pseudonym) {
		identity = data->pseudonym;
		identity_len = data->pseudonym_len;
	} else {
		struct eap_peer_config *config;

		config = eap_get_config(sm);
		if (config && config->imsi_identity) {
			identity = config->imsi_identity;
			identity_len = config->imsi_identity_len;
		} else {
			identity = eap_get_config_identity(sm, &identity_len);
		}
	}
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-SIM: Selected identity for MK "
			  "derivation", identity, identity_len);
	eap_sim_derive_mk(identity, identity_len, data->nonce_mt,
			  data->selected_version, data->ver_list,
			  data->ver_list_len, data->num_chal,
			  (const u8 *) data->kc, data->mk);
	eap_sim_derive_keys(data->mk, data->k_encr, data->k_aut, data->msk,
			    data->emsk);
	if (eap_sim_verify_mac(data->k_aut, reqData, attr->mac, data->nonce_mt,
			       EAP_SIM_NONCE_MT_LEN)) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Challenge message "
			   "used invalid AT_MAC");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	/* Old reauthentication identity must not be used anymore. In
	 * other words, if no new reauth identity is received, full
	 * authentication will be used on next reauthentication (using
	 * pseudonym identity or permanent identity). */
	eap_sim_clear_identities(sm, data, CLEAR_REAUTH_ID | CLEAR_EAP_ID);

	if (attr->encr_data) {
		u8 *decrypted;
		decrypted = eap_sim_parse_encr(data->k_encr, attr->encr_data,
					       attr->encr_data_len, attr->iv,
					       &eattr, 0);
		if (decrypted == NULL) {
			return eap_sim_client_error(
				data, id, EAP_SIM_UNABLE_TO_PROCESS_PACKET);
		}
		eap_sim_learn_ids(sm, data, &eattr);
		os_free(decrypted);
	}

	if (data->result_ind && attr->result_ind)
		data->use_result_ind = 1;

	if (data->state != FAILURE) {
		eap_sim_state(data, data->use_result_ind ?
			      RESULT_SUCCESS : SUCCESS);
	}

	data->num_id_req = 0;
	data->num_notification = 0;
	/* RFC 4186 specifies that counter is initialized to one after
	 * fullauth, but initializing it to zero makes it easier to implement
	 * reauth verification. */
	data->counter = 0;
	return eap_sim_response_challenge(data, id);
}


static int eap_sim_process_notification_reauth(struct eap_sim_data *data,
					       struct eap_sim_attrs *attr)
{
	struct eap_sim_attrs eattr;
	u8 *decrypted;

	if (attr->encr_data == NULL || attr->iv == NULL) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Notification message after "
			   "reauth did not include encrypted data");
		return -1;
	}

	decrypted = eap_sim_parse_encr(data->k_encr, attr->encr_data,
				       attr->encr_data_len, attr->iv, &eattr,
				       0);
	if (decrypted == NULL) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Failed to parse encrypted "
			   "data from notification message");
		return -1;
	}

	if (eattr.counter < 0 || (size_t) eattr.counter != data->counter) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Counter in notification "
			   "message does not match with counter in reauth "
			   "message");
		os_free(decrypted);
		return -1;
	}

	os_free(decrypted);
	return 0;
}


static int eap_sim_process_notification_auth(struct eap_sim_data *data,
					     const struct wpabuf *reqData,
					     struct eap_sim_attrs *attr)
{
	if (attr->mac == NULL) {
		wpa_printf(MSG_INFO, "EAP-SIM: no AT_MAC in after_auth "
			   "Notification message");
		return -1;
	}

	if (eap_sim_verify_mac(data->k_aut, reqData, attr->mac, (u8 *) "", 0))
	{
		wpa_printf(MSG_WARNING, "EAP-SIM: Notification message "
			   "used invalid AT_MAC");
		return -1;
	}

	if (data->reauth &&
	    eap_sim_process_notification_reauth(data, attr)) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Invalid notification "
			   "message after reauth");
		return -1;
	}

	return 0;
}


static struct wpabuf * eap_sim_process_notification(
	struct eap_sm *sm, struct eap_sim_data *data, u8 id,
	const struct wpabuf *reqData, struct eap_sim_attrs *attr)
{
	wpa_printf(MSG_DEBUG, "EAP-SIM: subtype Notification");
	if (data->num_notification > 0) {
		wpa_printf(MSG_INFO, "EAP-SIM: too many notification "
			   "rounds (only one allowed)");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}
	data->num_notification++;
	if (attr->notification == -1) {
		wpa_printf(MSG_INFO, "EAP-SIM: no AT_NOTIFICATION in "
			   "Notification message");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	if ((attr->notification & 0x4000) == 0 &&
	    eap_sim_process_notification_auth(data, reqData, attr)) {
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	eap_sim_report_notification(sm->msg_ctx, attr->notification, 0);
	if (attr->notification >= 0 && attr->notification < 32768) {
		data->error_code = attr->notification;
		eap_sim_state(data, FAILURE);
	} else if (attr->notification == EAP_SIM_SUCCESS &&
		   data->state == RESULT_SUCCESS)
		eap_sim_state(data, SUCCESS);
	return eap_sim_response_notification(data, id, attr->notification);
}


static struct wpabuf * eap_sim_process_reauthentication(
	struct eap_sm *sm, struct eap_sim_data *data, u8 id,
	const struct wpabuf *reqData, struct eap_sim_attrs *attr)
{
	struct eap_sim_attrs eattr;
	u8 *decrypted;

	wpa_printf(MSG_DEBUG, "EAP-SIM: subtype Reauthentication");

	if (data->reauth_id == NULL) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Server is trying "
			   "reauthentication, but no reauth_id available");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	data->reauth = 1;
	if (eap_sim_verify_mac(data->k_aut, reqData, attr->mac, (u8 *) "", 0))
	{
		wpa_printf(MSG_WARNING, "EAP-SIM: Reauthentication "
			   "did not have valid AT_MAC");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	if (attr->encr_data == NULL || attr->iv == NULL) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Reauthentication "
			   "message did not include encrypted data");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	decrypted = eap_sim_parse_encr(data->k_encr, attr->encr_data,
				       attr->encr_data_len, attr->iv, &eattr,
				       0);
	if (decrypted == NULL) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Failed to parse encrypted "
			   "data from reauthentication message");
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	if (eattr.nonce_s == NULL || eattr.counter < 0) {
		wpa_printf(MSG_INFO, "EAP-SIM: (encr) No%s%s in reauth packet",
			   !eattr.nonce_s ? " AT_NONCE_S" : "",
			   eattr.counter < 0 ? " AT_COUNTER" : "");
		os_free(decrypted);
		return eap_sim_client_error(data, id,
					    EAP_SIM_UNABLE_TO_PROCESS_PACKET);
	}

	if (eattr.counter < 0 || (size_t) eattr.counter <= data->counter) {
		struct wpabuf *res;
		wpa_printf(MSG_INFO, "EAP-SIM: (encr) Invalid counter "
			   "(%d <= %d)", eattr.counter, data->counter);
		data->counter_too_small = eattr.counter;

		/* Reply using Re-auth w/ AT_COUNTER_TOO_SMALL. The current
		 * reauth_id must not be used to start a new reauthentication.
		 * However, since it was used in the last EAP-Response-Identity
		 * packet, it has to saved for the following fullauth to be
		 * used in MK derivation. */
		os_free(data->last_eap_identity);
		data->last_eap_identity = data->reauth_id;
		data->last_eap_identity_len = data->reauth_id_len;
		data->reauth_id = NULL;
		data->reauth_id_len = 0;

		res = eap_sim_response_reauth(data, id, 1, eattr.nonce_s);
		os_free(decrypted);

		return res;
	}
	data->counter = eattr.counter;

	os_memcpy(data->nonce_s, eattr.nonce_s, EAP_SIM_NONCE_S_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-SIM: (encr) AT_NONCE_S",
		    data->nonce_s, EAP_SIM_NONCE_S_LEN);

	eap_sim_derive_keys_reauth(data->counter,
				   data->reauth_id, data->reauth_id_len,
				   data->nonce_s, data->mk, data->msk,
				   data->emsk);
	eap_sim_clear_identities(sm, data, CLEAR_REAUTH_ID | CLEAR_EAP_ID);
	eap_sim_learn_ids(sm, data, &eattr);

	if (data->result_ind && attr->result_ind)
		data->use_result_ind = 1;

	if (data->state != FAILURE) {
		eap_sim_state(data, data->use_result_ind ?
			      RESULT_SUCCESS : SUCCESS);
	}

	data->num_id_req = 0;
	data->num_notification = 0;
	if (data->counter > EAP_SIM_MAX_FAST_REAUTHS) {
		wpa_printf(MSG_DEBUG, "EAP-SIM: Maximum number of "
			   "fast reauths performed - force fullauth");
		eap_sim_clear_identities(sm, data,
					 CLEAR_REAUTH_ID | CLEAR_EAP_ID);
	}
	os_free(decrypted);
	return eap_sim_response_reauth(data, id, 0, data->nonce_s);
}


static struct wpabuf * eap_sim_process(struct eap_sm *sm, void *priv,
				       struct eap_method_ret *ret,
				       const struct wpabuf *reqData)
{
	struct eap_sim_data *data = priv;
	const struct eap_hdr *req;
	u8 subtype, id;
	struct wpabuf *res;
	const u8 *pos;
	struct eap_sim_attrs attr;
	size_t len;

	wpa_hexdump_buf(MSG_DEBUG, "EAP-SIM: EAP data", reqData);
	if (eap_get_config_identity(sm, &len) == NULL) {
		wpa_printf(MSG_INFO, "EAP-SIM: Identity not configured");
		eap_sm_request_identity(sm);
		ret->ignore = TRUE;
		return NULL;
	}

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_SIM, reqData, &len);
	if (pos == NULL || len < 3) {
		ret->ignore = TRUE;
		return NULL;
	}
	req = wpabuf_head(reqData);
	id = req->identifier;
	len = be_to_host16(req->length);

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = TRUE;

	subtype = *pos++;
	wpa_printf(MSG_DEBUG, "EAP-SIM: Subtype=%d", subtype);
	pos += 2; /* Reserved */

	if (eap_sim_parse_attr(pos, wpabuf_head_u8(reqData) + len, &attr, 0,
			       0)) {
		res = eap_sim_client_error(data, id,
					   EAP_SIM_UNABLE_TO_PROCESS_PACKET);
		goto done;
	}

	switch (subtype) {
	case EAP_SIM_SUBTYPE_START:
		res = eap_sim_process_start(sm, data, id, &attr);
		break;
	case EAP_SIM_SUBTYPE_CHALLENGE:
		res = eap_sim_process_challenge(sm, data, id, reqData, &attr);
		break;
	case EAP_SIM_SUBTYPE_NOTIFICATION:
		res = eap_sim_process_notification(sm, data, id, reqData,
						   &attr);
		break;
	case EAP_SIM_SUBTYPE_REAUTHENTICATION:
		res = eap_sim_process_reauthentication(sm, data, id, reqData,
						       &attr);
		break;
	case EAP_SIM_SUBTYPE_CLIENT_ERROR:
		wpa_printf(MSG_DEBUG, "EAP-SIM: subtype Client-Error");
		res = eap_sim_client_error(data, id,
					   EAP_SIM_UNABLE_TO_PROCESS_PACKET);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-SIM: Unknown subtype=%d", subtype);
		res = eap_sim_client_error(data, id,
					   EAP_SIM_UNABLE_TO_PROCESS_PACKET);
		break;
	}

done:
	if (data->state == FAILURE) {
		ret->decision = DECISION_FAIL;
		ret->methodState = METHOD_DONE;
	} else if (data->state == SUCCESS) {
		ret->decision = data->use_result_ind ?
			DECISION_UNCOND_SUCC : DECISION_COND_SUCC;
		ret->methodState = data->use_result_ind ?
			METHOD_DONE : METHOD_MAY_CONT;
	} else if (data->state == RESULT_SUCCESS)
		ret->methodState = METHOD_CONT;

	if (ret->methodState == METHOD_DONE) {
		ret->allowNotifications = FALSE;
	}

	return res;
}


static Boolean eap_sim_has_reauth_data(struct eap_sm *sm, void *priv)
{
	struct eap_sim_data *data = priv;
	return data->pseudonym || data->reauth_id;
}


static void eap_sim_deinit_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_sim_data *data = priv;
	eap_sim_clear_identities(sm, data, CLEAR_EAP_ID);
	data->use_result_ind = 0;
	eap_sim_clear_keys(data, 1);
}


static void * eap_sim_init_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_sim_data *data = priv;
	if (random_get_bytes(data->nonce_mt, EAP_SIM_NONCE_MT_LEN)) {
		wpa_printf(MSG_WARNING, "EAP-SIM: Failed to get random data "
			   "for NONCE_MT");
		eap_sim_deinit(sm, data);
		return NULL;
	}
	data->num_id_req = 0;
	data->num_notification = 0;
	eap_sim_state(data, CONTINUE);
	return priv;
}


static const u8 * eap_sim_get_identity(struct eap_sm *sm, void *priv,
				       size_t *len)
{
	struct eap_sim_data *data = priv;

	if (data->reauth_id) {
		*len = data->reauth_id_len;
		return data->reauth_id;
	}

	if (data->pseudonym) {
		*len = data->pseudonym_len;
		return data->pseudonym;
	}

	return NULL;
}


static Boolean eap_sim_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_sim_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_sim_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_sim_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->msk, EAP_SIM_KEYING_DATA_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_SIM_KEYING_DATA_LEN;

	return key;
}


static u8 * eap_sim_get_session_id(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_sim_data *data = priv;
	u8 *id;

	if (data->state != SUCCESS)
		return NULL;

	*len = 1 + data->num_chal * GSM_RAND_LEN + EAP_SIM_NONCE_MT_LEN;
	id = os_malloc(*len);
	if (id == NULL)
		return NULL;

	id[0] = EAP_TYPE_SIM;
	os_memcpy(id + 1, data->rand, data->num_chal * GSM_RAND_LEN);
	os_memcpy(id + 1 + data->num_chal * GSM_RAND_LEN, data->nonce_mt,
		  EAP_SIM_NONCE_MT_LEN);
	wpa_hexdump(MSG_DEBUG, "EAP-SIM: Derived Session-Id", id, *len);

	return id;
}


static u8 * eap_sim_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_sim_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->emsk, EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_EMSK_LEN;

	return key;
}


static int eap_sim_get_error_code(void *priv)
{
	struct eap_sim_data *data = priv;
	int current_data_error;

	if (!data)
		return NO_EAP_METHOD_ERROR;

	current_data_error = data->error_code;

	/* Now reset for next transaction */
	data->error_code = NO_EAP_METHOD_ERROR;

	return current_data_error;
}


int eap_peer_sim_register(void)
{
	struct eap_method *eap;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_SIM, "SIM");
	if (eap == NULL)
		return -1;

	eap->init = eap_sim_init;
	eap->deinit = eap_sim_deinit;
	eap->process = eap_sim_process;
	eap->isKeyAvailable = eap_sim_isKeyAvailable;
	eap->getKey = eap_sim_getKey;
	eap->getSessionId = eap_sim_get_session_id;
	eap->has_reauth_data = eap_sim_has_reauth_data;
	eap->deinit_for_reauth = eap_sim_deinit_for_reauth;
	eap->init_for_reauth = eap_sim_init_for_reauth;
	eap->get_identity = eap_sim_get_identity;
	eap->get_emsk = eap_sim_get_emsk;
	eap->get_error_code = eap_sim_get_error_code;

	return eap_peer_method_register(eap);
}
