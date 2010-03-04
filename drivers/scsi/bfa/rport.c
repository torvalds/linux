/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  rport.c Remote port implementation.
 */

#include <bfa.h>
#include <bfa_svc.h>
#include "fcbuild.h"
#include "fcs_vport.h"
#include "fcs_lport.h"
#include "fcs_rport.h"
#include "fcs_fcpim.h"
#include "fcs_fcptm.h"
#include "fcs_trcmod.h"
#include "fcs_fcxp.h"
#include "fcs.h"
#include <fcb/bfa_fcb_rport.h>
#include <aen/bfa_aen_rport.h>

BFA_TRC_FILE(FCS, RPORT);

#define BFA_FCS_RPORT_MAX_RETRIES		(5)

/* In millisecs */
static u32 bfa_fcs_rport_del_timeout =
			BFA_FCS_RPORT_DEF_DEL_TIMEOUT * 1000;

/*
 * forward declarations
 */
static struct bfa_fcs_rport_s *bfa_fcs_rport_alloc(struct bfa_fcs_port_s *port,
						   wwn_t pwwn, u32 rpid);
static void     bfa_fcs_rport_free(struct bfa_fcs_rport_s *rport);
static void     bfa_fcs_rport_hal_online(struct bfa_fcs_rport_s *rport);
static void     bfa_fcs_rport_online_action(struct bfa_fcs_rport_s *rport);
static void     bfa_fcs_rport_offline_action(struct bfa_fcs_rport_s *rport);
static void     bfa_fcs_rport_update(struct bfa_fcs_rport_s *rport,
				     struct fc_logi_s *plogi);
