/*
 * EAP peer state machines (RFC 4137)
 * Copyright (c) 2004-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This file implements the Peer State Machine as defined in RFC 4137. The used
 * states and state transitions match mostly with the RFC. However, there are
 * couple of additional transitions for working around small issues noticed
 * during testing. These exceptions are explained in comments within the
 * functions in this file. The method functions, m.func(), are similar to the
 * ones used in RFC 4137, but some small changes have used here to optimize
 * operations and to add functionality needed for fast re-authentication
 * (session resumption).
 */

#include "includes.h"

#include "common.h"
#include "pcsc_funcs.h"
#include "state_machine.h"
#include "ext_password.h"
#include "crypto/crypto.h"
#include "crypto/tls.h"
#include "crypto/sha256.h"
#include "common/wpa_ctrl.h"
#include "eap_common/eap_wsc_common.h"
#include "eap_i.h"
#include "eap_config.h"

#define STATE_MACHINE_DATA struct eap_sm
#define STATE_MACHINE_DEBUG_PREFIX "EAP"

#define EAP_MAX_AUTH_ROUNDS 50
#define EAP_CLIENT_TIMEOUT_DEFAULT 60


static Boolean eap_sm_allowMethod(struct eap_sm *sm, int vendor,
				  EapType method);
static struct wpabuf * eap_sm_buildNak(struct eap_sm *sm, int id);
static void eap_sm_processIdentity(struct eap_sm *sm,
				   const struct wpabuf *req);
static void eap_sm_processNotify(struct eap_sm *sm, const struct wpabuf *req);
static struct wpabuf * eap_sm_buildNotify(int id);
static void eap_sm_parseEapReq(struct eap_sm *sm, const struct wpabuf *req);
#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
static const char * eap_sm_method_state_txt(EapMethodState state);
static const char * eap_sm_decision_txt(EapDecision decision);
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */
static void eap_sm_request(struct eap_sm *sm, enum wpa_ctrl_req_type field,
			   const char *msg, size_t msglen);



static Boolean eapol_get_bool(struct eap_sm *sm, enum eapol_bool_var var)
{
	return sm->eapol_cb->get_bool(sm->eapol_ctx, var);
}


static void eapol_set_bool(struct eap_sm *sm, enum eapol_bool_var var,
			   Boolean value)
{
	sm->eapol_cb->set_bool(sm->eapol_ctx, var, value);
}


static unsigned int eapol_get_int(struct eap_sm *sm, enum eapol_int_var var)
{
	return sm->eapol_cb->get_int(sm->eapol_ctx, var);
}


static void eapol_set_int(struct eap_sm *sm, enum eapol_int_var var,
			  unsigned int value)
{
	sm->eapol_cb->set_int(sm->eapol_ctx, var, value);
}


static struct wpabuf * eapol_get_eapReqData(struct eap_sm *sm)
{
	return sm->eapol_cb->get_eapReqData(sm->eapol_ctx);
}


static void eap_notify_status(struct eap_sm *sm, const char *status,
				      const char *parameter)
{
	wpa_printf(MSG_DEBUG, "EAP: Status notification: %s (param=%s)",
		   status, parameter);
	if (sm->eapol_cb->notify_status)
		sm->eapol_cb->notify_status(sm->eapol_ctx, status, parameter);
}


static void eap_report_error(struct eap_sm *sm, int error_code)
{
	wpa_printf(MSG_DEBUG, "EAP: Error notification: %d", error_code);
	if (sm->eapol_cb->notify_eap_error)
		sm->eapol_cb->notify_eap_error(sm->eapol_ctx, error_code);
}


static void eap_sm_free_key(struct eap_sm *sm)
{
	if (sm->eapKeyData) {
		bin_clear_free(sm->eapKeyData, sm->eapKeyDataLen);
		sm->eapKeyData = NULL;
	}
}


static void eap_deinit_prev_method(struct eap_sm *sm, const char *txt)
{
	ext_password_free(sm->ext_pw_buf);
	sm->ext_pw_buf = NULL;

	if (sm->m == NULL || sm->eap_method_priv == NULL)
		return;

	wpa_printf(MSG_DEBUG, "EAP: deinitialize previously used EAP method "
		   "(%d, %s) at %s", sm->selectedMethod, sm->m->name, txt);
	sm->m->deinit(sm, sm->eap_method_priv);
	sm->eap_method_priv = NULL;
	sm->m = NULL;
}


/**
 * eap_config_allowed_method - Check whether EAP method is allowed
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @config: EAP configuration
 * @vendor: Vendor-Id for expanded types or 0 = IETF for legacy types
 * @method: EAP type
 * Returns: 1 = allowed EAP method, 0 = not allowed
 */
static int eap_config_allowed_method(struct eap_sm *sm,
				     struct eap_peer_config *config,
				     int vendor, u32 method)
{
	int i;
	struct eap_method_type *m;

	if (config == NULL || config->eap_methods == NULL)
		return 1;

	m = config->eap_methods;
	for (i = 0; m[i].vendor != EAP_VENDOR_IETF ||
		     m[i].method != EAP_TYPE_NONE; i++) {
		if (m[i].vendor == vendor && m[i].method == method)
			return 1;
	}
	return 0;
}


/**
 * eap_allowed_method - Check whether EAP method is allowed
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @vendor: Vendor-Id for expanded types or 0 = IETF for legacy types
 * @method: EAP type
 * Returns: 1 = allowed EAP method, 0 = not allowed
 */
int eap_allowed_method(struct eap_sm *sm, int vendor, u32 method)
{
	return eap_config_allowed_method(sm, eap_get_config(sm), vendor,
					 method);
}


#if defined(PCSC_FUNCS) || defined(CONFIG_EAP_PROXY)
static int eap_sm_append_3gpp_realm(struct eap_sm *sm, char *imsi,
				    size_t max_len, size_t *imsi_len,
				    int mnc_len)
{
	char *pos, mnc[4];

	if (*imsi_len + 36 > max_len) {
		wpa_printf(MSG_WARNING, "No room for realm in IMSI buffer");
		return -1;
	}

	if (mnc_len != 2 && mnc_len != 3)
		mnc_len = 3;

	if (mnc_len == 2) {
		mnc[0] = '0';
		mnc[1] = imsi[3];
		mnc[2] = imsi[4];
	} else if (mnc_len == 3) {
		mnc[0] = imsi[3];
		mnc[1] = imsi[4];
		mnc[2] = imsi[5];
	}
	mnc[3] = '\0';

	pos = imsi + *imsi_len;
	pos += os_snprintf(pos, imsi + max_len - pos,
			   "@wlan.mnc%s.mcc%c%c%c.3gppnetwork.org",
			   mnc, imsi[0], imsi[1], imsi[2]);
	*imsi_len = pos - imsi;

	return 0;
}
#endif /* PCSC_FUNCS || CONFIG_EAP_PROXY */


/*
 * This state initializes state machine variables when the machine is
 * activated (portEnabled = TRUE). This is also used when re-starting
 * authentication (eapRestart == TRUE).
 */
SM_STATE(EAP, INITIALIZE)
{
	SM_ENTRY(EAP, INITIALIZE);
	if (sm->fast_reauth && sm->m && sm->m->has_reauth_data &&
	    sm->m->has_reauth_data(sm, sm->eap_method_priv) &&
	    !sm->prev_failure &&
	    sm->last_config == eap_get_config(sm)) {
		wpa_printf(MSG_DEBUG, "EAP: maintaining EAP method data for "
			   "fast reauthentication");
		sm->m->deinit_for_reauth(sm, sm->eap_method_priv);
	} else {
		sm->last_config = eap_get_config(sm);
		eap_deinit_prev_method(sm, "INITIALIZE");
	}
	sm->selectedMethod = EAP_TYPE_NONE;
	sm->methodState = METHOD_NONE;
	sm->allowNotifications = TRUE;
	sm->decision = DECISION_FAIL;
	sm->ClientTimeout = EAP_CLIENT_TIMEOUT_DEFAULT;
	eapol_set_int(sm, EAPOL_idleWhile, sm->ClientTimeout);
	eapol_set_bool(sm, EAPOL_eapSuccess, FALSE);
	eapol_set_bool(sm, EAPOL_eapFail, FALSE);
	eap_sm_free_key(sm);
	os_free(sm->eapSessionId);
	sm->eapSessionId = NULL;
	sm->eapKeyAvailable = FALSE;
	eapol_set_bool(sm, EAPOL_eapRestart, FALSE);
	sm->lastId = -1; /* new session - make sure this does not match with
			  * the first EAP-Packet */
	/*
	 * RFC 4137 does not reset eapResp and eapNoResp here. However, this
	 * seemed to be able to trigger cases where both were set and if EAPOL
	 * state machine uses eapNoResp first, it may end up not sending a real
	 * reply correctly. This occurred when the workaround in FAIL state set
	 * eapNoResp = TRUE.. Maybe that workaround needs to be fixed to do
	 * something else(?)
	 */
	eapol_set_bool(sm, EAPOL_eapResp, FALSE);
	eapol_set_bool(sm, EAPOL_eapNoResp, FALSE);
	/*
	 * RFC 4137 does not reset ignore here, but since it is possible for
	 * some method code paths to end up not setting ignore=FALSE, clear the
	 * value here to avoid issues if a previous authentication attempt
	 * failed with ignore=TRUE being left behind in the last
	 * m.check(eapReqData) operation.
	 */
	sm->ignore = 0;
	sm->num_rounds = 0;
	sm->prev_failure = 0;
	sm->expected_failure = 0;
	sm->reauthInit = FALSE;
	sm->erp_seq = (u32) -1;
}


/*
 * This state is reached whenever service from the lower layer is interrupted
 * or unavailable (portEnabled == FALSE). Immediate transition to INITIALIZE
 * occurs when the port becomes enabled.
 */
SM_STATE(EAP, DISABLED)
{
	SM_ENTRY(EAP, DISABLED);
	sm->num_rounds = 0;
	/*
	 * RFC 4137 does not describe clearing of idleWhile here, but doing so
	 * allows the timer tick to be stopped more quickly when EAP is not in
	 * use.
	 */
	eapol_set_int(sm, EAPOL_idleWhile, 0);
}


/*
 * The state machine spends most of its time here, waiting for something to
 * happen. This state is entered unconditionally from INITIALIZE, DISCARD, and
 * SEND_RESPONSE states.
 */
SM_STATE(EAP, IDLE)
{
	SM_ENTRY(EAP, IDLE);
}


/*
 * This state is entered when an EAP packet is received (eapReq == TRUE) to
 * parse the packet header.
 */
SM_STATE(EAP, RECEIVED)
{
	const struct wpabuf *eapReqData;

	SM_ENTRY(EAP, RECEIVED);
	eapReqData = eapol_get_eapReqData(sm);
	/* parse rxReq, rxSuccess, rxFailure, reqId, reqMethod */
	eap_sm_parseEapReq(sm, eapReqData);
	sm->num_rounds++;
}


/*
 * This state is entered when a request for a new type comes in. Either the
 * correct method is started, or a Nak response is built.
 */
SM_STATE(EAP, GET_METHOD)
{
	int reinit;
	EapType method;
	const struct eap_method *eap_method;

	SM_ENTRY(EAP, GET_METHOD);

	if (sm->reqMethod == EAP_TYPE_EXPANDED)
		method = sm->reqVendorMethod;
	else
		method = sm->reqMethod;

	eap_method = eap_peer_get_eap_method(sm->reqVendor, method);

	if (!eap_sm_allowMethod(sm, sm->reqVendor, method)) {
		wpa_printf(MSG_DEBUG, "EAP: vendor %u method %u not allowed",
			   sm->reqVendor, method);
		wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_PROPOSED_METHOD
			"vendor=%u method=%u -> NAK",
			sm->reqVendor, method);
		eap_notify_status(sm, "refuse proposed method",
				  eap_method ?  eap_method->name : "unknown");
		goto nak;
	}

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_PROPOSED_METHOD
		"vendor=%u method=%u", sm->reqVendor, method);

	eap_notify_status(sm, "accept proposed method",
			  eap_method ?  eap_method->name : "unknown");
	/*
	 * RFC 4137 does not define specific operation for fast
	 * re-authentication (session resumption). The design here is to allow
	 * the previously used method data to be maintained for
	 * re-authentication if the method support session resumption.
	 * Otherwise, the previously used method data is freed and a new method
	 * is allocated here.
	 */
	if (sm->fast_reauth &&
	    sm->m && sm->m->vendor == sm->reqVendor &&
	    sm->m->method == method &&
	    sm->m->has_reauth_data &&
	    sm->m->has_reauth_data(sm, sm->eap_method_priv)) {
		wpa_printf(MSG_DEBUG, "EAP: Using previous method data"
			   " for fast re-authentication");
		reinit = 1;
	} else {
		eap_deinit_prev_method(sm, "GET_METHOD");
		reinit = 0;
	}

	sm->selectedMethod = sm->reqMethod;
	if (sm->m == NULL)
		sm->m = eap_method;
	if (!sm->m) {
		wpa_printf(MSG_DEBUG, "EAP: Could not find selected method: "
			   "vendor %d method %d",
			   sm->reqVendor, method);
		goto nak;
	}

	sm->ClientTimeout = EAP_CLIENT_TIMEOUT_DEFAULT;

	wpa_printf(MSG_DEBUG, "EAP: Initialize selected EAP method: "
		   "vendor %u method %u (%s)",
		   sm->reqVendor, method, sm->m->name);
	if (reinit) {
		sm->eap_method_priv = sm->m->init_for_reauth(
			sm, sm->eap_method_priv);
	} else {
		sm->waiting_ext_cert_check = 0;
		sm->ext_cert_check = 0;
		sm->eap_method_priv = sm->m->init(sm);
	}

	if (sm->eap_method_priv == NULL) {
		struct eap_peer_config *config = eap_get_config(sm);
		wpa_msg(sm->msg_ctx, MSG_INFO,
			"EAP: Failed to initialize EAP method: vendor %u "
			"method %u (%s)",
			sm->reqVendor, method, sm->m->name);
		sm->m = NULL;
		sm->methodState = METHOD_NONE;
		sm->selectedMethod = EAP_TYPE_NONE;
		if (sm->reqMethod == EAP_TYPE_TLS && config &&
		    (config->pending_req_pin ||
		     config->pending_req_passphrase)) {
			/*
			 * Return without generating Nak in order to allow
			 * entering of PIN code or passphrase to retry the
			 * current EAP packet.
			 */
			wpa_printf(MSG_DEBUG, "EAP: Pending PIN/passphrase "
				   "request - skip Nak");
			return;
		}

		goto nak;
	}

	sm->methodState = METHOD_INIT;
	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_METHOD
		"EAP vendor %u method %u (%s) selected",
		sm->reqVendor, method, sm->m->name);
	return;

