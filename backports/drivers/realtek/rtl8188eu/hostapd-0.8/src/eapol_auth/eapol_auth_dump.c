/*
 * IEEE 802.1X-2004 Authenticator - State dump
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
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


void eapol_auth_dump_state(FILE *f, const char *prefix,
			   struct eapol_state_machine *sm)
{
	fprintf(f, "%sEAPOL state machine:\n", prefix);
	fprintf(f, "%s  aWhile=%d quietWhile=%d reAuthWhen=%d\n", prefix,
		sm->aWhile, sm->quietWhile, sm->reAuthWhen);
#define _SB(b) ((b) ? "TRUE" : "FALSE")
	fprintf(f,
		"%s  authAbort=%s authFail=%s authPortStatus=%s authStart=%s\n"
		"%s  authTimeout=%s authSuccess=%s eapFail=%s eapolEap=%s\n"
		"%s  eapSuccess=%s eapTimeout=%s initialize=%s "
		"keyAvailable=%s\n"
		"%s  keyDone=%s keyRun=%s keyTxEnabled=%s portControl=%s\n"
		"%s  portEnabled=%s portValid=%s reAuthenticate=%s\n",
		prefix, _SB(sm->authAbort), _SB(sm->authFail),
		port_state_txt(sm->authPortStatus), _SB(sm->authStart),
		prefix, _SB(sm->authTimeout), _SB(sm->authSuccess),
		_SB(sm->eap_if->eapFail), _SB(sm->eapolEap),
		prefix, _SB(sm->eap_if->eapSuccess),
		_SB(sm->eap_if->eapTimeout),
		_SB(sm->initialize), _SB(sm->eap_if->eapKeyAvailable),
		prefix, _SB(sm->keyDone), _SB(sm->keyRun),
		_SB(sm->keyTxEnabled), port_type_txt(sm->portControl),
		prefix, _SB(sm->eap_if->portEnabled), _SB(sm->portValid),
		_SB(sm->reAuthenticate));

	fprintf(f, "%s  Authenticator PAE:\n"
		"%s    state=%s\n"
		"%s    eapolLogoff=%s eapolStart=%s eapRestart=%s\n"
		"%s    portMode=%s reAuthCount=%d\n"
		"%s    quietPeriod=%d reAuthMax=%d\n"
		"%s    authEntersConnecting=%d\n"
		"%s    authEapLogoffsWhileConnecting=%d\n"
		"%s    authEntersAuthenticating=%d\n"
		"%s    authAuthSuccessesWhileAuthenticating=%d\n"
		"%s    authAuthTimeoutsWhileAuthenticating=%d\n"
		"%s    authAuthFailWhileAuthenticating=%d\n"
		"%s    authAuthEapStartsWhileAuthenticating=%d\n"
		"%s    authAuthEapLogoffWhileAuthenticating=%d\n"
		"%s    authAuthReauthsWhileAuthenticated=%d\n"
		"%s    authAuthEapStartsWhileAuthenticated=%d\n"
		"%s    authAuthEapLogoffWhileAuthenticated=%d\n",
		prefix, prefix, auth_pae_state_txt(sm->auth_pae_state), prefix,
		_SB(sm->eapolLogoff), _SB(sm->eapolStart),
		_SB(sm->eap_if->eapRestart),
		prefix, port_type_txt(sm->portMode), sm->reAuthCount,
		prefix, sm->quietPeriod, sm->reAuthMax,
		prefix, sm->authEntersConnecting,
		prefix, sm->authEapLogoffsWhileConnecting,
		prefix, sm->authEntersAuthenticating,
		prefix, sm->authAuthSuccessesWhileAuthenticating,
		prefix, sm->authAuthTimeoutsWhileAuthenticating,
		prefix, sm->authAuthFailWhileAuthenticating,
		prefix, sm->authAuthEapStartsWhileAuthenticating,
		prefix, sm->authAuthEapLogoffWhileAuthenticating,
		prefix, sm->authAuthReauthsWhileAuthenticated,
		prefix, sm->authAuthEapStartsWhileAuthenticated,
		prefix, sm->authAuthEapLogoffWhileAuthenticated);

	fprintf(f, "%s  Backend Authentication:\n"
		"%s    state=%s\n"
		"%s    eapNoReq=%s eapReq=%s eapResp=%s\n"
		"%s    serverTimeout=%d\n"
		"%s    backendResponses=%d\n"
		"%s    backendAccessChallenges=%d\n"
		"%s    backendOtherRequestsToSupplicant=%d\n"
		"%s    backendAuthSuccesses=%d\n"
		"%s    backendAuthFails=%d\n",
		prefix, prefix,
		be_auth_state_txt(sm->be_auth_state),
		prefix, _SB(sm->eap_if->eapNoReq), _SB(sm->eap_if->eapReq),
		_SB(sm->eap_if->eapResp),
		prefix, sm->serverTimeout,
		prefix, sm->backendResponses,
		prefix, sm->backendAccessChallenges,
		prefix, sm->backendOtherRequestsToSupplicant,
		prefix, sm->backendAuthSuccesses,
		prefix, sm->backendAuthFails);

	fprintf(f, "%s  Reauthentication Timer:\n"
		"%s    state=%s\n"
		"%s    reAuthPeriod=%d reAuthEnabled=%s\n", prefix, prefix,
		reauth_timer_state_txt(sm->reauth_timer_state), prefix,
		sm->reAuthPeriod, _SB(sm->reAuthEnabled));

	fprintf(f, "%s  Authenticator Key Transmit:\n"
		"%s    state=%s\n", prefix, prefix,
		auth_key_tx_state_txt(sm->auth_key_tx_state));

	fprintf(f, "%s  Key Receive:\n"
		"%s    state=%s\n"
		"%s    rxKey=%s\n", prefix, prefix,
		key_rx_state_txt(sm->key_rx_state), prefix, _SB(sm->rxKey));

	fprintf(f, "%s  Controlled Directions:\n"
		"%s    state=%s\n"
		"%s    adminControlledDirections=%s "
		"operControlledDirections=%s\n"
		"%s    operEdge=%s\n", prefix, prefix,
		ctrl_dir_state_txt(sm->ctrl_dir_state),
		prefix, ctrl_dir_txt(sm->adminControlledDirections),
		ctrl_dir_txt(sm->operControlledDirections),
		prefix, _SB(sm->operEdge));
#undef _SB
}