static void     bfa_fcs_rport_fc4_pause(struct bfa_fcs_rport_s *rport);
static void     bfa_fcs_rport_fc4_resume(struct bfa_fcs_rport_s *rport);
static void     bfa_fcs_rport_timeout(void *arg);
static void     bfa_fcs_rport_send_plogi(void *rport_cbarg,
					 struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_rport_send_plogiacc(void *rport_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_rport_plogi_response(void *fcsarg,
					     struct bfa_fcxp_s *fcxp,
					     void *cbarg,
					     bfa_status_t req_status,
					     u32 rsp_len,
					     u32 resid_len,
					     struct fchs_s *rsp_fchs);
static void     bfa_fcs_rport_send_adisc(void *rport_cbarg,
					 struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_rport_adisc_response(void *fcsarg,
					     struct bfa_fcxp_s *fcxp,
					     void *cbarg,
					     bfa_status_t req_status,
					     u32 rsp_len,
					     u32 resid_len,
					     struct fchs_s *rsp_fchs);
static void     bfa_fcs_rport_send_gidpn(void *rport_cbarg,
					 struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_rport_gidpn_response(void *fcsarg,
					     struct bfa_fcxp_s *fcxp,
					     void *cbarg,
					     bfa_status_t req_status,
					     u32 rsp_len,
					     u32 resid_len,
					     struct fchs_s *rsp_fchs);
static void     bfa_fcs_rport_send_logo(void *rport_cbarg,
					struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_rport_send_logo_acc(void *rport_cbarg);
static void     bfa_fcs_rport_process_prli(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u16 len);
static void     bfa_fcs_rport_send_ls_rjt(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u8 reason_code,
			u8 reason_code_expl);
static void     bfa_fcs_rport_process_adisc(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u16 len);
/**
 *  fcs_rport_sm FCS rport state machine events
 */

enum rport_event {
	RPSM_EVENT_PLOGI_SEND = 1,	/*  new rport; start with PLOGI */
	RPSM_EVENT_PLOGI_RCVD = 2,	/*  Inbound PLOGI from remote port */
	RPSM_EVENT_PLOGI_COMP = 3,	/*  PLOGI completed to rport */
	RPSM_EVENT_LOGO_RCVD = 4,	/*  LOGO from remote device */
	RPSM_EVENT_LOGO_IMP = 5,	/*  implicit logo for SLER */
	RPSM_EVENT_FCXP_SENT = 6,	/*  Frame from has been sent */
	RPSM_EVENT_DELETE = 7,	/*  RPORT delete request */
	RPSM_EVENT_SCN = 8,	/*  state change notification */
	RPSM_EVENT_ACCEPTED = 9,/*  Good response from remote device */
	RPSM_EVENT_FAILED = 10,	/*  Request to rport failed.  */
	RPSM_EVENT_TIMEOUT = 11,	/*  Rport SM timeout event */
	RPSM_EVENT_HCB_ONLINE = 12,	/*  BFA rport online callback */
	RPSM_EVENT_HCB_OFFLINE = 13,	/*  BFA rport offline callback */
	RPSM_EVENT_FC4_OFFLINE = 14,	/*  FC-4 offline complete */
	RPSM_EVENT_ADDRESS_CHANGE = 15,	/*  Rport's PID has changed */
	RPSM_EVENT_ADDRESS_DISC = 16	/*  Need to Discover rport's PID */
};

static void     bfa_fcs_rport_sm_uninit(struct bfa_fcs_rport_s *rport,
					enum rport_event event);
static void     bfa_fcs_rport_sm_plogi_sending(struct bfa_fcs_rport_s *rport,
					       enum rport_event event);
static void     bfa_fcs_rport_sm_plogiacc_sending(struct bfa_fcs_rport_s *rport,
						  enum rport_event event);
static void     bfa_fcs_rport_sm_plogi_retry(struct bfa_fcs_rport_s *rport,
					     enum rport_event event);
static void     bfa_fcs_rport_sm_plogi(struct bfa_fcs_rport_s *rport,
				       enum rport_event event);
static void     bfa_fcs_rport_sm_hal_online(struct bfa_fcs_rport_s *rport,
					    enum rport_event event);
static void     bfa_fcs_rport_sm_online(struct bfa_fcs_rport_s *rport,
					enum rport_event event);
static void     bfa_fcs_rport_sm_nsquery_sending(struct bfa_fcs_rport_s *rport,
						 enum rport_event event);
static void     bfa_fcs_rport_sm_nsquery(struct bfa_fcs_rport_s *rport,
					 enum rport_event event);
static void     bfa_fcs_rport_sm_adisc_sending(struct bfa_fcs_rport_s *rport,
					       enum rport_event event);
static void     bfa_fcs_rport_sm_adisc(struct bfa_fcs_rport_s *rport,
				       enum rport_event event);
static void     bfa_fcs_rport_sm_fc4_logorcv(struct bfa_fcs_rport_s *rport,
					     enum rport_event event);
static void     bfa_fcs_rport_sm_fc4_logosend(struct bfa_fcs_rport_s *rport,
					      enum rport_event event);
static void     bfa_fcs_rport_sm_fc4_offline(struct bfa_fcs_rport_s *rport,
					     enum rport_event event);
static void     bfa_fcs_rport_sm_hcb_offline(struct bfa_fcs_rport_s *rport,
					     enum rport_event event);
static void     bfa_fcs_rport_sm_hcb_logorcv(struct bfa_fcs_rport_s *rport,
					     enum rport_event event);
static void     bfa_fcs_rport_sm_hcb_logosend(struct bfa_fcs_rport_s *rport,
					      enum rport_event event);
static void     bfa_fcs_rport_sm_logo_sending(struct bfa_fcs_rport_s *rport,
					      enum rport_event event);
static void     bfa_fcs_rport_sm_offline(struct bfa_fcs_rport_s *rport,
					 enum rport_event event);
static void     bfa_fcs_rport_sm_nsdisc_sending(struct bfa_fcs_rport_s *rport,
						enum rport_event event);
static void     bfa_fcs_rport_sm_nsdisc_retry(struct bfa_fcs_rport_s *rport,
					      enum rport_event event);
static void     bfa_fcs_rport_sm_nsdisc_sent(struct bfa_fcs_rport_s *rport,
					     enum rport_event event);
static void     bfa_fcs_rport_sm_nsdisc_sent(struct bfa_fcs_rport_s *rport,
					     enum rport_event event);

static struct bfa_sm_table_s rport_sm_table[] = {
	{BFA_SM(bfa_fcs_rport_sm_uninit), BFA_RPORT_UNINIT},
	{BFA_SM(bfa_fcs_rport_sm_plogi_sending), BFA_RPORT_PLOGI},
	{BFA_SM(bfa_fcs_rport_sm_plogiacc_sending), BFA_RPORT_ONLINE},
	{BFA_SM(bfa_fcs_rport_sm_plogi_retry), BFA_RPORT_PLOGI_RETRY},
	{BFA_SM(bfa_fcs_rport_sm_plogi), BFA_RPORT_PLOGI},
	{BFA_SM(bfa_fcs_rport_sm_hal_online), BFA_RPORT_ONLINE},
	{BFA_SM(bfa_fcs_rport_sm_online), BFA_RPORT_ONLINE},
	{BFA_SM(bfa_fcs_rport_sm_nsquery_sending), BFA_RPORT_NSQUERY},
	{BFA_SM(bfa_fcs_rport_sm_nsquery), BFA_RPORT_NSQUERY},
	{BFA_SM(bfa_fcs_rport_sm_adisc_sending), BFA_RPORT_ADISC},
	{BFA_SM(bfa_fcs_rport_sm_adisc), BFA_RPORT_ADISC},
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

/**
 * 		Beginning state.
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
		bfa_fcs_rport_send_gidpn(rport, NULL);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		PLOGI is being sent.
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

	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_gidpn(rport, NULL);
		break;

	case RPSM_EVENT_LOGO_IMP:
		rport->pid = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_SCN:
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		PLOGI is being sent.
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
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_fcs_rport_hal_online(rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_SCN:
		/**
		 * Ignore, SCN is possibly online notification.
		 */
		break;

	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_gidpn(rport, NULL);
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
		/**
		 * Ignore BFA callback, on a PLOGI receive we call bfa offline.
		 */
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		PLOGI is sent.
 */
static void
bfa_fcs_rport_sm_plogi_retry(struct bfa_fcs_rport_s *rport,
			enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_SCN:
		bfa_timer_stop(&rport->timer);
		/*
		 * !! fall through !!
		 */

	case RPSM_EVENT_TIMEOUT:
		rport->plogi_retries++;
		if (rport->plogi_retries < BFA_FCS_RPORT_MAX_RETRIES) {
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_sending);
			bfa_fcs_rport_send_plogi(rport, NULL);
		} else {
			rport->pid = 0;
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
			bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					bfa_fcs_rport_del_timeout);
		}
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_timer_stop(&rport->timer);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_gidpn(rport, NULL);
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
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_hal_online(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		PLOGI is sent.
 */
static void
bfa_fcs_rport_sm_plogi(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_ACCEPTED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		rport->plogi_retries = 0;
		bfa_fcs_rport_hal_online(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_fcs_rport_send_logo_acc(rport);
		bfa_fcxp_discard(rport->fcxp);
		/*
		 * !! fall through !!
		 */
	case RPSM_EVENT_FAILED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_retry);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				BFA_FCS_RETRY_TIMEOUT);
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
		bfa_fcxp_discard(rport->fcxp);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_gidpn(rport, NULL);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_SCN:
		/**
		 * Ignore SCN - wait for PLOGI response.
		 */
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_hal_online(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		PLOGI is complete. Awaiting BFA rport online callback. FC-4s
 * 		are offline.
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
		bfa_fcs_rport_online_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_logorcv);
		bfa_rport_offline(rport->bfa_rport);
		break;

	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_offline);
		bfa_rport_offline(rport->bfa_rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogiacc_sending);
		bfa_rport_offline(rport->bfa_rport);
		bfa_fcs_rport_send_plogiacc(rport, NULL);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_logosend);
		bfa_rport_offline(rport->bfa_rport);
		break;

	case RPSM_EVENT_SCN:
		/**
		 * @todo
		 * Ignore SCN - PLOGI just completed, FC-4 login should detect
		 * device failures.
		 */
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Rport is ONLINE. FC-4s active.
 */
static void
bfa_fcs_rport_sm_online(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_SCN:
		/**
		 * Pause FC-4 activity till rport is authenticated.
		 * In switched fabrics, check presence of device in nameserver
		 * first.
		 */
		bfa_fcs_rport_fc4_pause(rport);

		if (bfa_fcs_fabric_is_switched(rport->port->fabric)) {
			bfa_sm_set_state(rport,
					 bfa_fcs_rport_sm_nsquery_sending);
			rport->ns_retries = 0;
			bfa_fcs_rport_send_gidpn(rport, NULL);
		} else {
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_adisc_sending);
			bfa_fcs_rport_send_adisc(rport, NULL);
		}
		break;

	case RPSM_EVENT_PLOGI_RCVD:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		An SCN event is received in ONLINE state. NS query is being sent
 * 		prior to ADISC authentication with rport. FC-4s are paused.
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
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_SCN:
		/**
		 * ignore SCN, wait for response to query itself
		 */
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_IMP:
		rport->pid = 0;
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_offline_action(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 	An SCN event is received in ONLINE state. NS query is sent to rport.
 * 	FC-4s are paused.
 */
static void
bfa_fcs_rport_sm_nsquery(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_ACCEPTED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_adisc_sending);
		bfa_fcs_rport_send_adisc(rport, NULL);
		break;

	case RPSM_EVENT_FAILED:
		rport->ns_retries++;
		if (rport->ns_retries < BFA_FCS_RPORT_MAX_RETRIES) {
			bfa_sm_set_state(rport,
					 bfa_fcs_rport_sm_nsquery_sending);
			bfa_fcs_rport_send_gidpn(rport, NULL);
		} else {
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
			bfa_fcs_rport_offline_action(rport);
		}
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_SCN:
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
	case RPSM_EVENT_PLOGI_RCVD:
	case RPSM_EVENT_LOGO_IMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_offline_action(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 	An SCN event is received in ONLINE state. ADISC is being sent for
 * 	authenticating with rport. FC-4s are paused.
 */
static void
bfa_fcs_rport_sm_adisc_sending(struct bfa_fcs_rport_s *rport,
			       enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_adisc);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_SCN:
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_offline_action(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		An SCN event is received in ONLINE state. ADISC is to rport.
 * 		FC-4s are paused.
 */
static void
bfa_fcs_rport_sm_adisc(struct bfa_fcs_rport_s *rport, enum rport_event event)
{
	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->pid);
	bfa_trc(rport->fcs, event);

	switch (event) {
	case RPSM_EVENT_ACCEPTED:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_online);
		bfa_fcs_rport_fc4_resume(rport);
		break;

	case RPSM_EVENT_PLOGI_RCVD:
		/**
		 * Too complex to cleanup FC-4 & rport and then acc to PLOGI.
		 * At least go offline when a PLOGI is received.
		 */
		bfa_fcxp_discard(rport->fcxp);
		/*
		 * !!! fall through !!!
		 */

	case RPSM_EVENT_FAILED:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_SCN:
		/**
		 * already processing RSCN
		 */
		break;

	case RPSM_EVENT_LOGO_IMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_offline);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_offline_action(rport);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logorcv);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_offline_action(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Rport has sent LOGO. Awaiting FC-4 offline completion callback.
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
		bfa_rport_offline(rport->bfa_rport);
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		break;

	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_ADDRESS_CHANGE:
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		LOGO needs to be sent to rport. Awaiting FC-4 offline completion
 * 		callback.
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
		bfa_rport_offline(rport->bfa_rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 	Rport is going offline. Awaiting FC-4 offline completion callback.
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
		bfa_rport_offline(rport->bfa_rport);
		break;

	case RPSM_EVENT_SCN:
	case RPSM_EVENT_LOGO_IMP:
	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_ADDRESS_CHANGE:
		/**
		 * rport is already going offline.
		 * SCN - ignore and wait till transitioning to offline state
		 */
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_fc4_logosend);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Rport is offline. FC-4s are offline. Awaiting BFA rport offline
 * 		callback.
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
	case RPSM_EVENT_ADDRESS_CHANGE:
		if (bfa_fcs_port_is_online(rport->port)) {
			bfa_sm_set_state(rport,
					 bfa_fcs_rport_sm_nsdisc_sending);
			rport->ns_retries = 0;
			bfa_fcs_rport_send_gidpn(rport, NULL);
		} else {
			rport->pid = 0;
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
			bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					bfa_fcs_rport_del_timeout);
		}
		break;

	case RPSM_EVENT_DELETE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_SCN:
	case RPSM_EVENT_LOGO_RCVD:
		/**
		 * Ignore, already offline.
		 */
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Rport is offline. FC-4s are offline. Awaiting BFA rport offline
 * 		callback to send LOGO accept.
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
		if (rport->pid)
			bfa_fcs_rport_send_logo_acc(rport);
		/*
		 * If the lport is online and if the rport is not a well known
		 * address port, we try to re-discover the r-port.
		 */
		if (bfa_fcs_port_is_online(rport->port)
		    && (!BFA_FCS_PID_IS_WKA(rport->pid))) {
			bfa_sm_set_state(rport,
					 bfa_fcs_rport_sm_nsdisc_sending);
			rport->ns_retries = 0;
			bfa_fcs_rport_send_gidpn(rport, NULL);
		} else {
			/*
			 * if it is not a well known address, reset the pid to
			 *
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
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_logosend);
		break;

	case RPSM_EVENT_LOGO_IMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hcb_offline);
		break;

	case RPSM_EVENT_LOGO_RCVD:
		/**
		 * Ignore - already processing a LOGO.
		 */
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * Rport is being deleted. FC-4s are offline. Awaiting BFA rport offline
 * callback to send LOGO.
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
	case RPSM_EVENT_ADDRESS_CHANGE:
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Rport is being deleted. FC-4s are offline. LOGO is being sent.
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
		/*
		 * Once LOGO is sent, we donot wait for the response
		 */
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcs_rport_free(rport);
		break;

	case RPSM_EVENT_SCN:
	case RPSM_EVENT_ADDRESS_CHANGE:
		break;

	case RPSM_EVENT_LOGO_RCVD:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_free(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Rport is offline. FC-4s are offline. BFA rport is offline.
 * 		Timer active to delete stale rport.
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

	case RPSM_EVENT_SCN:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		bfa_timer_stop(&rport->timer);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_gidpn(rport, NULL);
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
	case RPSM_EVENT_LOGO_IMP:
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_hal_online(rport);
		break;

	case RPSM_EVENT_PLOGI_SEND:
		bfa_timer_stop(&rport->timer);
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_plogi_sending);
		rport->plogi_retries = 0;
		bfa_fcs_rport_send_plogi(rport, NULL);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 	Rport address has changed. Nameserver discovery request is being sent.
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

	case RPSM_EVENT_SCN:
	case RPSM_EVENT_LOGO_RCVD:
	case RPSM_EVENT_PLOGI_SEND:
		break;

	case RPSM_EVENT_ADDRESS_CHANGE:
		rport->ns_retries = 0;	/* reset the retry count */
		break;

	case RPSM_EVENT_LOGO_IMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_timer_start(rport->fcs->bfa, &rport->timer,
				bfa_fcs_rport_timeout, rport,
				bfa_fcs_rport_del_timeout);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_fcxp_walloc_cancel(rport->fcs->bfa, &rport->fcxp_wqe);
		bfa_fcs_rport_hal_online(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * 		Nameserver discovery failed. Waiting for timeout to retry.
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
		bfa_fcs_rport_send_gidpn(rport, NULL);
		break;

	case RPSM_EVENT_SCN:
	case RPSM_EVENT_ADDRESS_CHANGE:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_nsdisc_sending);
		bfa_timer_stop(&rport->timer);
		rport->ns_retries = 0;
		bfa_fcs_rport_send_gidpn(rport, NULL);
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

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_timer_stop(&rport->timer);
		bfa_fcs_rport_hal_online(rport);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 * Rport address has changed. Nameserver discovery request is sent.
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
			bfa_fcs_rport_send_gidpn(rport, NULL);
		}
		break;

	case RPSM_EVENT_FAILED:
		rport->ns_retries++;
		if (rport->ns_retries < BFA_FCS_RPORT_MAX_RETRIES) {
			bfa_sm_set_state(rport,
					 bfa_fcs_rport_sm_nsdisc_sending);
			bfa_fcs_rport_send_gidpn(rport, NULL);
		} else {
			rport->pid = 0;
			bfa_sm_set_state(rport, bfa_fcs_rport_sm_offline);
			bfa_timer_start(rport->fcs->bfa, &rport->timer,
					bfa_fcs_rport_timeout, rport,
					bfa_fcs_rport_del_timeout);
		};
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

	case RPSM_EVENT_SCN:
		/**
		 * ignore, wait for NS query response
		 */
		break;

	case RPSM_EVENT_LOGO_RCVD:
		/**
		 * Not logged-in yet. Accept LOGO.
		 */
		bfa_fcs_rport_send_logo_acc(rport);
		break;

	case RPSM_EVENT_PLOGI_COMP:
		bfa_sm_set_state(rport, bfa_fcs_rport_sm_hal_online);
		bfa_fcxp_discard(rport->fcxp);
		bfa_fcs_rport_hal_online(rport);
		break;

	default:
		bfa_assert(0);
	}
}



/**
 *  fcs_rport_private FCS RPORT provate functions
 */

static void
bfa_fcs_rport_send_plogi(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_port_s *port = rport->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(rport->fcs, rport->pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				    bfa_fcs_rport_send_plogi, rport);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_plogi_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
			     bfa_fcs_port_get_fcid(port), 0,
			     port->port_cfg.pwwn, port->port_cfg.nwwn,
			     bfa_pport_get_maxfrsize(port->fcs->bfa));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_rport_plogi_response,
		      (void *)rport, FC_MAX_PDUSZ, FC_RA_TOV);

	rport->stats.plogis++;
	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_rport_plogi_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
			     bfa_status_t req_status, u32 rsp_len,
			     u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *)cbarg;
	struct fc_logi_s	*plogi_rsp;
	struct fc_ls_rjt_s	*ls_rjt;
	struct bfa_fcs_rport_s *twin;
	struct list_head *qe;

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

	/**
	 * Check for failure first.
	 */
	if (plogi_rsp->els_cmd.els_code != FC_ELS_ACC) {
		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(rport->fcs, ls_rjt->reason_code);
		bfa_trc(rport->fcs, ls_rjt->reason_code_expl);

		rport->stats.plogi_rejects++;
		bfa_sm_send_event(rport, RPSM_EVENT_FAILED);
		return;
	}

	/**
	 * PLOGI is complete. Make sure this device is not one of the known
	 * device with a new FC port address.
	 */
	list_for_each(qe, &rport->port->rport_q) {
		twin = (struct bfa_fcs_rport_s *)qe;
		if (twin == rport)
			continue;
		if (!rport->pwwn && (plogi_rsp->port_name == twin->pwwn)) {
			bfa_trc(rport->fcs, twin->pid);
			bfa_trc(rport->fcs, rport->pid);

			/*
			 * Update plogi stats in twin
			 */
			twin->stats.plogis += rport->stats.plogis;
			twin->stats.plogi_rejects += rport->stats.plogi_rejects;
			twin->stats.plogi_timeouts +=
				rport->stats.plogi_timeouts;
			twin->stats.plogi_failed += rport->stats.plogi_failed;
			twin->stats.plogi_rcvd += rport->stats.plogi_rcvd;
			twin->stats.plogi_accs++;

			bfa_fcs_rport_delete(rport);

			bfa_fcs_rport_update(twin, plogi_rsp);
			twin->pid = rsp_fchs->s_id;
			bfa_sm_send_event(twin, RPSM_EVENT_PLOGI_COMP);
			return;
		}
	}

	/**
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
	struct bfa_fcs_port_s *port = rport->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_trc(rport->fcs, rport->reply_oxid);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				    bfa_fcs_rport_send_plogiacc, rport);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_plogi_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
				 bfa_fcs_port_get_fcid(port), rport->reply_oxid,
				 port->port_cfg.pwwn, port->port_cfg.nwwn,
				 bfa_pport_get_maxfrsize(port->fcs->bfa));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);

	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_rport_send_adisc(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_port_s *port = rport->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(rport->fcs, rport->pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				    bfa_fcs_rport_send_adisc, rport);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_adisc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
			     bfa_fcs_port_get_fcid(port), 0,
			     port->port_cfg.pwwn, port->port_cfg.nwwn);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_rport_adisc_response,
		      rport, FC_MAX_PDUSZ, FC_RA_TOV);

	rport->stats.adisc_sent++;
	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_rport_adisc_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
			     bfa_status_t req_status, u32 rsp_len,
			     u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *)cbarg;
	void           *pld = bfa_fcxp_get_rspbuf(fcxp);
	struct fc_ls_rjt_s    *ls_rjt;

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
bfa_fcs_rport_send_gidpn(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_port_s *port = rport->port;
	struct fchs_s          fchs;
	struct bfa_fcxp_s *fcxp;
	int             len;

	bfa_trc(rport->fcs, rport->pid);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				    bfa_fcs_rport_send_gidpn, rport);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_gidpn_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_fcs_port_get_fcid(port), 0, rport->pwwn);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_rport_gidpn_response,
		      (void *)rport, FC_MAX_PDUSZ, FC_RA_TOV);

	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_rport_gidpn_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
			     bfa_status_t req_status, u32 rsp_len,
			     u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *)cbarg;
	struct bfa_fcs_rport_s *twin;
	struct list_head *qe;
	struct ct_hdr_s       	*cthdr;
	struct fcgs_gidpn_resp_s	*gidpn_rsp;

	bfa_trc(rport->fcs, rport->pwwn);

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		/*
		 * Check if the pid is the same as before.
		 */
		gidpn_rsp = (struct fcgs_gidpn_resp_s *) (cthdr + 1);

		if (gidpn_rsp->dap == rport->pid) {
			/*
			 * Device is online
			 */
			bfa_sm_send_event(rport, RPSM_EVENT_ACCEPTED);
		} else {
			/*
			 * Device's PID has changed. We need to cleanup and
			 * re-login. If there is another device with the the
			 * newly discovered pid, send an scn notice so that its
			 * new pid can be discovered.
			 */
			list_for_each(qe, &rport->port->rport_q) {
				twin = (struct bfa_fcs_rport_s *)qe;
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

/**
 *    Called to send a logout to the rport.
 */
static void
bfa_fcs_rport_send_logo(void *rport_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_port_s *port;
	struct fchs_s          fchs;
	struct bfa_fcxp_s *fcxp;
	u16        len;

	bfa_trc(rport->fcs, rport->pid);

	port = rport->port;

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &rport->fcxp_wqe,
				    bfa_fcs_rport_send_logo, rport);
		return;
	}
	rport->fcxp = fcxp;

	len = fc_logo_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
			    bfa_fcs_port_get_fcid(port), 0,
			    bfa_fcs_port_get_pwwn(port));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, NULL, rport, FC_MAX_PDUSZ,
		      FC_ED_TOV);

	rport->stats.logos++;
	bfa_fcxp_discard(rport->fcxp);
	bfa_sm_send_event(rport, RPSM_EVENT_FCXP_SENT);
}

