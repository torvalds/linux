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

#include <bfa.h>
#include <bfa_svc.h>
#include "fcs_lport.h"
#include "fcs_rport.h"
#include "fcs_ms.h"
#include "fcs_trcmod.h"
#include "fcs_fcxp.h"
#include "fcs.h"
#include "lport_priv.h"

BFA_TRC_FILE(FCS, SCN);

#define FC_QOS_RSCN_EVENT		0x0c
#define FC_FABRIC_NAME_RSCN_EVENT	0x0d

/*
 * forward declarations
 */
static void     bfa_fcs_port_scn_send_scr(void *scn_cbarg,
					  struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_scn_scr_response(void *fcsarg,
					      struct bfa_fcxp_s *fcxp,
					      void *cbarg,
					      bfa_status_t req_status,
					      u32 rsp_len,
					      u32 resid_len,
					      struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_scn_send_ls_acc(struct bfa_fcs_port_s *port,
					     struct fchs_s *rx_fchs);
static void     bfa_fcs_port_scn_timeout(void *arg);

/**
 *  fcs_scm_sm FCS SCN state machine
 */

/**
 * VPort SCN State Machine events
 */
enum port_scn_event {
	SCNSM_EVENT_PORT_ONLINE = 1,
	SCNSM_EVENT_PORT_OFFLINE = 2,
	SCNSM_EVENT_RSP_OK = 3,
	SCNSM_EVENT_RSP_ERROR = 4,
	SCNSM_EVENT_TIMEOUT = 5,
	SCNSM_EVENT_SCR_SENT = 6,
};

static void     bfa_fcs_port_scn_sm_offline(struct bfa_fcs_port_scn_s *scn,
					    enum port_scn_event event);
static void     bfa_fcs_port_scn_sm_sending_scr(struct bfa_fcs_port_scn_s *scn,
						enum port_scn_event event);
static void     bfa_fcs_port_scn_sm_scr(struct bfa_fcs_port_scn_s *scn,
					enum port_scn_event event);
static void     bfa_fcs_port_scn_sm_scr_retry(struct bfa_fcs_port_scn_s *scn,
					      enum port_scn_event event);
static void     bfa_fcs_port_scn_sm_online(struct bfa_fcs_port_scn_s *scn,
					   enum port_scn_event event);

/**
 * 		Starting state - awaiting link up.
 */
static void
bfa_fcs_port_scn_sm_offline(struct bfa_fcs_port_scn_s *scn,
			    enum port_scn_event event)
{
	switch (event) {
	case SCNSM_EVENT_PORT_ONLINE:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_sending_scr);
		bfa_fcs_port_scn_send_scr(scn, NULL);
		break;

	case SCNSM_EVENT_PORT_OFFLINE:
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_scn_sm_sending_scr(struct bfa_fcs_port_scn_s *scn,
				enum port_scn_event event)
{
	switch (event) {
	case SCNSM_EVENT_SCR_SENT:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_scr);
		break;

	case SCNSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_offline);
		bfa_fcxp_walloc_cancel(scn->port->fcs->bfa, &scn->fcxp_wqe);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_scn_sm_scr(struct bfa_fcs_port_scn_s *scn,
			enum port_scn_event event)
{
	struct bfa_fcs_port_s *port = scn->port;

	switch (event) {
	case SCNSM_EVENT_RSP_OK:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_online);
		break;

	case SCNSM_EVENT_RSP_ERROR:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_scr_retry);
		bfa_timer_start(port->fcs->bfa, &scn->timer,
				bfa_fcs_port_scn_timeout, scn,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case SCNSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_offline);
		bfa_fcxp_discard(scn->fcxp);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_scn_sm_scr_retry(struct bfa_fcs_port_scn_s *scn,
			      enum port_scn_event event)
{
	switch (event) {
	case SCNSM_EVENT_TIMEOUT:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_sending_scr);
		bfa_fcs_port_scn_send_scr(scn, NULL);
		break;

	case SCNSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_offline);
		bfa_timer_stop(&scn->timer);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_scn_sm_online(struct bfa_fcs_port_scn_s *scn,
			   enum port_scn_event event)
{
	switch (event) {
	case SCNSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_offline);
		break;

	default:
		bfa_assert(0);
	}
}



/**
 *  fcs_scn_private FCS SCN private functions
 */

/**
 * This routine will be called to send a SCR command.
 */
static void
bfa_fcs_port_scn_send_scr(void *scn_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_scn_s *scn = scn_cbarg;
	struct bfa_fcs_port_s *port = scn->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &scn->fcxp_wqe,
				    bfa_fcs_port_scn_send_scr, scn);
		return;
	}
	scn->fcxp = fcxp;

	/*
	 * Handle VU registrations for Base port only
	 */
	if ((!port->vport) && bfa_ioc_get_fcmode(&port->fcs->bfa->ioc)) {
		len = fc_scr_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				   bfa_lps_is_brcd_fabric(port->fabric->lps),
				   port->pid, 0);
	} else {
		len = fc_scr_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), BFA_FALSE,
				   port->pid, 0);
	}

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_scn_scr_response,
		      (void *)scn, FC_MAX_PDUSZ, FC_RA_TOV);

	bfa_sm_send_event(scn, SCNSM_EVENT_SCR_SENT);
}

