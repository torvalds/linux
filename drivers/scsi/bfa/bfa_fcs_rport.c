// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
 */

/*
 *  rport.c Remote port implementation.
 */

#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfa_fcs.h"
#include "bfa_fcbuild.h"

BFA_TRC_FILE(FCS, RPORT);

static u32
bfa_fcs_rport_del_timeout = BFA_FCS_RPORT_DEF_DEL_TIMEOUT * 1000;
	 /* In millisecs */
/*
 * bfa_fcs_rport_max_logins is max count of bfa_fcs_rports
 * whereas DEF_CFG_NUM_RPORTS is max count of bfa_rports
 */
static u32 bfa_fcs_rport_max_logins = BFA_FCS_MAX_RPORT_LOGINS;

/*
 * forward declarations
 */
static struct bfa_fcs_rport_s *bfa_fcs_rport_alloc(
		struct bfa_fcs_lport_s *port, wwn_t pwwn, u32 rpid);
static void	bfa_fcs_rport_free(struct bfa_fcs_rport_s *rport);
static void	bfa_fcs_rport_hal_online(struct bfa_fcs_rport_s *rport);
static void	bfa_fcs_rport_fcs_online_action(struct bfa_fcs_rport_s *rport);
static void	bfa_fcs_rport_hal_online_action(struct bfa_fcs_rport_s *rport);
static void	bfa_fcs_rport_fcs_offline_action(struct bfa_fcs_rport_s *rport);
static void	bfa_fcs_rport_hal_offline_action(struct bfa_fcs_rport_s *rport);
static void	bfa_fcs_rport_update(struct bfa_fcs_rport_s *rport,
					struct fc_logi_s *plogi);
static void	bfa_fcs_rport_timeout(void *arg);
static void	bfa_fcs_rport_send_plogi(void *rport_cbarg,
					 struct bfa_fcxp_s *fcxp_alloced);
static void	bfa_fcs_rport_send_plogiacc(void *rport_cbarg,
					struct bfa_fcxp_s *fcxp_alloced);
static void	bfa_fcs_rport_plogi_response(void *fcsarg,
				struct bfa_fcxp_s *fcxp, void *cbarg,
				bfa_status_t req_status, u32 rsp_len,
				u32 resid_len, struct fchs_s *rsp_fchs);
static void	bfa_fcs_rport_send_adisc(void *rport_cbarg,
					 struct bfa_fcxp_s *fcxp_alloced);
static void	bfa_fcs_rport_adisc_response(void *fcsarg,
				struct bfa_fcxp_s *fcxp, void *cbarg,
				bfa_status_t req_status, u32 rsp_len,
				u32 resid_len, struct fchs_s *rsp_fchs);
static void	bfa_fcs_rport_send_nsdisc(void *rport_cbarg,
					 struct bfa_fcxp_s *fcxp_alloced);
static void	bfa_fcs_rport_gidpn_response(void *fcsarg,
				struct bfa_fcxp_s *fcxp, void *cbarg,
				bfa_status_t req_status, u32 rsp_len,
				u32 resid_len, struct fchs_s *rsp_fchs);
static void	bfa_fcs_rport_gpnid_response(void *fcsarg,
				struct bfa_fcxp_s *fcxp, void *cbarg,
				bfa_status_t req_status, u32 rsp_len,
				u32 resid_len, struct fchs_s *rsp_fchs);
static void	bfa_fcs_rport_send_logo(void *rport_cbarg,
					struct bfa_fcxp_s *fcxp_alloced);
static void	bfa_fcs_rport_send_logo_acc(void *rport_cbarg);
static void	bfa_fcs_rport_process_prli(struct bfa_fcs_rport_s *rport,
					struct fchs_s *rx_fchs, u16 len);
static void	bfa_fcs_rport_send_ls_rjt(struct bfa_fcs_rport_s *rport,
				struct fchs_s *rx_fchs, u8 reason_code,
					  u8 reason_code_expl);
static void	bfa_fcs_rport_process_adisc(struct bfa_fcs_rport_s *rport,
				struct fchs_s *rx_fchs, u16 len);
static void bfa_fcs_rport_send_prlo_acc(struct bfa_fcs_rport_s *rport);
static void	bfa_fcs_rport_hal_offline(struct bfa_fcs_rport_s *rport);

static void	bfa_fcs_rport_sm_uninit(struct bfa_fcs_rport_s *rport,
					enum rport_event event);