/**
 *    Send ACC for a LOGO received.
 */
static void
bfa_fcs_rport_send_logo_acc(void *rport_cbarg)
{
	struct bfa_fcs_rport_s *rport = rport_cbarg;
	struct bfa_fcs_port_s *port;
	struct fchs_s          fchs;
	struct bfa_fcxp_s *fcxp;
	u16        len;

	bfa_trc(rport->fcs, rport->pid);

	port = rport->port;

	fcxp = bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp)
		return;

	rport->stats.logo_rcvd++;
	len = fc_logo_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
				bfa_fcs_port_get_fcid(port), rport->reply_oxid);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);
}

/**
 *     This routine will be called by bfa_timer on timer timeouts.
 *
 * 	param[in] 	rport 			- pointer to bfa_fcs_port_ns_t.
 * 	param[out]	rport_status 	- pointer to return vport status in
 *
 * 	return
 * 		void
 *
*  	Special Considerations:
 *
 * 	note
 */
static void
bfa_fcs_rport_timeout(void *arg)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *)arg;

	rport->stats.plogi_timeouts++;
	bfa_sm_send_event(rport, RPSM_EVENT_TIMEOUT);
}

static void
bfa_fcs_rport_process_prli(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u16 len)
{
	struct bfa_fcxp_s *fcxp;
	struct fchs_s          fchs;
	struct bfa_fcs_port_s *port = rport->port;
	struct fc_prli_s      *prli;

	bfa_trc(port->fcs, rx_fchs->s_id);
	bfa_trc(port->fcs, rx_fchs->d_id);

	rport->stats.prli_rcvd++;

	if (BFA_FCS_VPORT_IS_TARGET_MODE(port)) {
		/*
		 * Target Mode : Let the fcptm handle it
		 */
		bfa_fcs_tin_rx_prli(rport->tin, rx_fchs, len);
		return;
	}

	/*
	 * We are either in Initiator or ipfc Mode
	 */
	prli = (struct fc_prli_s *) (rx_fchs + 1);

	if (prli->parampage.servparams.initiator) {
		bfa_trc(rport->fcs, prli->parampage.type);
		rport->scsi_function = BFA_RPORT_INITIATOR;
		bfa_fcs_itnim_is_initiator(rport->itnim);
	} else {
		/*
		 * @todo: PRLI from a target ?
		 */
		bfa_trc(port->fcs, rx_fchs->s_id);
		rport->scsi_function = BFA_RPORT_TARGET;
	}

	fcxp = bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp)
		return;

	len = fc_prli_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rx_fchs->s_id,
				bfa_fcs_port_get_fcid(port), rx_fchs->ox_id,
				port->port_cfg.roles);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);
}