nak:
	wpabuf_free(sm->eapRespData);
	sm->eapRespData = NULL;
	sm->eapRespData = eap_sm_buildNak(sm, sm->reqId);
}


#ifdef CONFIG_ERP

static char * eap_get_realm(struct eap_sm *sm, struct eap_peer_config *config)
{
	char *realm;
	size_t i, realm_len;

	if (!config)
		return NULL;

	if (config->identity) {
		for (i = 0; i < config->identity_len; i++) {
			if (config->identity[i] == '@')
				break;
		}
		if (i < config->identity_len) {
			realm_len = config->identity_len - i - 1;
			realm = os_malloc(realm_len + 1);
			if (realm == NULL)
				return NULL;
			os_memcpy(realm, &config->identity[i + 1], realm_len);
			realm[realm_len] = '\0';
			return realm;
		}
	}

	if (config->anonymous_identity) {
		for (i = 0; i < config->anonymous_identity_len; i++) {
			if (config->anonymous_identity[i] == '@')
				break;
		}
		if (i < config->anonymous_identity_len) {
			realm_len = config->anonymous_identity_len - i - 1;
			realm = os_malloc(realm_len + 1);
			if (realm == NULL)
				return NULL;
			os_memcpy(realm, &config->anonymous_identity[i + 1],
				  realm_len);
			realm[realm_len] = '\0';
			return realm;
		}
	}

#ifdef CONFIG_EAP_PROXY
	/* When identity is not provided in the config, build the realm from
	 * IMSI for eap_proxy based methods.
	 */
	if (!config->identity && !config->anonymous_identity &&
	    sm->eapol_cb->get_imsi &&
	    (eap_config_allowed_method(sm, config, EAP_VENDOR_IETF,
				       EAP_TYPE_SIM) ||
	     eap_config_allowed_method(sm, config, EAP_VENDOR_IETF,
				       EAP_TYPE_AKA) ||
	     eap_config_allowed_method(sm, config, EAP_VENDOR_IETF,
				       EAP_TYPE_AKA_PRIME))) {
		char imsi[100];
		size_t imsi_len;
		int mnc_len, pos;

		wpa_printf(MSG_DEBUG, "EAP: Build realm from IMSI (eap_proxy)");
		mnc_len = sm->eapol_cb->get_imsi(sm->eapol_ctx, config->sim_num,
						 imsi, &imsi_len);
		if (mnc_len < 0)
			return NULL;

		pos = imsi_len + 1; /* points to the beginning of the realm */
		if (eap_sm_append_3gpp_realm(sm, imsi, sizeof(imsi), &imsi_len,
					     mnc_len) < 0) {
			wpa_printf(MSG_WARNING, "Could not append realm");
			return NULL;
		}

		realm = os_strdup(&imsi[pos]);
		if (!realm)
			return NULL;

		wpa_printf(MSG_DEBUG, "EAP: Generated realm '%s'", realm);
		return realm;
	}
#endif /* CONFIG_EAP_PROXY */

	return NULL;
}


static char * eap_home_realm(struct eap_sm *sm)
{
	return eap_get_realm(sm, eap_get_config(sm));
}


static struct eap_erp_key *
eap_erp_get_key(struct eap_sm *sm, const char *realm)
{
	struct eap_erp_key *erp;

	dl_list_for_each(erp, &sm->erp_keys, struct eap_erp_key, list) {
		char *pos;

		pos = os_strchr(erp->keyname_nai, '@');
		if (!pos)
			continue;
		pos++;
		if (os_strcmp(pos, realm) == 0)
			return erp;
	}

	return NULL;
}


static struct eap_erp_key *
eap_erp_get_key_nai(struct eap_sm *sm, const char *nai)
{
	struct eap_erp_key *erp;

	dl_list_for_each(erp, &sm->erp_keys, struct eap_erp_key, list) {
		if (os_strcmp(erp->keyname_nai, nai) == 0)
			return erp;
	}

	return NULL;
}


static void eap_peer_erp_free_key(struct eap_erp_key *erp)
{
	dl_list_del(&erp->list);
	bin_clear_free(erp, sizeof(*erp));
}


static void eap_erp_remove_keys_realm(struct eap_sm *sm, const char *realm)
{
	struct eap_erp_key *erp;

	while ((erp = eap_erp_get_key(sm, realm)) != NULL) {
		wpa_printf(MSG_DEBUG, "EAP: Delete old ERP key %s",
			   erp->keyname_nai);
		eap_peer_erp_free_key(erp);
	}
}


int eap_peer_update_erp_next_seq_num(struct eap_sm *sm, u16 next_seq_num)
{
	struct eap_erp_key *erp;
	char *home_realm;

	home_realm = eap_home_realm(sm);
	if (!home_realm || os_strlen(home_realm) == 0) {
		os_free(home_realm);
		return -1;
	}

	erp = eap_erp_get_key(sm, home_realm);
	if (!erp) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Failed to find ERP key for realm: %s",
			   home_realm);
		os_free(home_realm);
		return -1;
	}

	if ((u32) next_seq_num < erp->next_seq) {
		/* Sequence number has wrapped around, clear this ERP
		 * info and do a full auth next time.
		 */
		eap_peer_erp_free_key(erp);
	} else {
		erp->next_seq = (u32) next_seq_num;
	}

	os_free(home_realm);
	return 0;
}


int eap_peer_get_erp_info(struct eap_sm *sm, struct eap_peer_config *config,
			  const u8 **username, size_t *username_len,
			  const u8 **realm, size_t *realm_len,
			  u16 *erp_next_seq_num, const u8 **rrk,
			  size_t *rrk_len)
{
	struct eap_erp_key *erp;
	char *home_realm;
	char *pos;

	if (config)
		home_realm = eap_get_realm(sm, config);
	else
		home_realm = eap_home_realm(sm);
	if (!home_realm || os_strlen(home_realm) == 0) {
		os_free(home_realm);
		return -1;
	}

	erp = eap_erp_get_key(sm, home_realm);
	os_free(home_realm);
	if (!erp)
		return -1;

	if (erp->next_seq >= 65536)
		return -1; /* SEQ has range of 0..65535 */

	pos = os_strchr(erp->keyname_nai, '@');
	if (!pos)
		return -1; /* this cannot really happen */
	*username_len = pos - erp->keyname_nai;
	*username = (u8 *) erp->keyname_nai;

	pos++;
	*realm_len = os_strlen(pos);
	*realm = (u8 *) pos;

	*erp_next_seq_num = (u16) erp->next_seq;

	*rrk_len = erp->rRK_len;
	*rrk = erp->rRK;

	if (*username_len == 0 || *realm_len == 0 || *rrk_len == 0)
		return -1;

	return 0;
}

#endif /* CONFIG_ERP */


void eap_peer_erp_free_keys(struct eap_sm *sm)
{
#ifdef CONFIG_ERP
	struct eap_erp_key *erp, *tmp;

	dl_list_for_each_safe(erp, tmp, &sm->erp_keys, struct eap_erp_key, list)
		eap_peer_erp_free_key(erp);
#endif /* CONFIG_ERP */
}


/* Note: If ext_session and/or ext_emsk are passed to this function, they are
 * expected to point to allocated memory and those allocations will be freed
 * unconditionally. */
void eap_peer_erp_init(struct eap_sm *sm, u8 *ext_session_id,
		       size_t ext_session_id_len, u8 *ext_emsk,
		       size_t ext_emsk_len)
{
#ifdef CONFIG_ERP
	u8 *emsk = NULL;
	size_t emsk_len = 0;
	u8 *session_id = NULL;
	size_t session_id_len = 0;
	u8 EMSKname[EAP_EMSK_NAME_LEN];
	u8 len[2], ctx[3];
	char *realm;
	size_t realm_len, nai_buf_len;
	struct eap_erp_key *erp = NULL;
	int pos;

	realm = eap_home_realm(sm);
	if (!realm)
		goto fail;
	realm_len = os_strlen(realm);
	wpa_printf(MSG_DEBUG, "EAP: Realm for ERP keyName-NAI: %s", realm);
	eap_erp_remove_keys_realm(sm, realm);

	nai_buf_len = 2 * EAP_EMSK_NAME_LEN + 1 + realm_len;
	if (nai_buf_len > 253) {
		/*
		 * keyName-NAI has a maximum length of 253 octet to fit in
		 * RADIUS attributes.
		 */
		wpa_printf(MSG_DEBUG,
			   "EAP: Too long realm for ERP keyName-NAI maximum length");
		goto fail;
	}
	nai_buf_len++; /* null termination */
	erp = os_zalloc(sizeof(*erp) + nai_buf_len);
	if (erp == NULL)
		goto fail;

	if (ext_emsk) {
		emsk = ext_emsk;
		emsk_len = ext_emsk_len;
	} else {
		emsk = sm->m->get_emsk(sm, sm->eap_method_priv, &emsk_len);
	}

	if (!emsk || emsk_len == 0 || emsk_len > ERP_MAX_KEY_LEN) {
		wpa_printf(MSG_DEBUG,
			   "EAP: No suitable EMSK available for ERP");
		goto fail;
	}

	wpa_hexdump_key(MSG_DEBUG, "EAP: EMSK", emsk, emsk_len);

	if (ext_session_id) {
		session_id = ext_session_id;
		session_id_len = ext_session_id_len;
	} else {
		session_id = sm->eapSessionId;
		session_id_len = sm->eapSessionIdLen;
	}

	if (!session_id || session_id_len == 0) {
		wpa_printf(MSG_DEBUG,
			   "EAP: No suitable session id available for ERP");
		goto fail;
	}

	WPA_PUT_BE16(len, EAP_EMSK_NAME_LEN);
	if (hmac_sha256_kdf(session_id, session_id_len, "EMSK", len,
			    sizeof(len), EMSKname, EAP_EMSK_NAME_LEN) < 0) {
		wpa_printf(MSG_DEBUG, "EAP: Could not derive EMSKname");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "EAP: EMSKname", EMSKname, EAP_EMSK_NAME_LEN);

	pos = wpa_snprintf_hex(erp->keyname_nai, nai_buf_len,
			       EMSKname, EAP_EMSK_NAME_LEN);
	erp->keyname_nai[pos] = '@';
	os_memcpy(&erp->keyname_nai[pos + 1], realm, realm_len);

	WPA_PUT_BE16(len, emsk_len);
	if (hmac_sha256_kdf(emsk, emsk_len,
			    "EAP Re-authentication Root Key@ietf.org",
			    len, sizeof(len), erp->rRK, emsk_len) < 0) {
		wpa_printf(MSG_DEBUG, "EAP: Could not derive rRK for ERP");
		goto fail;
	}
	erp->rRK_len = emsk_len;
	wpa_hexdump_key(MSG_DEBUG, "EAP: ERP rRK", erp->rRK, erp->rRK_len);

	ctx[0] = EAP_ERP_CS_HMAC_SHA256_128;
	WPA_PUT_BE16(&ctx[1], erp->rRK_len);
	if (hmac_sha256_kdf(erp->rRK, erp->rRK_len,
			    "Re-authentication Integrity Key@ietf.org",
			    ctx, sizeof(ctx), erp->rIK, erp->rRK_len) < 0) {
		wpa_printf(MSG_DEBUG, "EAP: Could not derive rIK for ERP");
		goto fail;
	}
	erp->rIK_len = erp->rRK_len;
	wpa_hexdump_key(MSG_DEBUG, "EAP: ERP rIK", erp->rIK, erp->rIK_len);

	wpa_printf(MSG_DEBUG, "EAP: Stored ERP keys %s", erp->keyname_nai);
	dl_list_add(&sm->erp_keys, &erp->list);
	erp = NULL;
fail:
	if (ext_emsk)
		bin_clear_free(ext_emsk, ext_emsk_len);
	else
		bin_clear_free(emsk, emsk_len);
	bin_clear_free(ext_session_id, ext_session_id_len);
	bin_clear_free(erp, sizeof(*erp));
	os_free(realm);
#endif /* CONFIG_ERP */
}