static void	bfa_fcs_rport_sm_plogi_sending(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_plogiacc_sending(struct bfa_fcs_rport_s *rport,
						  enum rport_event event);
static void	bfa_fcs_rport_sm_plogi_retry(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_plogi(struct bfa_fcs_rport_s *rport,
					enum rport_event event);
static void	bfa_fcs_rport_sm_fc4_fcs_online(struct bfa_fcs_rport_s *rport,
					enum rport_event event);
static void	bfa_fcs_rport_sm_hal_online(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_online(struct bfa_fcs_rport_s *rport,
					enum rport_event event);
static void	bfa_fcs_rport_sm_nsquery_sending(struct bfa_fcs_rport_s *rport,
						 enum rport_event event);
static void	bfa_fcs_rport_sm_nsquery(struct bfa_fcs_rport_s *rport,
					 enum rport_event event);
static void	bfa_fcs_rport_sm_adisc_online_sending(
			struct bfa_fcs_rport_s *rport, enum rport_event event);
static void	bfa_fcs_rport_sm_adisc_online(struct bfa_fcs_rport_s *rport,
					enum rport_event event);
static void	bfa_fcs_rport_sm_adisc_offline_sending(struct bfa_fcs_rport_s
					*rport, enum rport_event event);
static void	bfa_fcs_rport_sm_adisc_offline(struct bfa_fcs_rport_s *rport,
					enum rport_event event);
static void	bfa_fcs_rport_sm_fc4_logorcv(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_fc4_logosend(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_fc4_offline(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_hcb_offline(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_hcb_logorcv(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_hcb_logosend(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_logo_sending(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_offline(struct bfa_fcs_rport_s *rport,
					 enum rport_event event);
static void	bfa_fcs_rport_sm_nsdisc_sending(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_nsdisc_retry(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_nsdisc_sent(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_nsdisc_sent(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_fc4_off_delete(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void	bfa_fcs_rport_sm_delete_pending(struct bfa_fcs_rport_s *rport,
						enum rport_event event);

struct bfa_fcs_rport_sm_table_s {
	bfa_fcs_rport_sm_t sm;		/*  state machine function	*/
	enum bfa_rport_state state;	/*  state machine encoding	*/
	char		*name;		/*  state name for display	*/
};

static inline enum bfa_rport_state
bfa_rport_sm_to_state(struct bfa_fcs_rport_sm_table_s *smt, bfa_fcs_rport_sm_t sm)
{
	int i = 0;

	while (smt[i].sm && smt[i].sm != sm)
		i++;
	return smt[i].state;
}

static struct bfa_fcs_rport_sm_table_s rport_sm_table[] = {
	{BFA_SM(bfa_fcs_rport_sm_uninit), BFA_RPORT_UNINIT},
	{BFA_SM(bfa_fcs_rport_sm_plogi_sending), BFA_RPORT_PLOGI},
	{BFA_SM(bfa_fcs_rport_sm_plogiacc_sending), BFA_RPORT_ONLINE},
	{BFA_SM(bfa_fcs_rport_sm_plogi_retry), BFA_RPORT_PLOGI_RETRY},
	{BFA_SM(bfa_fcs_rport_sm_plogi), BFA_RPORT_PLOGI},
	{BFA_SM(bfa_fcs_rport_sm_fc4_fcs_online), BFA_RPORT_ONLINE},
	{BFA_SM(bfa_fcs_rport_sm_hal_online), BFA_RPORT_ONLINE},
	{BFA_SM(bfa_fcs_rport_sm_online), BFA_RPORT_ONLINE},
	{BFA_SM(bfa_fcs_rport_sm_nsquery_sending), BFA_RPORT_NSQUERY},
	{BFA_SM(bfa_fcs_rport_sm_nsquery), BFA_RPORT_NSQUERY},
	{BFA_SM(bfa_fcs_rport_sm_adisc_online_sending), BFA_RPORT_ADISC},
	{BFA_SM(bfa_fcs_rport_sm_adisc_online), BFA_RPORT_ADISC},
	{BFA_SM(bfa_fcs_rport_sm_adisc_offline_sending), BFA_RPORT_ADISC},
	{BFA_SM(bfa_fcs_rport_sm_adisc_offline), BFA_RPORT_ADISC},
	{BFA_SM(bfa_fcs_rport_sm_fc4_logorcv), BFA_RPORT_LOGORCV},
	{BFA_SM(bfa_fcs_rport_sm_fc4_logosend), BFA_RPORT_LOGO},
	{BFA_SM(bfa_fcs_rport_sm_fc4_offline), BFA_RPORT_OFFLINE},
	{BFA_SM(bfa_fcs_rport_sm_hcb_offline), BFA_RPORT_OFFLINE},
	{BFA_SM(bfa_fcs_rport_sm_hcb_logorcv), BFA_RPORT_LOGORCV},
	{BFA_SM(bfa_fcs_rport_sm_hcb_logosend), BFA_RPORT_LOGO},
	{BFA_SM(bfa_fcs_rport_sm_logo_sending), BFA_RPORT_LOGO},
	{BFA_SM(bfa_fcs_rport_sm_offline), BFA_RPORT_OFFLINE},
	{BFA_SM(bfa_fcs_rport_sm_nsdisc_sending), BFA_RPORT_NSDISC},
	{BFA_SM(bfa_fcs_rport_sm_nsdisc_retry), BFA_RPORT_NSDISC},
	{BFA_SM(bfa_fcs_rport_sm_nsdisc_sent), BFA_RPORT_NSDISC},
};

/*
 *		Beginning state.
 */
static void
bfa_fcs_rport_sm_uninit(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_PLOGI_SEND:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_sending);
		rport->plogi_retries = 0;
		bfa_fcs_rport_send_plogi(rport, NULL);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_fcs_rport_hal_online(rport);
		break;

	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_ADDRESS_DISC:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_nsdisc(rport, NULL);
		break;
	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		PLOGI is being sent.
 */
static void
bfa_fcs_rport_sm_plogi_sending(struct bfa_fcs_rport_s *rport,
	 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_SCN_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;
	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_FAB_SCN:
		/* query the NS */
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		WARN_ON(!(bfa_fcport_get_topology(rport->port->fcs->bfa) !=
					BFA_PORT_TOPOLOGY_LOOP));
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_nsdisc(rport, NULL);
		break;

	case RPSM_EVENT_LOGO_IMP:
		rport->pid = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;


	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		PLOGI is being sent.
 */
static void
bfa_fcs_rport_sm_plogiacc_sending(struct bfa_fcs_rport_s *rport,
	 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_fcs_online);
		bfa_fcs_rport_fcs_online_action(rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
	case RPSM_EVENT_PLOGI_COMP:
	case RPSM_EVENT_FAB_SCN:
		/*
		 * Ignore, SCN is possibly online notification.
		 */
		break;

	case RPSM_EVENT_SCN_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_nsdisc(rport, NULL);
		break;

	case RPSM_EVENT_LOGO_IMP:
		rport->pid = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_HCB_OFFLINE:
		/*
		 * Ignore BFA callback, on a PLOGI receive we call bfa offline.
		 */
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		PLOGI is sent.
 */
static void
bfa_fcs_rport_sm_plogi_retry(struct bfa_fcs_rport_s *rport,
			enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_TIMEOUT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_sending);
		bfa_fcs_rport_send_plogi(rport, NULL);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PRLO_RCVD:
	case RPSM_EVENT_LOGO_RCVD:
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_SCN_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_timer_stop(&rport->timer);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_FAB_SCN:
		bfa_timer_stop(&rport->timer);
		WARN_ON(!(bfa_fcport_get_topology(rport->port->fcs->bfa) !=
					BFA_PORT_TOPOLOGY_LOOP));
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_nsdisc(rport, NULL);
		break;

	case RPSM_EVENT_LOGO_IMP:
		rport->pid = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_timer_stop(&rport->timer);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_fcs_online);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_fcs_online_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		PLOGI is sent.
 */
static void
bfa_fcs_rport_sm_plogi(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_ACCEPTED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_fcs_online);
		rport->plogi_retries = 0;
		bfa_fcs_rport_fcs_online_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_fcs_rport_send_logo_acc(rport);
		fallthrough;
	case RPSM_EVENT_PRLO_RCVD:
		if (rport->prlo == BFA_TRUE)
			bfa_fcs_rport_send_prlo_acc(rport);

		bfa_fcxp_discard(rport->fcxp);
		fallthrough;
	case RPSM_EVENT_FAILED:
		if (rport->plogi_retries < BFA_FCS_RPORT_MAX_RETRIES) {
			rport->plogi_retries++;
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_retry);
			bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					BFA_FCS_RETRY_TIMEOUT);
		} else {
			bfa_stats(rport->port, rport_del_max_plogi_retry);
			rport->old_pid = rport->pid;
			rport->pid = 0;
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
			bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					bfa_fcs_rport_del_timeout);
		}
		break;

	case RPSM_EVENT_SCN_ONLINE:
		break;

	case RPSM_EVENT_SCN_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_discard(rport->fcxp);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_PLOGI_RETRY:
		rport->plogi_retries = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_retry);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				(FC_RA_TOV * 1000));
		break;

	case RPSM_EVENT_LOGO_IMP:
		rport->pid = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_discard(rport->fcxp);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_FAB_SCN:
		bfa_fcxp_discard(rport->fcxp);
		WARN_ON(!(bfa_fcport_get_topology(rport->port->fcs->bfa) !=
					BFA_PORT_TOPOLOGY_LOOP));
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_nsdisc(rport, NULL);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_fcs_online);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_fcs_online_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 * PLOGI is done. Await bfa_fcs_itnim to ascertain the scsi function
 */
static void
bfa_fcs_rport_sm_fc4_fcs_online(struct bfa_fcs_rport_s *rport,
				enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FC4_FCS_ONLINE:
		if (rport->scsi_function == BFA_RPORT_INITIATOR) {
			if (!BFA_FCS_PID_IS_WKA(rport->pid))
				bfa_fcs_rpf_rport_online(rport);
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_online);
			break;
		}

		if (!rport->bfa_rport)
			rport->bfa_rport =
				bfa_rport_create(rport->fcs->bfa, rport);

		if (rport->bfa_rport) {
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
			bfa_fcs_rport_hal_online(rport);
		} else {
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
			bfa_fcs_rport_fcs_offline_action(rport);
		}
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		rport->plogi_pending = BFA_TRUE;
		bfa_fcs_rport_fcs_offline_action(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_FAB_SCN:
	case RPSM_EVENT_SCN_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcs_rport_fcs_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcs_rport_fcs_offline_action(rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcs_rport_fcs_offline_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
		break;
	}
}

/*
 *		PLOGI is complete. Awaiting BFA rport online callback. FC-4s
 *		are offline.
 */
static void
bfa_fcs_rport_sm_hal_online(struct bfa_fcs_rport_s *rport,
			enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_HCB_ONLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_online);
		bfa_fcs_rport_hal_online_action(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		break;

	case RPSM_EVENT_PRLO_RCVD:
	case RPSM_EVENT_LOGO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcs_rport_fcs_offline_action(rport);
		break;

	case RPSM_EVENT_FAB_SCN:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_SCN_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcs_rport_fcs_offline_action(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		rport->plogi_pending = BFA_TRUE;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcs_rport_fcs_offline_action(rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcs_rport_fcs_offline_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		Rport is ONLINE. FC-4s active.
 */
static void
bfa_fcs_rport_sm_online(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FAB_SCN:
		if (bfa_fcs_fabric_is_switched(rport->port->fabric)) {
			bfa_sm_set_state(rport,
					 bfa_fcs_rport_sm_nsquery_sending);
			rport->ns_retries = 0;
			bfa_fcs_rport_send_nsdisc(rport, NULL);
		} else {
			bfa_sm_set_state(rport,
				bfa_fcs_rport_sm_adisc_online_sending);
			bfa_fcs_rport_send_adisc(rport, NULL);
		}
		break;

	case RPSM_EVENT_PLOGI_RCVD:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_SCN_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_SCN_ONLINE:
	case RPSM_EVENT_PLOGI_COMP:
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		An SCN event is received in ONLINE state. NS query is being sent
 *		prior to ADISC authentication with rport. FC-4s are paused.
 */
static void
bfa_fcs_rport_sm_nsquery_sending(struct bfa_fcs_rport_s *rport,
	 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsquery);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_FAB_SCN:
		/*
		 * ignore SCN, wait for response to query itself
		 */
		break;

	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_PLOGI_RCVD:
	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *	An SCN event is received in ONLINE state. NS query is sent to rport.
 *	FC-4s are paused.
 */
static void
bfa_fcs_rport_sm_nsquery(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_ACCEPTED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_adisc_online_sending);
		bfa_fcs_rport_send_adisc(rport, NULL);
		break;

	case RPSM_EVENT_FAILED:
		rport->ns_retries++;
		if (rport->ns_retries < BFA_FCS_RPORT_MAX_RETRIES) {
			bfa_sm_set_state(rport,
					 bfa_fcs_rport_sm_nsquery_sending);
			bfa_fcs_rport_send_nsdisc(rport, NULL);
		} else {
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
			bfa_fcs_rport_hal_offline_action(rport);
		}
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_FAB_SCN:
		break;

	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_PLOGI_RCVD:
	case RPSM_EVENT_LOGO_IMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *	An SCN event is received in ONLINE state. ADISC is being sent for
 *	authenticating with rport. FC-4s are paused.
 */
static void
bfa_fcs_rport_sm_adisc_online_sending(struct bfa_fcs_rport_s *rport,
	 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_adisc_online);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_FAB_SCN:
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		An SCN event is received in ONLINE state. ADISC is to rport.
 *		FC-4s are paused.
 */
static void
bfa_fcs_rport_sm_adisc_online(struct bfa_fcs_rport_s *rport,
				enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_ACCEPTED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_online);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		/*
		 * Too complex to cleanup FC-4 & rport and then acc to PLOGI.
		 * At least go offline when a PLOGI is received.
		 */
		bfa_fcxp_discard(rport->fcxp);
		fallthrough;

	case RPSM_EVENT_FAILED:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_FAB_SCN:
		/*
		 * already processing RSCN
		 */
		break;

	case RPSM_EVENT_LOGO_IMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_hal_offline_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 * ADISC is being sent for authenticating with rport
 * Already did offline actions.
 */
static void
bfa_fcs_rport_sm_adisc_offline_sending(struct bfa_fcs_rport_s *rport,
	enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_adisc_offline);
		break;

	case RPSM_EVENT_DELETE:
	case RPSM_EVENT_SCN_OFFLINE:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa,
			&rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
			bfa_fcs_rport_timeout, rport,
			bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 * ADISC to rport
 * Already did offline actions
 */
static void
bfa_fcs_rport_sm_adisc_offline(struct bfa_fcs_rport_s *rport,
			enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_ACCEPTED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_fcs_rport_hal_online(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_FAILED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
			bfa_fcs_rport_timeout, rport,
			bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_DELETE:
	case RPSM_EVENT_SCN_OFFLINE:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_discard(rport->fcxp);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
			bfa_fcs_rport_timeout, rport,
			bfa_fcs_rport_del_timeout);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 * Rport has sent LOGO. Awaiting FC-4 offline completion callback.
 */
static void
bfa_fcs_rport_sm_fc4_logorcv(struct bfa_fcs_rport_s *rport,
			enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FC4_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_logorcv);
		bfa_fcs_rport_hal_offline(rport);
		break;

	case RPSM_EVENT_DELETE:
		if (rport->pid && (rport->prlo == BFA_TRUE))
			bfa_fcs_rport_send_prlo_acc(rport);
		if (rport->pid && (rport->prlo == BFA_FALSE))
			bfa_fcs_rport_send_logo_acc(rport);

		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_off_delete);
		break;

	case RPSM_EVENT_SCN_ONLINE:
	case RPSM_EVENT_SCN_OFFLINE:
	case RPSM_EVENT_HCB_ONLINE:
	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
	case RPSM_EVENT_ADDRESS_CHANGE:
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		LOGO needs to be sent to rport. Awaiting FC-4 offline completion
 *		callback.
 */
static void
bfa_fcs_rport_sm_fc4_logosend(struct bfa_fcs_rport_s *rport,
	 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FC4_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_logosend);
		bfa_fcs_rport_hal_offline(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_fcs_rport_send_logo_acc(rport);
		fallthrough;
	case RPSM_EVENT_PRLO_RCVD:
		if (rport->prlo == BFA_TRUE)
			bfa_fcs_rport_send_prlo_acc(rport);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_off_delete);
		break;

	case RPSM_EVENT_HCB_ONLINE:
	case RPSM_EVENT_DELETE:
		/* Rport is being deleted */
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *	Rport is going offline. Awaiting FC-4 offline completion callback.
 */
static void
bfa_fcs_rport_sm_fc4_offline(struct bfa_fcs_rport_s *rport,
			enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FC4_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_offline);
		bfa_fcs_rport_hal_offline(rport);
		break;

	case RPSM_EVENT_SCN_ONLINE:
		break;
	case RPSM_EVENT_LOGO_RCVD:
		/*
		 * Rport is going offline. Just ack the logo
		 */
		bfa_fcs_rport_send_logo_acc(rport);
		break;

	case RPSM_EVENT_PRLO_RCVD:
		bfa_fcs_rport_send_prlo_acc(rport);
		break;

	case RPSM_EVENT_SCN_OFFLINE:
	case RPSM_EVENT_HCB_ONLINE:
	case RPSM_EVENT_FAB_SCN:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
		/*
		 * rport is already going offline.
		 * SCN - ignore and wait till transitioning to offline state
		 */
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		Rport is offline. FC-4s are offline. Awaiting BFA rport offline
 *		callback.
 */
static void
bfa_fcs_rport_sm_hcb_offline(struct bfa_fcs_rport_s *rport,
				enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_HCB_OFFLINE:
		if (bfa_fcs_lport_is_online(rport->port) &&
		    (rport->plogi_pending)) {
			rport->plogi_pending = BFA_FALSE;
			bfa_sm_set_state(rport,
				bfa_fcs_rport_sm_plogiacc_sending);
			bfa_fcs_rport_send_plogiacc(rport, NULL);
			break;
		}
		fallthrough;

	case RPSM_EVENT_ADDRESS_CHANGE:
		if (!bfa_fcs_lport_is_online(rport->port)) {
			rport->pid = 0;
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
			bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					bfa_fcs_rport_del_timeout);
			break;
		}
		if (bfa_fcs_fabric_is_switched(rport->port->fabric)) {
			bfa_sm_set_state(rport,
				bfa_fcs_rport_sm_nsdisc_sending);
			rport->ns_retries = 0;
			bfa_fcs_rport_send_nsdisc(rport, NULL);
		} else if (bfa_fcport_get_topology(rport->port->fcs->bfa) ==
					BFA_PORT_TOPOLOGY_LOOP) {
			if (rport->scn_online) {
				bfa_sm_set_state(rport,
					bfa_fcs_rport_sm_adisc_offline_sending);
				bfa_fcs_rport_send_adisc(rport, NULL);
			} else {
				bfa_sm_set_state(rport,
					bfa_fcs_rport_sm_offline);
				bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					bfa_fcs_rport_del_timeout);
			}
		} else {
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_sending);
			rport->plogi_retries = 0;
			bfa_fcs_rport_send_plogi(rport, NULL);
		}
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_SCN_ONLINE:
	case RPSM_EVENT_SCN_OFFLINE:
	case RPSM_EVENT_FAB_SCN:
	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
	case RPSM_EVENT_PLOGI_RCVD:
	case RPSM_EVENT_LOGO_IMP:
		/*
		 * Ignore, already offline.
		 */
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		Rport is offline. FC-4s are offline. Awaiting BFA rport offline
 *		callback to send LOGO accept.
 */
static void
bfa_fcs_rport_sm_hcb_logorcv(struct bfa_fcs_rport_s *rport,
			enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_HCB_OFFLINE:
	case RPSM_EVENT_ADDRESS_CHANGE:
		if (rport->pid && (rport->prlo == BFA_TRUE))
			bfa_fcs_rport_send_prlo_acc(rport);
		if (rport->pid && (rport->prlo == BFA_FALSE))
			bfa_fcs_rport_send_logo_acc(rport);
		/*
		 * If the lport is online and if the rport is not a well
		 * known address port,
		 * we try to re-discover the r-port.
		 */
		if (bfa_fcs_lport_is_online(rport->port) &&
			(!BFA_FCS_PID_IS_WKA(rport->pid))) {
			if (bfa_fcs_fabric_is_switched(rport->port->fabric)) {
				bfa_sm_set_state(rport,
					bfa_fcs_rport_sm_nsdisc_sending);
				rport->ns_retries = 0;
				bfa_fcs_rport_send_nsdisc(rport, NULL);
			} else {
				/* For N2N  Direct Attach, try to re-login */
				bfa_sm_set_state(rport,
					bfa_fcs_rport_sm_plogi_sending);
				rport->plogi_retries = 0;
				bfa_fcs_rport_send_plogi(rport, NULL);
			}
		} else {
			/*
			 * if it is not a well known address, reset the
			 * pid to 0.
			 */
			if (!BFA_FCS_PID_IS_WKA(rport->pid))
				rport->pid = 0;
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
			bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					bfa_fcs_rport_del_timeout);
		}
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_delete_pending);
		if (rport->pid && (rport->prlo == BFA_TRUE))
			bfa_fcs_rport_send_prlo_acc(rport);
		if (rport->pid && (rport->prlo == BFA_FALSE))
			bfa_fcs_rport_send_logo_acc(rport);
		break;

	case RPSM_EVENT_LOGO_IMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_offline);
		break;

	case RPSM_EVENT_SCN_ONLINE:
	case RPSM_EVENT_SCN_OFFLINE:
	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
		/*
		 * Ignore - already processing a LOGO.
		 */
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		Rport is being deleted. FC-4s are offline.
 *  Awaiting BFA rport offline
 *		callback to send LOGO.
 */
static void
bfa_fcs_rport_sm_hcb_logosend(struct bfa_fcs_rport_s *rport,
		 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_HCB_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_logo_sending);
		bfa_fcs_rport_send_logo(rport, NULL);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_fcs_rport_send_logo_acc(rport);
		fallthrough;
	case RPSM_EVENT_PRLO_RCVD:
		if (rport->prlo == BFA_TRUE)
			bfa_fcs_rport_send_prlo_acc(rport);

		bfa_sm_set_state(rport, bfa_fcs_rport_sm_delete_pending);
		break;

	case RPSM_EVENT_SCN_ONLINE:
	case RPSM_EVENT_SCN_OFFLINE:
	case RPSM_EVENT_ADDRESS_CHANGE:
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		Rport is being deleted. FC-4s are offline. LOGO is being sent.
 */
static void
bfa_fcs_rport_sm_logo_sending(struct bfa_fcs_rport_s *rport,
	 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FCXP_SENT:
		/* Once LOGO is sent, we donot wait for the response */
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_SCN_ONLINE:
	case RPSM_EVENT_SCN_OFFLINE:
	case RPSM_EVENT_FAB_SCN:
	case RPSM_EVENT_ADDRESS_CHANGE:
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_fcs_rport_send_logo_acc(rport);
		fallthrough;
	case RPSM_EVENT_PRLO_RCVD:
		if (rport->prlo == BFA_TRUE)
			bfa_fcs_rport_send_prlo_acc(rport);

		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_free(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		Rport is offline. FC-4s are offline. BFA rport is offline.
 *		Timer active to delete stale rport.
 */
static void
bfa_fcs_rport_sm_offline(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_TIMEOUT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_FAB_SCN:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_timer_stop(&rport->timer);
		WARN_ON(!(bfa_fcport_get_topology(rport->port->fcs->bfa) !=
					BFA_PORT_TOPOLOGY_LOOP));
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_nsdisc(rport, NULL);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_SCN_OFFLINE:
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_fcs_online);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_fcs_online_action(rport);
		break;

	case RPSM_EVENT_SCN_ONLINE:
		bfa_timer_stop(&rport->timer);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_sending);
		bfa_fcs_rport_send_plogi(rport, NULL);
		break;

	case RPSM_EVENT_PLOGI_SEND:
		bfa_timer_stop(&rport->timer);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_sending);
		rport->plogi_retries = 0;
		bfa_fcs_rport_send_plogi(rport, NULL);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *	Rport address has changed. Nameserver discovery request is being sent.
 */
static void
bfa_fcs_rport_sm_nsdisc_sending(struct bfa_fcs_rport_s *rport,
	 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sent);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_FAB_SCN:
	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PRLO_RCVD:
	case RPSM_EVENT_PLOGI_SEND:
		break;

	case RPSM_EVENT_ADDRESS_CHANGE:
		rport->ns_retries = 0; /* reset the retry count */
		break;

	case RPSM_EVENT_LOGO_IMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_fcs_online);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_fcs_online_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		Nameserver discovery failed. Waiting for timeout to retry.
 */
static void
bfa_fcs_rport_sm_nsdisc_retry(struct bfa_fcs_rport_s *rport,
	 enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_TIMEOUT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		bfa_fcs_rport_send_nsdisc(rport, NULL);
		break;

	case RPSM_EVENT_FAB_SCN:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		bfa_timer_stop(&rport->timer);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_nsdisc(rport, NULL);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_LOGO_IMP:
		rport->pid = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_timer_stop(&rport->timer);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_fcs_rport_send_logo_acc(rport);
		break;
	case RPSM_EVENT_PRLO_RCVD:
		bfa_fcs_rport_send_prlo_acc(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_fcs_online);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_fcs_online_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *		Rport address has changed. Nameserver discovery request is sent.
 */
static void
bfa_fcs_rport_sm_nsdisc_sent(struct bfa_fcs_rport_s *rport,
			enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_ACCEPTED:
	case RPSM_EVENT_ADDRESS_CHANGE:
		if (rport->pid) {
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_sending);
			bfa_fcs_rport_send_plogi(rport, NULL);
		} else {
			bfa_sm_set_state(rport,
				 bfa_fcs_rport_sm_nsdisc_sending);
			rport->ns_retries = 0;
			bfa_fcs_rport_send_nsdisc(rport, NULL);
		}
		break;

	case RPSM_EVENT_FAILED:
		rport->ns_retries++;
		if (rport->ns_retries < BFA_FCS_RPORT_MAX_RETRIES) {
			bfa_sm_set_state(rport,
				 bfa_fcs_rport_sm_nsdisc_sending);
			bfa_fcs_rport_send_nsdisc(rport, NULL);
		} else {
			rport->old_pid = rport->pid;
			rport->pid = 0;
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
			bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					bfa_fcs_rport_del_timeout);
		}
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_LOGO_IMP:
		rport->pid = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_discard(rport->fcxp);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;


	case RPSM_EVENT_PRLO_RCVD:
		bfa_fcs_rport_send_prlo_acc(rport);
		break;
	case RPSM_EVENT_FAB_SCN:
		/*
		 * ignore, wait for NS query response
		 */
		break;

	case RPSM_EVENT_LOGO_RCVD:
		/*
		 * Not logged-in yet. Accept LOGO.
		 */
		bfa_fcs_rport_send_logo_acc(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_fcs_online);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_fcs_online_action(rport);
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 * Rport needs to be deleted
 * waiting for ITNIM clean up to finish
 */
static void
bfa_fcs_rport_sm_fc4_off_delete(struct bfa_fcs_rport_s *rport,
				enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FC4_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_delete_pending);
		bfa_fcs_rport_hal_offline(rport);
		break;

	case RPSM_EVENT_DELETE:
	case RPSM_EVENT_PLOGI_RCVD:
		/* Ignore these events */
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
		break;
	}
}

/*
 * RPort needs to be deleted
 * waiting for BFA/FW to finish current processing
 */
static void
bfa_fcs_rport_sm_delete_pending(struct bfa_fcs_rport_s *rport,
				enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_HCB_OFFLINE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_DELETE:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_PLOGI_RCVD:
		/* Ignore these events */
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

/*
 *  fcs_rport_private FCS RPORT provate functions
 */

static void
bfa_fcs_rport_send_plogi(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_lport_s *port = rport->port;
	struct fchs_s	fchs;
	int		len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(rport->fcs, rport->pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				bfa_fcs_rport_send_plogi, rport, BFA_TRUE);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_plogi_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
				bfa_fcs_lport_get_fcid(port), 0,
				port->port_cfg.pwwn, port->port_cfg.nwwn,
				bfa_fcport_get_maxfrsize(port->fcs->bfa),
				bfa_fcport_get_rx_bbcredit(port->fcs->bfa));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			FC_CLASS_3, len, &fchs, bfa_fcs_rport_plogi_response,
			(void *)rport, FC_MAX_PDUSZ, FC_ELS_TOV);

	rport->stats.plogis++;
	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_rport_plogi_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
				bfa_status_t req_status, u32 rsp_len,
				u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) cbarg;
	struct fc_logi_s	*plogi_rsp;
	struct fc_ls_rjt_s	*ls_rjt;
	struct bfa_fcs_rport_s *twin;
	struct list_head	*qe;

	bfa_trc(rport->fcs, rport->pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(rport->fcs, req_status);
		rport->stats.plogi_failed++;
		bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
		return;
	}

	plogi_rsp = (struct fc_logi_s *) BFA_FCXP_RSP_PLD(fcxp);

	/*
	 * Check for failure first.
	 */
	if (plogi_rsp->els_cmd.els_code != FC_ELS_ACC) {
		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(rport->fcs, ls_rjt->reason_code);
		bfa_trc(rport->fcs, ls_rjt->reason_code_expl);

		if ((ls_rjt->reason_code == FC_LS_RJT_RSN_UNABLE_TO_PERF_CMD) &&
		 (ls_rjt->reason_code_expl == FC_LS_RJT_EXP_INSUFF_RES)) {
			rport->stats.rjt_insuff_res++;
			bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_RETRY);
			return;
		}

		rport->stats.plogi_rejects++;
		bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
		return;
	}

	/*
	 * PLOGI is complete. Make sure this device is not one of the known
	 * device with a new FC port address.
	 */
	list_for_each(qe, &rport->port->rport_q) {
		twin = (struct bfa_fcs_rport_s *) qe;
		if (twin == rport)
			continue;
		if (!rport->pwwn && (plogi_rsp->port_name == twin->pwwn)) {
			bfa_trc(rport->fcs, twin->pid);
			bfa_trc(rport->fcs, rport->pid);

			/* Update plogi stats in twin */
			twin->stats.plogis  += rport->stats.plogis;
			twin->stats.plogi_rejects  +=
				 rport->stats.plogi_rejects;
			twin->stats.plogi_timeouts  +=
				 rport->stats.plogi_timeouts;
			twin->stats.plogi_failed +=
				 rport->stats.plogi_failed;
			twin->stats.plogi_rcvd	  += rport->stats.plogi_rcvd;
			twin->stats.plogi_accs++;

			bfa_sm_send_event(rport, RPSM_EVENT_DELETE);

			bfa_fcs_rport_update(twin, plogi_rsp);
			twin->pid = rsp_fchs->s_id;
			bfa_sm_send_event(twin, RPSM_EVENT_PLOGI_COMP);
			return;
		}
	}

	/*
	 * Normal login path -- no evil twins.
	 */
	rport->stats.plogi_accs++;
	bfa_fcs_rport_update(rport, plogi_rsp);
	bfa_sm_send_event(rport, RPSM_EVENT_ACCEPTED);
}

static void
bfa_fcs_rport_send_plogiacc(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_lport_s *port = rport->port;
	struct fchs_s		fchs;
	int		len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->reply_oxid);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				bfa_fcs_rport_send_plogiacc, rport, BFA_FALSE);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_plogi_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				 rport->pid, bfa_fcs_lport_get_fcid(port),
				 rport->reply_oxid, port->port_cfg.pwwn,
				 port->port_cfg.nwwn,
				 bfa_fcport_get_maxfrsize(port->fcs->bfa),
				 bfa_fcport_get_rx_bbcredit(port->fcs->bfa));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);

	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_rport_send_adisc(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_lport_s *port = rport->port;
	struct fchs_s		fchs;
	int		len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(rport->fcs, rport->pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				bfa_fcs_rport_send_adisc, rport, BFA_TRUE);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_adisc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
				bfa_fcs_lport_get_fcid(port), 0,
				port->port_cfg.pwwn, port->port_cfg.nwwn);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			FC_CLASS_3, len, &fchs, bfa_fcs_rport_adisc_response,
			rport, FC_MAX_PDUSZ, FC_ELS_TOV);

	rport->stats.adisc_sent++;
	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_rport_adisc_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
				bfa_status_t req_status, u32 rsp_len,
				u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) cbarg;
	void		*pld = bfa_fcxp_get_rspbuf(fcxp);
	struct fc_ls_rjt_s	*ls_rjt;

	if (req_status != BFA_STATUS_OK) {
		bfa_trc(rport->fcs, req_status);
		rport->stats.adisc_failed++;
		bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
		return;
	}

	if (fc_adisc_rsp_parse((struct fc_adisc_s *)pld, rsp_len, rport->pwwn,
				rport->nwwn)  == FC_PARSE_OK) {
		rport->stats.adisc_accs++;
		bfa_sm_send_event(rport, RPSM_EVENT_ACCEPTED);
		return;
	}

	rport->stats.adisc_rejects++;
	ls_rjt = pld;
	bfa_trc(rport->fcs, ls_rjt->els_cmd.els_code);
	bfa_trc(rport->fcs, ls_rjt->reason_code);
	bfa_trc(rport->fcs, ls_rjt->reason_code_expl);
	bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
}