static void
bfa_fcs_rport_process_rpsc(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u16 len)
{
	struct bfa_fcxp_s *fcxp;
	struct fchs_s          fchs;
	struct bfa_fcs_port_s *port = rport->port;
	struct fc_rpsc_speed_info_s speeds;
	struct bfa_pport_attr_s pport_attr;

	bfa_trc(port->fcs, rx_fchs->s_id);
	bfa_trc(port->fcs, rx_fchs->d_id);

	rport->stats.rpsc_rcvd++;
	speeds.port_speed_cap =
		RPSC_SPEED_CAP_1G | RPSC_SPEED_CAP_2G | RPSC_SPEED_CAP_4G |
		RPSC_SPEED_CAP_8G;

	/*
	 * get curent speed from pport attributes from BFA
	 */
	bfa_pport_get_attr(port->fcs->bfa, &pport_attr);

	speeds.port_op_speed = fc_bfa_speed_to_rpsc_operspeed(pport_attr.speed);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp)
		return;

	len = fc_rpsc_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rx_fchs->s_id,
				bfa_fcs_port_get_fcid(port), rx_fchs->ox_id,
				&speeds);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);
}

static void
bfa_fcs_rport_process_adisc(struct bfa_fcs_rport_s *rport,
			struct fchs_s *rx_fchs, u16 len)
{
	struct bfa_fcxp_s *fcxp;
	struct fchs_s          fchs;
	struct bfa_fcs_port_s *port = rport->port;
	struct fc_adisc_s      *adisc;