#ifdef CONFIG_ERP
struct wpabuf * eap_peer_build_erp_reauth_start(struct eap_sm *sm, u8 eap_id)
{
	char *realm;
	struct eap_erp_key *erp;
	struct wpabuf *msg;
	u8 hash[SHA256_MAC_LEN];

	realm = eap_home_realm(sm);
	if (!realm)
		return NULL;

	erp = eap_erp_get_key(sm, realm);
	os_free(realm);
	realm = NULL;
	if (!erp)
		return NULL;

	if (erp->next_seq >= 65536)
		return NULL; /* SEQ has range of 0..65535 */

	/* TODO: check rRK lifetime expiration */

	wpa_printf(MSG_DEBUG, "EAP: Valid ERP key found %s (SEQ=%u)",
		   erp->keyname_nai, erp->next_seq);

	msg = eap_msg_alloc(EAP_VENDOR_IETF, (EapType) EAP_ERP_TYPE_REAUTH,
			    1 + 2 + 2 + os_strlen(erp->keyname_nai) + 1 + 16,
			    EAP_CODE_INITIATE, eap_id);
	if (msg == NULL)
		return NULL;

	wpabuf_put_u8(msg, 0x20); /* Flags: R=0 B=0 L=1 */
	wpabuf_put_be16(msg, erp->next_seq);

	wpabuf_put_u8(msg, EAP_ERP_TLV_KEYNAME_NAI);
	wpabuf_put_u8(msg, os_strlen(erp->keyname_nai));
	wpabuf_put_str(msg, erp->keyname_nai);

	wpabuf_put_u8(msg, EAP_ERP_CS_HMAC_SHA256_128); /* Cryptosuite */

	if (hmac_sha256(erp->rIK, erp->rIK_len,
			wpabuf_head(msg), wpabuf_len(msg), hash) < 0) {
		wpabuf_free(msg);
		return NULL;
	}
	wpabuf_put_data(msg, hash, 16);

	sm->erp_seq = erp->next_seq;
	erp->next_seq++;

	wpa_hexdump_buf(MSG_DEBUG, "ERP: EAP-Initiate/Re-auth", msg);

	return msg;
}


static int eap_peer_erp_reauth_start(struct eap_sm *sm, u8 eap_id)
{
	struct wpabuf *msg;

	msg = eap_peer_build_erp_reauth_start(sm, eap_id);
	if (!msg)
		return -1;

	wpa_printf(MSG_DEBUG, "EAP: Sending EAP-Initiate/Re-auth");
	wpabuf_free(sm->eapRespData);
	sm->eapRespData = msg;
	sm->reauthInit = TRUE;
	return 0;
}
#endif /* CONFIG_ERP */


/*
 * The method processing happens here. The request from the authenticator is
 * processed, and an appropriate response packet is built.
 */
SM_STATE(EAP, METHOD)
{
	struct wpabuf *eapReqData;
	struct eap_method_ret ret;
	int min_len = 1;

	SM_ENTRY(EAP, METHOD);
	if (sm->m == NULL) {
		wpa_printf(MSG_WARNING, "EAP::METHOD - method not selected");
		return;
	}

	eapReqData = eapol_get_eapReqData(sm);
	if (sm->m->vendor == EAP_VENDOR_IETF && sm->m->method == EAP_TYPE_LEAP)
		min_len = 0; /* LEAP uses EAP-Success without payload */
	if (!eap_hdr_len_valid(eapReqData, min_len))
		return;

	/*
	 * Get ignore, methodState, decision, allowNotifications, and
	 * eapRespData. RFC 4137 uses three separate method procedure (check,
	 * process, and buildResp) in this state. These have been combined into
	 * a single function call to m->process() in order to optimize EAP
	 * method implementation interface a bit. These procedures are only
	 * used from within this METHOD state, so there is no need to keep
	 * these as separate C functions.
	 *
	 * The RFC 4137 procedures return values as follows:
	 * ignore = m.check(eapReqData)
	 * (methodState, decision, allowNotifications) = m.process(eapReqData)
	 * eapRespData = m.buildResp(reqId)
	 */
	os_memset(&ret, 0, sizeof(ret));
	ret.ignore = sm->ignore;
	ret.methodState = sm->methodState;
	ret.decision = sm->decision;
	ret.allowNotifications = sm->allowNotifications;
	wpabuf_free(sm->eapRespData);
	sm->eapRespData = NULL;
	sm->eapRespData = sm->m->process(sm, sm->eap_method_priv, &ret,
					 eapReqData);
	wpa_printf(MSG_DEBUG, "EAP: method process -> ignore=%s "
		   "methodState=%s decision=%s eapRespData=%p",
		   ret.ignore ? "TRUE" : "FALSE",
		   eap_sm_method_state_txt(ret.methodState),
		   eap_sm_decision_txt(ret.decision),
		   sm->eapRespData);

	sm->ignore = ret.ignore;
	if (sm->ignore)
		return;
	sm->methodState = ret.methodState;
	sm->decision = ret.decision;
	sm->allowNotifications = ret.allowNotifications;

	if (sm->m->isKeyAvailable && sm->m->getKey &&
	    sm->m->isKeyAvailable(sm, sm->eap_method_priv)) {
		eap_sm_free_key(sm);
		sm->eapKeyData = sm->m->getKey(sm, sm->eap_method_priv,
					       &sm->eapKeyDataLen);
		os_free(sm->eapSessionId);
		sm->eapSessionId = NULL;
		if (sm->m->getSessionId) {
			sm->eapSessionId = sm->m->getSessionId(
				sm, sm->eap_method_priv,
				&sm->eapSessionIdLen);
			wpa_hexdump(MSG_DEBUG, "EAP: Session-Id",
				    sm->eapSessionId, sm->eapSessionIdLen);
		}
	}
}


/*
 * This state signals the lower layer that a response packet is ready to be
 * sent.
 */
SM_STATE(EAP, SEND_RESPONSE)
{
	SM_ENTRY(EAP, SEND_RESPONSE);
	wpabuf_free(sm->lastRespData);
	if (sm->eapRespData) {
		if (sm->workaround)
			os_memcpy(sm->last_sha1, sm->req_sha1, 20);
		sm->lastId = sm->reqId;
		sm->lastRespData = wpabuf_dup(sm->eapRespData);
		eapol_set_bool(sm, EAPOL_eapResp, TRUE);
	} else {
		wpa_printf(MSG_DEBUG, "EAP: No eapRespData available");
		sm->lastRespData = NULL;
	}
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);
	eapol_set_int(sm, EAPOL_idleWhile, sm->ClientTimeout);
	sm->reauthInit = FALSE;
}


/*
 * This state signals the lower layer that the request was discarded, and no
 * response packet will be sent at this time.
 */
SM_STATE(EAP, DISCARD)
{
	SM_ENTRY(EAP, DISCARD);
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);
	eapol_set_bool(sm, EAPOL_eapNoResp, TRUE);
}


/*
 * Handles requests for Identity method and builds a response.
 */
SM_STATE(EAP, IDENTITY)
{
	const struct wpabuf *eapReqData;

	SM_ENTRY(EAP, IDENTITY);
	eapReqData = eapol_get_eapReqData(sm);
	if (!eap_hdr_len_valid(eapReqData, 1))
		return;
	eap_sm_processIdentity(sm, eapReqData);
	wpabuf_free(sm->eapRespData);
	sm->eapRespData = NULL;
	sm->eapRespData = eap_sm_buildIdentity(sm, sm->reqId, 0);
}


/*
 * Handles requests for Notification method and builds a response.
 */
SM_STATE(EAP, NOTIFICATION)
{
	const struct wpabuf *eapReqData;

	SM_ENTRY(EAP, NOTIFICATION);
	eapReqData = eapol_get_eapReqData(sm);
	if (!eap_hdr_len_valid(eapReqData, 1))
		return;
	eap_sm_processNotify(sm, eapReqData);
	wpabuf_free(sm->eapRespData);
	sm->eapRespData = NULL;
	sm->eapRespData = eap_sm_buildNotify(sm->reqId);
}


/*
 * This state retransmits the previous response packet.
 */
SM_STATE(EAP, RETRANSMIT)
{
	SM_ENTRY(EAP, RETRANSMIT);
	wpabuf_free(sm->eapRespData);
	if (sm->lastRespData)
		sm->eapRespData = wpabuf_dup(sm->lastRespData);
	else
		sm->eapRespData = NULL;
}


/*
 * This state is entered in case of a successful completion of authentication
 * and state machine waits here until port is disabled or EAP authentication is
 * restarted.
 */
SM_STATE(EAP, SUCCESS)
{
	struct eap_peer_config *config = eap_get_config(sm);

	SM_ENTRY(EAP, SUCCESS);
	if (sm->eapKeyData != NULL)
		sm->eapKeyAvailable = TRUE;
	eapol_set_bool(sm, EAPOL_eapSuccess, TRUE);

	/*
	 * RFC 4137 does not clear eapReq here, but this seems to be required
	 * to avoid processing the same request twice when state machine is
	 * initialized.
	 */
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);

	/*
	 * RFC 4137 does not set eapNoResp here, but this seems to be required
	 * to get EAPOL Supplicant backend state machine into SUCCESS state. In
	 * addition, either eapResp or eapNoResp is required to be set after
	 * processing the received EAP frame.
	 */
	eapol_set_bool(sm, EAPOL_eapNoResp, TRUE);

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_SUCCESS
		"EAP authentication completed successfully");

	if (config->erp && sm->m->get_emsk && sm->eapSessionId &&
	    sm->m->isKeyAvailable &&
	    sm->m->isKeyAvailable(sm, sm->eap_method_priv))
		eap_peer_erp_init(sm, NULL, 0, NULL, 0);
}


/*
 * This state is entered in case of a failure and state machine waits here
 * until port is disabled or EAP authentication is restarted.
 */
SM_STATE(EAP, FAILURE)
{
	SM_ENTRY(EAP, FAILURE);
	eapol_set_bool(sm, EAPOL_eapFail, TRUE);

	/*
	 * RFC 4137 does not clear eapReq here, but this seems to be required
	 * to avoid processing the same request twice when state machine is
	 * initialized.
	 */
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);

	/*
	 * RFC 4137 does not set eapNoResp here. However, either eapResp or
	 * eapNoResp is required to be set after processing the received EAP
	 * frame.
	 */
	eapol_set_bool(sm, EAPOL_eapNoResp, TRUE);

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_FAILURE
		"EAP authentication failed");

	sm->prev_failure = 1;
}


static int eap_success_workaround(struct eap_sm *sm, int reqId, int lastId)
{
	/*
	 * At least Microsoft IAS and Meetinghouse Aegis seem to be sending
	 * EAP-Success/Failure with lastId + 1 even though RFC 3748 and
	 * RFC 4137 require that reqId == lastId. In addition, it looks like
	 * Ringmaster v2.1.2.0 would be using lastId + 2 in EAP-Success.
	 *
	 * Accept this kind of Id if EAP workarounds are enabled. These are
	 * unauthenticated plaintext messages, so this should have minimal
	 * security implications (bit easier to fake EAP-Success/Failure).
	 */
	if (sm->workaround && (reqId == ((lastId + 1) & 0xff) ||
			       reqId == ((lastId + 2) & 0xff))) {
		wpa_printf(MSG_DEBUG, "EAP: Workaround for unexpected "
			   "identifier field in EAP Success: "
			   "reqId=%d lastId=%d (these are supposed to be "
			   "same)", reqId, lastId);
		return 1;
	}
	wpa_printf(MSG_DEBUG, "EAP: EAP-Success Id mismatch - reqId=%d "
		   "lastId=%d", reqId, lastId);
	return 0;
}


/*
 * RFC 4137 - Appendix A.1: EAP Peer State Machine - State transitions
 */