static void
bfa_fcs_rport_send_nsdisc(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_lport_s *port = rport->port;
	struct fchs_s	fchs;
	struct bfa_fcxp_s *fcxp;
	int		len;
	bfa_cb_fcxp_send_t cbfn;

	bfa_trc(rport->fcs, rport->pid);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				bfa_fcs_rport_send_nsdisc, rport, BFA_TRUE);
		return;
	}
	rport->fcxp = fcxp;

	if (rport->pwwn) {
		len = fc_gidpn_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				bfa_fcs_lport_get_fcid(port), 0, rport->pwwn);
		cbfn = bfa_fcs_rport_gidpn_response;
	} else {
		len = fc_gpnid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				bfa_fcs_lport_get_fcid(port), 0, rport->pid);
		cbfn = bfa_fcs_rport_gpnid_response;
	}

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			FC_CLASS_3, len, &fchs, cbfn,
			(void *)rport, FC_MAX_PDUSZ, FC_FCCT_TOV);

	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_rport_gidpn_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
				bfa_status_t req_status, u32 rsp_len,
				u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) cbarg;
	struct ct_hdr_s	*cthdr;
	struct fcgs_gidpn_resp_s	*gidpn_rsp;
	struct bfa_fcs_rport_s	*twin;
	struct list_head	*qe;

	bfa_trc(rport->fcs, rport->pwwn);

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		/* Check if the pid is the same as before. */
		gidpn_rsp = (struct fcgs_gidpn_resp_s *) (cthdr + 1);

		if (gidpn_rsp->dap == rport->pid) {
			/* Device is online  */
			bfa_sm_send_event(rport, RPSM_EVENT_ACCEPTED);
		} else {
			/*
			 * Device's PID has changed. We need to cleanup
			 * and re-login. If there is another device with
			 * the the newly discovered pid, send an scn notice
			 * so that its new pid can be discovered.
			 */
			list_for_each(qe, &rport->port->rport_q) {
				twin = (struct bfa_fcs_rport_s *) qe;
				if (twin == rport)
					continue;
				if (gidpn_rsp->dap == twin->pid) {
					bfa_trc(rport->fcs, twin->pid);
					bfa_trc(rport->fcs, rport->pid);

					twin->pid = 0;
					bfa_sm_send_event(twin,
					 RPSM_EVENT_ADDRESS_CHANGE);
				}
			}
			rport->pid = gidpn_rsp->dap;
			bfa_sm_send_event(rport, RPSM_EVENT_ADDRESS_CHANGE);
		}
		return;
	}

	/*
	 * Reject Response
	 */
	switch (cthdr->reason_code) {
	case CT_RSN_LOGICAL_BUSY:
		/*
		 * Need to retry
		 */
		bfa_sm_send_event(rport, RPSM_EVENT_TIMEOUT);
		break;

	case CT_RSN_UNABLE_TO_PERF:
		/*
		 * device doesn't exist : Start timer to cleanup this later.
		 */
		bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
		break;

	default:
		bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
		break;
	}
}

