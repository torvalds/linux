/*
 * hostapd / EAP Full Authenticator state machine (RFC 4137)
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
 *
 * This state machine is based on the full authenticator state machine defined
 * in RFC 4137. However, to support backend authentication in RADIUS
 * authentication server functionality, parts of backend authenticator (also
 * from RFC 4137) are mixed in. This functionality is enabled by setting
 * backend_auth configuration variable to TRUE.
 */

#include "includes.h"

#include "common.h"
#include "eap_i.h"
#include "state_machine.h"
#include "common/wpa_ctrl.h"

#define STATE_MACHINE_DATA struct eap_sm
#define STATE_MACHINE_DEBUG_PREFIX "EAP"

#define EAP_MAX_AUTH_ROUNDS 50

static void eap_user_free(struct eap_user *user);


/* EAP state machines are described in RFC 4137 */

static int eap_sm_calculateTimeout(struct eap_sm *sm, int retransCount,
				   int eapSRTT, int eapRTTVAR,
				   int methodTimeout);
static void eap_sm_parseEapResp(struct eap_sm *sm, const struct wpabuf *resp);
static int eap_sm_getId(const struct wpabuf *data);
static struct wpabuf * eap_sm_buildSuccess(struct eap_sm *sm, u8 id);
static struct wpabuf * eap_sm_buildFailure(struct eap_sm *sm, u8 id);
static int eap_sm_nextId(struct eap_sm *sm, int id);
static void eap_sm_Policy_update(struct eap_sm *sm, const u8 *nak_list,
				 size_t len);
static EapType eap_sm_Policy_getNextMethod(struct eap_sm *sm, int *vendor);
static int eap_sm_Policy_getDecision(struct eap_sm *sm);
static Boolean eap_sm_Policy_doPickUp(struct eap_sm *sm, EapType method);


static int eap_copy_buf(struct wpabuf **dst, const struct wpabuf *src)
{
	if (src == NULL)
		return -1;

	wpabuf_free(*dst);
	*dst = wpabuf_dup(src);
	return *dst ? 0 : -1;
}


static int eap_copy_data(u8 **dst, size_t *dst_len,
			 const u8 *src, size_t src_len)
{
	if (src == NULL)
		return -1;

	os_free(*dst);
	*dst = os_malloc(src_len);
	if (*dst) {
		os_memcpy(*dst, src, src_len);
		*dst_len = src_len;
		return 0;
	} else {
		*dst_len = 0;
		return -1;
	}
}

#define EAP_COPY(dst, src) \
	eap_copy_data((dst), (dst ## Len), (src), (src ## Len))


/**
 * eap_user_get - Fetch user information from the database
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 * @identity: Identity (User-Name) of the user
 * @identity_len: Length of identity in bytes
 * @phase2: 0 = EAP phase1 user, 1 = EAP phase2 (tunneled) user
 * Returns: 0 on success, or -1 on failure
 *
 * This function is used to fetch user information for EAP. The user will be
 * selected based on the specified identity. sm->user and
 * sm->user_eap_method_index are updated for the new user when a matching user
 * is found. sm->user can be used to get user information (e.g., password).
 */
int eap_user_get(struct eap_sm *sm, const u8 *identity, size_t identity_len,
		 int phase2)
{
	struct eap_user *user;

	if (sm == NULL || sm->eapol_cb == NULL ||
	    sm->eapol_cb->get_eap_user == NULL)
		return -1;

	eap_user_free(sm->user);
	sm->user = NULL;

	user = os_zalloc(sizeof(*user));
	if (user == NULL)
	    return -1;

	if (sm->eapol_cb->get_eap_user(sm->eapol_ctx, identity,
				       identity_len, phase2, user) != 0) {
		eap_user_free(user);
		return -1;
	}

	sm->user = user;
	sm->user_eap_method_index = 0;

	return 0;
}


SM_STATE(EAP, DISABLED)
{
	SM_ENTRY(EAP, DISABLED);
	sm->num_rounds = 0;
}


SM_STATE(EAP, INITIALIZE)
{
	SM_ENTRY(EAP, INITIALIZE);

	sm->currentId = -1;
	sm->eap_if.eapSuccess = FALSE;
	sm->eap_if.eapFail = FALSE;
	sm->eap_if.eapTimeout = FALSE;
	os_free(sm->eap_if.eapKeyData);
	sm->eap_if.eapKeyData = NULL;
	sm->eap_if.eapKeyDataLen = 0;
	sm->eap_if.eapKeyAvailable = FALSE;
	sm->eap_if.eapRestart = FALSE;

	/*
	 * This is not defined in RFC 4137, but method state needs to be
	 * reseted here so that it does not remain in success state when
	 * re-authentication starts.
	 */
	if (sm->m && sm->eap_method_priv) {
		sm->m->reset(sm, sm->eap_method_priv);
		sm->eap_method_priv = NULL;
	}
	sm->m = NULL;
	sm->user_eap_method_index = 0;

	if (sm->backend_auth) {
		sm->currentMethod = EAP_TYPE_NONE;
		/* parse rxResp, respId, respMethod */
		eap_sm_parseEapResp(sm, sm->eap_if.eapRespData);
		if (sm->rxResp) {
			sm->currentId = sm->respId;
		}
	}
	sm->num_rounds = 0;
	sm->method_pending = METHOD_PENDING_NONE;

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_STARTED
		MACSTR, MAC2STR(sm->peer_addr));
}


SM_STATE(EAP, PICK_UP_METHOD)
{
	SM_ENTRY(EAP, PICK_UP_METHOD);

	if (eap_sm_Policy_doPickUp(sm, sm->respMethod)) {
		sm->currentMethod = sm->respMethod;
		if (sm->m && sm->eap_method_priv) {
			sm->m->reset(sm, sm->eap_method_priv);
			sm->eap_method_priv = NULL;
		}
		sm->m = eap_server_get_eap_method(EAP_VENDOR_IETF,
						  sm->currentMethod);
		if (sm->m && sm->m->initPickUp) {
			sm->eap_method_priv = sm->m->initPickUp(sm);
			if (sm->eap_method_priv == NULL) {
				wpa_printf(MSG_DEBUG, "EAP: Failed to "
					   "initialize EAP method %d",
					   sm->currentMethod);
				sm->m = NULL;
				sm->currentMethod = EAP_TYPE_NONE;
			}
		} else {
			sm->m = NULL;
			sm->currentMethod = EAP_TYPE_NONE;
		}
	}

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_PROPOSED_METHOD
		"method=%u", sm->currentMethod);
}