static void eap_peer_sm_step_idle(struct eap_sm *sm)
{
	/*
	 * The first three transitions are from RFC 4137. The last two are
	 * local additions to handle special cases with LEAP and PEAP server
	 * not sending EAP-Success in some cases.
	 */
	if (eapol_get_bool(sm, EAPOL_eapReq))
		SM_ENTER(EAP, RECEIVED);
	else if ((eapol_get_bool(sm, EAPOL_altAccept) &&
		  sm->decision != DECISION_FAIL) ||
		 (eapol_get_int(sm, EAPOL_idleWhile) == 0 &&
		  sm->decision == DECISION_UNCOND_SUCC))
		SM_ENTER(EAP, SUCCESS);
	else if (eapol_get_bool(sm, EAPOL_altReject) ||
		 (eapol_get_int(sm, EAPOL_idleWhile) == 0 &&
		  sm->decision != DECISION_UNCOND_SUCC) ||
		 (eapol_get_bool(sm, EAPOL_altAccept) &&
		  sm->methodState != METHOD_CONT &&
		  sm->decision == DECISION_FAIL))
		SM_ENTER(EAP, FAILURE);
	else if (sm->selectedMethod == EAP_TYPE_LEAP &&
		 sm->leap_done && sm->decision != DECISION_FAIL &&
		 sm->methodState == METHOD_DONE)
		SM_ENTER(EAP, SUCCESS);
	else if (sm->selectedMethod == EAP_TYPE_PEAP &&
		 sm->peap_done && sm->decision != DECISION_FAIL &&
		 sm->methodState == METHOD_DONE)
		SM_ENTER(EAP, SUCCESS);
}


static int eap_peer_req_is_duplicate(struct eap_sm *sm)
{
	int duplicate;

	duplicate = (sm->reqId == sm->lastId) && sm->rxReq;
	if (sm->workaround && duplicate &&
	    os_memcmp(sm->req_sha1, sm->last_sha1, 20) != 0) {
		/*
		 * RFC 4137 uses (reqId == lastId) as the only verification for
		 * duplicate EAP requests. However, this misses cases where the
		 * AS is incorrectly using the same id again; and
		 * unfortunately, such implementations exist. Use SHA1 hash as
		 * an extra verification for the packets being duplicate to
		 * workaround these issues.
		 */
		wpa_printf(MSG_DEBUG, "EAP: AS used the same Id again, but "
			   "EAP packets were not identical");
		wpa_printf(MSG_DEBUG, "EAP: workaround - assume this is not a "
			   "duplicate packet");
		duplicate = 0;
	}

	return duplicate;
}


static int eap_peer_sm_allow_canned(struct eap_sm *sm)
{
	struct eap_peer_config *config = eap_get_config(sm);

	return config && config->phase1 &&
		os_strstr(config->phase1, "allow_canned_success=1");
}


static void eap_peer_sm_step_received(struct eap_sm *sm)
{
	int duplicate = eap_peer_req_is_duplicate(sm);

	/*
	 * Two special cases below for LEAP are local additions to work around
	 * odd LEAP behavior (EAP-Success in the middle of authentication and
	 * then swapped roles). Other transitions are based on RFC 4137.
	 */
	if (sm->rxSuccess && sm->decision != DECISION_FAIL &&
	    (sm->reqId == sm->lastId ||
	     eap_success_workaround(sm, sm->reqId, sm->lastId)))
		SM_ENTER(EAP, SUCCESS);
	else if (sm->workaround && sm->lastId == -1 && sm->rxSuccess &&
		 !sm->rxFailure && !sm->rxReq && eap_peer_sm_allow_canned(sm))
		SM_ENTER(EAP, SUCCESS); /* EAP-Success prior any EAP method */
	else if (sm->workaround && sm->lastId == -1 && sm->rxFailure &&
		 !sm->rxReq && sm->methodState != METHOD_CONT &&
		 eap_peer_sm_allow_canned(sm))
		SM_ENTER(EAP, FAILURE); /* EAP-Failure prior any EAP method */
	else if (sm->workaround && sm->rxSuccess && !sm->rxFailure &&
		 !sm->rxReq && sm->methodState != METHOD_CONT &&
		 eap_peer_sm_allow_canned(sm))
		SM_ENTER(EAP, SUCCESS); /* EAP-Success after Identity */
	else if (sm->methodState != METHOD_CONT &&
		 ((sm->rxFailure &&
		   sm->decision != DECISION_UNCOND_SUCC) ||
		  (sm->rxSuccess && sm->decision == DECISION_FAIL &&
		   (sm->selectedMethod != EAP_TYPE_LEAP ||
		    sm->methodState != METHOD_MAY_CONT))) &&
		 (sm->reqId == sm->lastId ||
		  eap_success_workaround(sm, sm->reqId, sm->lastId)))
		SM_ENTER(EAP, FAILURE);
	else if (sm->rxReq && duplicate)
		SM_ENTER(EAP, RETRANSMIT);
	else if (sm->rxReq && !duplicate &&
		 sm->reqMethod == EAP_TYPE_NOTIFICATION &&
		 sm->allowNotifications)
		SM_ENTER(EAP, NOTIFICATION);
	else if (sm->rxReq && !duplicate &&
		 sm->selectedMethod == EAP_TYPE_NONE &&
		 sm->reqMethod == EAP_TYPE_IDENTITY)
		SM_ENTER(EAP, IDENTITY);
	else if (sm->rxReq && !duplicate &&
		 sm->selectedMethod == EAP_TYPE_NONE &&
		 sm->reqMethod != EAP_TYPE_IDENTITY &&
		 sm->reqMethod != EAP_TYPE_NOTIFICATION)
		SM_ENTER(EAP, GET_METHOD);
	else if (sm->rxReq && !duplicate &&
		 sm->reqMethod == sm->selectedMethod &&
		 sm->methodState != METHOD_DONE)
		SM_ENTER(EAP, METHOD);
	else if (sm->selectedMethod == EAP_TYPE_LEAP &&
		 (sm->rxSuccess || sm->rxResp))
		SM_ENTER(EAP, METHOD);
	else if (sm->reauthInit)
		SM_ENTER(EAP, SEND_RESPONSE);
	else
		SM_ENTER(EAP, DISCARD);
}


static void eap_peer_sm_step_local(struct eap_sm *sm)
{
	switch (sm->EAP_state) {
	case EAP_INITIALIZE:
		SM_ENTER(EAP, IDLE);
		break;
	case EAP_DISABLED:
		if (eapol_get_bool(sm, EAPOL_portEnabled) &&
		    !sm->force_disabled)
			SM_ENTER(EAP, INITIALIZE);
		break;
	case EAP_IDLE:
		eap_peer_sm_step_idle(sm);
		break;
	case EAP_RECEIVED:
		eap_peer_sm_step_received(sm);
		break;
	case EAP_GET_METHOD:
		if (sm->selectedMethod == sm->reqMethod)
			SM_ENTER(EAP, METHOD);
		else
			SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_METHOD:
		/*
		 * Note: RFC 4137 uses methodState == DONE && decision == FAIL
		 * as the condition. eapRespData == NULL here is used to allow
		 * final EAP method response to be sent without having to change
		 * all methods to either use methodState MAY_CONT or leaving
		 * decision to something else than FAIL in cases where the only
		 * expected response is EAP-Failure.
		 */
		if (sm->ignore)
			SM_ENTER(EAP, DISCARD);
		else if (sm->methodState == METHOD_DONE &&
			 sm->decision == DECISION_FAIL && !sm->eapRespData)
			SM_ENTER(EAP, FAILURE);
		else
			SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_SEND_RESPONSE:
		SM_ENTER(EAP, IDLE);
		break;
	case EAP_DISCARD:
		SM_ENTER(EAP, IDLE);
		break;
	case EAP_IDENTITY:
		SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_NOTIFICATION:
		SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_RETRANSMIT:
		SM_ENTER(EAP, SEND_RESPONSE);
		break;
	case EAP_SUCCESS:
		break;
	case EAP_FAILURE:
		break;
	}
}


SM_STEP(EAP)
{
	/* Global transitions */
	if (eapol_get_bool(sm, EAPOL_eapRestart) &&
	    eapol_get_bool(sm, EAPOL_portEnabled))
		SM_ENTER_GLOBAL(EAP, INITIALIZE);
	else if (!eapol_get_bool(sm, EAPOL_portEnabled) || sm->force_disabled)
		SM_ENTER_GLOBAL(EAP, DISABLED);
	else if (sm->num_rounds > EAP_MAX_AUTH_ROUNDS) {
		/* RFC 4137 does not place any limit on number of EAP messages
		 * in an authentication session. However, some error cases have
		 * ended up in a state were EAP messages were sent between the
		 * peer and server in a loop (e.g., TLS ACK frame in both
		 * direction). Since this is quite undesired outcome, limit the
		 * total number of EAP round-trips and abort authentication if
		 * this limit is exceeded.
		 */
		if (sm->num_rounds == EAP_MAX_AUTH_ROUNDS + 1) {
			wpa_msg(sm->msg_ctx, MSG_INFO, "EAP: more than %d "
				"authentication rounds - abort",
				EAP_MAX_AUTH_ROUNDS);
			sm->num_rounds++;
			SM_ENTER_GLOBAL(EAP, FAILURE);
		}
	} else {
		/* Local transitions */
		eap_peer_sm_step_local(sm);
	}
}


static Boolean eap_sm_allowMethod(struct eap_sm *sm, int vendor,
				  EapType method)
{
	if (!eap_allowed_method(sm, vendor, method)) {
		wpa_printf(MSG_DEBUG, "EAP: configuration does not allow: "
			   "vendor %u method %u", vendor, method);
		return FALSE;
	}
	if (eap_peer_get_eap_method(vendor, method))
		return TRUE;
	wpa_printf(MSG_DEBUG, "EAP: not included in build: "
		   "vendor %u method %u", vendor, method);
	return FALSE;
}


static struct wpabuf * eap_sm_build_expanded_nak(
	struct eap_sm *sm, int id, const struct eap_method *methods,
	size_t count)
{
	struct wpabuf *resp;
	int found = 0;
	const struct eap_method *m;

	wpa_printf(MSG_DEBUG, "EAP: Building expanded EAP-Nak");

	/* RFC 3748 - 5.3.2: Expanded Nak */
	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_EXPANDED,
			     8 + 8 * (count + 1), EAP_CODE_RESPONSE, id);
	if (resp == NULL)
		return NULL;

	wpabuf_put_be24(resp, EAP_VENDOR_IETF);
	wpabuf_put_be32(resp, EAP_TYPE_NAK);

	for (m = methods; m; m = m->next) {
		if (sm->reqVendor == m->vendor &&
		    sm->reqVendorMethod == m->method)
			continue; /* do not allow the current method again */
		if (eap_allowed_method(sm, m->vendor, m->method)) {
			wpa_printf(MSG_DEBUG, "EAP: allowed type: "
				   "vendor=%u method=%u",
				   m->vendor, m->method);
			wpabuf_put_u8(resp, EAP_TYPE_EXPANDED);
			wpabuf_put_be24(resp, m->vendor);
			wpabuf_put_be32(resp, m->method);

			found++;
		}
	}
	if (!found) {
		wpa_printf(MSG_DEBUG, "EAP: no more allowed methods");
		wpabuf_put_u8(resp, EAP_TYPE_EXPANDED);
		wpabuf_put_be24(resp, EAP_VENDOR_IETF);
		wpabuf_put_be32(resp, EAP_TYPE_NONE);
	}

	eap_update_len(resp);

	return resp;
}


static struct wpabuf * eap_sm_buildNak(struct eap_sm *sm, int id)
{
	struct wpabuf *resp;
	u8 *start;
	int found = 0, expanded_found = 0;
	size_t count;
	const struct eap_method *methods, *m;

	wpa_printf(MSG_DEBUG, "EAP: Building EAP-Nak (requested type %u "
		   "vendor=%u method=%u not allowed)", sm->reqMethod,
		   sm->reqVendor, sm->reqVendorMethod);
	methods = eap_peer_get_methods(&count);
	if (methods == NULL)
		return NULL;
	if (sm->reqMethod == EAP_TYPE_EXPANDED)
		return eap_sm_build_expanded_nak(sm, id, methods, count);

	/* RFC 3748 - 5.3.1: Legacy Nak */
	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_NAK,
			     sizeof(struct eap_hdr) + 1 + count + 1,
			     EAP_CODE_RESPONSE, id);
	if (resp == NULL)
		return NULL;

	start = wpabuf_put(resp, 0);
	for (m = methods; m; m = m->next) {
		if (m->vendor == EAP_VENDOR_IETF && m->method == sm->reqMethod)
			continue; /* do not allow the current method again */
		if (eap_allowed_method(sm, m->vendor, m->method)) {
			if (m->vendor != EAP_VENDOR_IETF) {
				if (expanded_found)
					continue;
				expanded_found = 1;
				wpabuf_put_u8(resp, EAP_TYPE_EXPANDED);
			} else
				wpabuf_put_u8(resp, m->method);
			found++;
		}
	}
	if (!found)
		wpabuf_put_u8(resp, EAP_TYPE_NONE);
	wpa_hexdump(MSG_DEBUG, "EAP: allowed methods", start, found);

	eap_update_len(resp);

	return resp;
}