	bfa_trc(port->fcs, rx_fchs->s_id);
	bfa_trc(port->fcs, rx_fchs->d_id);

	rport->stats.adisc_rcvd++;

	if (BFA_FCS_VPORT_IS_TARGET_MODE(port)) {
		/*
		 * @todo : Target Mode handling
		 */
		bfa_trc(port->fcs, rx_fchs->d_id);
		bfa_assert(0);
		return;
	}

	adisc = (struct fc_adisc_s *) (rx_fchs + 1);

	/*
	 * Accept if the itnim for this rport is online. Else reject the ADISC
	 */
	if (bfa_fcs_itnim_get_online_state(rport->itnim) == BFA_STATUS_OK) {

		fcxp = bfa_fcs_fcxp_alloc(port->fcs);
		if (!fcxp)
			return;

		len = fc_adisc_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
					 rx_fchs->s_id,
					 bfa_fcs_port_get_fcid(port),
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
	struct bfa_fcs_port_s *port = rport->port;
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
bfa_fcs_rport_fc4_pause(struct bfa_fcs_rport_s *rport)
{
	if (bfa_fcs_port_is_initiator(rport->port))
		bfa_fcs_itnim_pause(rport->itnim);

	if (bfa_fcs_port_is_target(rport->port))
		bfa_fcs_tin_pause(rport->tin);
}

static void
bfa_fcs_rport_fc4_resume(struct bfa_fcs_rport_s *rport)
{
	if (bfa_fcs_port_is_initiator(rport->port))
		bfa_fcs_itnim_resume(rport->itnim);

	if (bfa_fcs_port_is_target(rport->port))
		bfa_fcs_tin_resume(rport->tin);
}

static struct bfa_fcs_rport_s *
bfa_fcs_rport_alloc(struct bfa_fcs_port_s *port, wwn_t pwwn, u32 rpid)
{
	struct bfa_fcs_s *fcs = port->fcs;
	struct bfa_fcs_rport_s *rport;
	struct bfad_rport_s *rport_drv;

	/**
	 * allocate rport
	 */
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

	/**
	 * allocate BFA rport
	 */
	rport->bfa_rport = bfa_rport_create(port->fcs->bfa, rport);
	if (!rport->bfa_rport) {
		bfa_trc(fcs, rpid);
		kfree(rport_drv);
		return NULL;
	}

	/**
	 * allocate FC-4s
	 */
	bfa_assert(bfa_fcs_port_is_initiator(port) ^
		   bfa_fcs_port_is_target(port));

	if (bfa_fcs_port_is_initiator(port)) {
		rport->itnim = bfa_fcs_itnim_create(rport);
		if (!rport->itnim) {
			bfa_trc(fcs, rpid);
			bfa_rport_delete(rport->bfa_rport);
			kfree(rport_drv);
			return NULL;
		}
	}

	if (bfa_fcs_port_is_target(port)) {
		rport->tin = bfa_fcs_tin_create(rport);
		if (!rport->tin) {
			bfa_trc(fcs, rpid);
			bfa_rport_delete(rport->bfa_rport);
			kfree(rport_drv);
			return NULL;
		}
	}

	bfa_fcs_port_add_rport(port, rport);

	bfa_sm_set_state(rport, bfa_fcs_rport_sm_uninit);

	/*
	 * Initialize the Rport Features(RPF) Sub Module
	 */
	if (!BFA_FCS_PID_IS_WKA(rport->pid))
		bfa_fcs_rpf_init(rport);

	return rport;
}


static void
bfa_fcs_rport_free(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_port_s *port = rport->port;

	/**
	 * - delete FC-4s
	 * - delete BFA rport
	 * - remove from queue of rports
	 */
	if (bfa_fcs_port_is_initiator(port))
		bfa_fcs_itnim_delete(rport->itnim);

	if (bfa_fcs_port_is_target(port))
		bfa_fcs_tin_delete(rport->tin);

	bfa_rport_delete(rport->bfa_rport);
	bfa_fcs_port_del_rport(port, rport);
	kfree(rport->rp_drv);
}

static void
bfa_fcs_rport_aen_post(struct bfa_fcs_rport_s *rport,
		       enum bfa_rport_aen_event event,
		       struct bfa_rport_aen_data_s *data)
{
	union bfa_aen_data_u aen_data;
	struct bfa_log_mod_s *logmod = rport->fcs->logm;
	wwn_t           lpwwn = bfa_fcs_port_get_pwwn(rport->port);
	wwn_t           rpwwn = rport->pwwn;
	char            lpwwn_ptr[BFA_STRING_32];
	char            rpwwn_ptr[BFA_STRING_32];
	char           *prio_str[] = { "unknown", "high", "medium", "low" };

	wwn2str(lpwwn_ptr, lpwwn);
	wwn2str(rpwwn_ptr, rpwwn);

	switch (event) {
	case BFA_RPORT_AEN_ONLINE:
		bfa_log(logmod, BFA_AEN_RPORT_ONLINE, rpwwn_ptr, lpwwn_ptr);
		break;
	case BFA_RPORT_AEN_OFFLINE:
		bfa_log(logmod, BFA_AEN_RPORT_OFFLINE, rpwwn_ptr, lpwwn_ptr);
		break;
	case BFA_RPORT_AEN_DISCONNECT:
		bfa_log(logmod, BFA_AEN_RPORT_DISCONNECT, rpwwn_ptr, lpwwn_ptr);
		break;
	case BFA_RPORT_AEN_QOS_PRIO:
		aen_data.rport.priv.qos = data->priv.qos;
		bfa_log(logmod, BFA_AEN_RPORT_QOS_PRIO,
			prio_str[aen_data.rport.priv.qos.qos_priority],
			rpwwn_ptr, lpwwn_ptr);
		break;
	case BFA_RPORT_AEN_QOS_FLOWID:
		aen_data.rport.priv.qos = data->priv.qos;
		bfa_log(logmod, BFA_AEN_RPORT_QOS_FLOWID,
			aen_data.rport.priv.qos.qos_flow_id, rpwwn_ptr,
			lpwwn_ptr);
		break;
	default:
		break;
	}

	aen_data.rport.vf_id = rport->port->fabric->vf_id;
	aen_data.rport.ppwwn =
		bfa_fcs_port_get_pwwn(bfa_fcs_get_base_port(rport->fcs));
	aen_data.rport.lpwwn = lpwwn;
	aen_data.rport.rpwwn = rpwwn;
}

static void
bfa_fcs_rport_online_action(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_port_s *port = rport->port;

	rport->stats.onlines++;

	if (bfa_fcs_port_is_initiator(port)) {
		bfa_fcs_itnim_rport_online(rport->itnim);
		if (!BFA_FCS_PID_IS_WKA(rport->pid))
			bfa_fcs_rpf_rport_online(rport);
	};

	if (bfa_fcs_port_is_target(port))
		bfa_fcs_tin_rport_online(rport->tin);

	/*
	 * Don't post events for well known addresses
	 */
	if (!BFA_FCS_PID_IS_WKA(rport->pid))
		bfa_fcs_rport_aen_post(rport, BFA_RPORT_AEN_ONLINE, NULL);
}

static void
bfa_fcs_rport_offline_action(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_port_s *port = rport->port;

	rport->stats.offlines++;

	/*
	 * Don't post events for well known addresses
	 */
	if (!BFA_FCS_PID_IS_WKA(rport->pid)) {
		if (bfa_fcs_port_is_online(rport->port) == BFA_TRUE) {
			bfa_fcs_rport_aen_post(rport, BFA_RPORT_AEN_DISCONNECT,
					       NULL);
		} else {
			bfa_fcs_rport_aen_post(rport, BFA_RPORT_AEN_OFFLINE,
					       NULL);
		}
	}

	if (bfa_fcs_port_is_initiator(port)) {
		bfa_fcs_itnim_rport_offline(rport->itnim);
		if (!BFA_FCS_PID_IS_WKA(rport->pid))
			bfa_fcs_rpf_rport_offline(rport);
	}

	if (bfa_fcs_port_is_target(port))
		bfa_fcs_tin_rport_offline(rport->tin);
}

/**
 * Update rport parameters from PLOGI or PLOGI accept.
 */
static void
bfa_fcs_rport_update(struct bfa_fcs_rport_s *rport, struct fc_logi_s *plogi)
{
	struct bfa_fcs_port_s *port = rport->port;

	/**
	 * - port name
	 * - node name
	 */
	rport->pwwn = plogi->port_name;
	rport->nwwn = plogi->node_name;

	/**
	 * - class of service
	 */
	rport->fc_cos = 0;
	if (plogi->class3.class_valid)
		rport->fc_cos = FC_CLASS_3;

	if (plogi->class2.class_valid)
		rport->fc_cos |= FC_CLASS_2;

	/**
	 * - CISC
	 * - MAX receive frame size
	 */
	rport->cisc = plogi->csp.cisc;
	rport->maxfrsize = bfa_os_ntohs(plogi->class3.rxsz);

	bfa_trc(port->fcs, bfa_os_ntohs(plogi->csp.bbcred));
	bfa_trc(port->fcs, port->fabric->bb_credit);
	/**
	 * Direct Attach P2P mode :
	 * This is to handle a bug (233476) in IBM targets in Direct Attach
	 * Mode. Basically, in FLOGI Accept the target would have erroneously
	 * set the BB Credit to the value used in the FLOGI sent by the HBA.
	 * It uses the correct value (its own BB credit) in PLOGI.
	 */
	if ((!bfa_fcs_fabric_is_switched(port->fabric))
	    && (bfa_os_ntohs(plogi->csp.bbcred) < port->fabric->bb_credit)) {

		bfa_trc(port->fcs, bfa_os_ntohs(plogi->csp.bbcred));
		bfa_trc(port->fcs, port->fabric->bb_credit);

		port->fabric->bb_credit = bfa_os_ntohs(plogi->csp.bbcred);
		bfa_pport_set_tx_bbcredit(port->fcs->bfa,
					  port->fabric->bb_credit);
	}

}

/**
 *   Called to handle LOGO received from an existing remote port.
 */
static void
bfa_fcs_rport_process_logo(struct bfa_fcs_rport_s *rport, struct fchs_s *fchs)
{
	rport->reply_oxid = fchs->ox_id;
	bfa_trc(rport->fcs, rport->reply_oxid);

	rport->stats.logo_rcvd++;
	bfa_sm_send_event(rport, RPSM_EVENT_LOGO_RCVD);
}



/**
 *  fcs_rport_public FCS rport public interfaces
 */

/**
 * 	Called by bport/vport to create a remote port instance for a discovered
 * 	remote device.
 *
 * @param[in] port	- base port or vport
 * @param[in] rpid	- remote port ID
 *
 * @return None
 */
struct bfa_fcs_rport_s *
bfa_fcs_rport_create(struct bfa_fcs_port_s *port, u32 rpid)
{
	struct bfa_fcs_rport_s *rport;

	bfa_trc(port->fcs, rpid);
	rport = bfa_fcs_rport_alloc(port, WWN_NULL, rpid);
	if (!rport)
		return NULL;

	bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_SEND);
	return rport;
}

/**
 * Called to create a rport for which only the wwn is known.
 *
 * @param[in] port	- base port
 * @param[in] rpwwn	- remote port wwn
 *
 * @return None
 */
struct bfa_fcs_rport_s *
bfa_fcs_rport_create_by_wwn(struct bfa_fcs_port_s *port, wwn_t rpwwn)
{
	struct bfa_fcs_rport_s *rport;

	bfa_trc(port->fcs, rpwwn);
	rport = bfa_fcs_rport_alloc(port, rpwwn, 0);
	if (!rport)
		return NULL;

	bfa_sm_send_event(rport, RPSM_EVENT_ADDRESS_DISC);
	return rport;
}

/**
 * Called by bport in private loop topology to indicate that a
 * rport has been discovered and plogi has been completed.
 *
 * @param[in] port	- base port or vport
 * @param[in] rpid	- remote port ID
 */
void
bfa_fcs_rport_start(struct bfa_fcs_port_s *port, struct fchs_s *fchs,
			struct fc_logi_s *plogi)
{
	struct bfa_fcs_rport_s *rport;

