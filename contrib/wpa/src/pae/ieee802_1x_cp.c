/*
 * IEEE 802.1X-2010 Controlled Port of PAE state machine - CP state machine
 * Copyright (c) 2013-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/defs.h"
#include "common/ieee802_1x_defs.h"
#include "utils/state_machine.h"
#include "ieee802_1x_kay.h"
#include "ieee802_1x_secy_ops.h"
#include "pae/ieee802_1x_cp.h"

#define STATE_MACHINE_DATA struct ieee802_1x_cp_sm
#define STATE_MACHINE_DEBUG_PREFIX "CP"

static u64 default_cs_id = CS_ID_GCM_AES_128;

/* The variable defined in clause 12 in IEEE Std 802.1X-2010 */
enum connect_type { PENDING, UNAUTHENTICATED, AUTHENTICATED, SECURE };

struct ieee802_1x_cp_sm {
	enum cp_states {
		CP_BEGIN, CP_INIT, CP_CHANGE, CP_ALLOWED, CP_AUTHENTICATED,
		CP_SECURED, CP_RECEIVE, CP_RECEIVING, CP_READY, CP_TRANSMIT,
		CP_TRANSMITTING, CP_ABANDON, CP_RETIRE
	} CP_state;
	Boolean changed;

	/* CP -> Client */
	Boolean port_valid;

	/* Logon -> CP */
	enum connect_type connect;
	u8 *authorization_data;

	/* KaY -> CP */
	Boolean chgd_server; /* clear by CP */
	Boolean elected_self;
	u8 *authorization_data1;
	enum confidentiality_offset cipher_offset;
	u64 cipher_suite;
	Boolean new_sak; /* clear by CP */
	struct ieee802_1x_mka_ki distributed_ki;
	u8 distributed_an;
	Boolean using_receive_sas;
	Boolean all_receiving;
	Boolean server_transmitting;
	Boolean using_transmit_sa;

	/* CP -> KaY */
	struct ieee802_1x_mka_ki *lki;
	u8 lan;
	Boolean ltx;
	Boolean lrx;
	struct ieee802_1x_mka_ki *oki;
	u8 oan;
	Boolean otx;
	Boolean orx;

	/* CP -> SecY */
	Boolean protect_frames;
	enum validate_frames validate_frames;

	Boolean replay_protect;
	u32 replay_window;

	u64 current_cipher_suite;
	enum confidentiality_offset confidentiality_offset;
	Boolean controlled_port_enabled;

	/* SecY -> CP */
	Boolean port_enabled; /* SecY->CP */

	/* private */
	u32 transmit_when;
	u32 transmit_delay;
	u32 retire_when;
	u32 retire_delay;

	/* not defined IEEE Std 802.1X-2010 */
	struct ieee802_1x_kay *kay;
};

static void ieee802_1x_cp_retire_when_timeout(void *eloop_ctx,
					      void *timeout_ctx);
static void ieee802_1x_cp_transmit_when_timeout(void *eloop_ctx,
						void *timeout_ctx);


static int changed_cipher(struct ieee802_1x_cp_sm *sm)
{
	return sm->confidentiality_offset != sm->cipher_offset ||
		sm->current_cipher_suite != sm->cipher_suite;
}


static int changed_connect(struct ieee802_1x_cp_sm *sm)
{
	return sm->connect != SECURE || sm->chgd_server || changed_cipher(sm);
}


SM_STATE(CP, INIT)
{
	SM_ENTRY(CP, INIT);

	sm->controlled_port_enabled = FALSE;
	secy_cp_control_enable_port(sm->kay, sm->controlled_port_enabled);

	sm->port_valid = FALSE;

	os_free(sm->lki);
	sm->lki = NULL;
	sm->ltx = FALSE;
	sm->lrx = FALSE;

	os_free(sm->oki);
	sm->oki = NULL;
	sm->otx = FALSE;
	sm->orx = FALSE;

	sm->port_enabled = TRUE;
	sm->chgd_server = FALSE;
}


SM_STATE(CP, CHANGE)
{
	SM_ENTRY(CP, CHANGE);

	sm->port_valid = FALSE;
	sm->controlled_port_enabled = FALSE;
	secy_cp_control_enable_port(sm->kay, sm->controlled_port_enabled);

	if (sm->lki)
		ieee802_1x_kay_delete_sas(sm->kay, sm->lki);
	if (sm->oki)
		ieee802_1x_kay_delete_sas(sm->kay, sm->oki);
}