static void eap_sm_processIdentity(struct eap_sm *sm, const struct wpabuf *req)
{
	const u8 *pos;
	size_t msg_len;

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_STARTED
		"EAP authentication started");
	eap_notify_status(sm, "started", "");

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_IDENTITY, req,
			       &msg_len);
	if (pos == NULL)
		return;

	/*
	 * RFC 3748 - 5.1: Identity
	 * Data field may contain a displayable message in UTF-8. If this
	 * includes NUL-character, only the data before that should be
	 * displayed. Some EAP implementasitons may piggy-back additional
	 * options after the NUL.
	 */
	/* TODO: could save displayable message so that it can be shown to the
	 * user in case of interaction is required */
	wpa_hexdump_ascii(MSG_DEBUG, "EAP: EAP-Request Identity data",
			  pos, msg_len);
}


#ifdef PCSC_FUNCS

/*
 * Rules for figuring out MNC length based on IMSI for SIM cards that do not
 * include MNC length field.
 */
static int mnc_len_from_imsi(const char *imsi)
{
	char mcc_str[4];
	unsigned int mcc;

	os_memcpy(mcc_str, imsi, 3);
	mcc_str[3] = '\0';
	mcc = atoi(mcc_str);

	if (mcc == 228)
		return 2; /* Networks in Switzerland use 2-digit MNC */
	if (mcc == 244)
		return 2; /* Networks in Finland use 2-digit MNC */

	return -1;
}


static int eap_sm_imsi_identity(struct eap_sm *sm,
				struct eap_peer_config *conf)
{
	enum { EAP_SM_SIM, EAP_SM_AKA, EAP_SM_AKA_PRIME } method = EAP_SM_SIM;
	char imsi[100];
	size_t imsi_len;
	struct eap_method_type *m = conf->eap_methods;
	int i, mnc_len;

	imsi_len = sizeof(imsi);
	if (scard_get_imsi(sm->scard_ctx, imsi, &imsi_len)) {
		wpa_printf(MSG_WARNING, "Failed to get IMSI from SIM");
		return -1;
	}

	wpa_hexdump_ascii(MSG_DEBUG, "IMSI", (u8 *) imsi, imsi_len);

	if (imsi_len < 7) {
		wpa_printf(MSG_WARNING, "Too short IMSI for SIM identity");
		return -1;
	}

	/* MNC (2 or 3 digits) */
	mnc_len = scard_get_mnc_len(sm->scard_ctx);
	if (mnc_len < 0)
		mnc_len = mnc_len_from_imsi(imsi);
	if (mnc_len < 0) {
		wpa_printf(MSG_INFO, "Failed to get MNC length from (U)SIM "
			   "assuming 3");
		mnc_len = 3;
	}

	if (eap_sm_append_3gpp_realm(sm, imsi, sizeof(imsi), &imsi_len,
				     mnc_len) < 0) {
		wpa_printf(MSG_WARNING, "Could not add realm to SIM identity");
		return -1;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "IMSI + realm", (u8 *) imsi, imsi_len);

	for (i = 0; m && (m[i].vendor != EAP_VENDOR_IETF ||
			  m[i].method != EAP_TYPE_NONE); i++) {
		if (m[i].vendor == EAP_VENDOR_IETF &&
		    m[i].method == EAP_TYPE_AKA_PRIME) {
			method = EAP_SM_AKA_PRIME;
			break;
		}

		if (m[i].vendor == EAP_VENDOR_IETF &&
		    m[i].method == EAP_TYPE_AKA) {
			method = EAP_SM_AKA;
			break;
		}
	}

	os_free(conf->identity);
	conf->identity = os_malloc(1 + imsi_len);
	if (conf->identity == NULL) {
		wpa_printf(MSG_WARNING, "Failed to allocate buffer for "
			   "IMSI-based identity");
		return -1;
	}

	switch (method) {
	case EAP_SM_SIM:
		conf->identity[0] = '1';
		break;
	case EAP_SM_AKA:
		conf->identity[0] = '0';
		break;
	case EAP_SM_AKA_PRIME:
		conf->identity[0] = '6';
		break;
	}
	os_memcpy(conf->identity + 1, imsi, imsi_len);
	conf->identity_len = 1 + imsi_len;

	return 0;
}


static int eap_sm_set_scard_pin(struct eap_sm *sm,
				struct eap_peer_config *conf)
{
	if (scard_set_pin(sm->scard_ctx, conf->pin)) {
		/*
		 * Make sure the same PIN is not tried again in order to avoid
		 * blocking SIM.
		 */
		os_free(conf->pin);
		conf->pin = NULL;

		wpa_printf(MSG_WARNING, "PIN validation failed");
		eap_sm_request_pin(sm);
		return -1;
	}
	return 0;
}


static int eap_sm_get_scard_identity(struct eap_sm *sm,
				     struct eap_peer_config *conf)
{
	if (eap_sm_set_scard_pin(sm, conf))
		return -1;

	return eap_sm_imsi_identity(sm, conf);
}

#endif /* PCSC_FUNCS */


/**
 * eap_sm_buildIdentity - Build EAP-Identity/Response for the current network
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @id: EAP identifier for the packet
 * @encrypted: Whether the packet is for encrypted tunnel (EAP phase 2)
 * Returns: Pointer to the allocated EAP-Identity/Response packet or %NULL on
 * failure
 *
 * This function allocates and builds an EAP-Identity/Response packet for the
 * current network. The caller is responsible for freeing the returned data.
 */
struct wpabuf * eap_sm_buildIdentity(struct eap_sm *sm, int id, int encrypted)
{
	struct eap_peer_config *config = eap_get_config(sm);
	struct wpabuf *resp;
	const u8 *identity;
	size_t identity_len;

	if (config == NULL) {
		wpa_printf(MSG_WARNING, "EAP: buildIdentity: configuration "
			   "was not available");
		return NULL;
	}

	if (sm->m && sm->m->get_identity &&
	    (identity = sm->m->get_identity(sm, sm->eap_method_priv,
					    &identity_len)) != NULL) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP: using method re-auth "
				  "identity", identity, identity_len);
	} else if (!encrypted && config->anonymous_identity) {
		identity = config->anonymous_identity;
		identity_len = config->anonymous_identity_len;
		wpa_hexdump_ascii(MSG_DEBUG, "EAP: using anonymous identity",
				  identity, identity_len);
	} else {
		identity = config->identity;
		identity_len = config->identity_len;
		wpa_hexdump_ascii(MSG_DEBUG, "EAP: using real identity",
				  identity, identity_len);
	}

	if (config->pcsc) {
#ifdef PCSC_FUNCS
		if (!identity) {
			if (eap_sm_get_scard_identity(sm, config) < 0)
				return NULL;
			identity = config->identity;
			identity_len = config->identity_len;
			wpa_hexdump_ascii(MSG_DEBUG,
					  "permanent identity from IMSI",
					  identity, identity_len);
		} else if (eap_sm_set_scard_pin(sm, config) < 0) {
			return NULL;
		}
#else /* PCSC_FUNCS */
		return NULL;
#endif /* PCSC_FUNCS */
	} else if (!identity) {
		wpa_printf(MSG_WARNING,
			"EAP: buildIdentity: identity configuration was not available");
		eap_sm_request_identity(sm);
		return NULL;
	}

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_IDENTITY, identity_len,
			     EAP_CODE_RESPONSE, id);
	if (resp == NULL)
		return NULL;

	wpabuf_put_data(resp, identity, identity_len);

	return resp;
}


static void eap_sm_processNotify(struct eap_sm *sm, const struct wpabuf *req)
{
	const u8 *pos;
	char *msg;
	size_t i, msg_len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_NOTIFICATION, req,
			       &msg_len);
	if (pos == NULL)
		return;
	wpa_hexdump_ascii(MSG_DEBUG, "EAP: EAP-Request Notification data",
			  pos, msg_len);

	msg = os_malloc(msg_len + 1);
	if (msg == NULL)
		return;
	for (i = 0; i < msg_len; i++)
		msg[i] = isprint(pos[i]) ? (char) pos[i] : '_';
	msg[msg_len] = '\0';
	wpa_msg(sm->msg_ctx, MSG_INFO, "%s%s",
		WPA_EVENT_EAP_NOTIFICATION, msg);
	os_free(msg);
}


static struct wpabuf * eap_sm_buildNotify(int id)
{
	wpa_printf(MSG_DEBUG, "EAP: Generating EAP-Response Notification");
	return eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_NOTIFICATION, 0,
			EAP_CODE_RESPONSE, id);
}


static void eap_peer_initiate(struct eap_sm *sm, const struct eap_hdr *hdr,
			      size_t len)
{
#ifdef CONFIG_ERP
	const u8 *pos = (const u8 *) (hdr + 1);
	const u8 *end = ((const u8 *) hdr) + len;
	struct erp_tlvs parse;

	if (len < sizeof(*hdr) + 1) {
		wpa_printf(MSG_DEBUG, "EAP: Ignored too short EAP-Initiate");
		return;
	}

	if (*pos != EAP_ERP_TYPE_REAUTH_START) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Ignored unexpected EAP-Initiate Type=%u",
			   *pos);
		return;
	}

	pos++;
	if (pos >= end) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Too short EAP-Initiate/Re-auth-Start");
		return;
	}
	pos++; /* Reserved */
	wpa_hexdump(MSG_DEBUG, "EAP: EAP-Initiate/Re-auth-Start TVs/TLVs",
		    pos, end - pos);

	if (erp_parse_tlvs(pos, end, &parse, 0) < 0)
		goto invalid;

	if (parse.domain) {
		wpa_hexdump_ascii(MSG_DEBUG,
				  "EAP: EAP-Initiate/Re-auth-Start - Domain name",
				  parse.domain, parse.domain_len);
		/* TODO: Derivation of domain specific keys for local ER */
	}

	if (eap_peer_erp_reauth_start(sm, hdr->identifier) == 0)
		return;

invalid:
#endif /* CONFIG_ERP */
	wpa_printf(MSG_DEBUG,
		   "EAP: EAP-Initiate/Re-auth-Start - No suitable ERP keys available - try to start full EAP authentication");
	eapol_set_bool(sm, EAPOL_eapTriggerStart, TRUE);
}


void eap_peer_finish(struct eap_sm *sm, const struct eap_hdr *hdr, size_t len)
{
#ifdef CONFIG_ERP
	const u8 *pos = (const u8 *) (hdr + 1);
	const u8 *end = ((const u8 *) hdr) + len;
	const u8 *start;
	struct erp_tlvs parse;
	u8 flags;
	u16 seq;
	u8 hash[SHA256_MAC_LEN];
	size_t hash_len;
	struct eap_erp_key *erp;
	int max_len;
	char nai[254];
	u8 seed[4];
	int auth_tag_ok = 0;

	if (len < sizeof(*hdr) + 1) {
		wpa_printf(MSG_DEBUG, "EAP: Ignored too short EAP-Finish");
		return;
	}

	if (*pos != EAP_ERP_TYPE_REAUTH) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Ignored unexpected EAP-Finish Type=%u", *pos);
		return;
	}

	if (len < sizeof(*hdr) + 4) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Ignored too short EAP-Finish/Re-auth");
		return;
	}

	pos++;
	flags = *pos++;
	seq = WPA_GET_BE16(pos);
	pos += 2;
	wpa_printf(MSG_DEBUG, "EAP: Flags=0x%x SEQ=%u", flags, seq);

	if (seq != sm->erp_seq) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Unexpected EAP-Finish/Re-auth SEQ=%u", seq);
		return;
	}

	/*
	 * Parse TVs/TLVs. Since we do not yet know the length of the
	 * Authentication Tag, stop parsing if an unknown TV/TLV is seen and
	 * just try to find the keyName-NAI first so that we can check the
	 * Authentication Tag.
	 */
	if (erp_parse_tlvs(pos, end, &parse, 1) < 0)
		return;

	if (!parse.keyname) {
		wpa_printf(MSG_DEBUG,
			   "EAP: No keyName-NAI in EAP-Finish/Re-auth Packet");
		return;
	}

	wpa_hexdump_ascii(MSG_DEBUG, "EAP: EAP-Finish/Re-auth - keyName-NAI",
			  parse.keyname, parse.keyname_len);
	if (parse.keyname_len > 253) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Too long keyName-NAI in EAP-Finish/Re-auth");
		return;
	}
	os_memcpy(nai, parse.keyname, parse.keyname_len);
	nai[parse.keyname_len] = '\0';

	erp = eap_erp_get_key_nai(sm, nai);
	if (!erp) {
		wpa_printf(MSG_DEBUG, "EAP: No matching ERP key found for %s",
			   nai);
		return;
	}

	/* Is there enough room for Cryptosuite and Authentication Tag? */
	start = parse.keyname + parse.keyname_len;
	max_len = end - start;
	hash_len = 16;
	if (max_len < 1 + (int) hash_len) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Not enough room for Authentication Tag");
		if (flags & 0x80)
			goto no_auth_tag;
		return;
	}
	if (end[-17] != EAP_ERP_CS_HMAC_SHA256_128) {
		wpa_printf(MSG_DEBUG, "EAP: Different Cryptosuite used");
		if (flags & 0x80)
			goto no_auth_tag;
		return;
	}

	if (hmac_sha256(erp->rIK, erp->rIK_len, (const u8 *) hdr,
			end - ((const u8 *) hdr) - hash_len, hash) < 0)
		return;
	if (os_memcmp(end - hash_len, hash, hash_len) != 0) {
		wpa_printf(MSG_DEBUG,
			   "EAP: Authentication Tag mismatch");
		return;
	}
	auth_tag_ok = 1;
	end -= 1 + hash_len;

