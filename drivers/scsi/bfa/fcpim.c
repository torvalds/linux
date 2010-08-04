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
 *  fcpim.c - FCP initiator mode i-t nexus state machine
 */

#include <bfa.h>
#include <bfa_svc.h>
#include "fcs_fcpim.h"
#include "fcs_rport.h"
#include "fcs_lport.h"
#include "fcs_trcmod.h"
#include "fcs_fcxp.h"
#include "fcs.h"
#include <fcs/bfa_fcs_fcpim.h>
#include <fcb/bfa_fcb_fcpim.h>
#include <aen/bfa_aen_itnim.h>

BFA_TRC_FILE(FCS, FCPIM);

/*
 * forward declarations
 */
static void     bfa_fcs_itnim_timeout(void *arg);
static void     bfa_fcs_itnim_free(struct bfa_fcs_itnim_s *itnim);
static void     bfa_fcs_itnim_send_prli(void *itnim_cbarg,
					struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_itnim_prli_response(void *fcsarg,
					    struct bfa_fcxp_s *fcxp,
					    void *cbarg,
					    bfa_status_t req_status,
					    u32 rsp_len,
					    u32 resid_len,
					    struct fchs_s *rsp_fchs);
static void     bfa_fcs_itnim_aen_post(struct bfa_fcs_itnim_s *itnim,
				       enum bfa_itnim_aen_event event);

/**
 *  fcs_itnim_sm FCS itnim state machine events
 */

enum bfa_fcs_itnim_event {
	BFA_FCS_ITNIM_SM_ONLINE = 1,	/*  rport online event */
	BFA_FCS_ITNIM_SM_OFFLINE = 2,	/*  rport offline */
	BFA_FCS_ITNIM_SM_FRMSENT = 3,	/*  prli frame is sent */
	BFA_FCS_ITNIM_SM_RSP_OK = 4,	/*  good response */
	BFA_FCS_ITNIM_SM_RSP_ERROR = 5,	/*  error response */
	BFA_FCS_ITNIM_SM_TIMEOUT = 6,	/*  delay timeout */
	BFA_FCS_ITNIM_SM_HCB_OFFLINE = 7,	/*  BFA online callback */
	BFA_FCS_ITNIM_SM_HCB_ONLINE = 8,	/*  BFA offline callback */
	BFA_FCS_ITNIM_SM_INITIATOR = 9,	/*  rport is initiator */
	BFA_FCS_ITNIM_SM_DELETE = 10,	/*  delete event from rport */
	BFA_FCS_ITNIM_SM_PRLO = 11,	/*  delete event from rport */
};

static void     bfa_fcs_itnim_sm_offline(struct bfa_fcs_itnim_s *itnim,
					 enum bfa_fcs_itnim_event event);
static void     bfa_fcs_itnim_sm_prli_send(struct bfa_fcs_itnim_s *itnim,
					   enum bfa_fcs_itnim_event event);
static void     bfa_fcs_itnim_sm_prli(struct bfa_fcs_itnim_s *itnim,
				      enum bfa_fcs_itnim_event event);
static void     bfa_fcs_itnim_sm_prli_retry(struct bfa_fcs_itnim_s *itnim,
					    enum bfa_fcs_itnim_event event);
static void     bfa_fcs_itnim_sm_hcb_online(struct bfa_fcs_itnim_s *itnim,
					    enum bfa_fcs_itnim_event event);
static void     bfa_fcs_itnim_sm_online(struct bfa_fcs_itnim_s *itnim,
					enum bfa_fcs_itnim_event event);
static void     bfa_fcs_itnim_sm_hcb_offline(struct bfa_fcs_itnim_s *itnim,
					     enum bfa_fcs_itnim_event event);
static void     bfa_fcs_itnim_sm_initiator(struct bfa_fcs_itnim_s *itnim,
					   enum bfa_fcs_itnim_event event);

static struct bfa_sm_table_s itnim_sm_table[] = {
	{BFA_SM(bfa_fcs_itnim_sm_offline), BFA_ITNIM_OFFLINE},
	{BFA_SM(bfa_fcs_itnim_sm_prli_send), BFA_ITNIM_PRLI_SEND},
	{BFA_SM(bfa_fcs_itnim_sm_prli), BFA_ITNIM_PRLI_SENT},
	{BFA_SM(bfa_fcs_itnim_sm_prli_retry), BFA_ITNIM_PRLI_RETRY},
	{BFA_SM(bfa_fcs_itnim_sm_hcb_online), BFA_ITNIM_HCB_ONLINE},
	{BFA_SM(bfa_fcs_itnim_sm_online), BFA_ITNIM_ONLINE},
	{BFA_SM(bfa_fcs_itnim_sm_hcb_offline), BFA_ITNIM_HCB_OFFLINE},
	{BFA_SM(bfa_fcs_itnim_sm_initiator), BFA_ITNIM_INITIATIOR},
};

/**
 *  fcs_itnim_sm FCS itnim state machine
 */

static void
bfa_fcs_itnim_sm_offline(struct bfa_fcs_itnim_s *itnim,
			 enum bfa_fcs_itnim_event event)
{
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_trc(itnim->fcs, event);

	switch (event) {
	case BFA_FCS_ITNIM_SM_ONLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_prli_send);
		bfa_fcs_itnim_send_prli(itnim, NULL);
		break;

	case BFA_FCS_ITNIM_SM_OFFLINE:
		bfa_fcs_rport_itnim_ack(itnim->rport);
		break;

	case BFA_FCS_ITNIM_SM_INITIATOR:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_initiator);
		break;

	case BFA_FCS_ITNIM_SM_DELETE:
		bfa_fcs_itnim_free(itnim);
		break;

	default:
		bfa_sm_fault(itnim->fcs, event);
	}

}