static void
bfa_fcs_port_scn_scr_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			      void *cbarg, bfa_status_t req_status,
			      u32 rsp_len, u32 resid_len,
			      struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_scn_s *scn = (struct bfa_fcs_port_scn_s *)cbarg;
	struct bfa_fcs_port_s *port = scn->port;
	struct fc_els_cmd_s   *els_cmd;
	struct fc_ls_rjt_s    *ls_rjt;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(scn, SCNSM_EVENT_RSP_ERROR);
		return;
	}

	els_cmd = (struct fc_els_cmd_s *) BFA_FCXP_RSP_PLD(fcxp);

	switch (els_cmd->els_code) {

	case FC_ELS_ACC:
		bfa_sm_send_event(scn, SCNSM_EVENT_RSP_OK);
		break;

	case FC_ELS_LS_RJT:

		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(port->fcs, ls_rjt->reason_code);
		bfa_trc(port->fcs, ls_rjt->reason_code_expl);

		bfa_sm_send_event(scn, SCNSM_EVENT_RSP_ERROR);
		break;

	default:
		bfa_sm_send_event(scn, SCNSM_EVENT_RSP_ERROR);
	}
}

/*
 * Send a LS Accept
 */
static void
bfa_fcs_port_scn_send_ls_acc(struct bfa_fcs_port_s *port,
			struct fchs_s *rx_fchs)
{
	struct fchs_s          fchs;
	struct bfa_fcxp_s *fcxp;
	struct bfa_rport_s *bfa_rport = NULL;
	int             len;

	bfa_trc(port->fcs, rx_fchs->s_id);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp)
		return;

	len = fc_ls_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), rx_fchs->s_id,
			      bfa_fcs_port_get_fcid(port), rx_fchs->ox_id);

	bfa_fcxp_send(fcxp, bfa_rport, port->fabric->vf_id, port->lp_tag,
		      BFA_FALSE, FC_CLASS_3, len, &fchs, NULL, NULL,
		      FC_MAX_PDUSZ, 0);
}

/**
 *     This routine will be called by bfa_timer on timer timeouts.
 *
 * 	param[in] 	vport 			- pointer to bfa_fcs_port_t.
 * 	param[out]	vport_status 	- pointer to return vport status in
 *
 * 	return
 * 		void
 *
*  	Special Considerations:
 *
 * 	note
 */
static void
bfa_fcs_port_scn_timeout(void *arg)
{
	struct bfa_fcs_port_scn_s *scn = (struct bfa_fcs_port_scn_s *)arg;

	bfa_sm_send_event(scn, SCNSM_EVENT_TIMEOUT);
}



/**
 *  fcs_scn_public FCS state change notification public interfaces
 */

/*
 * Functions called by port/fab
 */
void
bfa_fcs_port_scn_init(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_scn_s *scn = BFA_FCS_GET_SCN_FROM_PORT(port);

	scn->port = port;
	bfa_sm_set_state(scn, bfa_fcs_port_scn_sm_offline);
}

void
bfa_fcs_port_scn_offline(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_scn_s *scn = BFA_FCS_GET_SCN_FROM_PORT(port);

	scn->port = port;
	bfa_sm_send_event(scn, SCNSM_EVENT_PORT_OFFLINE);
}

void
bfa_fcs_port_scn_online(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_scn_s *scn = BFA_FCS_GET_SCN_FROM_PORT(port);

	scn->port = port;
	bfa_sm_send_event(scn, SCNSM_EVENT_PORT_ONLINE);
}