no_auth_tag:
	/*
	 * Parse TVs/TLVs again now that we know the exact part of the buffer
	 * that contains them.
	 */
	wpa_hexdump(MSG_DEBUG, "EAP: EAP-Finish/Re-Auth TVs/TLVs",
		    pos, end - pos);
	if (erp_parse_tlvs(pos, end, &parse, 0) < 0)
		return;

	if (flags & 0x80 || !auth_tag_ok) {
		wpa_printf(MSG_DEBUG,
			   "EAP: EAP-Finish/Re-auth indicated failure");
		eapol_set_bool(sm, EAPOL_eapFail, TRUE);
		eapol_set_bool(sm, EAPOL_eapReq, FALSE);
		eapol_set_bool(sm, EAPOL_eapNoResp, TRUE);
		wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_FAILURE
			"EAP authentication failed");
		sm->prev_failure = 1;
		wpa_printf(MSG_DEBUG,
			   "EAP: Drop ERP key to try full authentication on next attempt");
		eap_peer_erp_free_key(erp);
		return;
	}

	eap_sm_free_key(sm);
	sm->eapKeyDataLen = 0;
	sm->eapKeyData = os_malloc(erp->rRK_len);
	if (!sm->eapKeyData)
		return;
	sm->eapKeyDataLen = erp->rRK_len;

	WPA_PUT_BE16(seed, seq);
	WPA_PUT_BE16(&seed[2], erp->rRK_len);
	if (hmac_sha256_kdf(erp->rRK, erp->rRK_len,
			    "Re-authentication Master Session Key@ietf.org",
			    seed, sizeof(seed),
			    sm->eapKeyData, erp->rRK_len) < 0) {
		wpa_printf(MSG_DEBUG, "EAP: Could not derive rMSK for ERP");
		eap_sm_free_key(sm);
		return;
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP: ERP rMSK",
			sm->eapKeyData, sm->eapKeyDataLen);
	sm->eapKeyAvailable = TRUE;
	eapol_set_bool(sm, EAPOL_eapSuccess, TRUE);
	eapol_set_bool(sm, EAPOL_eapReq, FALSE);
	eapol_set_bool(sm, EAPOL_eapNoResp, TRUE);
	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_SUCCESS
		"EAP re-authentication completed successfully");
#endif /* CONFIG_ERP */
}


static void eap_sm_parseEapReq(struct eap_sm *sm, const struct wpabuf *req)
{
	const struct eap_hdr *hdr;
	size_t plen;
	const u8 *pos;

	sm->rxReq = sm->rxResp = sm->rxSuccess = sm->rxFailure = FALSE;
	sm->reqId = 0;
	sm->reqMethod = EAP_TYPE_NONE;
	sm->reqVendor = EAP_VENDOR_IETF;
	sm->reqVendorMethod = EAP_TYPE_NONE;

	if (req == NULL || wpabuf_len(req) < sizeof(*hdr))
		return;

	hdr = wpabuf_head(req);
	plen = be_to_host16(hdr->length);
	if (plen > wpabuf_len(req)) {
		wpa_printf(MSG_DEBUG, "EAP: Ignored truncated EAP-Packet "
			   "(len=%lu plen=%lu)",
			   (unsigned long) wpabuf_len(req),
			   (unsigned long) plen);
		return;
	}

	sm->reqId = hdr->identifier;

	if (sm->workaround) {
		const u8 *addr[1];
		addr[0] = wpabuf_head(req);
		sha1_vector(1, addr, &plen, sm->req_sha1);
	}

	switch (hdr->code) {
	case EAP_CODE_REQUEST:
		if (plen < sizeof(*hdr) + 1) {
			wpa_printf(MSG_DEBUG, "EAP: Too short EAP-Request - "
				   "no Type field");
			return;
		}
		sm->rxReq = TRUE;
		pos = (const u8 *) (hdr + 1);
		sm->reqMethod = *pos++;
		if (sm->reqMethod == EAP_TYPE_EXPANDED) {
			if (plen < sizeof(*hdr) + 8) {
				wpa_printf(MSG_DEBUG, "EAP: Ignored truncated "
					   "expanded EAP-Packet (plen=%lu)",
					   (unsigned long) plen);
				return;
			}
			sm->reqVendor = WPA_GET_BE24(pos);
			pos += 3;
			sm->reqVendorMethod = WPA_GET_BE32(pos);
		}
		wpa_printf(MSG_DEBUG, "EAP: Received EAP-Request id=%d "
			   "method=%u vendor=%u vendorMethod=%u",
			   sm->reqId, sm->reqMethod, sm->reqVendor,
			   sm->reqVendorMethod);
		break;
	case EAP_CODE_RESPONSE:
		if (sm->selectedMethod == EAP_TYPE_LEAP) {
			/*
			 * LEAP differs from RFC 4137 by using reversed roles
			 * for mutual authentication and because of this, we
			 * need to accept EAP-Response frames if LEAP is used.
			 */
			if (plen < sizeof(*hdr) + 1) {
				wpa_printf(MSG_DEBUG, "EAP: Too short "
					   "EAP-Response - no Type field");
				return;
			}
			sm->rxResp = TRUE;
			pos = (const u8 *) (hdr + 1);
			sm->reqMethod = *pos;
			wpa_printf(MSG_DEBUG, "EAP: Received EAP-Response for "
				   "LEAP method=%d id=%d",
				   sm->reqMethod, sm->reqId);
			break;
		}
		wpa_printf(MSG_DEBUG, "EAP: Ignored EAP-Response");
		break;
	case EAP_CODE_SUCCESS:
		wpa_printf(MSG_DEBUG, "EAP: Received EAP-Success");
		eap_notify_status(sm, "completion", "success");
		sm->rxSuccess = TRUE;
		break;
	case EAP_CODE_FAILURE:
		wpa_printf(MSG_DEBUG, "EAP: Received EAP-Failure");
		eap_notify_status(sm, "completion", "failure");

		/* Get the error code from method */
		if (sm->m && sm->m->get_error_code) {
			int error_code;

			error_code = sm->m->get_error_code(sm->eap_method_priv);
			if (error_code != NO_EAP_METHOD_ERROR)
				eap_report_error(sm, error_code);
		}
		sm->rxFailure = TRUE;
		break;
	case EAP_CODE_INITIATE:
		eap_peer_initiate(sm, hdr, plen);
		break;
	case EAP_CODE_FINISH:
		eap_peer_finish(sm, hdr, plen);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP: Ignored EAP-Packet with unknown "
			   "code %d", hdr->code);
		break;
	}
}


static void eap_peer_sm_tls_event(void *ctx, enum tls_event ev,
				  union tls_event_data *data)
{
	struct eap_sm *sm = ctx;
	char *hash_hex = NULL;

	switch (ev) {
	case TLS_CERT_CHAIN_SUCCESS:
		eap_notify_status(sm, "remote certificate verification",
				  "success");
		if (sm->ext_cert_check) {
			sm->waiting_ext_cert_check = 1;
			eap_sm_request(sm, WPA_CTRL_REQ_EXT_CERT_CHECK,
				       NULL, 0);
		}
		break;
	case TLS_CERT_CHAIN_FAILURE:
		wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_TLS_CERT_ERROR
			"reason=%d depth=%d subject='%s' err='%s'",
			data->cert_fail.reason,
			data->cert_fail.depth,
			data->cert_fail.subject,
			data->cert_fail.reason_txt);
		eap_notify_status(sm, "remote certificate verification",
				  data->cert_fail.reason_txt);
		break;
	case TLS_PEER_CERTIFICATE:
		if (!sm->eapol_cb->notify_cert)
			break;

		if (data->peer_cert.hash) {
			size_t len = data->peer_cert.hash_len * 2 + 1;
			hash_hex = os_malloc(len);
			if (hash_hex) {
				wpa_snprintf_hex(hash_hex, len,
						 data->peer_cert.hash,
						 data->peer_cert.hash_len);
			}
		}

		sm->eapol_cb->notify_cert(sm->eapol_ctx,
					  data->peer_cert.depth,
					  data->peer_cert.subject,
					  data->peer_cert.altsubject,
					  data->peer_cert.num_altsubject,
					  hash_hex, data->peer_cert.cert);
		break;
	case TLS_ALERT:
		if (data->alert.is_local)
			eap_notify_status(sm, "local TLS alert",
					  data->alert.description);
		else
			eap_notify_status(sm, "remote TLS alert",
					  data->alert.description);
		break;
	}

	os_free(hash_hex);
}


/**
 * eap_peer_sm_init - Allocate and initialize EAP peer state machine
 * @eapol_ctx: Context data to be used with eapol_cb calls
 * @eapol_cb: Pointer to EAPOL callback functions
 * @msg_ctx: Context data for wpa_msg() calls
 * @conf: EAP configuration
 * Returns: Pointer to the allocated EAP state machine or %NULL on failure
 *
 * This function allocates and initializes an EAP state machine. In addition,
 * this initializes TLS library for the new EAP state machine. eapol_cb pointer
 * will be in use until eap_peer_sm_deinit() is used to deinitialize this EAP
 * state machine. Consequently, the caller must make sure that this data
 * structure remains alive while the EAP state machine is active.
 */
struct eap_sm * eap_peer_sm_init(void *eapol_ctx,
				 const struct eapol_callbacks *eapol_cb,
				 void *msg_ctx, struct eap_config *conf)
{
	struct eap_sm *sm;
	struct tls_config tlsconf;

	sm = os_zalloc(sizeof(*sm));
	if (sm == NULL)
		return NULL;
	sm->eapol_ctx = eapol_ctx;
	sm->eapol_cb = eapol_cb;
	sm->msg_ctx = msg_ctx;
	sm->ClientTimeout = EAP_CLIENT_TIMEOUT_DEFAULT;
	sm->wps = conf->wps;
	dl_list_init(&sm->erp_keys);

	os_memset(&tlsconf, 0, sizeof(tlsconf));
	tlsconf.opensc_engine_path = conf->opensc_engine_path;
	tlsconf.pkcs11_engine_path = conf->pkcs11_engine_path;
	tlsconf.pkcs11_module_path = conf->pkcs11_module_path;
	tlsconf.openssl_ciphers = conf->openssl_ciphers;
#ifdef CONFIG_FIPS
	tlsconf.fips_mode = 1;
#endif /* CONFIG_FIPS */
	tlsconf.event_cb = eap_peer_sm_tls_event;
	tlsconf.cb_ctx = sm;
	tlsconf.cert_in_cb = conf->cert_in_cb;
	sm->ssl_ctx = tls_init(&tlsconf);
	if (sm->ssl_ctx == NULL) {
		wpa_printf(MSG_WARNING, "SSL: Failed to initialize TLS "
			   "context.");
		os_free(sm);
		return NULL;
	}

	sm->ssl_ctx2 = tls_init(&tlsconf);
	if (sm->ssl_ctx2 == NULL) {
		wpa_printf(MSG_INFO, "SSL: Failed to initialize TLS "
			   "context (2).");
		/* Run without separate TLS context within TLS tunnel */
	}

	return sm;
}


/**
 * eap_peer_sm_deinit - Deinitialize and free an EAP peer state machine
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * This function deinitializes EAP state machine and frees all allocated
 * resources.
 */
void eap_peer_sm_deinit(struct eap_sm *sm)
{
	if (sm == NULL)
		return;
	eap_deinit_prev_method(sm, "EAP deinit");
	eap_sm_abort(sm);
	if (sm->ssl_ctx2)
		tls_deinit(sm->ssl_ctx2);
	tls_deinit(sm->ssl_ctx);
	eap_peer_erp_free_keys(sm);
	os_free(sm);
}


/**
 * eap_peer_sm_step - Step EAP peer state machine
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * Returns: 1 if EAP state was changed or 0 if not
 *
 * This function advances EAP state machine to a new state to match with the
 * current variables. This should be called whenever variables used by the EAP
 * state machine have changed.
 */
int eap_peer_sm_step(struct eap_sm *sm)
{
	int res = 0;
	do {
		sm->changed = FALSE;
		SM_STEP_RUN(EAP);
		if (sm->changed)
			res = 1;
	} while (sm->changed);
	return res;
}


/**
 * eap_sm_abort - Abort EAP authentication
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * Release system resources that have been allocated for the authentication
 * session without fully deinitializing the EAP state machine.
 */