static void
bfa_fcs_rport_gpnid_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
				bfa_status_t req_status, u32 rsp_len,
				u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) cbarg;
	struct ct_hdr_s	*cthdr;

	bfa_trc(rport->fcs, rport->pwwn);

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		bfa_sm_send_event(rport, RPSM_EVENT_ACCEPTED);
		return;
	}

	/*
	 * Reject Response
	 */
	switch (cthdr->reason_code) {
	case CT_RSN_LOGICAL_BUSY:
		/*
		 * Need to retry
		 */
		bfa_sm_send_event(rport, RPSM_EVENT_TIMEOUT);
		break;

	case CT_RSN_UNABLE_TO_PERF:
		/*
		 * device doesn't exist : Start timer to cleanup this later.
		 */
		bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
		break;

	default:
		bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
		break;
	}
}

/*
 *	Called to send a logout to the rport.
 */
static void
bfa_fcs_rport_send_logo(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_lport_s *port;
	struct fchs_s	fchs;
	struct bfa_fcxp_s *fcxp;
	u16	len;

	bfa_trc(rport->fcs, rport->pid);

	port = rport->port;

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				bfa_fcs_rport_send_logo, rport, BFA_FALSE);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_logo_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
				bfa_fcs_lport_get_fcid(port), 0,
				bfa_fcs_lport_get_pwwn(port));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			FC_CLASS_3, len, &fchs, NULL,
			rport, FC_MAX_PDUSZ, FC_ELS_TOV);

	rport->stats.logos++;
	bfa_fcxp_discard(rport->fcxp);
	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