static void
bfa_fcs_port_scn_portid_rscn(struct bfa_fcs_port_s *port, u32 rpid)
{
	struct bfa_fcs_rport_s *rport;

	bfa_trc(port->fcs, rpid);

	/**
	 * If this is an unknown device, then it just came online.
	 * Otherwise let rport handle the RSCN event.
	 */
	rport = bfa_fcs_port_get_rport_by_pid(port, rpid);
	if (rport == NULL) {
		/*
		 * If min cfg mode is enabled, we donot need to
		 * discover any new rports.
		 */
		if (!__fcs_min_cfg(port->fcs))
			rport = bfa_fcs_rport_create(port, rpid);
	} else {
		bfa_fcs_rport_scn(rport);
	}
}

/**
 * rscn format based PID comparison
 */
#define __fc_pid_match(__c0, __c1, __fmt)		\
	(((__fmt) == FC_RSCN_FORMAT_FABRIC) ||		\
	 (((__fmt) == FC_RSCN_FORMAT_DOMAIN) &&		\
	  ((__c0)[0] == (__c1)[0])) ||			\
	 (((__fmt) == FC_RSCN_FORMAT_AREA) &&		\
	  ((__c0)[0] == (__c1)[0]) &&			\
	  ((__c0)[1] == (__c1)[1])))

static void
bfa_fcs_port_scn_multiport_rscn(struct bfa_fcs_port_s *port,
			enum fc_rscn_format format, u32 rscn_pid)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head *qe, *qe_next;
	u8        *c0, *c1;

	bfa_trc(port->fcs, format);
	bfa_trc(port->fcs, rscn_pid);

	c0 = (u8 *) &rscn_pid;

	list_for_each_safe(qe, qe_next, &port->rport_q) {
		rport = (struct bfa_fcs_rport_s *)qe;
		c1 = (u8 *) &rport->pid;
		if (__fc_pid_match(c0, c1, format))
			bfa_fcs_rport_scn(rport);
	}
}

void
bfa_fcs_port_scn_process_rscn(struct bfa_fcs_port_s *port, struct fchs_s *fchs,
			      u32 len)
{
	struct fc_rscn_pl_s   *rscn = (struct fc_rscn_pl_s *) (fchs + 1);
	int             num_entries;
	u32        rscn_pid;
	bfa_boolean_t   nsquery = BFA_FALSE;
	int             i = 0;

	num_entries =
		(bfa_os_ntohs(rscn->payldlen) -
		 sizeof(u32)) / sizeof(rscn->event[0]);

	bfa_trc(port->fcs, num_entries);

	port->stats.num_rscn++;

	bfa_fcs_port_scn_send_ls_acc(port, fchs);

	for (i = 0; i < num_entries; i++) {
		rscn_pid = rscn->event[i].portid;

		bfa_trc(port->fcs, rscn->event[i].format);
		bfa_trc(port->fcs, rscn_pid);

		switch (rscn->event[i].format) {
		case FC_RSCN_FORMAT_PORTID:
			if (rscn->event[i].qualifier == FC_QOS_RSCN_EVENT) {
				/*
				 * Ignore this event. f/w would have processed
				 * it
				 */
				bfa_trc(port->fcs, rscn_pid);
			} else {
				port->stats.num_portid_rscn++;
				bfa_fcs_port_scn_portid_rscn(port, rscn_pid);
			}
			break;

		case FC_RSCN_FORMAT_FABRIC:
			if (rscn->event[i].qualifier ==
			    FC_FABRIC_NAME_RSCN_EVENT) {
				bfa_fcs_port_ms_fabric_rscn(port);
				break;
			}
			/*
			 * !!!!!!!!! Fall Through !!!!!!!!!!!!!
			 */

		case FC_RSCN_FORMAT_AREA:
		case FC_RSCN_FORMAT_DOMAIN:
			nsquery = BFA_TRUE;
			bfa_fcs_port_scn_multiport_rscn(port,
							rscn->event[i].format,
							rscn_pid);
			break;

		default:
			bfa_assert(0);
			nsquery = BFA_TRUE;
		}
	}

	/**
	 * If any of area, domain or fabric RSCN is received, do a fresh discovery
	 * to find new devices.
	 */
	if (nsquery)
		bfa_fcs_port_ns_query(port);
}