static void
bfa_fcs_itnim_sm_prli_send(struct bfa_fcs_itnim_s *itnim,
			   enum bfa_fcs_itnim_event event)
{
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_trc(itnim->fcs, event);

	switch (event) {
	case BFA_FCS_ITNIM_SM_FRMSENT:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_prli);
		break;

	case BFA_FCS_ITNIM_SM_INITIATOR:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_initiator);
		bfa_fcxp_walloc_cancel(itnim->fcs->bfa, &itnim->fcxp_wqe);
		break;

	case BFA_FCS_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcxp_walloc_cancel(itnim->fcs->bfa, &itnim->fcxp_wqe);
		bfa_fcs_rport_itnim_ack(itnim->rport);
		break;

	case BFA_FCS_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcxp_walloc_cancel(itnim->fcs->bfa, &itnim->fcxp_wqe);
		bfa_fcs_itnim_free(itnim);
		break;

	default:
		bfa_sm_fault(itnim->fcs, event);
	}
}

static void
bfa_fcs_itnim_sm_prli(struct bfa_fcs_itnim_s *itnim,
		      enum bfa_fcs_itnim_event event)
{
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_trc(itnim->fcs, event);

	switch (event) {
	case BFA_FCS_ITNIM_SM_RSP_OK:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_hcb_online);
		bfa_itnim_online(itnim->bfa_itnim, itnim->seq_rec);
		break;

	case BFA_FCS_ITNIM_SM_RSP_ERROR:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_prli_retry);
		bfa_timer_start(itnim->fcs->bfa, &itnim->timer,
				bfa_fcs_itnim_timeout, itnim,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case BFA_FCS_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcxp_discard(itnim->fcxp);
		bfa_fcs_rport_itnim_ack(itnim->rport);
		break;

	case BFA_FCS_ITNIM_SM_INITIATOR:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_initiator);
		/*
		 * dont discard fcxp. accept will reach same state
		 */
		break;

	case BFA_FCS_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcxp_discard(itnim->fcxp);
		bfa_fcs_itnim_free(itnim);
		break;

	default:
		bfa_sm_fault(itnim->fcs, event);
	}
}

static void
bfa_fcs_itnim_sm_prli_retry(struct bfa_fcs_itnim_s *itnim,
			    enum bfa_fcs_itnim_event event)
{
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_trc(itnim->fcs, event);

	switch (event) {
	case BFA_FCS_ITNIM_SM_TIMEOUT:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_prli_send);
		bfa_fcs_itnim_send_prli(itnim, NULL);
		break;

	case BFA_FCS_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_timer_stop(&itnim->timer);
		bfa_fcs_rport_itnim_ack(itnim->rport);
		break;

	case BFA_FCS_ITNIM_SM_INITIATOR:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_initiator);
		bfa_timer_stop(&itnim->timer);
		break;

	case BFA_FCS_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_timer_stop(&itnim->timer);
		bfa_fcs_itnim_free(itnim);
		break;

	default:
		bfa_sm_fault(itnim->fcs, event);
	}
}