SM_STATE(CP, ALLOWED)
{
	SM_ENTRY(CP, ALLOWED);

	sm->protect_frames = FALSE;
	sm->replay_protect = FALSE;
	sm->validate_frames = Checked;

	sm->port_valid = FALSE;
	sm->controlled_port_enabled = TRUE;

	secy_cp_control_enable_port(sm->kay, sm->controlled_port_enabled);
	secy_cp_control_protect_frames(sm->kay, sm->protect_frames);
	secy_cp_control_encrypt(sm->kay, sm->kay->macsec_encrypt);
	secy_cp_control_validate_frames(sm->kay, sm->validate_frames);
	secy_cp_control_replay(sm->kay, sm->replay_protect, sm->replay_window);
}


SM_STATE(CP, AUTHENTICATED)
{
	SM_ENTRY(CP, AUTHENTICATED);

	sm->protect_frames = FALSE;
	sm->replay_protect = FALSE;
	sm->validate_frames = Checked;

	sm->port_valid = FALSE;
	sm->controlled_port_enabled = TRUE;

	secy_cp_control_enable_port(sm->kay, sm->controlled_port_enabled);
	secy_cp_control_protect_frames(sm->kay, sm->protect_frames);
	secy_cp_control_encrypt(sm->kay, sm->kay->macsec_encrypt);
	secy_cp_control_validate_frames(sm->kay, sm->validate_frames);
	secy_cp_control_replay(sm->kay, sm->replay_protect, sm->replay_window);
}


SM_STATE(CP, SECURED)
{
	SM_ENTRY(CP, SECURED);

	sm->chgd_server = FALSE;

	sm->protect_frames = sm->kay->macsec_protect;
	sm->replay_protect = sm->kay->macsec_replay_protect;
	sm->validate_frames = sm->kay->macsec_validate;

	/* NOTE: now no other than default cipher suite (AES-GCM-128) */
	sm->current_cipher_suite = sm->cipher_suite;
	secy_cp_control_current_cipher_suite(sm->kay, sm->current_cipher_suite);

	sm->confidentiality_offset = sm->cipher_offset;

	sm->port_valid = TRUE;

	secy_cp_control_confidentiality_offset(sm->kay,
					       sm->confidentiality_offset);
	secy_cp_control_protect_frames(sm->kay, sm->protect_frames);
	secy_cp_control_encrypt(sm->kay, sm->kay->macsec_encrypt);
	secy_cp_control_validate_frames(sm->kay, sm->validate_frames);
	secy_cp_control_replay(sm->kay, sm->replay_protect, sm->replay_window);
}


SM_STATE(CP, RECEIVE)
{
	SM_ENTRY(CP, RECEIVE);
	/* RECEIVE state machine not keep with Figure 12-2 in
	 * IEEE Std 802.1X-2010 */
	sm->oki = sm->lki;
	sm->oan = sm->lan;
	sm->otx = sm->ltx;
	sm->orx = sm->lrx;
	ieee802_1x_kay_set_old_sa_attr(sm->kay, sm->oki, sm->oan,
				       sm->otx, sm->orx);

	sm->lki = os_malloc(sizeof(*sm->lki));
	if (!sm->lki) {
		wpa_printf(MSG_ERROR, "CP-%s: Out of memory", __func__);
		return;
	}
	os_memcpy(sm->lki, &sm->distributed_ki, sizeof(*sm->lki));
	sm->lan = sm->distributed_an;
	sm->ltx = FALSE;
	sm->lrx = FALSE;
	ieee802_1x_kay_set_latest_sa_attr(sm->kay, sm->lki, sm->lan,
					  sm->ltx, sm->lrx);
	ieee802_1x_kay_create_sas(sm->kay, sm->lki);
	ieee802_1x_kay_enable_rx_sas(sm->kay, sm->lki);
	sm->new_sak = FALSE;
	sm->all_receiving = FALSE;
}


SM_STATE(CP, RECEIVING)
{
	SM_ENTRY(CP, RECEIVING);

	sm->lrx = TRUE;
	ieee802_1x_kay_set_latest_sa_attr(sm->kay, sm->lki, sm->lan,
					  sm->ltx, sm->lrx);
	sm->transmit_when = sm->transmit_delay;
	eloop_cancel_timeout(ieee802_1x_cp_transmit_when_timeout, sm, NULL);
	eloop_register_timeout(sm->transmit_when / 1000, 0,
			       ieee802_1x_cp_transmit_when_timeout, sm, NULL);
	/* the electedSelf have been set before CP entering to RECEIVING
	 * but the CP will transmit from RECEIVING to READY under
	 * the !electedSelf when KaY is not key server */
	ieee802_1x_cp_sm_step(sm);
	sm->using_receive_sas = FALSE;
	sm->server_transmitting = FALSE;
}