	rport = bfa_fcs_rport_alloc(port, WWN_NULL, fchs->s_id);
	if (!rport)
		return;

	bfa_fcs_rport_update(rport, plogi);

	bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_COMP);
}

/**
 *   Called by bport/vport to handle PLOGI received from a new remote port.
 *   If an existing rport does a plogi, it will be handled separately.
 */
void
bfa_fcs_rport_plogi_create(struct bfa_fcs_port_s *port, struct fchs_s *fchs,
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

static int
wwn_compare(wwn_t wwn1, wwn_t wwn2)
{
	u8        *b1 = (u8 *) &wwn1;
	u8        *b2 = (u8 *) &wwn2;
	int             i;

	for (i = 0; i < sizeof(wwn_t); i++) {
		if (b1[i] < b2[i])
			return -1;
		if (b1[i] > b2[i])
			return 1;
	}
	return 0;
}

/**
 *   Called by bport/vport to handle PLOGI received from an existing
 * 	 remote port.
 */
void
bfa_fcs_rport_plogi(struct bfa_fcs_rport_s *rport, struct fchs_s *rx_fchs,
		    struct fc_logi_s *plogi)
{
	/**
	 * @todo Handle P2P and initiator-initiator.
	 */

	bfa_fcs_rport_update(rport, plogi);

	rport->reply_oxid = rx_fchs->ox_id;
	bfa_trc(rport->fcs, rport->reply_oxid);

	/**
	 * In Switched fabric topology,
	 * PLOGI to each other. If our pwwn is smaller, ignore it,
	 * if it is not a well known address.
	 * If the link topology is N2N,
	 * this Plogi should be accepted.
	 */
	if ((wwn_compare(rport->port->port_cfg.pwwn, rport->pwwn) == -1)
	    && (bfa_fcs_fabric_is_switched(rport->port->fabric))
	    && (!BFA_FCS_PID_IS_WKA(rport->pid))) {
		bfa_trc(rport->fcs, rport->pid);
		return;
	}

	rport->stats.plogi_rcvd++;
	bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_RCVD);
}