static void
bfa_fcs_itnim_sm_hcb_online(struct bfa_fcs_itnim_s *itnim,
			    enum bfa_fcs_itnim_event event)
{
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_trc(itnim->fcs, event);

	switch (event) {
	case BFA_FCS_ITNIM_SM_HCB_ONLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_online);
		bfa_fcb_itnim_online(itnim->itnim_drv);
		bfa_fcs_itnim_aen_post(itnim, BFA_ITNIM_AEN_ONLINE);
		break;

	case BFA_FCS_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_itnim_offline(itnim->bfa_itnim);
		bfa_fcs_rport_itnim_ack(itnim->rport);
		break;

	case BFA_FCS_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcs_itnim_free(itnim);
		break;

	default:
		bfa_sm_fault(itnim->fcs, event);
	}
}

static void
bfa_fcs_itnim_sm_online(struct bfa_fcs_itnim_s *itnim,
			enum bfa_fcs_itnim_event event)
{
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_trc(itnim->fcs, event);

	switch (event) {
	case BFA_FCS_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_hcb_offline);
		bfa_fcb_itnim_offline(itnim->itnim_drv);
		bfa_itnim_offline(itnim->bfa_itnim);
		if (bfa_fcs_port_is_online(itnim->rport->port) == BFA_TRUE)
			bfa_fcs_itnim_aen_post(itnim, BFA_ITNIM_AEN_DISCONNECT);
		else
			bfa_fcs_itnim_aen_post(itnim, BFA_ITNIM_AEN_OFFLINE);
		break;

	case BFA_FCS_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcs_itnim_free(itnim);
		break;

	default:
		bfa_sm_fault(itnim->fcs, event);
	}
}

static void
bfa_fcs_itnim_sm_hcb_offline(struct bfa_fcs_itnim_s *itnim,
			     enum bfa_fcs_itnim_event event)
{
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_trc(itnim->fcs, event);

	switch (event) {
	case BFA_FCS_ITNIM_SM_HCB_OFFLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcs_rport_itnim_ack(itnim->rport);
		break;

	case BFA_FCS_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcs_itnim_free(itnim);
		break;

	default:
		bfa_sm_fault(itnim->fcs, event);
	}
}

/*
 * This state is set when a discovered rport is also in intiator mode.
 * This ITN is marked as no_op and is not active and will not be truned into
 * online state.
 */
static void
bfa_fcs_itnim_sm_initiator(struct bfa_fcs_itnim_s *itnim,
			   enum bfa_fcs_itnim_event event)
{
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_trc(itnim->fcs, event);

	switch (event) {
	case BFA_FCS_ITNIM_SM_OFFLINE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcs_rport_itnim_ack(itnim->rport);
		break;

	case BFA_FCS_ITNIM_SM_RSP_ERROR:
	case BFA_FCS_ITNIM_SM_ONLINE:
	case BFA_FCS_ITNIM_SM_INITIATOR:
		break;

	case BFA_FCS_ITNIM_SM_DELETE:
		bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);
		bfa_fcs_itnim_free(itnim);
		break;

	default:
		bfa_sm_fault(itnim->fcs, event);
	}
}



/**
 *  itnim_private FCS ITNIM private interfaces
 */

static void
bfa_fcs_itnim_aen_post(struct bfa_fcs_itnim_s *itnim,
		       enum bfa_itnim_aen_event event)
{
	struct bfa_fcs_rport_s *rport = itnim->rport;
	union bfa_aen_data_u aen_data;
	struct bfa_log_mod_s *logmod = rport->fcs->logm;
	wwn_t           lpwwn = bfa_fcs_port_get_pwwn(rport->port);
	wwn_t           rpwwn = rport->pwwn;
	char            lpwwn_ptr[BFA_STRING_32];
	char            rpwwn_ptr[BFA_STRING_32];

	/*
	 * Don't post events for well known addresses
	 */
	if (BFA_FCS_PID_IS_WKA(rport->pid))
		return;

	wwn2str(lpwwn_ptr, lpwwn);
	wwn2str(rpwwn_ptr, rpwwn);

	bfa_log(logmod, BFA_LOG_CREATE_ID(BFA_AEN_CAT_ITNIM, event),
		rpwwn_ptr, lpwwn_ptr);