SM_STATE(CP, READY)
{
	SM_ENTRY(CP, READY);

	ieee802_1x_kay_enable_new_info(sm->kay);
}


SM_STATE(CP, TRANSMIT)
{
	SM_ENTRY(CP, TRANSMIT);

	sm->controlled_port_enabled = TRUE;
	secy_cp_control_enable_port(sm->kay, sm->controlled_port_enabled);
	sm->ltx = TRUE;
	ieee802_1x_kay_set_latest_sa_attr(sm->kay, sm->lki, sm->lan,
					  sm->ltx, sm->lrx);
	ieee802_1x_kay_enable_tx_sas(sm->kay,  sm->lki);
	sm->all_receiving = FALSE;
	sm->server_transmitting = FALSE;
}


SM_STATE(CP, TRANSMITTING)
{
	SM_ENTRY(CP, TRANSMITTING);
	sm->retire_when = sm->orx ? sm->retire_delay : 0;
	sm->otx = FALSE;
	ieee802_1x_kay_set_old_sa_attr(sm->kay, sm->oki, sm->oan,
				       sm->otx, sm->orx);
	ieee802_1x_kay_enable_new_info(sm->kay);
	eloop_cancel_timeout(ieee802_1x_cp_retire_when_timeout, sm, NULL);
	eloop_register_timeout(sm->retire_when / 1000, 0,
			       ieee802_1x_cp_retire_when_timeout, sm, NULL);
	sm->using_transmit_sa = FALSE;
}


SM_STATE(CP, ABANDON)
{
	SM_ENTRY(CP, ABANDON);
	sm->lrx = FALSE;
	ieee802_1x_kay_set_latest_sa_attr(sm->kay, sm->lki, sm->lan,
					  sm->ltx, sm->lrx);
	ieee802_1x_kay_delete_sas(sm->kay, sm->lki);

	os_free(sm->lki);
	sm->lki = NULL;
	ieee802_1x_kay_set_latest_sa_attr(sm->kay, sm->lki, sm->lan,
					  sm->ltx, sm->lrx);
	sm->new_sak = FALSE;
}


SM_STATE(CP, RETIRE)
{
	SM_ENTRY(CP, RETIRE);
	/* RETIRE state machine not keep with Figure 12-2 in
	 * IEEE Std 802.1X-2010 */
	os_free(sm->oki);
	sm->oki = NULL;
	sm->orx = FALSE;
	sm->otx = FALSE;
	ieee802_1x_kay_set_old_sa_attr(sm->kay, sm->oki, sm->oan,
				       sm->otx, sm->orx);
}


/**
 * CP state machine handler entry
 */
SM_STEP(CP)
{
	if (!sm->port_enabled)
		SM_ENTER(CP, INIT);

	switch (sm->CP_state) {
	case CP_BEGIN:
		SM_ENTER(CP, INIT);
		break;

	case CP_INIT:
		SM_ENTER(CP, CHANGE);
		break;

	case CP_CHANGE:
		if (sm->connect == UNAUTHENTICATED)
			SM_ENTER(CP, ALLOWED);
		else if (sm->connect == AUTHENTICATED)
			SM_ENTER(CP, AUTHENTICATED);
		else if (sm->connect == SECURE)
			SM_ENTER(CP, SECURED);
		break;

	case CP_ALLOWED:
		if (sm->connect != UNAUTHENTICATED)
			SM_ENTER(CP, CHANGE);
		break;

	case CP_AUTHENTICATED:
		if (sm->connect != AUTHENTICATED)
			SM_ENTER(CP, CHANGE);
		break;

	case CP_SECURED:
		if (changed_connect(sm))
			SM_ENTER(CP, CHANGE);
		else if (sm->new_sak)
			SM_ENTER(CP, RECEIVE);
		break;

	case CP_RECEIVE:
		if (sm->using_receive_sas)
			SM_ENTER(CP, RECEIVING);
		break;

	case CP_RECEIVING:
		if (sm->new_sak || changed_connect(sm))
			SM_ENTER(CP, ABANDON);
		if (!sm->elected_self)
			SM_ENTER(CP, READY);
		if (sm->elected_self &&
		    (sm->all_receiving || !sm->transmit_when))
			SM_ENTER(CP, TRANSMIT);
		break;

	case CP_TRANSMIT:
		if (sm->using_transmit_sa)
			SM_ENTER(CP, TRANSMITTING);
		break;

	case CP_TRANSMITTING:
		if (!sm->retire_when || changed_connect(sm))
			SM_ENTER(CP, RETIRE);
		break;

	case CP_RETIRE:
		if (changed_connect(sm))
			SM_ENTER(CP, CHANGE);
		else if (sm->new_sak)
			SM_ENTER(CP, RECEIVE);
		break;

	case CP_READY:
		if (sm->new_sak || changed_connect(sm))
			SM_ENTER(CP, RECEIVE);
		if (sm->server_transmitting)
			SM_ENTER(CP, TRANSMIT);
		break;
	case CP_ABANDON:
		if (changed_connect(sm))
			SM_ENTER(CP, RETIRE);
		else if (sm->new_sak)
			SM_ENTER(CP, RECEIVE);
		break;
	default:
		wpa_printf(MSG_ERROR, "CP: the state machine is not defined");
		break;
	}
}