/*
 *	Send ACC for a LOGO received.
 */
static void
bfa_fcs_rport_send_logo_acc(void *rport_cbarg)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_lport_s *port;
	struct fchs_s	fchs;
	struct bfa_fcxp_s *fcxp;
	u16	len;

	bfa_trc(rport->fcs, rport->pid);

	port = rport->port;

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	rport->stats.logo_rcvd++;
	len = fc_logo_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				rport->pid, bfa_fcs_lport_get_fcid(port),
				rport->reply_oxid);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);
}

/*
 *	brief
 *	This routine will be called by bfa_timer on timer timeouts.
 *
 *	param[in]	rport			- pointer to bfa_fcs_lport_ns_t.
 *	param[out]	rport_status	- pointer to return vport status in
 *
 *	return
 *		void
 *
 *	Special Considerations:
 *
 *	note
 */
static void
bfa_fcs_rport_timeout(void *arg)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) arg;

	rport->stats.plogi_timeouts++;
	bfa_stats(rport->port, rport_plogi_timeouts);
	bfa_sm_send_event(rport, RPSM_EVENT_TIMEOUT);
}

static void
bfa_fcs_rport_process_prli(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u16 len)
{
	struct bfa_fcxp_s *fcxp;
	struct fchs_s	fchs;
	struct bfa_fcs_lport_s *port = rport->port;
	struct fc_prli_s	*prli;

	bfa_trc(port->fcs, rx_fchs->s_id);
	bfa_trc(port->fcs, rx_fchs->d_id);

	rport->stats.prli_rcvd++;

	/*
	 * We are in Initiator Mode
	 */
	prli = (struct fc_prli_s *) (rx_fchs + 1);

	if (prli->parampage.servparams.target) {
		/*
		 * PRLI from a target ?
		 * Send the Acc.
		 * PRLI sent by us will be used to transition the IT nexus,
		 * once the response is received from the target.
		 */
		bfa_trc(port->fcs, rx_fchs->s_id);
		rport->scsi_function = BFA_RPORT_TARGET;
	} else {
		bfa_trc(rport->fcs, prli->parampage.type);
		rport->scsi_function = BFA_RPORT_INITIATOR;
		bfa_fcs_itnim_is_initiator(rport->itnim);
	}

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	len = fc_prli_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
				rx_fchs->ox_id, port->port_cfg.roles);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);
}

static void
bfa_fcs_rport_process_rpsc(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u16 len)
{
	struct bfa_fcxp_s *fcxp;
	struct fchs_s	fchs;
	struct bfa_fcs_lport_s *port = rport->port;
	struct fc_rpsc_speed_info_s speeds;
	struct bfa_port_attr_s pport_attr;

	bfa_trc(port->fcs, rx_fchs->s_id);
	bfa_trc(port->fcs, rx_fchs->d_id);

	rport->stats.rpsc_rcvd++;
	speeds.port_speed_cap =
		RPSC_SPEED_CAP_1G | RPSC_SPEED_CAP_2G | RPSC_SPEED_CAP_4G |
		RPSC_SPEED_CAP_8G;

	/*
	 * get curent speed from pport attributes from BFA
	 */
	bfa_fcport_get_attr(port->fcs->bfa, &pport_attr);

	speeds.port_op_speed = fc_bfa_speed_to_rpsc_operspeed(pport_attr.speed);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	len = fc_rpsc_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
				rx_fchs->ox_id, &speeds);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);
}

static void
bfa_fcs_rport_process_adisc(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u16 len)
{
	struct bfa_fcxp_s *fcxp;
	struct fchs_s	fchs;
	struct bfa_fcs_lport_s *port = rport->port;

	bfa_trc(port->fcs, rx_fchs->s_id);
	bfa_trc(port->fcs, rx_fchs->d_id);

	rport->stats.adisc_rcvd++;

	/*
	 * Accept if the itnim for this rport is online.
	 * Else reject the ADISC.
	 */
	if (bfa_fcs_itnim_get_online_state(rport->itnim) == BFA_STATUS_OK) {

		fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
		if (!fcxp)
			return;

		len = fc_adisc_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			 rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
			 rx_fchs->ox_id, port->port_cfg.pwwn,
			 port->port_cfg.nwwn);

		bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag,
				BFA_FALSE, FC_CLASS_3, len, &fchs, NULL, NULL,
				FC_MAX_PDUSZ, 0);
	} else {
		rport->stats.adisc_rejected++;
		bfa_fcs_rport_send_ls_rjt(rport, rx_fchs,
					  FC_LS_RJT_RSN_UNABLE_TO_PERF_CMD,
					  FC_LS_RJT_EXP_LOGIN_REQUIRED);
	}
}

static void
bfa_fcs_rport_hal_online(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_lport_s *port = rport->port;
	struct bfa_rport_info_s rport_info;

	rport_info.pid = rport->pid;
	rport_info.local_pid = port->pid;
	rport_info.lp_tag = port->lp_tag;
	rport_info.vf_id = port->fabric->vf_id;
	rport_info.vf_en = port->fabric->is_vf;
	rport_info.fc_class = rport->fc_cos;
	rport_info.cisc = rport->cisc;
	rport_info.max_frmsz = rport->maxfrsize;
	bfa_rport_online(rport->bfa_rport, &rport_info);
}

static void
bfa_fcs_rport_hal_offline(struct bfa_fcs_rport_s *rport)
{
	if (rport->bfa_rport)
		bfa_sm_send_event(rport->bfa_rport, BFA_RPORT_SM_OFFLINE);
	else
		bfa_cb_rport_offline(rport);
}

static struct bfa_fcs_rport_s *
bfa_fcs_rport_alloc(struct bfa_fcs_lport_s *port, wwn_t pwwn, u32 rpid)
{
	struct bfa_fcs_s	*fcs = port->fcs;
	struct bfa_fcs_rport_s *rport;
	struct bfad_rport_s	*rport_drv;

	/*
	 * allocate rport
	 */
	if (fcs->num_rport_logins >= bfa_fcs_rport_max_logins) {
		bfa_trc(fcs, rpid);
		return NULL;
	}

	if (bfa_fcb_rport_alloc(fcs->bfad, &rport, &rport_drv)
		!= BFA_STATUS_OK) {
		bfa_trc(fcs, rpid);
		return NULL;
	}

	/*
	 * Initialize r-port
	 */
	rport->port = port;
	rport->fcs = fcs;
	rport->rp_drv = rport_drv;
	rport->pid = rpid;
	rport->pwwn = pwwn;
	rport->old_pid = 0;

	rport->bfa_rport = NULL;

	/*
	 * allocate FC-4s
	 */
	WARN_ON(!bfa_fcs_lport_is_initiator(port));

	if (bfa_fcs_lport_is_initiator(port)) {
		rport->itnim = bfa_fcs_itnim_create(rport);
		if (!rport->itnim) {
			bfa_trc(fcs, rpid);
			kfree(rport_drv);
			return NULL;
		}
	}

	bfa_fcs_lport_add_rport(port, rport);
	fcs->num_rport_logins++;

	bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);

	/* Initialize the Rport Features(RPF) Sub Module  */
	if (!BFA_FCS_PID_IS_WKA(rport->pid))
		bfa_fcs_rpf_init(rport);

	return rport;
}


static void
bfa_fcs_rport_free(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_lport_s *port = rport->port;
	struct bfa_fcs_s *fcs = port->fcs;

	/*
	 * - delete FC-4s
	 * - delete BFA rport
	 * - remove from queue of rports
	 */
	rport->plogi_pending = BFA_FALSE;

	if (bfa_fcs_lport_is_initiator(port)) {
		bfa_fcs_itnim_delete(rport->itnim);
		if (rport->pid != 0 && !BFA_FCS_PID_IS_WKA(rport->pid))
			bfa_fcs_rpf_rport_offline(rport);
	}

	if (rport->bfa_rport) {
		bfa_sm_send_event(rport->bfa_rport, BFA_RPORT_SM_DELETE);
		rport->bfa_rport = NULL;
	}

	bfa_fcs_lport_del_rport(port, rport);
	fcs->num_rport_logins--;
	kfree(rport->rp_drv);
}

static void
bfa_fcs_rport_aen_post(struct bfa_fcs_rport_s *rport,
			enum bfa_rport_aen_event event,
			struct bfa_rport_aen_data_s *data)
{
	struct bfa_fcs_lport_s *port = rport->port;
	struct bfad_s *bfad = (struct bfad_s *)port->fcs->bfad;
	struct bfa_aen_entry_s  *aen_entry;

	bfad_get_aen_entry(bfad, aen_entry);
	if (!aen_entry)
		return;

	if (event == BFA_RPORT_AEN_QOS_PRIO)
		aen_entry->aen_data.rport.priv.qos = data->priv.qos;
	else if (event == BFA_RPORT_AEN_QOS_FLOWID)
		aen_entry->aen_data.rport.priv.qos = data->priv.qos;

	aen_entry->aen_data.rport.vf_id = rport->port->fabric->vf_id;
	aen_entry->aen_data.rport.ppwwn = bfa_fcs_lport_get_pwwn(
					bfa_fcs_get_base_port(rport->fcs));
	aen_entry->aen_data.rport.lpwwn = bfa_fcs_lport_get_pwwn(rport->port);
	aen_entry->aen_data.rport.rpwwn = rport->pwwn;

	/* Send the AEN notification */
	bfad_im_post_vendor_event(aen_entry, bfad, ++rport->fcs->fcs_aen_seq,
				  BFA_AEN_CAT_RPORT, event);
}

static void
bfa_fcs_rport_fcs_online_action(struct bfa_fcs_rport_s *rport)
{
	if ((!rport->pid) || (!rport->pwwn)) {
		bfa_trc(rport->fcs, rport->pid);
		bfa_sm_fault(rport->fcs, rport->pid);
	}

	bfa_sm_send_event(rport->itnim, BFA_FCS_ITNIM_SM_FCS_ONLINE);
}

