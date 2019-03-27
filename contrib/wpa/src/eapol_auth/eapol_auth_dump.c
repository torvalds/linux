/*
 * IEEE 802.1X-2004 Authenticator - State dump
 * Copyright (c) 2002-2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_server/eap.h"
#include "eapol_auth_sm.h"
#include "eapol_auth_sm_i.h"

static inline const char * port_type_txt(PortTypes pt)
{
	switch (pt) {
	case ForceUnauthorized: return "ForceUnauthorized";
	case ForceAuthorized: return "ForceAuthorized";
	case Auto: return "Auto";
	default: return "Unknown";
	}
}


static inline const char * port_state_txt(PortState ps)
{
	switch (ps) {
	case Unauthorized: return "Unauthorized";
	case Authorized: return "Authorized";
	default: return "Unknown";
	}
}


static inline const char * ctrl_dir_txt(ControlledDirection dir)
{
	switch (dir) {
	case Both: return "Both";
	case In: return "In";
	default: return "Unknown";
	}
}


static inline const char * auth_pae_state_txt(int s)
{
	switch (s) {
	case AUTH_PAE_INITIALIZE: return "INITIALIZE";
	case AUTH_PAE_DISCONNECTED: return "DISCONNECTED";
	case AUTH_PAE_CONNECTING: return "CONNECTING";
	case AUTH_PAE_AUTHENTICATING: return "AUTHENTICATING";
	case AUTH_PAE_AUTHENTICATED: return "AUTHENTICATED";
	case AUTH_PAE_ABORTING: return "ABORTING";
	case AUTH_PAE_HELD: return "HELD";
	case AUTH_PAE_FORCE_AUTH: return "FORCE_AUTH";
	case AUTH_PAE_FORCE_UNAUTH: return "FORCE_UNAUTH";
	case AUTH_PAE_RESTART: return "RESTART";
	default: return "Unknown";
	}
}


static inline const char * be_auth_state_txt(int s)
{
	switch (s) {
	case BE_AUTH_REQUEST: return "REQUEST";
	case BE_AUTH_RESPONSE: return "RESPONSE";
	case BE_AUTH_SUCCESS: return "SUCCESS";
	case BE_AUTH_FAIL: return "FAIL";
	case BE_AUTH_TIMEOUT: return "TIMEOUT";
	case BE_AUTH_IDLE: return "IDLE";
	case BE_AUTH_INITIALIZE: return "INITIALIZE";
	case BE_AUTH_IGNORE: return "IGNORE";
	default: return "Unknown";
	}
}


static inline const char * reauth_timer_state_txt(int s)
{
	switch (s) {
	case REAUTH_TIMER_INITIALIZE: return "INITIALIZE";
	case REAUTH_TIMER_REAUTHENTICATE: return "REAUTHENTICATE";
	default: return "Unknown";
	}
}


static inline const char * auth_key_tx_state_txt(int s)
{
	switch (s) {
	case AUTH_KEY_TX_NO_KEY_TRANSMIT: return "NO_KEY_TRANSMIT";
	case AUTH_KEY_TX_KEY_TRANSMIT: return "KEY_TRANSMIT";
	default: return "Unknown";
	}
}


static inline const char * key_rx_state_txt(int s)
{
	switch (s) {
	case KEY_RX_NO_KEY_RECEIVE: return "NO_KEY_RECEIVE";
	case KEY_RX_KEY_RECEIVE: return "KEY_RECEIVE";
	default: return "Unknown";
	}
}


static inline const char * ctrl_dir_state_txt(int s)
{
	switch (s) {
	case CTRL_DIR_FORCE_BOTH: return "FORCE_BOTH";
	case CTRL_DIR_IN_OR_BOTH: return "IN_OR_BOTH";
	default: return "Unknown";
	}
}


int eapol_auth_dump_state(struct eapol_state_machine *sm, char *buf,
			  size_t buflen)
{
	char *pos, *end;
	int ret;

	pos = buf;
	end = pos + buflen;

	ret = os_snprintf(pos, end - pos, "aWhile=%d\nquietWhile=%d\n"
			  "reAuthWhen=%d\n",
			  sm->aWhile, sm->quietWhile, sm->reAuthWhen);
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

#define _SB(b) ((b) ? "TRUE" : "FALSE")
	ret = os_snprintf(pos, end - pos,
			  "authAbort=%s\n"
			  "authFail=%s\n"
			  "authPortStatus=%s\n"
			  "authStart=%s\n"
			  "authTimeout=%s\n"
			  "authSuccess=%s\n"
			  "eapFail=%s\n"
			  "eapolEap=%s\n"
			  "eapSuccess=%s\n"
			  "eapTimeout=%s\n"
			  "initialize=%s\n"
			  "keyAvailable=%s\n"
			  "keyDone=%s\n"
			  "keyRun=%s\n"
			  "keyTxEnabled=%s\n"
			  "portControl=%s\n"
			  "portEnabled=%s\n"
			  "portValid=%s\n"
			  "reAuthenticate=%s\n",
			  _SB(sm->authAbort),
			  _SB(sm->authFail),
			  port_state_txt(sm->authPortStatus),
			  _SB(sm->authStart),
			  _SB(sm->authTimeout),
			  _SB(sm->authSuccess),
			  _SB(sm->eap_if->eapFail),
			  _SB(sm->eapolEap),
			  _SB(sm->eap_if->eapSuccess),
			  _SB(sm->eap_if->eapTimeout),
			  _SB(sm->initialize),
			  _SB(sm->eap_if->eapKeyAvailable),
			  _SB(sm->keyDone), _SB(sm->keyRun),
			  _SB(sm->keyTxEnabled),
			  port_type_txt(sm->portControl),
			  _SB(sm->eap_if->portEnabled),
			  _SB(sm->portValid),
			  _SB(sm->reAuthenticate));
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	ret = os_snprintf(pos, end - pos,
			  "auth_pae_state=%s\n"
			  "eapolLogoff=%s\n"
			  "eapolStart=%s\n"
			  "eapRestart=%s\n"
			  "portMode=%s\n"
			  "reAuthCount=%d\n"
			  "quietPeriod=%d\n"
			  "reAuthMax=%d\n"
			  "authEntersConnecting=%d\n"
			  "authEapLogoffsWhileConnecting=%d\n"
			  "authEntersAuthenticating=%d\n"
			  "authAuthSuccessesWhileAuthenticating=%d\n"
			  "authAuthTimeoutsWhileAuthenticating=%d\n"
			  "authAuthFailWhileAuthenticating=%d\n"
			  "authAuthEapStartsWhileAuthenticating=%d\n"
			  "authAuthEapLogoffWhileAuthenticating=%d\n"
			  "authAuthReauthsWhileAuthenticated=%d\n"
			  "authAuthEapStartsWhileAuthenticated=%d\n"
			  "authAuthEapLogoffWhileAuthenticated=%d\n",
			  auth_pae_state_txt(sm->auth_pae_state),
			  _SB(sm->eapolLogoff),
			  _SB(sm->eapolStart),
			  _SB(sm->eap_if->eapRestart),
			  port_type_txt(sm->portMode),
			  sm->reAuthCount,
			  sm->quietPeriod, sm->reAuthMax,
			  sm->authEntersConnecting,
			  sm->authEapLogoffsWhileConnecting,
			  sm->authEntersAuthenticating,
			  sm->authAuthSuccessesWhileAuthenticating,
			  sm->authAuthTimeoutsWhileAuthenticating,
			  sm->authAuthFailWhileAuthenticating,
			  sm->authAuthEapStartsWhileAuthenticating,
			  sm->authAuthEapLogoffWhileAuthenticating,
			  sm->authAuthReauthsWhileAuthenticated,
			  sm->authAuthEapStartsWhileAuthenticated,
			  sm->authAuthEapLogoffWhileAuthenticated);
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	ret = os_snprintf(pos, end - pos,
			  "be_auth_state=%s\n"
			  "eapNoReq=%s\n"
			  "eapReq=%s\n"
			  "eapResp=%s\n"
			  "serverTimeout=%d\n"
			  "backendResponses=%d\n"
			  "backendAccessChallenges=%d\n"
			  "backendOtherRequestsToSupplicant=%d\n"
			  "backendAuthSuccesses=%d\n"
			  "backendAuthFails=%d\n",
			  be_auth_state_txt(sm->be_auth_state),
			  _SB(sm->eap_if->eapNoReq),
			  _SB(sm->eap_if->eapReq),
			  _SB(sm->eap_if->eapResp),
			  sm->serverTimeout,
			  sm->backendResponses,
			  sm->backendAccessChallenges,
			  sm->backendOtherRequestsToSupplicant,
			  sm->backendAuthSuccesses,
			  sm->backendAuthFails);
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	ret = os_snprintf(pos, end - pos,
			  "reauth_timer_state=%s\n"
			  "reAuthPeriod=%d\n"
			  "reAuthEnabled=%s\n",
			  reauth_timer_state_txt(sm->reauth_timer_state),
			  sm->reAuthPeriod,
			  _SB(sm->reAuthEnabled));
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	ret = os_snprintf(pos, end - pos,
			  "auth_key_tx_state=%s\n",
			  auth_key_tx_state_txt(sm->auth_key_tx_state));
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	ret = os_snprintf(pos, end - pos,
			  "key_rx_state=%s\n"
			  "rxKey=%s\n",
			  key_rx_state_txt(sm->key_rx_state),
			  _SB(sm->rxKey));
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	ret = os_snprintf(pos, end - pos,
			  "ctrl_dir_state=%s\n"
			  "adminControlledDirections=%s\n"
			  "operControlledDirections=%s\n"
			  "operEdge=%s\n",
			  ctrl_dir_state_txt(sm->ctrl_dir_state),
			  ctrl_dir_txt(sm->adminControlledDirections),
			  ctrl_dir_txt(sm->operControlledDirections),
			  _SB(sm->operEdge));
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;
#undef _SB

	return pos - buf;
}