/**
 * ieee802_1x_cp_sm_init -
 */
struct ieee802_1x_cp_sm * ieee802_1x_cp_sm_init(struct ieee802_1x_kay *kay)
{
	struct ieee802_1x_cp_sm *sm;

	sm = os_zalloc(sizeof(*sm));
	if (sm == NULL) {
		wpa_printf(MSG_ERROR, "CP-%s: out of memory", __func__);
		return NULL;
	}

	sm->kay = kay;

	sm->port_valid = FALSE;

	sm->chgd_server = FALSE;

	sm->protect_frames = kay->macsec_protect;
	sm->validate_frames = kay->macsec_validate;
	sm->replay_protect = kay->macsec_replay_protect;
	sm->replay_window = kay->macsec_replay_window;

	sm->controlled_port_enabled = FALSE;

	sm->lki = NULL;
	sm->lrx = FALSE;
	sm->ltx = FALSE;
	sm->oki = NULL;
	sm->orx = FALSE;
	sm->otx = FALSE;

	sm->current_cipher_suite = default_cs_id;
	sm->cipher_suite = default_cs_id;
	sm->cipher_offset = CONFIDENTIALITY_OFFSET_0;
	sm->confidentiality_offset = sm->cipher_offset;
	sm->transmit_delay = MKA_LIFE_TIME;
	sm->retire_delay = MKA_SAK_RETIRE_TIME;
	sm->CP_state = CP_BEGIN;
	sm->changed = FALSE;
	sm->authorization_data = NULL;

	wpa_printf(MSG_DEBUG, "CP: state machine created");

	secy_cp_control_protect_frames(sm->kay, sm->protect_frames);
	secy_cp_control_encrypt(sm->kay, sm->kay->macsec_encrypt);
	secy_cp_control_validate_frames(sm->kay, sm->validate_frames);
	secy_cp_control_replay(sm->kay, sm->replay_protect, sm->replay_window);
	secy_cp_control_enable_port(sm->kay, sm->controlled_port_enabled);
	secy_cp_control_confidentiality_offset(sm->kay,
					       sm->confidentiality_offset);

	SM_ENTER(CP, INIT);
	SM_STEP_RUN(CP);

	return sm;
}


static void ieee802_1x_cp_step_run(struct ieee802_1x_cp_sm *sm)
{
	enum cp_states prev_state;
	int i;

	for (i = 0; i < 100; i++) {
		prev_state = sm->CP_state;
		SM_STEP_RUN(CP);
		if (prev_state == sm->CP_state)
			break;
	}
}


static void ieee802_1x_cp_step_cb(void *eloop_ctx, void *timeout_ctx)
{
	struct ieee802_1x_cp_sm *sm = eloop_ctx;
	ieee802_1x_cp_step_run(sm);
}


/**
 * ieee802_1x_cp_sm_deinit -
 */
void ieee802_1x_cp_sm_deinit(struct ieee802_1x_cp_sm *sm)
{
	wpa_printf(MSG_DEBUG, "CP: state machine removed");
	if (!sm)
		return;

	eloop_cancel_timeout(ieee802_1x_cp_retire_when_timeout, sm, NULL);
	eloop_cancel_timeout(ieee802_1x_cp_transmit_when_timeout, sm, NULL);
	eloop_cancel_timeout(ieee802_1x_cp_step_cb, sm, NULL);
	os_free(sm->lki);
	os_free(sm->oki);
	os_free(sm->authorization_data);
	os_free(sm);
}


/**
 * ieee802_1x_cp_connect_pending
 */
void ieee802_1x_cp_connect_pending(void *cp_ctx)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;

	sm->connect = PENDING;
}