SM_STATE(EAP, IDLE)
{
	SM_ENTRY(EAP, IDLE);

	sm->eap_if.retransWhile = eap_sm_calculateTimeout(
		sm, sm->retransCount, sm->eap_if.eapSRTT, sm->eap_if.eapRTTVAR,
		sm->methodTimeout);
}


SM_STATE(EAP, RETRANSMIT)
{
	SM_ENTRY(EAP, RETRANSMIT);

	sm->retransCount++;
	if (sm->retransCount <= sm->MaxRetrans && sm->lastReqData) {
		if (eap_copy_buf(&sm->eap_if.eapReqData, sm->lastReqData) == 0)
			sm->eap_if.eapReq = TRUE;
	}
}


SM_STATE(EAP, RECEIVED)
{
	SM_ENTRY(EAP, RECEIVED);

	/* parse rxResp, respId, respMethod */
	eap_sm_parseEapResp(sm, sm->eap_if.eapRespData);
	sm->num_rounds++;
}


SM_STATE(EAP, DISCARD)
{
	SM_ENTRY(EAP, DISCARD);
	sm->eap_if.eapResp = FALSE;
	sm->eap_if.eapNoReq = TRUE;
}


SM_STATE(EAP, SEND_REQUEST)
{
	SM_ENTRY(EAP, SEND_REQUEST);

	sm->retransCount = 0;
	if (sm->eap_if.eapReqData) {
		if (eap_copy_buf(&sm->lastReqData, sm->eap_if.eapReqData) == 0)
		{
			sm->eap_if.eapResp = FALSE;
			sm->eap_if.eapReq = TRUE;
		} else {
			sm->eap_if.eapResp = FALSE;
			sm->eap_if.eapReq = FALSE;
		}
	} else {
		wpa_printf(MSG_INFO, "EAP: SEND_REQUEST - no eapReqData");
		sm->eap_if.eapResp = FALSE;
		sm->eap_if.eapReq = FALSE;
		sm->eap_if.eapNoReq = TRUE;
	}
}


SM_STATE(EAP, INTEGRITY_CHECK)
{
	SM_ENTRY(EAP, INTEGRITY_CHECK);

	if (sm->m->check) {
		sm->ignore = sm->m->check(sm, sm->eap_method_priv,
					  sm->eap_if.eapRespData);
	}
}


SM_STATE(EAP, METHOD_REQUEST)
{
	SM_ENTRY(EAP, METHOD_REQUEST);

	if (sm->m == NULL) {
		wpa_printf(MSG_DEBUG, "EAP: method not initialized");
		return;
	}

	sm->currentId = eap_sm_nextId(sm, sm->currentId);
	wpa_printf(MSG_DEBUG, "EAP: building EAP-Request: Identifier %d",
		   sm->currentId);
	sm->lastId = sm->currentId;
	wpabuf_free(sm->eap_if.eapReqData);
	sm->eap_if.eapReqData = sm->m->buildReq(sm, sm->eap_method_priv,
						sm->currentId);
	if (sm->m->getTimeout)
		sm->methodTimeout = sm->m->getTimeout(sm, sm->eap_method_priv);
	else
		sm->methodTimeout = 0;
}


SM_STATE(EAP, METHOD_RESPONSE)
{
	SM_ENTRY(EAP, METHOD_RESPONSE);

	sm->m->process(sm, sm->eap_method_priv, sm->eap_if.eapRespData);
	if (sm->m->isDone(sm, sm->eap_method_priv)) {
		eap_sm_Policy_update(sm, NULL, 0);
		os_free(sm->eap_if.eapKeyData);
		if (sm->m->getKey) {
			sm->eap_if.eapKeyData = sm->m->getKey(
				sm, sm->eap_method_priv,
				&sm->eap_if.eapKeyDataLen);
		} else {
			sm->eap_if.eapKeyData = NULL;
			sm->eap_if.eapKeyDataLen = 0;
		}
		sm->methodState = METHOD_END;
	} else {
		sm->methodState = METHOD_CONTINUE;
	}
}


SM_STATE(EAP, PROPOSE_METHOD)
{
	int vendor;
	EapType type;

	SM_ENTRY(EAP, PROPOSE_METHOD);

	type = eap_sm_Policy_getNextMethod(sm, &vendor);
	if (vendor == EAP_VENDOR_IETF)
		sm->currentMethod = type;
	else
		sm->currentMethod = EAP_TYPE_EXPANDED;
	if (sm->m && sm->eap_method_priv) {
		sm->m->reset(sm, sm->eap_method_priv);
		sm->eap_method_priv = NULL;
	}
	sm->m = eap_server_get_eap_method(vendor, type);
	if (sm->m) {
		sm->eap_method_priv = sm->m->init(sm);
		if (sm->eap_method_priv == NULL) {
			wpa_printf(MSG_DEBUG, "EAP: Failed to initialize EAP "
				   "method %d", sm->currentMethod);
			sm->m = NULL;
			sm->currentMethod = EAP_TYPE_NONE;
		}
	}
	if (sm->currentMethod == EAP_TYPE_IDENTITY ||
	    sm->currentMethod == EAP_TYPE_NOTIFICATION)
		sm->methodState = METHOD_CONTINUE;
	else
		sm->methodState = METHOD_PROPOSED;

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_PROPOSED_METHOD
		"vendor=%u method=%u", vendor, sm->currentMethod);
}