	aen_data.itnim.vf_id = rport->port->fabric->vf_id;
	aen_data.itnim.ppwwn =
		bfa_fcs_port_get_pwwn(bfa_fcs_get_base_port(itnim->fcs));
	aen_data.itnim.lpwwn = lpwwn;
	aen_data.itnim.rpwwn = rpwwn;
}

static void
bfa_fcs_itnim_send_prli(void *itnim_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_itnim_s *itnim = itnim_cbarg;
	struct bfa_fcs_rport_s *rport = itnim->rport;
	struct bfa_fcs_port_s *port = rport->port;
	struct fchs_s          fchs;
	struct bfa_fcxp_s *fcxp;
	int             len;

	bfa_trc(itnim->fcs, itnim->rport->pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		itnim->stats.fcxp_alloc_wait++;
		bfa_fcxp_alloc_wait(port->fcs->bfa, &itnim->fcxp_wqe,
				    bfa_fcs_itnim_send_prli, itnim);
		return;
	}
	itnim->fcxp = fcxp;

	len = fc_prli_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), itnim->rport->pid,
			    bfa_fcs_port_get_fcid(port), 0);

	bfa_fcxp_send(fcxp, rport->bfa_rport, port->fabric->vf_id, port->lp_tag,
		      BFA_FALSE, FC_CLASS_3, len, &fchs,
		      bfa_fcs_itnim_prli_response, (void *)itnim, FC_MAX_PDUSZ,
		      FC_RA_TOV);

	itnim->stats.prli_sent++;
	bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_FRMSENT);
}

static void
bfa_fcs_itnim_prli_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
			    bfa_status_t req_status, u32 rsp_len,
			    u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_itnim_s *itnim = (struct bfa_fcs_itnim_s *)cbarg;
	struct fc_els_cmd_s   *els_cmd;
	struct fc_prli_s      *prli_resp;
	struct fc_ls_rjt_s    *ls_rjt;
	struct fc_prli_params_s *sparams;

	bfa_trc(itnim->fcs, req_status);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		itnim->stats.prli_rsp_err++;
		bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_RSP_ERROR);
		return;
	}

	els_cmd = (struct fc_els_cmd_s *) BFA_FCXP_RSP_PLD(fcxp);

	if (els_cmd->els_code == FC_ELS_ACC) {
		prli_resp = (struct fc_prli_s *) els_cmd;

		if (fc_prli_rsp_parse(prli_resp, rsp_len) != FC_PARSE_OK) {
			bfa_trc(itnim->fcs, rsp_len);
			/*
			 * Check if this  r-port is also in Initiator mode.
			 * If so, we need to set this ITN as a no-op.
			 */
			if (prli_resp->parampage.servparams.initiator) {
				bfa_trc(itnim->fcs, prli_resp->parampage.type);
				itnim->rport->scsi_function =
					BFA_RPORT_INITIATOR;
				itnim->stats.prli_rsp_acc++;
				bfa_sm_send_event(itnim,
						  BFA_FCS_ITNIM_SM_INITIATOR);
				return;
			}

			itnim->stats.prli_rsp_parse_err++;
			return;
		}
		itnim->rport->scsi_function = BFA_RPORT_TARGET;

		sparams = &prli_resp->parampage.servparams;
		itnim->seq_rec = sparams->retry;
		itnim->rec_support = sparams->rec_support;
		itnim->task_retry_id = sparams->task_retry_id;
		itnim->conf_comp = sparams->confirm;

		itnim->stats.prli_rsp_acc++;
		bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_RSP_OK);
	} else {
		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(itnim->fcs, ls_rjt->reason_code);
		bfa_trc(itnim->fcs, ls_rjt->reason_code_expl);

		itnim->stats.prli_rsp_rjt++;
		bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_RSP_ERROR);
	}
}

static void
bfa_fcs_itnim_timeout(void *arg)
{
	struct bfa_fcs_itnim_s *itnim = (struct bfa_fcs_itnim_s *)arg;

	itnim->stats.timeout++;
	bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_TIMEOUT);
}

static void
bfa_fcs_itnim_free(struct bfa_fcs_itnim_s *itnim)
{
	bfa_itnim_delete(itnim->bfa_itnim);
	bfa_fcb_itnim_free(itnim->fcs->bfad, itnim->itnim_drv);
}



/**
 *  itnim_public FCS ITNIM public interfaces
 */