static void
bfa_fcs_rport_hal_online_action(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_lport_s *port = rport->port;
	struct bfad_s *bfad = (struct bfad_s *)port->fcs->bfad;
	char	lpwwn_buf[BFA_STRING_32];
	char	rpwwn_buf[BFA_STRING_32];

	rport->stats.onlines++;

	if ((!rport->pid) || (!rport->pwwn)) {
		bfa_trc(rport->fcs, rport->pid);
		bfa_sm_fault(rport->fcs, rport->pid);
	}

	if (bfa_fcs_lport_is_initiator(port)) {
		bfa_fcs_itnim_brp_online(rport->itnim);
		if (!BFA_FCS_PID_IS_WKA(rport->pid))
			bfa_fcs_rpf_rport_online(rport);
	}

	wwn2str(lpwwn_buf, bfa_fcs_lport_get_pwwn(port));
	wwn2str(rpwwn_buf, rport->pwwn);
	if (!BFA_FCS_PID_IS_WKA(rport->pid)) {
		BFA_LOG(KERN_INFO, bfad, bfa_log_level,
		"Remote port (WWN = %s) online for logical port (WWN = %s)\n",
		rpwwn_buf, lpwwn_buf);
		bfa_fcs_rport_aen_post(rport, BFA_RPORT_AEN_ONLINE, NULL);
	}
}

static void
bfa_fcs_rport_fcs_offline_action(struct bfa_fcs_rport_s *rport)
{
	if (!BFA_FCS_PID_IS_WKA(rport->pid))
		bfa_fcs_rpf_rport_offline(rport);

	bfa_fcs_itnim_rport_offline(rport->itnim);
}

static void
bfa_fcs_rport_hal_offline_action(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_lport_s *port = rport->port;
	struct bfad_s *bfad = (struct bfad_s *)port->fcs->bfad;
	char	lpwwn_buf[BFA_STRING_32];
	char	rpwwn_buf[BFA_STRING_32];

	if (!rport->bfa_rport) {
		bfa_fcs_rport_fcs_offline_action(rport);
		return;
	}

	rport->stats.offlines++;

	wwn2str(lpwwn_buf, bfa_fcs_lport_get_pwwn(port));
	wwn2str(rpwwn_buf, rport->pwwn);
	if (!BFA_FCS_PID_IS_WKA(rport->pid)) {
		if (bfa_fcs_lport_is_online(rport->port) == BFA_TRUE) {
			BFA_LOG(KERN_ERR, bfad, bfa_log_level,
				"Remote port (WWN = %s) connectivity lost for "
				"logical port (WWN = %s)\n",
				rpwwn_buf, lpwwn_buf);
			bfa_fcs_rport_aen_post(rport,
				BFA_RPORT_AEN_DISCONNECT, NULL);
		} else {
			BFA_LOG(KERN_INFO, bfad, bfa_log_level,
				"Remote port (WWN = %s) offlined by "
				"logical port (WWN = %s)\n",
				rpwwn_buf, lpwwn_buf);
			bfa_fcs_rport_aen_post(rport,
				BFA_RPORT_AEN_OFFLINE, NULL);
		}
	}

	if (bfa_fcs_lport_is_initiator(port)) {
		bfa_fcs_itnim_rport_offline(rport->itnim);
		if (!BFA_FCS_PID_IS_WKA(rport->pid))
			bfa_fcs_rpf_rport_offline(rport);
	}
}

/*
 * Update rport parameters from PLOGI or PLOGI accept.
 */
static void
bfa_fcs_rport_update(struct bfa_fcs_rport_s *rport, struct fc_logi_s *plogi)
{
	bfa_fcs_lport_t *port = rport->port;

	/*
	 * - port name
	 * - node name
	 */
	rport->pwwn = plogi->port_name;
	rport->nwwn = plogi->node_name;

	/*
	 * - class of service
	 */
	rport->fc_cos = 0;
	if (plogi->class3.class_valid)
		rport->fc_cos = FC_CLASS_3;

	if (plogi->class2.class_valid)
		rport->fc_cos |= FC_CLASS_2;

	/*
	 * - CISC
	 * - MAX receive frame size
	 */
	rport->cisc = plogi->csp.cisc;
	if (be16_to_cpu(plogi->class3.rxsz) < be16_to_cpu(plogi->csp.rxsz))
		rport->maxfrsize = be16_to_cpu(plogi->class3.rxsz);
	else
		rport->maxfrsize = be16_to_cpu(plogi->csp.rxsz);

	bfa_trc(port->fcs, be16_to_cpu(plogi->csp.bbcred));
	bfa_trc(port->fcs, port->fabric->bb_credit);
	/*
	 * Direct Attach P2P mode :
	 * This is to handle a bug (233476) in IBM targets in Direct Attach
	 *  Mode. Basically, in FLOGI Accept the target would have
	 * erroneously set the BB Credit to the value used in the FLOGI
	 * sent by the HBA. It uses the correct value (its own BB credit)
	 * in PLOGI.
	 */
	if ((!bfa_fcs_fabric_is_switched(port->fabric))	 &&
		(be16_to_cpu(plogi->csp.bbcred) < port->fabric->bb_credit)) {

		bfa_trc(port->fcs, be16_to_cpu(plogi->csp.bbcred));
		bfa_trc(port->fcs, port->fabric->bb_credit);

		port->fabric->bb_credit = be16_to_cpu(plogi->csp.bbcred);
		bfa_fcport_set_tx_bbcredit(port->fcs->bfa,
					  port->fabric->bb_credit);
	}

}

/*
 *	Called to handle LOGO received from an existing remote port.
 */
static void
bfa_fcs_rport_process_logo(struct bfa_fcs_rport_s *rport, struct fchs_s *fchs)
{
	rport->reply_oxid = fchs->ox_id;
	bfa_trc(rport->fcs, rport->reply_oxid);

	rport->prlo = BFA_FALSE;
	rport->stats.logo_rcvd++;
	bfa_sm_send_event(rport, RPSM_EVENT_LOGO_RCVD);
}



/*
 *  fcs_rport_public FCS rport public interfaces
 */

/*
 *	Called by bport/vport to create a remote port instance for a discovered
 *	remote device.
 *
 * @param[in] port	- base port or vport
 * @param[in] rpid	- remote port ID
 *
 * @return None
 */
struct bfa_fcs_rport_s *
bfa_fcs_rport_create(struct bfa_fcs_lport_s *port, u32 rpid)
{
	struct bfa_fcs_rport_s *rport;

	bfa_trc(port->fcs, rpid);
	rport = bfa_fcs_rport_alloc(port, WWN_NULL, rpid);
	if (!rport)
		return NULL;

	bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_SEND);
	return rport;
}

/*
 * Called to create a rport for which only the wwn is known.
 *
 * @param[in] port	- base port
 * @param[in] rpwwn	- remote port wwn
 *
 * @return None
 */
struct bfa_fcs_rport_s *
bfa_fcs_rport_create_by_wwn(struct bfa_fcs_lport_s *port, wwn_t rpwwn)
{
	struct bfa_fcs_rport_s *rport;
	bfa_trc(port->fcs, rpwwn);
	rport = bfa_fcs_rport_alloc(port, rpwwn, 0);
	if (!rport)
		return NULL;

	bfa_sm_send_event(rport, RPSM_EVENT_ADDRESS_DISC);
	return rport;
}
/*
 * Called by bport in private loop topology to indicate that a
 * rport has been discovered and plogi has been completed.
 *
 * @param[in] port	- base port or vport
 * @param[in] rpid	- remote port ID
 */
void
bfa_fcs_rport_start(struct bfa_fcs_lport_s *port, struct fchs_s *fchs,
	 struct fc_logi_s *plogi)
{
	struct bfa_fcs_rport_s *rport;

	rport = bfa_fcs_rport_alloc(port, WWN_NULL, fchs->s_id);
	if (!rport)
		return;

	bfa_fcs_rport_update(rport, plogi);

	bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_COMP);
}

/*
 *	Called by bport/vport to handle PLOGI received from a new remote port.
 *	If an existing rport does a plogi, it will be handled separately.
 */
void
bfa_fcs_rport_plogi_create(struct bfa_fcs_lport_s *port, struct fchs_s *fchs,
				struct fc_logi_s *plogi)
{
	struct bfa_fcs_rport_s *rport;

	rport = bfa_fcs_rport_alloc(port, plogi->port_name, fchs->s_id);
	if (!rport)
		return;

	bfa_fcs_rport_update(rport, plogi);

	rport->reply_oxid = fchs->ox_id;
	bfa_trc(rport->fcs, rport->reply_oxid);

	rport->stats.plogi_rcvd++;
	bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_RCVD);
}

/*
 *	Called by bport/vport to handle PLOGI received from an existing
 *	 remote port.
 */
void
bfa_fcs_rport_plogi(struct bfa_fcs_rport_s *rport, struct fchs_s *rx_fchs,
			struct fc_logi_s *plogi)
{
	/*
	 * @todo Handle P2P and initiator-initiator.
	 */

	bfa_fcs_rport_update(rport, plogi);

	rport->reply_oxid = rx_fchs->ox_id;
	bfa_trc(rport->fcs, rport->reply_oxid);

	rport->pid = rx_fchs->s_id;
	bfa_trc(rport->fcs, rport->pid);

	rport->stats.plogi_rcvd++;
	bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_RCVD);
}


/*
 *	Called by bport/vport to notify SCN for the remote port
 */
void
bfa_fcs_rport_scn(struct bfa_fcs_rport_s *rport)
{
	rport->stats.rscns++;
	bfa_sm_send_event(rport, RPSM_EVENT_FAB_SCN);
}

/*
 *	brief
 *	This routine BFA callback for bfa_rport_online() call.
 *
 *	param[in]	cb_arg	-  rport struct.
 *
 *	return
 *		void
 *
 *	Special Considerations:
 *
 *	note
 */
void
bfa_cb_rport_online(void *cbarg)
{

	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) cbarg;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_sm_send_event(rport, RPSM_EVENT_HCB_ONLINE);
}

/*
 *	brief
 *	This routine BFA callback for bfa_rport_offline() call.
 *
 *	param[in]	rport	-
 *
 *	return
 *		void
 *
 *	Special Considerations:
 *
 *	note
 */
void
bfa_cb_rport_offline(void *cbarg)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) cbarg;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_sm_send_event(rport, RPSM_EVENT_HCB_OFFLINE);
}

/*
 *	brief
 *	This routine is a static BFA callback when there is a QoS flow_id
 *	change notification
 *
 *	param[in]	rport	-
 *
 *	return
 *		void
 *
 *	Special Considerations:
 *
 *	note
 */
void
bfa_cb_rport_qos_scn_flowid(void *cbarg,
		struct bfa_rport_qos_attr_s old_qos_attr,
		struct bfa_rport_qos_attr_s new_qos_attr)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) cbarg;
	struct bfa_rport_aen_data_s aen_data;

	bfa_trc(rport->fcs, rport->pwwn);
	aen_data.priv.qos = new_qos_attr;
	bfa_fcs_rport_aen_post(rport, BFA_RPORT_AEN_QOS_FLOWID, &aen_data);
}