SM_STATE(EAP, NAK)
{
	const struct eap_hdr *nak;
	size_t len = 0;
	const u8 *pos;
	const u8 *nak_list = NULL;

	SM_ENTRY(EAP, NAK);

	if (sm->eap_method_priv) {
		sm->m->reset(sm, sm->eap_method_priv);
		sm->eap_method_priv = NULL;
	}
	sm->m = NULL;

	nak = wpabuf_head(sm->eap_if.eapRespData);
	if (nak && wpabuf_len(sm->eap_if.eapRespData) > sizeof(*nak)) {
		len = be_to_host16(nak->length);
		if (len > wpabuf_len(sm->eap_if.eapRespData))
			len = wpabuf_len(sm->eap_if.eapRespData);
		pos = (const u8 *) (nak + 1);
		len -= sizeof(*nak);
		if (*pos == EAP_TYPE_NAK) {
			pos++;
			len--;
			nak_list = pos;
		}
	}
	eap_sm_Policy_update(sm, nak_list, len);
}


SM_STATE(EAP, SELECT_ACTION)
{
	SM_ENTRY(EAP, SELECT_ACTION);

	sm->decision = eap_sm_Policy_getDecision(sm);
}


SM_STATE(EAP, TIMEOUT_FAILURE)
{
	SM_ENTRY(EAP, TIMEOUT_FAILURE);

	sm->eap_if.eapTimeout = TRUE;
}


SM_STATE(EAP, FAILURE)
{
	SM_ENTRY(EAP, FAILURE);

	wpabuf_free(sm->eap_if.eapReqData);
	sm->eap_if.eapReqData = eap_sm_buildFailure(sm, sm->currentId);
	wpabuf_free(sm->lastReqData);
	sm->lastReqData = NULL;
	sm->eap_if.eapFail = TRUE;

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_FAILURE
		MACSTR, MAC2STR(sm->peer_addr));
}


SM_STATE(EAP, SUCCESS)
{
	SM_ENTRY(EAP, SUCCESS);

	wpabuf_free(sm->eap_if.eapReqData);
	sm->eap_if.eapReqData = eap_sm_buildSuccess(sm, sm->currentId);
	wpabuf_free(sm->lastReqData);
	sm->lastReqData = NULL;
	if (sm->eap_if.eapKeyData)
		sm->eap_if.eapKeyAvailable = TRUE;
	sm->eap_if.eapSuccess = TRUE;

	wpa_msg(sm->msg_ctx, MSG_INFO, WPA_EVENT_EAP_SUCCESS
		MACSTR, MAC2STR(sm->peer_addr));
}


SM_STATE(EAP, INITIALIZE_PASSTHROUGH)
{
	SM_ENTRY(EAP, INITIALIZE_PASSTHROUGH);

	wpabuf_free(sm->eap_if.aaaEapRespData);
	sm->eap_if.aaaEapRespData = NULL;
}


SM_STATE(EAP, IDLE2)
{
	SM_ENTRY(EAP, IDLE2);

	sm->eap_if.retransWhile = eap_sm_calculateTimeout(
		sm, sm->retransCount, sm->eap_if.eapSRTT, sm->eap_if.eapRTTVAR,
		sm->methodTimeout);
}


SM_STATE(EAP, RETRANSMIT2)
{
	SM_ENTRY(EAP, RETRANSMIT2);

	sm->retransCount++;
	if (sm->retransCount <= sm->MaxRetrans && sm->lastReqData) {
		if (eap_copy_buf(&sm->eap_if.eapReqData, sm->lastReqData) == 0)
			sm->eap_if.eapReq = TRUE;
	}
}


SM_STATE(EAP, RECEIVED2)
{
	SM_ENTRY(EAP, RECEIVED2);

	/* parse rxResp, respId, respMethod */
	eap_sm_parseEapResp(sm, sm->eap_if.eapRespData);
}


SM_STATE(EAP, DISCARD2)
{
	SM_ENTRY(EAP, DISCARD2);
	sm->eap_if.eapResp = FALSE;
	sm->eap_if.eapNoReq = TRUE;
}


SM_STATE(EAP, SEND_REQUEST2)
{
	SM_ENTRY(EAP, SEND_REQUEST2);

	sm->retransCount = 0;
	if (sm->eap_if.eapReqData) {
		if (eap_copy_buf(&sm->lastReqData, sm->eap_if.eapReqData) == 0)
		{
			sm->eap_if.eapResp = FALSE;
			sm->eap_if.eapReq = TRUE;
		} else {
			sm->eap_if.eapResp = FALSE;
			sm->eap_if.eapReq = FALSE;
		}
	} else {
		wpa_printf(MSG_INFO, "EAP: SEND_REQUEST2 - no eapReqData");
		sm->eap_if.eapResp = FALSE;
		sm->eap_if.eapReq = FALSE;
		sm->eap_if.eapNoReq = TRUE;
	}
}


SM_STATE(EAP, AAA_REQUEST)
{
	SM_ENTRY(EAP, AAA_REQUEST);

	if (sm->eap_if.eapRespData == NULL) {
		wpa_printf(MSG_INFO, "EAP: AAA_REQUEST - no eapRespData");
		return;
	}

	/*
	 * if (respMethod == IDENTITY)
	 *	aaaIdentity = eapRespData
	 * This is already taken care of by the EAP-Identity method which
	 * stores the identity into sm->identity.
	 */

	eap_copy_buf(&sm->eap_if.aaaEapRespData, sm->eap_if.eapRespData);
}


SM_STATE(EAP, AAA_RESPONSE)
{
	SM_ENTRY(EAP, AAA_RESPONSE);

	eap_copy_buf(&sm->eap_if.eapReqData, sm->eap_if.aaaEapReqData);
	sm->currentId = eap_sm_getId(sm->eap_if.eapReqData);
	sm->methodTimeout = sm->eap_if.aaaMethodTimeout;
}


SM_STATE(EAP, AAA_IDLE)
{
	SM_ENTRY(EAP, AAA_IDLE);

	sm->eap_if.aaaFail = FALSE;
	sm->eap_if.aaaSuccess = FALSE;
	sm->eap_if.aaaEapReq = FALSE;
	sm->eap_if.aaaEapNoReq = FALSE;
	sm->eap_if.aaaEapResp = TRUE;
}


