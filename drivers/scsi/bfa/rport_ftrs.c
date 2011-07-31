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
 *  rport_ftrs.c Remote port features (RPF) implementation.
 */

#include <bfa.h>
#include <bfa_svc.h>
#include "fcbuild.h"
#include "fcs_rport.h"
#include "fcs_lport.h"
#include "fcs_trcmod.h"
#include "fcs_fcxp.h"
#include "fcs.h"

BFA_TRC_FILE(FCS, RPORT_FTRS);

#define BFA_FCS_RPF_RETRIES	(3)
#define BFA_FCS_RPF_RETRY_TIMEOUT  (1000) /* 1 sec (In millisecs) */

static void     bfa_fcs_rpf_send_rpsc2(void *rport_cbarg,
			struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_rpf_rpsc2_response(void *fcsarg,
			struct bfa_fcxp_s *fcxp, void *cbarg,
			bfa_status_t req_status, u32 rsp_len,
			u32 resid_len,
			struct fchs_s *rsp_fchs);
static void     bfa_fcs_rpf_timeout(void *arg);

/**
 *  fcs_rport_ftrs_sm FCS rport state machine events
 */

enum rpf_event {
	RPFSM_EVENT_RPORT_OFFLINE  = 1,     /*  Rport offline            */
	RPFSM_EVENT_RPORT_ONLINE   = 2,     /*  Rport online            */
	RPFSM_EVENT_FCXP_SENT      = 3,    /*  Frame from has been sent */
	RPFSM_EVENT_TIMEOUT  	   = 4,    /*  Rport SM timeout event   */
	RPFSM_EVENT_RPSC_COMP      = 5,
	RPFSM_EVENT_RPSC_FAIL      = 6,
	RPFSM_EVENT_RPSC_ERROR     = 7,
};

static void	bfa_fcs_rpf_sm_uninit(struct bfa_fcs_rpf_s *rpf,
					enum rpf_event event);
static void     bfa_fcs_rpf_sm_rpsc_sending(struct bfa_fcs_rpf_s *rpf,
					       enum rpf_event event);
static void     bfa_fcs_rpf_sm_rpsc(struct bfa_fcs_rpf_s *rpf,
					       enum rpf_event event);
static void 	bfa_fcs_rpf_sm_rpsc_retry(struct bfa_fcs_rpf_s *rpf,
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
			((bfa_lps_is_brcd_fabric(rport->port->fabric->lps)) ||
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
		if (rpf->rpsc_speed != BFA_PPORT_SPEED_UNKNOWN)
			bfa_rport_speed(rport->bfa_rport, rpf->rpsc_speed);
		else if (rpf->assigned_speed != BFA_PPORT_SPEED_UNKNOWN)
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
/**
 * Called when Rport is created.
 */
void  bfa_fcs_rpf_init(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_rpf_s *rpf = &rport->rpf;

	bfa_trc(rport->fcs, rport->pid);
	rpf->rport = rport;

	bfa_sm_set_state(rpf, bfa_fcs_rpf_sm_uninit);
}

/**
 * Called when Rport becomes online
 */
void  bfa_fcs_rpf_rport_online(struct bfa_fcs_rport_s *rport)
{
	bfa_trc(rport->fcs, rport->pid);

	if (__fcs_min_cfg(rport->port->fcs))
		return;

	if (bfa_fcs_fabric_is_switched(rport->port->fabric))
		bfa_sm_send_event(&rport->rpf, RPFSM_EVENT_RPORT_ONLINE);
}

/**
 * Called when Rport becomes offline
 */
void  bfa_fcs_rpf_rport_offline(struct bfa_fcs_rport_s *rport)
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
	struct bfa_fcs_rpf_s *rpf 	= (struct bfa_fcs_rpf_s *)rpf_cbarg;
	struct bfa_fcs_rport_s *rport = rpf->rport;
	struct bfa_fcs_port_s *port = rport->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(rport->fcs, rport->pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &rpf->fcxp_wqe,
					bfa_fcs_rpf_send_rpsc2, rpf);
		return;
	}
	rpf->fcxp = fcxp;

	len = fc_rpsc2_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rport->pid,
			    bfa_fcs_port_get_fcid(port), &rport->pid, 1);

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
	struct fc_ls_rjt_s    *ls_rjt;
	struct fc_rpsc2_acc_s  *rpsc2_acc;
	u16        num_ents;

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
		num_ents = bfa_os_ntohs(rpsc2_acc->num_pids);
		bfa_trc(rport->fcs, num_ents);
		if (num_ents > 0) {
			bfa_assert(rpsc2_acc->port_info[0].pid != rport->pid);
			bfa_trc(rport->fcs,
				bfa_os_ntohs(rpsc2_acc->port_info[0].pid));
			bfa_trc(rport->fcs,
				bfa_os_ntohs(rpsc2_acc->port_info[0].speed));
			bfa_trc(rport->fcs,
				bfa_os_ntohs(rpsc2_acc->port_info[0].index));
			bfa_trc(rport->fcs,
				rpsc2_acc->port_info[0].type);

			if (rpsc2_acc->port_info[0].speed == 0) {
				bfa_sm_send_event(rpf, RPFSM_EVENT_RPSC_ERROR);
				return;
			}

			rpf->rpsc_speed = fc_rpsc_operspeed_to_bfa_speed(
				bfa_os_ntohs(rpsc2_acc->port_info[0].speed));

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