void eap_sm_abort(struct eap_sm *sm)
{
	wpabuf_free(sm->lastRespData);
	sm->lastRespData = NULL;
	wpabuf_free(sm->eapRespData);
	sm->eapRespData = NULL;
	eap_sm_free_key(sm);
	os_free(sm->eapSessionId);
	sm->eapSessionId = NULL;

	/* This is not clearly specified in the EAP statemachines draft, but
	 * it seems necessary to make sure that some of the EAPOL variables get
	 * cleared for the next authentication. */
	eapol_set_bool(sm, EAPOL_eapSuccess, FALSE);
}


#ifdef CONFIG_CTRL_IFACE
static const char * eap_sm_state_txt(int state)
{
	switch (state) {
	case EAP_INITIALIZE:
		return "INITIALIZE";
	case EAP_DISABLED:
		return "DISABLED";
	case EAP_IDLE:
		return "IDLE";
	case EAP_RECEIVED:
		return "RECEIVED";
	case EAP_GET_METHOD:
		return "GET_METHOD";
	case EAP_METHOD:
		return "METHOD";
	case EAP_SEND_RESPONSE:
		return "SEND_RESPONSE";
	case EAP_DISCARD:
		return "DISCARD";
	case EAP_IDENTITY:
		return "IDENTITY";
	case EAP_NOTIFICATION:
		return "NOTIFICATION";
	case EAP_RETRANSMIT:
		return "RETRANSMIT";
	case EAP_SUCCESS:
		return "SUCCESS";
	case EAP_FAILURE:
		return "FAILURE";
	default:
		return "UNKNOWN";
	}
}
#endif /* CONFIG_CTRL_IFACE */


#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
static const char * eap_sm_method_state_txt(EapMethodState state)
{
	switch (state) {
	case METHOD_NONE:
		return "NONE";
	case METHOD_INIT:
		return "INIT";
	case METHOD_CONT:
		return "CONT";
	case METHOD_MAY_CONT:
		return "MAY_CONT";
	case METHOD_DONE:
		return "DONE";
	default:
		return "UNKNOWN";
	}
}


static const char * eap_sm_decision_txt(EapDecision decision)
{
	switch (decision) {
	case DECISION_FAIL:
		return "FAIL";
	case DECISION_COND_SUCC:
		return "COND_SUCC";
	case DECISION_UNCOND_SUCC:
		return "UNCOND_SUCC";
	default:
		return "UNKNOWN";
	}
}
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */


#ifdef CONFIG_CTRL_IFACE

/**
 * eap_sm_get_status - Get EAP state machine status
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @buf: Buffer for status information
 * @buflen: Maximum buffer length
 * @verbose: Whether to include verbose status information
 * Returns: Number of bytes written to buf.
 *
 * Query EAP state machine for status information. This function fills in a
 * text area with current status information from the EAPOL state machine. If
 * the buffer (buf) is not large enough, status information will be truncated
 * to fit the buffer.
 */
int eap_sm_get_status(struct eap_sm *sm, char *buf, size_t buflen, int verbose)
{
	int len, ret;

	if (sm == NULL)
		return 0;

	len = os_snprintf(buf, buflen,
			  "EAP state=%s\n",
			  eap_sm_state_txt(sm->EAP_state));
	if (os_snprintf_error(buflen, len))
		return 0;

	if (sm->selectedMethod != EAP_TYPE_NONE) {
		const char *name;
		if (sm->m) {
			name = sm->m->name;
		} else {
			const struct eap_method *m =
				eap_peer_get_eap_method(EAP_VENDOR_IETF,
							sm->selectedMethod);
			if (m)
				name = m->name;
			else
				name = "?";
		}
		ret = os_snprintf(buf + len, buflen - len,
				  "selectedMethod=%d (EAP-%s)\n",
				  sm->selectedMethod, name);
		if (os_snprintf_error(buflen - len, ret))
			return len;
		len += ret;

		if (sm->m && sm->m->get_status) {
			len += sm->m->get_status(sm, sm->eap_method_priv,
						 buf + len, buflen - len,
						 verbose);
		}
	}

	if (verbose) {
		ret = os_snprintf(buf + len, buflen - len,
				  "reqMethod=%d\n"
				  "methodState=%s\n"
				  "decision=%s\n"
				  "ClientTimeout=%d\n",
				  sm->reqMethod,
				  eap_sm_method_state_txt(sm->methodState),
				  eap_sm_decision_txt(sm->decision),
				  sm->ClientTimeout);
		if (os_snprintf_error(buflen - len, ret))
			return len;
		len += ret;
	}

	return len;
}
#endif /* CONFIG_CTRL_IFACE */


static void eap_sm_request(struct eap_sm *sm, enum wpa_ctrl_req_type field,
			   const char *msg, size_t msglen)
{
#if defined(CONFIG_CTRL_IFACE) || !defined(CONFIG_NO_STDOUT_DEBUG)
	struct eap_peer_config *config;
	const char *txt = NULL;
	char *tmp;

	if (sm == NULL)
		return;
	config = eap_get_config(sm);
	if (config == NULL)
		return;

	switch (field) {
	case WPA_CTRL_REQ_EAP_IDENTITY:
		config->pending_req_identity++;
		break;
	case WPA_CTRL_REQ_EAP_PASSWORD:
		config->pending_req_password++;
		break;
	case WPA_CTRL_REQ_EAP_NEW_PASSWORD:
		config->pending_req_new_password++;
		break;
	case WPA_CTRL_REQ_EAP_PIN:
		config->pending_req_pin++;
		break;
	case WPA_CTRL_REQ_EAP_OTP:
		if (msg) {
			tmp = os_malloc(msglen + 3);
			if (tmp == NULL)
				return;
			tmp[0] = '[';
			os_memcpy(tmp + 1, msg, msglen);
			tmp[msglen + 1] = ']';
			tmp[msglen + 2] = '\0';
			txt = tmp;
			os_free(config->pending_req_otp);
			config->pending_req_otp = tmp;
			config->pending_req_otp_len = msglen + 3;
		} else {
			if (config->pending_req_otp == NULL)
				return;
			txt = config->pending_req_otp;
		}
		break;
	case WPA_CTRL_REQ_EAP_PASSPHRASE:
		config->pending_req_passphrase++;
		break;
	case WPA_CTRL_REQ_SIM:
		config->pending_req_sim++;
		txt = msg;
		break;
	case WPA_CTRL_REQ_EXT_CERT_CHECK:
		break;
	default:
		return;
	}

	if (sm->eapol_cb->eap_param_needed)
		sm->eapol_cb->eap_param_needed(sm->eapol_ctx, field, txt);
#endif /* CONFIG_CTRL_IFACE || !CONFIG_NO_STDOUT_DEBUG */
}


const char * eap_sm_get_method_name(struct eap_sm *sm)
{
	if (sm->m == NULL)
		return "UNKNOWN";
	return sm->m->name;
}


/**
 * eap_sm_request_identity - Request identity from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * EAP methods can call this function to request identity information for the
 * current network. This is normally called when the identity is not included
 * in the network configuration. The request will be sent to monitor programs
 * through the control interface.
 */
void eap_sm_request_identity(struct eap_sm *sm)
{
	eap_sm_request(sm, WPA_CTRL_REQ_EAP_IDENTITY, NULL, 0);
}


/**
 * eap_sm_request_password - Request password from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * EAP methods can call this function to request password information for the
 * current network. This is normally called when the password is not included
 * in the network configuration. The request will be sent to monitor programs
 * through the control interface.
 */
void eap_sm_request_password(struct eap_sm *sm)
{
	eap_sm_request(sm, WPA_CTRL_REQ_EAP_PASSWORD, NULL, 0);
}


/**
 * eap_sm_request_new_password - Request new password from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * EAP methods can call this function to request new password information for
 * the current network. This is normally called when the EAP method indicates
 * that the current password has expired and password change is required. The
 * request will be sent to monitor programs through the control interface.
 */
void eap_sm_request_new_password(struct eap_sm *sm)
{
	eap_sm_request(sm, WPA_CTRL_REQ_EAP_NEW_PASSWORD, NULL, 0);
}


/**
 * eap_sm_request_pin - Request SIM or smart card PIN from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * EAP methods can call this function to request SIM or smart card PIN
 * information for the current network. This is normally called when the PIN is
 * not included in the network configuration. The request will be sent to
 * monitor programs through the control interface.
 */
void eap_sm_request_pin(struct eap_sm *sm)
{
	eap_sm_request(sm, WPA_CTRL_REQ_EAP_PIN, NULL, 0);
}


/**
 * eap_sm_request_otp - Request one time password from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @msg: Message to be displayed to the user when asking for OTP
 * @msg_len: Length of the user displayable message
 *
 * EAP methods can call this function to request open time password (OTP) for
 * the current network. The request will be sent to monitor programs through
 * the control interface.
 */
void eap_sm_request_otp(struct eap_sm *sm, const char *msg, size_t msg_len)
{
	eap_sm_request(sm, WPA_CTRL_REQ_EAP_OTP, msg, msg_len);
}


/**
 * eap_sm_request_passphrase - Request passphrase from user (ctrl_iface)
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * EAP methods can call this function to request passphrase for a private key
 * for the current network. This is normally called when the passphrase is not
 * included in the network configuration. The request will be sent to monitor
 * programs through the control interface.
 */
void eap_sm_request_passphrase(struct eap_sm *sm)
{
	eap_sm_request(sm, WPA_CTRL_REQ_EAP_PASSPHRASE, NULL, 0);
}


/**
 * eap_sm_request_sim - Request external SIM processing
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @req: EAP method specific request
 */
void eap_sm_request_sim(struct eap_sm *sm, const char *req)
{
	eap_sm_request(sm, WPA_CTRL_REQ_SIM, req, os_strlen(req));
}


/**
 * eap_sm_notify_ctrl_attached - Notification of attached monitor
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * Notify EAP state machines that a monitor was attached to the control
 * interface to trigger re-sending of pending requests for user input.
 */
void eap_sm_notify_ctrl_attached(struct eap_sm *sm)
{
	struct eap_peer_config *config = eap_get_config(sm);

	if (config == NULL)
		return;

	/* Re-send any pending requests for user data since a new control
	 * interface was added. This handles cases where the EAP authentication
	 * starts immediately after system startup when the user interface is
	 * not yet running. */
	if (config->pending_req_identity)
		eap_sm_request_identity(sm);
	if (config->pending_req_password)
		eap_sm_request_password(sm);
	if (config->pending_req_new_password)
		eap_sm_request_new_password(sm);
	if (config->pending_req_otp)
		eap_sm_request_otp(sm, NULL, 0);
	if (config->pending_req_pin)
		eap_sm_request_pin(sm);
	if (config->pending_req_passphrase)
		eap_sm_request_passphrase(sm);
}


static int eap_allowed_phase2_type(int vendor, int type)
{
	if (vendor != EAP_VENDOR_IETF)
		return 0;
	return type != EAP_TYPE_PEAP && type != EAP_TYPE_TTLS &&
		type != EAP_TYPE_FAST;
}


/**
 * eap_get_phase2_type - Get EAP type for the given EAP phase 2 method name
 * @name: EAP method name, e.g., MD5
 * @vendor: Buffer for returning EAP Vendor-Id
 * Returns: EAP method type or %EAP_TYPE_NONE if not found
 *
 * This function maps EAP type names into EAP type numbers that are allowed for
 * Phase 2, i.e., for tunneled authentication. Phase 2 is used, e.g., with
 * EAP-PEAP, EAP-TTLS, and EAP-FAST.
 */
u32 eap_get_phase2_type(const char *name, int *vendor)
{
	int v;
	u32 type = eap_peer_get_type(name, &v);
	if (eap_allowed_phase2_type(v, type)) {
		*vendor = v;
		return type;
	}
	*vendor = EAP_VENDOR_IETF;
	return EAP_TYPE_NONE;
}


/**
 * eap_get_phase2_types - Get list of allowed EAP phase 2 types
 * @config: Pointer to a network configuration
 * @count: Pointer to a variable to be filled with number of returned EAP types
 * Returns: Pointer to allocated type list or %NULL on failure
 *
 * This function generates an array of allowed EAP phase 2 (tunneled) types for
 * the given network configuration.
 */
struct eap_method_type * eap_get_phase2_types(struct eap_peer_config *config,
					      size_t *count)
{
	struct eap_method_type *buf;
	u32 method;
	int vendor;
	size_t mcount;
	const struct eap_method *methods, *m;

	methods = eap_peer_get_methods(&mcount);
	if (methods == NULL)
		return NULL;
	*count = 0;
	buf = os_malloc(mcount * sizeof(struct eap_method_type));
	if (buf == NULL)
		return NULL;

	for (m = methods; m; m = m->next) {
		vendor = m->vendor;
		method = m->method;
		if (eap_allowed_phase2_type(vendor, method)) {
			if (vendor == EAP_VENDOR_IETF &&
			    method == EAP_TYPE_TLS && config &&
			    config->private_key2 == NULL)
				continue;
			buf[*count].vendor = vendor;
			buf[*count].method = method;
			(*count)++;
		}
	}

	return buf;
}


/**
 * eap_set_fast_reauth - Update fast_reauth setting
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @enabled: 1 = Fast reauthentication is enabled, 0 = Disabled
 */