SM_STATE(EAP, TIMEOUT_FAILURE2)
{
	SM_ENTRY(EAP, TIMEOUT_FAILURE2);

	sm->eap_if.eapTimeout = TRUE;
}


SM_STATE(EAP, FAILURE2)
{
	SM_ENTRY(EAP, FAILURE2);

	eap_copy_buf(&sm->eap_if.eapReqData, sm->eap_if.aaaEapReqData);
	sm->eap_if.eapFail = TRUE;
}


SM_STATE(EAP, SUCCESS2)
{
	SM_ENTRY(EAP, SUCCESS2);

	eap_copy_buf(&sm->eap_if.eapReqData, sm->eap_if.aaaEapReqData);

	sm->eap_if.eapKeyAvailable = sm->eap_if.aaaEapKeyAvailable;
	if (sm->eap_if.aaaEapKeyAvailable) {
		EAP_COPY(&sm->eap_if.eapKeyData, sm->eap_if.aaaEapKeyData);
	} else {
		os_free(sm->eap_if.eapKeyData);
		sm->eap_if.eapKeyData = NULL;
		sm->eap_if.eapKeyDataLen = 0;
	}

	sm->eap_if.eapSuccess = TRUE;

	/*
	 * Start reauthentication with identity request even though we know the
	 * previously used identity. This is needed to get reauthentication
	 * started properly.
	 */
	sm->start_reauth = TRUE;
}


SM_STEP(EAP)
{
	if (sm->eap_if.eapRestart && sm->eap_if.portEnabled)
		SM_ENTER_GLOBAL(EAP, INITIALIZE);
	else if (!sm->eap_if.portEnabled)
		SM_ENTER_GLOBAL(EAP, DISABLED);
	else if (sm->num_rounds > EAP_MAX_AUTH_ROUNDS) {
		if (sm->num_rounds == EAP_MAX_AUTH_ROUNDS + 1) {
			wpa_printf(MSG_DEBUG, "EAP: more than %d "
				   "authentication rounds - abort",
				   EAP_MAX_AUTH_ROUNDS);
			sm->num_rounds++;
			SM_ENTER_GLOBAL(EAP, FAILURE);
		}
	} else switch (sm->EAP_state) {
	case EAP_INITIALIZE:
		if (sm->backend_auth) {
			if (!sm->rxResp)
				SM_ENTER(EAP, SELECT_ACTION);
			else if (sm->rxResp &&
				 (sm->respMethod == EAP_TYPE_NAK ||
				  (sm->respMethod == EAP_TYPE_EXPANDED &&
				   sm->respVendor == EAP_VENDOR_IETF &&
				   sm->respVendorMethod == EAP_TYPE_NAK)))
				SM_ENTER(EAP, NAK);
			else
				SM_ENTER(EAP, PICK_UP_METHOD);
		} else {
			SM_ENTER(EAP, SELECT_ACTION);
		}
		break;
	case EAP_PICK_UP_METHOD:
		if (sm->currentMethod == EAP_TYPE_NONE) {
			SM_ENTER(EAP, SELECT_ACTION);
		} else {
			SM_ENTER(EAP, METHOD_RESPONSE);
		}
		break;
	case EAP_DISABLED:
		if (sm->eap_if.portEnabled)
			SM_ENTER(EAP, INITIALIZE);
		break;
	case EAP_IDLE:
		if (sm->eap_if.retransWhile == 0)
			SM_ENTER(EAP, RETRANSMIT);
		else if (sm->eap_if.eapResp)
			SM_ENTER(EAP, RECEIVED);
		break;
	case EAP_RETRANSMIT:
		if (sm->retransCount > sm->MaxRetrans)
			SM_ENTER(EAP, TIMEOUT_FAILURE);
		else
			SM_ENTER(EAP, IDLE);
		break;
	case EAP_RECEIVED:
		if (sm->rxResp && (sm->respId == sm->currentId) &&
		    (sm->respMethod == EAP_TYPE_NAK ||
		     (sm->respMethod == EAP_TYPE_EXPANDED &&
		      sm->respVendor == EAP_VENDOR_IETF &&
		      sm->respVendorMethod == EAP_TYPE_NAK))
		    && (sm->methodState == METHOD_PROPOSED))
			SM_ENTER(EAP, NAK);
		else if (sm->rxResp && (sm->respId == sm->currentId) &&
			 ((sm->respMethod == sm->currentMethod) ||
			  (sm->respMethod == EAP_TYPE_EXPANDED &&
			   sm->respVendor == EAP_VENDOR_IETF &&
			   sm->respVendorMethod == sm->currentMethod)))
			SM_ENTER(EAP, INTEGRITY_CHECK);
		else {
			wpa_printf(MSG_DEBUG, "EAP: RECEIVED->DISCARD: "
				   "rxResp=%d respId=%d currentId=%d "
				   "respMethod=%d currentMethod=%d",
				   sm->rxResp, sm->respId, sm->currentId,
				   sm->respMethod, sm->currentMethod);
			SM_ENTER(EAP, DISCARD);
		}
		break;
	case EAP_DISCARD:
		SM_ENTER(EAP, IDLE);
		break;
	case EAP_SEND_REQUEST:
		SM_ENTER(EAP, IDLE);
		break;
	case EAP_INTEGRITY_CHECK:
		if (sm->ignore)
			SM_ENTER(EAP, DISCARD);
		else
			SM_ENTER(EAP, METHOD_RESPONSE);
		break;
	case EAP_METHOD_REQUEST:
		SM_ENTER(EAP, SEND_REQUEST);
		break;
	case EAP_METHOD_RESPONSE:
		/*
		 * Note: Mechanism to allow EAP methods to wait while going
		 * through pending processing is an extension to RFC 4137
		 * which only defines the transits to SELECT_ACTION and
		 * METHOD_REQUEST from this METHOD_RESPONSE state.
		 */
		if (sm->methodState == METHOD_END)
			SM_ENTER(EAP, SELECT_ACTION);
		else if (sm->method_pending == METHOD_PENDING_WAIT) {
			wpa_printf(MSG_DEBUG, "EAP: Method has pending "
				   "processing - wait before proceeding to "
				   "METHOD_REQUEST state");
		} else if (sm->method_pending == METHOD_PENDING_CONT) {
			wpa_printf(MSG_DEBUG, "EAP: Method has completed "
				   "pending processing - reprocess pending "
				   "EAP message");
			sm->method_pending = METHOD_PENDING_NONE;
			SM_ENTER(EAP, METHOD_RESPONSE);
		} else
			SM_ENTER(EAP, METHOD_REQUEST);
		break;
	case EAP_PROPOSE_METHOD:
		/*
		 * Note: Mechanism to allow EAP methods to wait while going
		 * through pending processing is an extension to RFC 4137
		 * which only defines the transit to METHOD_REQUEST from this
		 * PROPOSE_METHOD state.
		 */
		if (sm->method_pending == METHOD_PENDING_WAIT) {
			wpa_printf(MSG_DEBUG, "EAP: Method has pending "
				   "processing - wait before proceeding to "
				   "METHOD_REQUEST state");
			if (sm->user_eap_method_index > 0)
				sm->user_eap_method_index--;
		} else if (sm->method_pending == METHOD_PENDING_CONT) {
			wpa_printf(MSG_DEBUG, "EAP: Method has completed "
				   "pending processing - reprocess pending "
				   "EAP message");
			sm->method_pending = METHOD_PENDING_NONE;
			SM_ENTER(EAP, PROPOSE_METHOD);
		} else
			SM_ENTER(EAP, METHOD_REQUEST);
		break;
	case EAP_NAK:
		SM_ENTER(EAP, SELECT_ACTION);
		break;
	case EAP_SELECT_ACTION:
		if (sm->decision == DECISION_FAILURE)
			SM_ENTER(EAP, FAILURE);
		else if (sm->decision == DECISION_SUCCESS)
			SM_ENTER(EAP, SUCCESS);
		else if (sm->decision == DECISION_PASSTHROUGH)
			SM_ENTER(EAP, INITIALIZE_PASSTHROUGH);
		else
			SM_ENTER(EAP, PROPOSE_METHOD);
		break;
	case EAP_TIMEOUT_FAILURE:
		break;
	case EAP_FAILURE:
		break;
	case EAP_SUCCESS:
		break;

	case EAP_INITIALIZE_PASSTHROUGH:
		if (sm->currentId == -1)
			SM_ENTER(EAP, AAA_IDLE);
		else
			SM_ENTER(EAP, AAA_REQUEST);
		break;
	case EAP_IDLE2:
		if (sm->eap_if.eapResp)
			SM_ENTER(EAP, RECEIVED2);
		else if (sm->eap_if.retransWhile == 0)
			SM_ENTER(EAP, RETRANSMIT2);
		break;
	case EAP_RETRANSMIT2:
		if (sm->retransCount > sm->MaxRetrans)
			SM_ENTER(EAP, TIMEOUT_FAILURE2);
		else
			SM_ENTER(EAP, IDLE2);
		break;
	case EAP_RECEIVED2:
		if (sm->rxResp && (sm->respId == sm->currentId))
			SM_ENTER(EAP, AAA_REQUEST);
		else
			SM_ENTER(EAP, DISCARD2);
		break;
	case EAP_DISCARD2:
		SM_ENTER(EAP, IDLE2);
		break;
	case EAP_SEND_REQUEST2:
		SM_ENTER(EAP, IDLE2);
		break;
	case EAP_AAA_REQUEST:
		SM_ENTER(EAP, AAA_IDLE);
		break;
	case EAP_AAA_RESPONSE:
		SM_ENTER(EAP, SEND_REQUEST2);
		break;
	case EAP_AAA_IDLE:
		if (sm->eap_if.aaaFail)
			SM_ENTER(EAP, FAILURE2);
		else if (sm->eap_if.aaaSuccess)
			SM_ENTER(EAP, SUCCESS2);
		else if (sm->eap_if.aaaEapReq)
			SM_ENTER(EAP, AAA_RESPONSE);
		else if (sm->eap_if.aaaTimeout)
			SM_ENTER(EAP, TIMEOUT_FAILURE2);
		break;
	case EAP_TIMEOUT_FAILURE2:
		break;
	case EAP_FAILURE2:
		break;
	case EAP_SUCCESS2:
		break;
	}
}