void
bfa_cb_rport_scn_online(struct bfa_s *bfa)
{
	struct bfa_fcs_s *fcs = &((struct bfad_s *)bfa->bfad)->bfa_fcs;
	struct bfa_fcs_lport_s *port = bfa_fcs_get_base_port(fcs);
	struct bfa_fcs_rport_s *rp;
	struct list_head *qe;

	list_for_each(qe, &port->rport_q) {
		rp = (struct bfa_fcs_rport_s *) qe;
		bfa_sm_send_event(rp, RPSM_EVENT_SCN_ONLINE);
		rp->scn_online = BFA_TRUE;
	}

	if (bfa_fcs_lport_is_online(port))
		bfa_fcs_lport_lip_scn_online(port);
}

void
bfa_cb_rport_scn_no_dev(void *rport)
{
	struct bfa_fcs_rport_s *rp = rport;

	bfa_sm_send_event(rp, RPSM_EVENT_SCN_OFFLINE);
	rp->scn_online = BFA_FALSE;
}

void
bfa_cb_rport_scn_offline(struct bfa_s *bfa)
{
	struct bfa_fcs_s *fcs = &((struct bfad_s *)bfa->bfad)->bfa_fcs;
	struct bfa_fcs_lport_s *port = bfa_fcs_get_base_port(fcs);
	struct bfa_fcs_rport_s *rp;
	struct list_head *qe;

	list_for_each(qe, &port->rport_q) {
		rp = (struct bfa_fcs_rport_s *) qe;
		bfa_sm_send_event(rp, RPSM_EVENT_SCN_OFFLINE);
		rp->scn_online = BFA_FALSE;
	}
}

/*
 *	brief
 *	This routine is a static BFA callback when there is a QoS priority
 *	change notification
 *
 *	param[in]	rport	-
 *
 *	return
 *		void
 *
 *	Special Considerations:
 *
 *	note
 */
void
bfa_cb_rport_qos_scn_prio(void *cbarg,
		struct bfa_rport_qos_attr_s old_qos_attr,
		struct bfa_rport_qos_attr_s new_qos_attr)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *) cbarg;
	struct bfa_rport_aen_data_s aen_data;

	bfa_trc(rport->fcs, rport->pwwn);
	aen_data.priv.qos = new_qos_attr;
	bfa_fcs_rport_aen_post(rport, BFA_RPORT_AEN_QOS_PRIO, &aen_data);
}

/*
 *		Called to process any unsolicted frames from this remote port
 */
void
bfa_fcs_rport_uf_recv(struct bfa_fcs_rport_s *rport,
			struct fchs_s *fchs, u16 len)
{
	struct bfa_fcs_lport_s *port = rport->port;
	struct fc_els_cmd_s	*els_cmd;

	bfa_trc(rport->fcs, fchs->s_id);
	bfa_trc(rport->fcs, fchs->d_id);
	bfa_trc(rport->fcs, fchs->type);

	if (fchs->type != FC_TYPE_ELS)
		return;

	els_cmd = (struct fc_els_cmd_s *) (fchs + 1);

	bfa_trc(rport->fcs, els_cmd->els_code);

	switch (els_cmd->els_code) {
	case FC_ELS_LOGO:
		bfa_stats(port, plogi_rcvd);
		bfa_fcs_rport_process_logo(rport, fchs);
		break;

	case FC_ELS_ADISC:
		bfa_stats(port, adisc_rcvd);
		bfa_fcs_rport_process_adisc(rport, fchs, len);
		break;

	case FC_ELS_PRLO:
		bfa_stats(port, prlo_rcvd);
		if (bfa_fcs_lport_is_initiator(port))
			bfa_fcs_fcpim_uf_recv(rport->itnim, fchs, len);
		break;

	case FC_ELS_PRLI:
		bfa_stats(port, prli_rcvd);
		bfa_fcs_rport_process_prli(rport, fchs, len);
		break;

	case FC_ELS_RPSC:
		bfa_stats(port, rpsc_rcvd);
		bfa_fcs_rport_process_rpsc(rport, fchs, len);
		break;

	default:
		bfa_stats(port, un_handled_els_rcvd);
		bfa_fcs_rport_send_ls_rjt(rport, fchs,
					  FC_LS_RJT_RSN_CMD_NOT_SUPP,
					  FC_LS_RJT_EXP_NO_ADDL_INFO);
		break;
	}
}

/* send best case  acc to prlo */
static void
bfa_fcs_rport_send_prlo_acc(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_lport_s *port = rport->port;
	struct fchs_s	fchs;
	struct bfa_fcxp_s *fcxp;
	int		len;

	bfa_trc(rport->fcs, rport->pid);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;
	len = fc_prlo_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			rport->pid, bfa_fcs_lport_get_fcid(port),
			rport->reply_oxid, 0);

	bfa_fcxp_send(fcxp, rport->bfa_rport, port->fabric->vf_id,
		port->lp_tag, BFA_FALSE, FC_CLASS_3, len, &fchs,
		NULL, NULL, FC_MAX_PDUSZ, 0);
}

/*
 * Send a LS reject
 */
static void
bfa_fcs_rport_send_ls_rjt(struct bfa_fcs_rport_s *rport, struct fchs_s *rx_fchs,
			  u8 reason_code, u8 reason_code_expl)
{
	struct bfa_fcs_lport_s *port = rport->port;
	struct fchs_s	fchs;
	struct bfa_fcxp_s *fcxp;
	int		len;

	bfa_trc(rport->fcs, rx_fchs->s_id);

	fcxp = bfa_fcs_fcxp_alloc(rport->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	len = fc_ls_rjt_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
				rx_fchs->ox_id, reason_code, reason_code_expl);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag,
			BFA_FALSE, FC_CLASS_3, len, &fchs, NULL, NULL,
			FC_MAX_PDUSZ, 0);
}

/*
 * Return state of rport.
 */
int
bfa_fcs_rport_get_state(struct bfa_fcs_rport_s *rport)
{
	return bfa_rport_sm_to_state(rport_sm_table, rport->sm);
}


/*
 *	brief
 *		 Called by the Driver to set rport delete/ageout timeout
 *
 *	param[in]		rport timeout value in seconds.
 *
 *	return None
 */
void
bfa_fcs_rport_set_del_timeout(u8 rport_tmo)
{
	/* convert to Millisecs */
	if (rport_tmo > 0)
		bfa_fcs_rport_del_timeout = rport_tmo * 1000;
}
void
bfa_fcs_rport_prlo(struct bfa_fcs_rport_s *rport, __be16 ox_id)
{
	bfa_trc(rport->fcs, rport->pid);

	rport->prlo = BFA_TRUE;
	rport->reply_oxid = ox_id;
	bfa_sm_send_event(rport, RPSM_EVENT_PRLO_RCVD);
}

/*
 * Called by BFAD to set the max limit on number of bfa_fcs_rport allocation
 * which limits number of concurrent logins to remote ports
 */
void
bfa_fcs_rport_set_max_logins(u32 max_logins)
{
	if (max_logins > 0)
		bfa_fcs_rport_max_logins = max_logins;
}

void
bfa_fcs_rport_get_attr(struct bfa_fcs_rport_s *rport,
		struct bfa_rport_attr_s *rport_attr)
{
	struct bfa_rport_qos_attr_s qos_attr;
	struct bfa_fcs_lport_s *port = rport->port;
	bfa_port_speed_t rport_speed = rport->rpf.rpsc_speed;
	struct bfa_port_attr_s port_attr;

	bfa_fcport_get_attr(rport->fcs->bfa, &port_attr);

	memset(rport_attr, 0, sizeof(struct bfa_rport_attr_s));
	memset(&qos_attr, 0, sizeof(struct bfa_rport_qos_attr_s));

	rport_attr->pid = rport->pid;
	rport_attr->pwwn = rport->pwwn;
	rport_attr->nwwn = rport->nwwn;
	rport_attr->cos_supported = rport->fc_cos;
	rport_attr->df_sz = rport->maxfrsize;
	rport_attr->state = bfa_fcs_rport_get_state(rport);
	rport_attr->fc_cos = rport->fc_cos;
	rport_attr->cisc = rport->cisc;
	rport_attr->scsi_function = rport->scsi_function;
	rport_attr->curr_speed  = rport->rpf.rpsc_speed;
	rport_attr->assigned_speed  = rport->rpf.assigned_speed;

	if (rport->bfa_rport) {
		qos_attr.qos_priority = rport->bfa_rport->qos_attr.qos_priority;
		qos_attr.qos_flow_id =
			cpu_to_be32(rport->bfa_rport->qos_attr.qos_flow_id);
	}
	rport_attr->qos_attr = qos_attr;

	rport_attr->trl_enforced = BFA_FALSE;
	if (bfa_fcport_is_ratelim(port->fcs->bfa) &&
	    (rport->scsi_function == BFA_RPORT_TARGET)) {
		if (rport_speed == BFA_PORT_SPEED_UNKNOWN)
			rport_speed =
				bfa_fcport_get_ratelim_speed(rport->fcs->bfa);

		if ((bfa_fcs_lport_get_rport_max_speed(port) !=
		    BFA_PORT_SPEED_UNKNOWN) && (rport_speed < port_attr.speed))
			rport_attr->trl_enforced = BFA_TRUE;
	}
}

/*
 * Remote port implementation.
 */

/*
 *  fcs_rport_api FCS rport API.
 */

struct bfa_fcs_rport_s *
bfa_fcs_rport_lookup(struct bfa_fcs_lport_s *port, wwn_t rpwwn)
{
	struct bfa_fcs_rport_s *rport;

	rport = bfa_fcs_lport_get_rport_by_pwwn(port, rpwwn);
	if (rport == NULL) {
		/*
		 * TBD Error handling
		 */
	}

	return rport;
}

struct bfa_fcs_rport_s *
bfa_fcs_rport_lookup_by_nwwn(struct bfa_fcs_lport_s *port, wwn_t rnwwn)
{
	struct bfa_fcs_rport_s *rport;

	rport = bfa_fcs_lport_get_rport_by_nwwn(port, rnwwn);
	if (rport == NULL) {
		/*
		 * TBD Error handling
		 */
	}

	return rport;
}

/*
 * Remote port features (RPF) implementation.
 */

#define BFA_FCS_RPF_RETRIES	(3)
#define BFA_FCS_RPF_RETRY_TIMEOUT  (1000) /* 1 sec (In millisecs) */