void eap_set_fast_reauth(struct eap_sm *sm, int enabled)
{
	sm->fast_reauth = enabled;
}


/**
 * eap_set_workaround - Update EAP workarounds setting
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @workaround: 1 = Enable EAP workarounds, 0 = Disable EAP workarounds
 */
void eap_set_workaround(struct eap_sm *sm, unsigned int workaround)
{
	sm->workaround = workaround;
}


/**
 * eap_get_config - Get current network configuration
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * Returns: Pointer to the current network configuration or %NULL if not found
 *
 * EAP peer methods should avoid using this function if they can use other
 * access functions, like eap_get_config_identity() and
 * eap_get_config_password(), that do not require direct access to
 * struct eap_peer_config.
 */
struct eap_peer_config * eap_get_config(struct eap_sm *sm)
{
	return sm->eapol_cb->get_config(sm->eapol_ctx);
}


/**
 * eap_get_config_identity - Get identity from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @len: Buffer for the length of the identity
 * Returns: Pointer to the identity or %NULL if not found
 */
const u8 * eap_get_config_identity(struct eap_sm *sm, size_t *len)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	*len = config->identity_len;
	return config->identity;
}


static int eap_get_ext_password(struct eap_sm *sm,
				struct eap_peer_config *config)
{
	char *name;

	if (config->password == NULL)
		return -1;

	name = os_zalloc(config->password_len + 1);
	if (name == NULL)
		return -1;
	os_memcpy(name, config->password, config->password_len);

	ext_password_free(sm->ext_pw_buf);
	sm->ext_pw_buf = ext_password_get(sm->ext_pw, name);
	os_free(name);

	return sm->ext_pw_buf == NULL ? -1 : 0;
}


/**
 * eap_get_config_password - Get password from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @len: Buffer for the length of the password
 * Returns: Pointer to the password or %NULL if not found
 */
const u8 * eap_get_config_password(struct eap_sm *sm, size_t *len)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;

	if (config->flags & EAP_CONFIG_FLAGS_EXT_PASSWORD) {
		if (eap_get_ext_password(sm, config) < 0)
			return NULL;
		*len = wpabuf_len(sm->ext_pw_buf);
		return wpabuf_head(sm->ext_pw_buf);
	}

	*len = config->password_len;
	return config->password;
}


/**
 * eap_get_config_password2 - Get password from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @len: Buffer for the length of the password
 * @hash: Buffer for returning whether the password is stored as a
 * NtPasswordHash instead of plaintext password; can be %NULL if this
 * information is not needed
 * Returns: Pointer to the password or %NULL if not found
 */
const u8 * eap_get_config_password2(struct eap_sm *sm, size_t *len, int *hash)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;

	if (config->flags & EAP_CONFIG_FLAGS_EXT_PASSWORD) {
		if (eap_get_ext_password(sm, config) < 0)
			return NULL;
		if (hash)
			*hash = 0;
		*len = wpabuf_len(sm->ext_pw_buf);
		return wpabuf_head(sm->ext_pw_buf);
	}

	*len = config->password_len;
	if (hash)
		*hash = !!(config->flags & EAP_CONFIG_FLAGS_PASSWORD_NTHASH);
	return config->password;
}


/**
 * eap_get_config_new_password - Get new password from network configuration
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @len: Buffer for the length of the new password
 * Returns: Pointer to the new password or %NULL if not found
 */
const u8 * eap_get_config_new_password(struct eap_sm *sm, size_t *len)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	*len = config->new_password_len;
	return config->new_password;
}


/**
 * eap_get_config_otp - Get one-time password from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @len: Buffer for the length of the one-time password
 * Returns: Pointer to the one-time password or %NULL if not found
 */
const u8 * eap_get_config_otp(struct eap_sm *sm, size_t *len)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	*len = config->otp_len;
	return config->otp;
}


/**
 * eap_clear_config_otp - Clear used one-time password
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * This function clears a used one-time password (OTP) from the current network
 * configuration. This should be called when the OTP has been used and is not
 * needed anymore.
 */
void eap_clear_config_otp(struct eap_sm *sm)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return;
	os_memset(config->otp, 0, config->otp_len);
	os_free(config->otp);
	config->otp = NULL;
	config->otp_len = 0;
}


/**
 * eap_get_config_phase1 - Get phase1 data from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * Returns: Pointer to the phase1 data or %NULL if not found
 */
const char * eap_get_config_phase1(struct eap_sm *sm)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	return config->phase1;
}


/**
 * eap_get_config_phase2 - Get phase2 data from the network configuration
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * Returns: Pointer to the phase1 data or %NULL if not found
 */
const char * eap_get_config_phase2(struct eap_sm *sm)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return NULL;
	return config->phase2;
}


int eap_get_config_fragment_size(struct eap_sm *sm)
{
	struct eap_peer_config *config = eap_get_config(sm);
	if (config == NULL)
		return -1;
	return config->fragment_size;
}


/**
 * eap_key_available - Get key availability (eapKeyAvailable variable)
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * Returns: 1 if EAP keying material is available, 0 if not
 */
int eap_key_available(struct eap_sm *sm)
{
	return sm ? sm->eapKeyAvailable : 0;
}


/**
 * eap_notify_success - Notify EAP state machine about external success trigger
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * This function is called when external event, e.g., successful completion of
 * WPA-PSK key handshake, is indicating that EAP state machine should move to
 * success state. This is mainly used with security modes that do not use EAP
 * state machine (e.g., WPA-PSK).
 */
void eap_notify_success(struct eap_sm *sm)
{
	if (sm) {
		sm->decision = DECISION_COND_SUCC;
		sm->EAP_state = EAP_SUCCESS;
	}
}


/**
 * eap_notify_lower_layer_success - Notification of lower layer success
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * Notify EAP state machines that a lower layer has detected a successful
 * authentication. This is used to recover from dropped EAP-Success messages.
 */
void eap_notify_lower_layer_success(struct eap_sm *sm)
{
	if (sm == NULL)
		return;

	if (eapol_get_bool(sm, EAPOL_eapSuccess) ||
	    sm->decision == DECISION_FAIL ||
	    (sm->methodState != METHOD_MAY_CONT &&
	     sm->methodState != METHOD_DONE))
		return;

	if (sm->eapKeyData != NULL)
		sm->eapKeyAvailable = TRUE;
	eapol_set_bool(sm, EAPOL_eapSuccess, TRUE);
	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_SUCCESS
		"EAP authentication completed successfully (based on lower "
		"layer success)");
}


/**
 * eap_get_eapSessionId - Get Session-Id from EAP state machine
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @len: Pointer to variable that will be set to number of bytes in the session
 * Returns: Pointer to the EAP Session-Id or %NULL on failure
 *
 * Fetch EAP Session-Id from the EAP state machine. The Session-Id is available
 * only after a successful authentication. EAP state machine continues to manage
 * the Session-Id and the caller must not change or free the returned data.
 */
const u8 * eap_get_eapSessionId(struct eap_sm *sm, size_t *len)
{
	if (sm == NULL || sm->eapSessionId == NULL) {
		*len = 0;
		return NULL;
	}

	*len = sm->eapSessionIdLen;
	return sm->eapSessionId;
}


/**
 * eap_get_eapKeyData - Get master session key (MSK) from EAP state machine
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @len: Pointer to variable that will be set to number of bytes in the key
 * Returns: Pointer to the EAP keying data or %NULL on failure
 *
 * Fetch EAP keying material (MSK, eapKeyData) from the EAP state machine. The
 * key is available only after a successful authentication. EAP state machine
 * continues to manage the key data and the caller must not change or free the
 * returned data.
 */
const u8 * eap_get_eapKeyData(struct eap_sm *sm, size_t *len)
{
	if (sm == NULL || sm->eapKeyData == NULL) {
		*len = 0;
		return NULL;
	}

	*len = sm->eapKeyDataLen;
	return sm->eapKeyData;
}


/**
 * eap_get_eapKeyData - Get EAP response data
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * Returns: Pointer to the EAP response (eapRespData) or %NULL on failure
 *
 * Fetch EAP response (eapRespData) from the EAP state machine. This data is
 * available when EAP state machine has processed an incoming EAP request. The
 * EAP state machine does not maintain a reference to the response after this
 * function is called and the caller is responsible for freeing the data.
 */
struct wpabuf * eap_get_eapRespData(struct eap_sm *sm)
{
	struct wpabuf *resp;

	if (sm == NULL || sm->eapRespData == NULL)
		return NULL;

	resp = sm->eapRespData;
	sm->eapRespData = NULL;

	return resp;
}


/**
 * eap_sm_register_scard_ctx - Notification of smart card context
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @ctx: Context data for smart card operations
 *
 * Notify EAP state machines of context data for smart card operations. This
 * context data will be used as a parameter for scard_*() functions.
 */
void eap_register_scard_ctx(struct eap_sm *sm, void *ctx)
{
	if (sm)
		sm->scard_ctx = ctx;
}


/**
 * eap_set_config_blob - Set or add a named configuration blob
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @blob: New value for the blob
 *
 * Adds a new configuration blob or replaces the current value of an existing
 * blob.
 */
void eap_set_config_blob(struct eap_sm *sm, struct wpa_config_blob *blob)
{
#ifndef CONFIG_NO_CONFIG_BLOBS
	sm->eapol_cb->set_config_blob(sm->eapol_ctx, blob);
#endif /* CONFIG_NO_CONFIG_BLOBS */
}


/**
 * eap_get_config_blob - Get a named configuration blob
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @name: Name of the blob
 * Returns: Pointer to blob data or %NULL if not found
 */
const struct wpa_config_blob * eap_get_config_blob(struct eap_sm *sm,
						   const char *name)
{
#ifndef CONFIG_NO_CONFIG_BLOBS
	return sm->eapol_cb->get_config_blob(sm->eapol_ctx, name);
#else /* CONFIG_NO_CONFIG_BLOBS */
	return NULL;
#endif /* CONFIG_NO_CONFIG_BLOBS */
}


/**
 * eap_set_force_disabled - Set force_disabled flag
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @disabled: 1 = EAP disabled, 0 = EAP enabled
 *
 * This function is used to force EAP state machine to be disabled when it is
 * not in use (e.g., with WPA-PSK or plaintext connections).
 */
void eap_set_force_disabled(struct eap_sm *sm, int disabled)
{
	sm->force_disabled = disabled;
}


/**
 * eap_set_external_sim - Set external_sim flag
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @external_sim: Whether external SIM/USIM processing is used
 */
void eap_set_external_sim(struct eap_sm *sm, int external_sim)
{
	sm->external_sim = external_sim;
}


 /**
 * eap_notify_pending - Notify that EAP method is ready to re-process a request
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 *
 * An EAP method can perform a pending operation (e.g., to get a response from
 * an external process). Once the response is available, this function can be
 * used to request EAPOL state machine to retry delivering the previously
 * received (and still unanswered) EAP request to EAP state machine.
 */
void eap_notify_pending(struct eap_sm *sm)
{
	sm->eapol_cb->notify_pending(sm->eapol_ctx);
}


/**
 * eap_invalidate_cached_session - Mark cached session data invalid
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 */
void eap_invalidate_cached_session(struct eap_sm *sm)
{
	if (sm)
		eap_deinit_prev_method(sm, "invalidate");
}


int eap_is_wps_pbc_enrollee(struct eap_peer_config *conf)
{
	if (conf->identity_len != WSC_ID_ENROLLEE_LEN ||
	    os_memcmp(conf->identity, WSC_ID_ENROLLEE, WSC_ID_ENROLLEE_LEN))
		return 0; /* Not a WPS Enrollee */

	if (conf->phase1 == NULL || os_strstr(conf->phase1, "pbc=1") == NULL)
		return 0; /* Not using PBC */

	return 1;
}


int eap_is_wps_pin_enrollee(struct eap_peer_config *conf)
{
	if (conf->identity_len != WSC_ID_ENROLLEE_LEN ||
	    os_memcmp(conf->identity, WSC_ID_ENROLLEE, WSC_ID_ENROLLEE_LEN))
		return 0; /* Not a WPS Enrollee */

	if (conf->phase1 == NULL || os_strstr(conf->phase1, "pin=") == NULL)
		return 0; /* Not using PIN */

	return 1;
}


void eap_sm_set_ext_pw_ctx(struct eap_sm *sm, struct ext_password_data *ext)
{
	ext_password_free(sm->ext_pw_buf);
	sm->ext_pw_buf = NULL;
	sm->ext_pw = ext;
}


/**
 * eap_set_anon_id - Set or add anonymous identity
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @id: Anonymous identity (e.g., EAP-SIM pseudonym) or %NULL to clear
 * @len: Length of anonymous identity in octets
 */
void eap_set_anon_id(struct eap_sm *sm, const u8 *id, size_t len)
{
	if (sm->eapol_cb->set_anon_id)
		sm->eapol_cb->set_anon_id(sm->eapol_ctx, id, len);
}


int eap_peer_was_failure_expected(struct eap_sm *sm)
{
	return sm->expected_failure;
}