static int eap_sm_calculateTimeout(struct eap_sm *sm, int retransCount,
				   int eapSRTT, int eapRTTVAR,
				   int methodTimeout)
{
	int rto, i;

	if (methodTimeout) {
		/*
		 * EAP method (either internal or through AAA server, provided
		 * timeout hint. Use that as-is as a timeout for retransmitting
		 * the EAP request if no response is received.
		 */
		wpa_printf(MSG_DEBUG, "EAP: retransmit timeout %d seconds "
			   "(from EAP method hint)", methodTimeout);
		return methodTimeout;
	}

	/*
	 * RFC 3748 recommends algorithms described in RFC 2988 for estimation
	 * of the retransmission timeout. This should be implemented once
	 * round-trip time measurements are available. For nowm a simple
	 * backoff mechanism is used instead if there are no EAP method
	 * specific hints.
	 *
	 * SRTT = smoothed round-trip time
	 * RTTVAR = round-trip time variation
	 * RTO = retransmission timeout
	 */

	/*
	 * RFC 2988, 2.1: before RTT measurement, set RTO to 3 seconds for
	 * initial retransmission and then double the RTO to provide back off
	 * per 5.5. Limit the maximum RTO to 20 seconds per RFC 3748, 4.3
	 * modified RTOmax.
	 */
	rto = 3;
	for (i = 0; i < retransCount; i++) {
		rto *= 2;
		if (rto >= 20) {
			rto = 20;
			break;
		}
	}

	wpa_printf(MSG_DEBUG, "EAP: retransmit timeout %d seconds "
		   "(from dynamic back off; retransCount=%d)",
		   rto, retransCount);

	return rto;
}