/**
 * 	Called by rport when a new rport is created.
 *
 * @param[in] rport	-  remote port.
 */
struct bfa_fcs_itnim_s *
bfa_fcs_itnim_create(struct bfa_fcs_rport_s *rport)
{
	struct bfa_fcs_port_s *port = rport->port;
	struct bfa_fcs_itnim_s *itnim;
	struct bfad_itnim_s *itnim_drv;
	struct bfa_itnim_s *bfa_itnim;

	/*
	 * call bfad to allocate the itnim
	 */
	bfa_fcb_itnim_alloc(port->fcs->bfad, &itnim, &itnim_drv);
	if (itnim == NULL) {
		bfa_trc(port->fcs, rport->pwwn);
		return NULL;
	}

	/*
	 * Initialize itnim
	 */
	itnim->rport = rport;
	itnim->fcs = rport->fcs;
	itnim->itnim_drv = itnim_drv;

	/*
	 * call BFA to create the itnim
	 */
	bfa_itnim = bfa_itnim_create(port->fcs->bfa, rport->bfa_rport, itnim);

	if (bfa_itnim == NULL) {
		bfa_trc(port->fcs, rport->pwwn);
		bfa_fcb_itnim_free(port->fcs->bfad, itnim_drv);
		bfa_assert(0);
		return NULL;
	}

	itnim->bfa_itnim = bfa_itnim;
	itnim->seq_rec = BFA_FALSE;
	itnim->rec_support = BFA_FALSE;
	itnim->conf_comp = BFA_FALSE;
	itnim->task_retry_id = BFA_FALSE;

	/*
	 * Set State machine
	 */
	bfa_sm_set_state(itnim, bfa_fcs_itnim_sm_offline);

	return itnim;
}

/**
 * 	Called by rport to delete  the instance of FCPIM.
 *
 * @param[in] rport	-  remote port.
 */
void
bfa_fcs_itnim_delete(struct bfa_fcs_itnim_s *itnim)
{
	bfa_trc(itnim->fcs, itnim->rport->pid);
	bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_DELETE);
}

/**
 * Notification from rport that PLOGI is complete to initiate FC-4 session.
 */
void
bfa_fcs_itnim_rport_online(struct bfa_fcs_itnim_s *itnim)
{
	itnim->stats.onlines++;

	if (!BFA_FCS_PID_IS_WKA(itnim->rport->pid)) {
		bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_ONLINE);
	} else {
		/*
		 * For well known addresses, we set the itnim to initiator
		 * state
		 */
		itnim->stats.initiator++;
		bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_INITIATOR);
	}
}

/**
 * Called by rport to handle a remote device offline.
 */
void
bfa_fcs_itnim_rport_offline(struct bfa_fcs_itnim_s *itnim)
{
	itnim->stats.offlines++;
	bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_OFFLINE);
}

/**
 * Called by rport when remote port is known to be an initiator from
 * PRLI received.
 */
void
bfa_fcs_itnim_is_initiator(struct bfa_fcs_itnim_s *itnim)
{
	bfa_trc(itnim->fcs, itnim->rport->pid);
	itnim->stats.initiator++;
	bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_INITIATOR);
}

/**
 * Called by rport to check if the itnim is online.
 */
bfa_status_t
bfa_fcs_itnim_get_online_state(struct bfa_fcs_itnim_s *itnim)
{
	bfa_trc(itnim->fcs, itnim->rport->pid);
	switch (bfa_sm_to_state(itnim_sm_table, itnim->sm)) {
	case BFA_ITNIM_ONLINE:
	case BFA_ITNIM_INITIATIOR:
		return BFA_STATUS_OK;

	default:
		return BFA_STATUS_NO_FCPIM_NEXUS;

	}
}

/**
 * BFA completion callback for bfa_itnim_online().
 */
void
bfa_cb_itnim_online(void *cbarg)
{
	struct bfa_fcs_itnim_s *itnim = (struct bfa_fcs_itnim_s *)cbarg;

	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_HCB_ONLINE);
}

/**
 * BFA completion callback for bfa_itnim_offline().
 */
void
bfa_cb_itnim_offline(void *cb_arg)
{
	struct bfa_fcs_itnim_s *itnim = (struct bfa_fcs_itnim_s *)cb_arg;

	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_HCB_OFFLINE);
}

/**
 * Mark the beginning of PATH TOV handling. IO completion callbacks
 * are still pending.
 */