/**
 * Called by bport/vport to delete a remote port instance.
 *
* Rport delete is called under the following conditions:
 * 		- vport is deleted
 * 		- vf is deleted
 * 		- explicit request from OS to delete rport (vmware)
 */
void
bfa_fcs_rport_delete(struct bfa_fcs_rport_s *rport)
{
	bfa_sm_send_event(rport, RPSM_EVENT_DELETE);
}

/**
 * Called by bport/vport to  when a target goes offline.
 *
 */
void
bfa_fcs_rport_offline(struct bfa_fcs_rport_s *rport)
{
	bfa_sm_send_event(rport, RPSM_EVENT_LOGO_IMP);
}

/**
 * Called by bport in n2n when a target (attached port) becomes online.
 *
 */
void
bfa_fcs_rport_online(struct bfa_fcs_rport_s *rport)
{
	bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_SEND);
}

/**
 *   Called by bport/vport to notify SCN for the remote port
 */
void
bfa_fcs_rport_scn(struct bfa_fcs_rport_s *rport)
{

	rport->stats.rscns++;
	bfa_sm_send_event(rport, RPSM_EVENT_SCN);
}

/**
 *   Called by  fcpim to notify that the ITN cleanup is done.
 */
void
bfa_fcs_rport_itnim_ack(struct bfa_fcs_rport_s *rport)
{
	bfa_sm_send_event(rport, RPSM_EVENT_FC4_OFFLINE);
}