static void eap_sm_parseEapResp(struct eap_sm *sm, const struct wpabuf *resp)
{
	const struct eap_hdr *hdr;
	size_t plen;

	/* parse rxResp, respId, respMethod */
	sm->rxResp = FALSE;
	sm->respId = -1;
	sm->respMethod = EAP_TYPE_NONE;
	sm->respVendor = EAP_VENDOR_IETF;
	sm->respVendorMethod = EAP_TYPE_NONE;

	if (resp == NULL || wpabuf_len(resp) < sizeof(*hdr)) {
		wpa_printf(MSG_DEBUG, "EAP: parseEapResp: invalid resp=%p "
			   "len=%lu", resp,
			   resp ? (unsigned long) wpabuf_len(resp) : 0);
		return;
	}

	hdr = wpabuf_head(resp);
	plen = be_to_host16(hdr->length);
	if (plen > wpabuf_len(resp)) {
		wpa_printf(MSG_DEBUG, "EAP: Ignored truncated EAP-Packet "
			   "(len=%lu plen=%lu)",
			   (unsigned long) wpabuf_len(resp),
			   (unsigned long) plen);
		return;
	}

	sm->respId = hdr->identifier;

	if (hdr->code == EAP_CODE_RESPONSE)
		sm->rxResp = TRUE;

	if (plen > sizeof(*hdr)) {
		u8 *pos = (u8 *) (hdr + 1);
		sm->respMethod = *pos++;
		if (sm->respMethod == EAP_TYPE_EXPANDED) {
			if (plen < sizeof(*hdr) + 8) {
				wpa_printf(MSG_DEBUG, "EAP: Ignored truncated "
					   "expanded EAP-Packet (plen=%lu)",
					   (unsigned long) plen);
				return;
			}
			sm->respVendor = WPA_GET_BE24(pos);
			pos += 3;
			sm->respVendorMethod = WPA_GET_BE32(pos);
		}
	}

	wpa_printf(MSG_DEBUG, "EAP: parseEapResp: rxResp=%d respId=%d "
		   "respMethod=%u respVendor=%u respVendorMethod=%u",
		   sm->rxResp, sm->respId, sm->respMethod, sm->respVendor,
		   sm->respVendorMethod);
}


static int eap_sm_getId(const struct wpabuf *data)
{
	const struct eap_hdr *hdr;

	if (data == NULL || wpabuf_len(data) < sizeof(*hdr))
		return -1;

	hdr = wpabuf_head(data);
	wpa_printf(MSG_DEBUG, "EAP: getId: id=%d", hdr->identifier);
	return hdr->identifier;
}


static struct wpabuf * eap_sm_buildSuccess(struct eap_sm *sm, u8 id)
{
	struct wpabuf *msg;
	struct eap_hdr *resp;
	wpa_printf(MSG_DEBUG, "EAP: Building EAP-Success (id=%d)", id);

	msg = wpabuf_alloc(sizeof(*resp));
	if (msg == NULL)
		return NULL;
	resp = wpabuf_put(msg, sizeof(*resp));
	resp->code = EAP_CODE_SUCCESS;
	resp->identifier = id;
	resp->length = host_to_be16(sizeof(*resp));

	return msg;
}


static struct wpabuf * eap_sm_buildFailure(struct eap_sm *sm, u8 id)
{
	struct wpabuf *msg;
	struct eap_hdr *resp;
	wpa_printf(MSG_DEBUG, "EAP: Building EAP-Failure (id=%d)", id);

	msg = wpabuf_alloc(sizeof(*resp));
	if (msg == NULL)
		return NULL;
	resp = wpabuf_put(msg, sizeof(*resp));
	resp->code = EAP_CODE_FAILURE;
	resp->identifier = id;
	resp->length = host_to_be16(sizeof(*resp));

	return msg;
}


static int eap_sm_nextId(struct eap_sm *sm, int id)
{
	if (id < 0) {
		/* RFC 3748 Ch 4.1: recommended to initialize Identifier with a
		 * random number */
		id = rand() & 0xff;
		if (id != sm->lastId)
			return id;
	}
	return (id + 1) & 0xff;
}


/**
 * eap_sm_process_nak - Process EAP-Response/Nak
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 * @nak_list: Nak list (allowed methods) from the supplicant
 * @len: Length of nak_list in bytes
 *
 * This function is called when EAP-Response/Nak is received from the
 * supplicant. This can happen for both phase 1 and phase 2 authentications.
 */
void eap_sm_process_nak(struct eap_sm *sm, const u8 *nak_list, size_t len)
{
	int i;
	size_t j;

	if (sm->user == NULL)
		return;

	wpa_printf(MSG_MSGDUMP, "EAP: processing NAK (current EAP method "
		   "index %d)", sm->user_eap_method_index);

	wpa_hexdump(MSG_MSGDUMP, "EAP: configured methods",
		    (u8 *) sm->user->methods,
		    EAP_MAX_METHODS * sizeof(sm->user->methods[0]));
	wpa_hexdump(MSG_MSGDUMP, "EAP: list of methods supported by the peer",
		    nak_list, len);

	i = sm->user_eap_method_index;
	while (i < EAP_MAX_METHODS &&
	       (sm->user->methods[i].vendor != EAP_VENDOR_IETF ||
		sm->user->methods[i].method != EAP_TYPE_NONE)) {
		if (sm->user->methods[i].vendor != EAP_VENDOR_IETF)
			goto not_found;
		for (j = 0; j < len; j++) {
			if (nak_list[j] == sm->user->methods[i].method) {
				break;
			}
		}

		if (j < len) {
			/* found */
			i++;
			continue;
		}

	not_found:
		/* not found - remove from the list */
		os_memmove(&sm->user->methods[i], &sm->user->methods[i + 1],
			   (EAP_MAX_METHODS - i - 1) *
			   sizeof(sm->user->methods[0]));
		sm->user->methods[EAP_MAX_METHODS - 1].vendor =
			EAP_VENDOR_IETF;
		sm->user->methods[EAP_MAX_METHODS - 1].method = EAP_TYPE_NONE;
	}

	wpa_hexdump(MSG_MSGDUMP, "EAP: new list of configured methods",
		    (u8 *) sm->user->methods, EAP_MAX_METHODS *
		    sizeof(sm->user->methods[0]));
}