void
bfa_cb_itnim_tov_begin(void *cb_arg)
{
	struct bfa_fcs_itnim_s *itnim = (struct bfa_fcs_itnim_s *)cb_arg;

	bfa_trc(itnim->fcs, itnim->rport->pwwn);
}

/**
 * Mark the end of PATH TOV handling. All pending IOs are already cleaned up.
 */
void
bfa_cb_itnim_tov(void *cb_arg)
{
	struct bfa_fcs_itnim_s *itnim = (struct bfa_fcs_itnim_s *)cb_arg;

	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_fcb_itnim_tov(itnim->itnim_drv);
}

/**
 * 		BFA notification to FCS/driver for second level error recovery.
 *
 * Atleast one I/O request has timedout and target is unresponsive to
 * repeated abort requests. Second level error recovery should be initiated
 * by starting implicit logout and recovery procedures.
 */
void
bfa_cb_itnim_sler(void *cb_arg)
{
	struct bfa_fcs_itnim_s *itnim = (struct bfa_fcs_itnim_s *)cb_arg;

	itnim->stats.sler++;
	bfa_trc(itnim->fcs, itnim->rport->pwwn);
	bfa_fcs_rport_logo_imp(itnim->rport);
}

struct bfa_fcs_itnim_s *
bfa_fcs_itnim_lookup(struct bfa_fcs_port_s *port, wwn_t rpwwn)
{
	struct bfa_fcs_rport_s *rport;
	rport = bfa_fcs_rport_lookup(port, rpwwn);

	if (!rport)
		return NULL;

	bfa_assert(rport->itnim != NULL);
	return rport->itnim;
}

bfa_status_t
bfa_fcs_itnim_attr_get(struct bfa_fcs_port_s *port, wwn_t rpwwn,
		       struct bfa_itnim_attr_s *attr)
{
	struct bfa_fcs_itnim_s *itnim = NULL;

	itnim = bfa_fcs_itnim_lookup(port, rpwwn);

	if (itnim == NULL)
		return BFA_STATUS_NO_FCPIM_NEXUS;

	attr->state = bfa_sm_to_state(itnim_sm_table, itnim->sm);
	attr->retry = itnim->seq_rec;
	attr->rec_support = itnim->rec_support;
	attr->conf_comp = itnim->conf_comp;
	attr->task_retry_id = itnim->task_retry_id;

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcs_itnim_stats_get(struct bfa_fcs_port_s *port, wwn_t rpwwn,
			struct bfa_itnim_stats_s *stats)
{
	struct bfa_fcs_itnim_s *itnim = NULL;

	bfa_assert(port != NULL);

	itnim = bfa_fcs_itnim_lookup(port, rpwwn);

	if (itnim == NULL)
		return BFA_STATUS_NO_FCPIM_NEXUS;

	bfa_os_memcpy(stats, &itnim->stats, sizeof(struct bfa_itnim_stats_s));

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcs_itnim_stats_clear(struct bfa_fcs_port_s *port, wwn_t rpwwn)
{
	struct bfa_fcs_itnim_s *itnim = NULL;

	bfa_assert(port != NULL);

	itnim = bfa_fcs_itnim_lookup(port, rpwwn);

	if (itnim == NULL)
		return BFA_STATUS_NO_FCPIM_NEXUS;

	bfa_os_memset(&itnim->stats, 0, sizeof(struct bfa_itnim_stats_s));
	return BFA_STATUS_OK;
}

void
bfa_fcs_fcpim_uf_recv(struct bfa_fcs_itnim_s *itnim, struct fchs_s *fchs,
		      u16 len)
{
	struct fc_els_cmd_s   *els_cmd;

	bfa_trc(itnim->fcs, fchs->type);

	if (fchs->type != FC_TYPE_ELS)
		return;

	els_cmd = (struct fc_els_cmd_s *) (fchs + 1);

	bfa_trc(itnim->fcs, els_cmd->els_code);

	switch (els_cmd->els_code) {
	case FC_ELS_PRLO:
		/* bfa_sm_send_event(itnim, BFA_FCS_ITNIM_SM_PRLO); */
		break;

	default:
		bfa_assert(0);
	}
}

void
bfa_fcs_itnim_pause(struct bfa_fcs_itnim_s *itnim)
{
}

void
bfa_fcs_itnim_resume(struct bfa_fcs_itnim_s *itnim)
{
}