static void     bfa_fcs_rpf_send_rpsc2(void *rport_cbarg,
				struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_rpf_rpsc2_response(void *fcsarg,
			struct bfa_fcxp_s *fcxp,
			void *cbarg,
			bfa_status_t req_status,
			u32 rsp_len,
			u32 resid_len,
			struct fchs_s *rsp_fchs);

static void     bfa_fcs_rpf_timeout(void *arg);

static void	bfa_fcs_rpf_sm_uninit(struct bfa_fcs_rpf_s *rpf,
					enum rpf_event event);
static void     bfa_fcs_rpf_sm_rpsc_sending(struct bfa_fcs_rpf_s *rpf,
				       enum rpf_event event);
static void     bfa_fcs_rpf_sm_rpsc(struct bfa_fcs_rpf_s *rpf,
				       enum rpf_event event);
static void	bfa_fcs_rpf_sm_rpsc_retry(struct bfa_fcs_rpf_s *rpf,
					enum rpf_event event);
static void     bfa_fcs_rpf_sm_offline(struct bfa_fcs_rpf_s *rpf,
					enum rpf_event event);
static void     bfa_fcs_rpf_sm_online(struct bfa_fcs_rpf_s *rpf,
					enum rpf_event event);

static void
bfa_fcs_rpf_sm_uninit(struct bfa_fcs_rpf_s *rpf, enum rpf_event event)
{
	struct bfa_fcs_rport_s *rport = rpf->rport;
	struct bfa_fcs_fabric_s *fabric = &rport->fcs->fabric;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPFSM_EVENT_RPORT_ONLINE:
		/* Send RPSC2 to a Brocade fabric only. */
		if ((!BFA_FCS_PID_IS_WKA(rport->pid)) &&
			((rport->port->fabric->lps->brcd_switch) ||
			(bfa_fcs_fabric_get_switch_oui(fabric) ==
						BFA_FCS_BRCD_SWITCH_OUI))) {
			bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_rpsc_sending);
			rpf->rpsc_retries = 0;
			bfa_fcs_rpf_send_rpsc2(rpf, NULL);
		}
		break;

	case RPFSM_EVENT_RPORT_OFFLINE:
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

static void
bfa_fcs_rpf_sm_rpsc_sending(struct bfa_fcs_rpf_s *rpf, enum rpf_event event)
{
	struct bfa_fcs_rport_s *rport = rpf->rport;

	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPFSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_rpsc);
		break;

	case RPFSM_EVENT_RPORT_OFFLINE:
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rpf->fcxp_wqe);
		rpf->rpsc_retries = 0;
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

static void
bfa_fcs_rpf_sm_rpsc(struct bfa_fcs_rpf_s *rpf, enum rpf_event event)
{
	struct bfa_fcs_rport_s *rport = rpf->rport;

	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPFSM_EVENT_RPSC_COMP:
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_online);
		/* Update speed info in f/w via BFA */
		if (rpf->rpsc_speed != BFA_PORT_SPEED_UNKNOWN)
			bfa_rport_speed(rport->bfa_rport, rpf->rpsc_speed);
		else if (rpf->assigned_speed != BFA_PORT_SPEED_UNKNOWN)
			bfa_rport_speed(rport->bfa_rport, rpf->assigned_speed);
		break;

	case RPFSM_EVENT_RPSC_FAIL:
		/* RPSC not supported by rport */
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_online);
		break;

	case RPFSM_EVENT_RPSC_ERROR:
		/* need to retry...delayed a bit. */
		if (rpf->rpsc_retries++ < BFA_FCS_RPF_RETRIES) {
			bfa_timer_start(rport->fcs->bfa, &rpf->timer,
				    bfa_fcs_rpf_timeout, rpf,
				    BFA_FCS_RPF_RETRY_TIMEOUT);
			bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_rpsc_retry);
		} else {
			bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_online);
		}
		break;

	case RPFSM_EVENT_RPORT_OFFLINE:
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_offline);
		bfa_fcxp_discard(rpf->fcxp);
		rpf->rpsc_retries = 0;
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

static void
bfa_fcs_rpf_sm_rpsc_retry(struct bfa_fcs_rpf_s *rpf, enum rpf_event event)
{
	struct bfa_fcs_rport_s *rport = rpf->rport;

	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPFSM_EVENT_TIMEOUT:
		/* re-send the RPSC */
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_rpsc_sending);
		bfa_fcs_rpf_send_rpsc2(rpf, NULL);
		break;

	case RPFSM_EVENT_RPORT_OFFLINE:
		bfa_timer_stop(&rpf->timer);
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_offline);
		rpf->rpsc_retries = 0;
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

static void
bfa_fcs_rpf_sm_online(struct bfa_fcs_rpf_s *rpf, enum rpf_event event)
{
	struct bfa_fcs_rport_s *rport = rpf->rport;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPFSM_EVENT_RPORT_OFFLINE:
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_offline);
		rpf->rpsc_retries = 0;
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}

static void
bfa_fcs_rpf_sm_offline(struct bfa_fcs_rpf_s *rpf, enum rpf_event event)
{
	struct bfa_fcs_rport_s *rport = rpf->rport;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPFSM_EVENT_RPORT_ONLINE:
		bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_rpsc_sending);
		bfa_fcs_rpf_send_rpsc2(rpf, NULL);
		break;

	case RPFSM_EVENT_RPORT_OFFLINE:
		break;

	default:
		bfa_sm_fault(rport->fcs, event);
	}
}
/*
 * Called when Rport is created.
 */
void
bfa_fcs_rpf_init(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_rpf_s *rpf = &rport->rpf;

	bfa_trc(rport->fcs, rport->pid);
	rpf->rport = rport;

	bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_uninit);
}

/*
 * Called when Rport becomes online
 */
void
bfa_fcs_rpf_rport_online(struct bfa_fcs_rport_s *rport)
{
	bfa_trc(rport->fcs, rport->pid);

	if (__fcs_min_cfg(rport->port->fcs))
		return;

	if (bfa_fcs_fabric_is_switched(rport->port->fabric))
		bfa_sm_send_event(&rport->rpf, RPFSM_EVENT_RPORT_ONLINE);
}

/*
 * Called when Rport becomes offline
 */
void
bfa_fcs_rpf_rport_offline(struct bfa_fcs_rport_s *rport)
{
	bfa_trc(rport->fcs, rport->pid);

	if (__fcs_min_cfg(rport->port->fcs))
		return;

	rport->rpf.rpsc_speed = 0;
	bfa_sm_send_event(&rport->rpf, RPFSM_EVENT_RPORT_OFFLINE);
}

static void
bfa_fcs_rpf_timeout(void *arg)
{
	struct bfa_fcs_rpf_s *rpf = (struct bfa_fcs_rpf_s *) arg;
	struct bfa_fcs_rport_s *rport = rpf->rport;

	bfa_trc(rport->fcs, rport->pid);
	bfa_sm_send_event(rpf, RPFSM_EVENT_TIMEOUT);
}

static void
bfa_fcs_rpf_send_rpsc2(void *rpf_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rpf_s *rpf = (struct bfa_fcs_rpf_s *)rpf_cbarg;
	struct bfa_fcs_rport_s *rport = rpf->rport;
	struct bfa_fcs_lport_s *port = rport->port;
	struct fchs_s	fchs;
	int		len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(rport->fcs, rport->pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &rpf->fcxp_wqe,
				bfa_fcs_rpf_send_rpsc2, rpf, BFA_TRUE);
		return;
	}
	rpf->fcxp = fcxp;

	len = fc_rpsc2_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
			    bfa_fcs_lport_get_fcid(port), &rport->pid, 1);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs, bfa_fcs_rpf_rpsc2_response,
			  rpf, FC_MAX_PDUSZ, FC_ELS_TOV);
	rport->stats.rpsc_sent++;
	bfa_sm_send_event(rpf, RPFSM_EVENT_FCXP_SENT);

}

static void
bfa_fcs_rpf_rpsc2_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
			    bfa_status_t req_status, u32 rsp_len,
			    u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_rpf_s *rpf = (struct bfa_fcs_rpf_s *) cbarg;
	struct bfa_fcs_rport_s *rport = rpf->rport;
	struct fc_ls_rjt_s *ls_rjt;
	struct fc_rpsc2_acc_s *rpsc2_acc;
	u16	num_ents;

	bfa_trc(rport->fcs, req_status);

	if (req_status != BFA_STATUS_OK) {
		bfa_trc(rport->fcs, req_status);
		if (req_status == BFA_STATUS_ETIMER)
			rport->stats.rpsc_failed++;
		bfa_sm_send_event(rpf, RPFSM_EVENT_RPSC_ERROR);
		return;
	}

	rpsc2_acc = (struct fc_rpsc2_acc_s *) BFA_FCXP_RSP_PLD(fcxp);
	if (rpsc2_acc->els_cmd == FC_ELS_ACC) {
		rport->stats.rpsc_accs++;
		num_ents = be16_to_cpu(rpsc2_acc->num_pids);
		bfa_trc(rport->fcs, num_ents);
		if (num_ents > 0) {
			WARN_ON(be32_to_cpu(rpsc2_acc->port_info[0].pid) !=
						bfa_ntoh3b(rport->pid));
			bfa_trc(rport->fcs,
				be32_to_cpu(rpsc2_acc->port_info[0].pid));
			bfa_trc(rport->fcs,
				be16_to_cpu(rpsc2_acc->port_info[0].speed));
			bfa_trc(rport->fcs,
				be16_to_cpu(rpsc2_acc->port_info[0].index));
			bfa_trc(rport->fcs,
				rpsc2_acc->port_info[0].type);

			if (rpsc2_acc->port_info[0].speed == 0) {
				bfa_sm_send_event(rpf, RPFSM_EVENT_RPSC_ERROR);
				return;
			}

			rpf->rpsc_speed = fc_rpsc_operspeed_to_bfa_speed(
				be16_to_cpu(rpsc2_acc->port_info[0].speed));

			bfa_sm_send_event(rpf, RPFSM_EVENT_RPSC_COMP);
		}
	} else {
		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);
		bfa_trc(rport->fcs, ls_rjt->reason_code);
		bfa_trc(rport->fcs, ls_rjt->reason_code_expl);
		rport->stats.rpsc_rejects++;
		if (ls_rjt->reason_code == FC_LS_RJT_RSN_CMD_NOT_SUPP)
			bfa_sm_send_event(rpf, RPFSM_EVENT_RPSC_FAIL);
		else
			bfa_sm_send_event(rpf, RPFSM_EVENT_RPSC_ERROR);
	}
}