static void eap_sm_Policy_update(struct eap_sm *sm, const u8 *nak_list,
				 size_t len)
{
	if (nak_list == NULL || sm == NULL || sm->user == NULL)
		return;

	if (sm->user->phase2) {
		wpa_printf(MSG_DEBUG, "EAP: EAP-Nak received after Phase2 user"
			   " info was selected - reject");
		sm->decision = DECISION_FAILURE;
		return;
	}

	eap_sm_process_nak(sm, nak_list, len);
}


static EapType eap_sm_Policy_getNextMethod(struct eap_sm *sm, int *vendor)
{
	EapType next;
	int idx = sm->user_eap_method_index;

	/* In theory, there should be no problems with starting
	 * re-authentication with something else than EAP-Request/Identity and
	 * this does indeed work with wpa_supplicant. However, at least Funk
	 * Supplicant seemed to ignore re-auth if it skipped
	 * EAP-Request/Identity.
	 * Re-auth sets currentId == -1, so that can be used here to select
	 * whether Identity needs to be requested again. */
	if (sm->identity == NULL || sm->currentId == -1) {
		*vendor = EAP_VENDOR_IETF;
		next = EAP_TYPE_IDENTITY;
		sm->update_user = TRUE;
	} else if (sm->user && idx < EAP_MAX_METHODS &&
		   (sm->user->methods[idx].vendor != EAP_VENDOR_IETF ||
		    sm->user->methods[idx].method != EAP_TYPE_NONE)) {
		*vendor = sm->user->methods[idx].vendor;
		next = sm->user->methods[idx].method;
		sm->user_eap_method_index++;
	} else {
		*vendor = EAP_VENDOR_IETF;
		next = EAP_TYPE_NONE;
	}
	wpa_printf(MSG_DEBUG, "EAP: getNextMethod: vendor %d type %d",
		   *vendor, next);
	return next;
}


static int eap_sm_Policy_getDecision(struct eap_sm *sm)
{
	if (!sm->eap_server && sm->identity && !sm->start_reauth) {
		wpa_printf(MSG_DEBUG, "EAP: getDecision: -> PASSTHROUGH");
		return DECISION_PASSTHROUGH;
	}

	if (sm->m && sm->currentMethod != EAP_TYPE_IDENTITY &&
	    sm->m->isSuccess(sm, sm->eap_method_priv)) {
		wpa_printf(MSG_DEBUG, "EAP: getDecision: method succeeded -> "
			   "SUCCESS");
		sm->update_user = TRUE;
		return DECISION_SUCCESS;
	}

	if (sm->m && sm->m->isDone(sm, sm->eap_method_priv) &&
	    !sm->m->isSuccess(sm, sm->eap_method_priv)) {
		wpa_printf(MSG_DEBUG, "EAP: getDecision: method failed -> "
			   "FAILURE");
		sm->update_user = TRUE;
		return DECISION_FAILURE;
	}

	if ((sm->user == NULL || sm->update_user) && sm->identity &&
	    !sm->start_reauth) {
		/*
		 * Allow Identity method to be started once to allow identity
		 * selection hint to be sent from the authentication server,
		 * but prevent a loop of Identity requests by only allowing
		 * this to happen once.
		 */
		int id_req = 0;
		if (sm->user && sm->currentMethod == EAP_TYPE_IDENTITY &&
		    sm->user->methods[0].vendor == EAP_VENDOR_IETF &&
		    sm->user->methods[0].method == EAP_TYPE_IDENTITY)
			id_req = 1;
		if (eap_user_get(sm, sm->identity, sm->identity_len, 0) != 0) {
			wpa_printf(MSG_DEBUG, "EAP: getDecision: user not "
				   "found from database -> FAILURE");
			return DECISION_FAILURE;
		}
		if (id_req && sm->user &&
		    sm->user->methods[0].vendor == EAP_VENDOR_IETF &&
		    sm->user->methods[0].method == EAP_TYPE_IDENTITY) {
			wpa_printf(MSG_DEBUG, "EAP: getDecision: stop "
				   "identity request loop -> FAILURE");
			sm->update_user = TRUE;
			return DECISION_FAILURE;
		}
		sm->update_user = FALSE;
	}
	sm->start_reauth = FALSE;

	if (sm->user && sm->user_eap_method_index < EAP_MAX_METHODS &&
	    (sm->user->methods[sm->user_eap_method_index].vendor !=
	     EAP_VENDOR_IETF ||
	     sm->user->methods[sm->user_eap_method_index].method !=
	     EAP_TYPE_NONE)) {
		wpa_printf(MSG_DEBUG, "EAP: getDecision: another method "
			   "available -> CONTINUE");
		return DECISION_CONTINUE;
	}

	if (sm->identity == NULL || sm->currentId == -1) {
		wpa_printf(MSG_DEBUG, "EAP: getDecision: no identity known "
			   "yet -> CONTINUE");
		return DECISION_CONTINUE;
	}

	wpa_printf(MSG_DEBUG, "EAP: getDecision: no more methods available -> "
		   "FAILURE");
	return DECISION_FAILURE;
}


static Boolean eap_sm_Policy_doPickUp(struct eap_sm *sm, EapType method)
{
	return method == EAP_TYPE_IDENTITY ? TRUE : FALSE;
}


/**
 * eap_server_sm_step - Step EAP server state machine
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 * Returns: 1 if EAP state was changed or 0 if not
 *
 * This function advances EAP state machine to a new state to match with the
 * current variables. This should be called whenever variables used by the EAP
 * state machine have changed.
 */
int eap_server_sm_step(struct eap_sm *sm)
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


static void eap_user_free(struct eap_user *user)
{
	if (user == NULL)
		return;
	os_free(user->password);
	user->password = NULL;
	os_free(user);
}


/**
 * eap_server_sm_init - Allocate and initialize EAP server state machine
 * @eapol_ctx: Context data to be used with eapol_cb calls
 * @eapol_cb: Pointer to EAPOL callback functions
 * @conf: EAP configuration
 * Returns: Pointer to the allocated EAP state machine or %NULL on failure
 *
 * This function allocates and initializes an EAP state machine.
 */