/**
 * ieee802_1x_cp_connect_unauthenticated
 */
void ieee802_1x_cp_connect_unauthenticated(void *cp_ctx)
{
	struct ieee802_1x_cp_sm *sm = (struct ieee802_1x_cp_sm *)cp_ctx;

	sm->connect = UNAUTHENTICATED;
}


/**
 * ieee802_1x_cp_connect_authenticated
 */
void ieee802_1x_cp_connect_authenticated(void *cp_ctx)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;

	sm->connect = AUTHENTICATED;
}


/**
 * ieee802_1x_cp_connect_secure
 */
void ieee802_1x_cp_connect_secure(void *cp_ctx)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;

	sm->connect = SECURE;
}


/**
 * ieee802_1x_cp_set_chgdserver -
 */
void ieee802_1x_cp_signal_chgdserver(void *cp_ctx)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;

	sm->chgd_server = TRUE;
}


/**
 * ieee802_1x_cp_set_electedself -
 */
void ieee802_1x_cp_set_electedself(void *cp_ctx, Boolean status)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->elected_self = status;
}


/**
 * ieee802_1x_cp_set_authorizationdata -
 */
void ieee802_1x_cp_set_authorizationdata(void *cp_ctx, u8 *pdata, int len)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	os_free(sm->authorization_data);
	sm->authorization_data = os_zalloc(len);
	if (sm->authorization_data)
		os_memcpy(sm->authorization_data, pdata, len);
}


/**
 * ieee802_1x_cp_set_ciphersuite -
 */
void ieee802_1x_cp_set_ciphersuite(void *cp_ctx, u64 cs)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->cipher_suite = cs;
}


/**
 * ieee802_1x_cp_set_offset -
 */
void ieee802_1x_cp_set_offset(void *cp_ctx, enum confidentiality_offset offset)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->cipher_offset = offset;
}


/**
 * ieee802_1x_cp_signal_newsak -
 */
void ieee802_1x_cp_signal_newsak(void *cp_ctx)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->new_sak = TRUE;
}


/**
 * ieee802_1x_cp_set_distributedki -
 */
void ieee802_1x_cp_set_distributedki(void *cp_ctx,
				     const struct ieee802_1x_mka_ki *dki)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	os_memcpy(&sm->distributed_ki, dki, sizeof(struct ieee802_1x_mka_ki));
}


/**
 * ieee802_1x_cp_set_distributedan -
 */
void ieee802_1x_cp_set_distributedan(void *cp_ctx, u8 an)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->distributed_an = an;
}


/**
 * ieee802_1x_cp_set_usingreceivesas -
 */
void ieee802_1x_cp_set_usingreceivesas(void *cp_ctx, Boolean status)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->using_receive_sas = status;
}


/**
 * ieee802_1x_cp_set_allreceiving -
 */
void ieee802_1x_cp_set_allreceiving(void *cp_ctx, Boolean status)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->all_receiving = status;
}


/**
 * ieee802_1x_cp_set_servertransmitting -
 */
void ieee802_1x_cp_set_servertransmitting(void *cp_ctx, Boolean status)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->server_transmitting = status;
}


/**
 * ieee802_1x_cp_set_usingtransmitsas -
 */
void ieee802_1x_cp_set_usingtransmitas(void *cp_ctx, Boolean status)
{
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	sm->using_transmit_sa = status;
}


/**
 * ieee802_1x_cp_sm_step - Advance EAPOL state machines
 * @sm: EAPOL state machine
 *
 * This function is called to advance CP state machines after any change
 * that could affect their state.
 */
void ieee802_1x_cp_sm_step(void *cp_ctx)
{
	/*
	 * Run ieee802_1x_cp_step_run from a registered timeout
	 * to make sure that other possible timeouts/events are processed
	 * and to avoid long function call chains.
	 */
	struct ieee802_1x_cp_sm *sm = cp_ctx;
	eloop_cancel_timeout(ieee802_1x_cp_step_cb, sm, NULL);
	eloop_register_timeout(0, 0, ieee802_1x_cp_step_cb, sm, NULL);
}


static void ieee802_1x_cp_retire_when_timeout(void *eloop_ctx,
					      void *timeout_ctx)
{
	struct ieee802_1x_cp_sm *sm = eloop_ctx;
	sm->retire_when = 0;
	ieee802_1x_cp_step_run(sm);
}


static void
ieee802_1x_cp_transmit_when_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct ieee802_1x_cp_sm *sm = eloop_ctx;
	sm->transmit_when = 0;
	ieee802_1x_cp_step_run(sm);
}