/**
 *   Called by fcptm to notify that the ITN cleanup is done.
 */
void
bfa_fcs_rport_tin_ack(struct bfa_fcs_rport_s *rport)
{
	bfa_sm_send_event(rport, RPSM_EVENT_FC4_OFFLINE);
}

/**
 *     This routine BFA callback for bfa_rport_online() call.
 *
 * 	param[in] 	cb_arg	-  rport struct.
 *
 * 	return
 * 		void
 *
* 	Special Considerations:
 *
 * 	note
 */
void
bfa_cb_rport_online(void *cbarg)
{

	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *)cbarg;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_sm_send_event(rport, RPSM_EVENT_HCB_ONLINE);
}

/**
 *     This routine BFA callback for bfa_rport_offline() call.
 *
 * 	param[in] 	rport 	-
 *
 * 	return
 * 		void
 *
 * 	Special Considerations:
 *
 * 	note
 */
void
bfa_cb_rport_offline(void *cbarg)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *)cbarg;

	bfa_trc(rport->fcs, rport->pwwn);
	bfa_sm_send_event(rport, RPSM_EVENT_HCB_OFFLINE);
}

/**
 * This routine is a static BFA callback when there is a QoS flow_id
 * change notification
 *
 * @param[in] 	rport 	-
 *
 * @return  	void
 *
 * Special Considerations:
 *
 * @note
 */
void
bfa_cb_rport_qos_scn_flowid(void *cbarg,
			    struct bfa_rport_qos_attr_s old_qos_attr,
			    struct bfa_rport_qos_attr_s new_qos_attr)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *)cbarg;
	struct bfa_rport_aen_data_s aen_data;

	bfa_trc(rport->fcs, rport->pwwn);
	aen_data.priv.qos = new_qos_attr;
	bfa_fcs_rport_aen_post(rport, BFA_RPORT_AEN_QOS_FLOWID, &aen_data);
}

/**
 * This routine is a static BFA callback when there is a QoS priority
 * change notification
 *
 * @param[in] 	rport 	-
 *
 * @return 	void
 *
 * Special Considerations:
 *
 * @note
 */
void
bfa_cb_rport_qos_scn_prio(void *cbarg, struct bfa_rport_qos_attr_s old_qos_attr,
			  struct bfa_rport_qos_attr_s new_qos_attr)
{
	struct bfa_fcs_rport_s *rport = (struct bfa_fcs_rport_s *)cbarg;
	struct bfa_rport_aen_data_s aen_data;

	bfa_trc(rport->fcs, rport->pwwn);
	aen_data.priv.qos = new_qos_attr;
	bfa_fcs_rport_aen_post(rport, BFA_RPORT_AEN_QOS_PRIO, &aen_data);
}

/**
 * 		Called to process any unsolicted frames from this remote port
 */
void
bfa_fcs_rport_logo_imp(struct bfa_fcs_rport_s *rport)
{
	bfa_sm_send_event(rport, RPSM_EVENT_LOGO_IMP);
}

/**
 * 		Called to process any unsolicted frames from this remote port
 */
void
bfa_fcs_rport_uf_recv(struct bfa_fcs_rport_s *rport, struct fchs_s *fchs,
			u16 len)
{
	struct bfa_fcs_port_s *port = rport->port;
	struct fc_els_cmd_s   *els_cmd;

	bfa_trc(rport->fcs, fchs->s_id);
	bfa_trc(rport->fcs, fchs->d_id);
	bfa_trc(rport->fcs, fchs->type);

	if (fchs->type != FC_TYPE_ELS)
		return;

	els_cmd = (struct fc_els_cmd_s *) (fchs + 1);

	bfa_trc(rport->fcs, els_cmd->els_code);

	switch (els_cmd->els_code) {
	case FC_ELS_LOGO:
		bfa_fcs_rport_process_logo(rport, fchs);
		break;

	case FC_ELS_ADISC:
		bfa_fcs_rport_process_adisc(rport, fchs, len);
		break;

	case FC_ELS_PRLO:
		if (bfa_fcs_port_is_initiator(port))
			bfa_fcs_fcpim_uf_recv(rport->itnim, fchs, len);

		if (bfa_fcs_port_is_target(port))
			bfa_fcs_fcptm_uf_recv(rport->tin, fchs, len);
		break;

	case FC_ELS_PRLI:
		bfa_fcs_rport_process_prli(rport, fchs, len);
		break;

	case FC_ELS_RPSC:
		bfa_fcs_rport_process_rpsc(rport, fchs, len);
		break;

	default:
		bfa_fcs_rport_send_ls_rjt(rport, fchs,
					  FC_LS_RJT_RSN_CMD_NOT_SUPP,
					  FC_LS_RJT_EXP_NO_ADDL_INFO);
		break;
	}
}

/*
 * Send a LS reject
 */
static void
bfa_fcs_rport_send_ls_rjt(struct bfa_fcs_rport_s *rport, struct fchs_s *rx_fchs,
			  u8 reason_code, u8 reason_code_expl)
{
	struct bfa_fcs_port_s *port = rport->port;
	struct fchs_s          fchs;
	struct bfa_fcxp_s *fcxp;
	int             len;

	bfa_trc(rport->fcs, rx_fchs->s_id);

	fcxp = bfa_fcs_fcxp_alloc(rport->fcs);
	if (!fcxp)
		return;

	len = fc_ls_rjt_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rx_fchs->s_id,
			      bfa_fcs_port_get_fcid(port), rx_fchs->ox_id,
			      reason_code, reason_code_expl);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);
}

/**
 * Return state of rport.
 */
int
bfa_fcs_rport_get_state(struct bfa_fcs_rport_s *rport)
{
	return bfa_sm_to_state(rport_sm_table, rport->sm);
}

/**
 * 		 Called by the Driver to set rport delete/ageout timeout
 *
 * 	param[in]		rport timeout value in seconds.
 *
 * 	return None
 */
void
bfa_fcs_rport_set_del_timeout(u8 rport_tmo)
{
	/*
	 * convert to Millisecs
	 */
	if (rport_tmo > 0)
		bfa_fcs_rport_del_timeout = rport_tmo * 1000;
}