struct eap_sm * eap_server_sm_init(void *eapol_ctx,
				   struct eapol_callbacks *eapol_cb,
				   struct eap_config *conf)
{
	struct eap_sm *sm;

	sm = os_zalloc(sizeof(*sm));
	if (sm == NULL)
		return NULL;
	sm->eapol_ctx = eapol_ctx;
	sm->eapol_cb = eapol_cb;
	sm->MaxRetrans = 5; /* RFC 3748: max 3-5 retransmissions suggested */
	sm->ssl_ctx = conf->ssl_ctx;
	sm->msg_ctx = conf->msg_ctx;
	sm->eap_sim_db_priv = conf->eap_sim_db_priv;
	sm->backend_auth = conf->backend_auth;
	sm->eap_server = conf->eap_server;
	if (conf->pac_opaque_encr_key) {
		sm->pac_opaque_encr_key = os_malloc(16);
		if (sm->pac_opaque_encr_key) {
			os_memcpy(sm->pac_opaque_encr_key,
				  conf->pac_opaque_encr_key, 16);
		}
	}
	if (conf->eap_fast_a_id) {
		sm->eap_fast_a_id = os_malloc(conf->eap_fast_a_id_len);
		if (sm->eap_fast_a_id) {
			os_memcpy(sm->eap_fast_a_id, conf->eap_fast_a_id,
				  conf->eap_fast_a_id_len);
			sm->eap_fast_a_id_len = conf->eap_fast_a_id_len;
		}
	}
	if (conf->eap_fast_a_id_info)
		sm->eap_fast_a_id_info = os_strdup(conf->eap_fast_a_id_info);
	sm->eap_fast_prov = conf->eap_fast_prov;
	sm->pac_key_lifetime = conf->pac_key_lifetime;
	sm->pac_key_refresh_time = conf->pac_key_refresh_time;
	sm->eap_sim_aka_result_ind = conf->eap_sim_aka_result_ind;
	sm->tnc = conf->tnc;
	sm->wps = conf->wps;
	if (conf->assoc_wps_ie)
		sm->assoc_wps_ie = wpabuf_dup(conf->assoc_wps_ie);
	if (conf->assoc_p2p_ie)
		sm->assoc_p2p_ie = wpabuf_dup(conf->assoc_p2p_ie);
	if (conf->peer_addr)
		os_memcpy(sm->peer_addr, conf->peer_addr, ETH_ALEN);
	sm->fragment_size = conf->fragment_size;
	sm->pwd_group = conf->pwd_group;

	wpa_printf(MSG_DEBUG, "EAP: Server state machine created");

	return sm;
}


/**
 * eap_server_sm_deinit - Deinitialize and free an EAP server state machine
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 *
 * This function deinitializes EAP state machine and frees all allocated
 * resources.
 */
void eap_server_sm_deinit(struct eap_sm *sm)
{
	if (sm == NULL)
		return;
	wpa_printf(MSG_DEBUG, "EAP: Server state machine removed");
	if (sm->m && sm->eap_method_priv)
		sm->m->reset(sm, sm->eap_method_priv);
	wpabuf_free(sm->eap_if.eapReqData);
	os_free(sm->eap_if.eapKeyData);
	wpabuf_free(sm->lastReqData);
	wpabuf_free(sm->eap_if.eapRespData);
	os_free(sm->identity);
	os_free(sm->pac_opaque_encr_key);
	os_free(sm->eap_fast_a_id);
	os_free(sm->eap_fast_a_id_info);
	wpabuf_free(sm->eap_if.aaaEapReqData);
	wpabuf_free(sm->eap_if.aaaEapRespData);
	os_free(sm->eap_if.aaaEapKeyData);
	eap_user_free(sm->user);
	wpabuf_free(sm->assoc_wps_ie);
	wpabuf_free(sm->assoc_p2p_ie);
	os_free(sm);
}


/**
 * eap_sm_notify_cached - Notify EAP state machine of cached PMK
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 *
 * This function is called when PMKSA caching is used to skip EAP
 * authentication.
 */
void eap_sm_notify_cached(struct eap_sm *sm)
{
	if (sm == NULL)
		return;

	sm->EAP_state = EAP_SUCCESS;
}


/**
 * eap_sm_pending_cb - EAP state machine callback for a pending EAP request
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 *
 * This function is called when data for a pending EAP-Request is received.
 */
void eap_sm_pending_cb(struct eap_sm *sm)
{
	if (sm == NULL)
		return;
	wpa_printf(MSG_DEBUG, "EAP: Callback for pending request received");
	if (sm->method_pending == METHOD_PENDING_WAIT)
		sm->method_pending = METHOD_PENDING_CONT;
}


/**
 * eap_sm_method_pending - Query whether EAP method is waiting for pending data
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 * Returns: 1 if method is waiting for pending data or 0 if not
 */
int eap_sm_method_pending(struct eap_sm *sm)
{
	if (sm == NULL)
		return 0;
	return sm->method_pending == METHOD_PENDING_WAIT;
}


/**
 * eap_get_identity - Get the user identity (from EAP-Response/Identity)
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 * @len: Buffer for returning identity length
 * Returns: Pointer to the user identity or %NULL if not available
 */
const u8 * eap_get_identity(struct eap_sm *sm, size_t *len)
{
	*len = sm->identity_len;
	return sm->identity;
}


/**
 * eap_get_interface - Get pointer to EAP-EAPOL interface data
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 * Returns: Pointer to the EAP-EAPOL interface data
 */
struct eap_eapol_interface * eap_get_interface(struct eap_sm *sm)
{
	return &sm->eap_if;
}


/**
 * eap_server_clear_identity - Clear EAP identity information
 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
 *
 * This function can be used to clear the EAP identity information in the EAP
 * server context. This allows the EAP/Identity method to be used again after
 * EAPOL-Start or EAPOL-Logoff.
 */
void eap_server_clear_identity(struct eap_sm *sm)
{
	os_free(sm->identity);
	sm->identity = NULL;
}
