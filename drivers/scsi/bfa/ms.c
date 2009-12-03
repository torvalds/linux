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
#include "fcs_trcmod.h"
#include "fcs_fcxp.h"
#include "lport_priv.h"

BFA_TRC_FILE(FCS, MS);

#define BFA_FCS_MS_CMD_MAX_RETRIES  2
/*
 * forward declarations
 */
static void     bfa_fcs_port_ms_send_plogi(void *ms_cbarg,
					   struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_ms_timeout(void *arg);
static void     bfa_fcs_port_ms_plogi_response(void *fcsarg,
					       struct bfa_fcxp_s *fcxp,
					       void *cbarg,
					       bfa_status_t req_status,
					       u32 rsp_len,
					       u32 resid_len,
					       struct fchs_s *rsp_fchs);

static void     bfa_fcs_port_ms_send_gmal(void *ms_cbarg,
					  struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_ms_gmal_response(void *fcsarg,
					      struct bfa_fcxp_s *fcxp,
					      void *cbarg,
					      bfa_status_t req_status,
					      u32 rsp_len,
					      u32 resid_len,
					      struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_ms_send_gfn(void *ms_cbarg,
					 struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_ms_gfn_response(void *fcsarg,
					     struct bfa_fcxp_s *fcxp,
					     void *cbarg,
					     bfa_status_t req_status,
					     u32 rsp_len,
					     u32 resid_len,
					     struct fchs_s *rsp_fchs);
/**
 *  fcs_ms_sm FCS MS state machine
 */

/**
 *  MS State Machine events
 */
enum port_ms_event {
	MSSM_EVENT_PORT_ONLINE = 1,
	MSSM_EVENT_PORT_OFFLINE = 2,
	MSSM_EVENT_RSP_OK = 3,
	MSSM_EVENT_RSP_ERROR = 4,
	MSSM_EVENT_TIMEOUT = 5,
	MSSM_EVENT_FCXP_SENT = 6,
	MSSM_EVENT_PORT_FABRIC_RSCN = 7
};

static void     bfa_fcs_port_ms_sm_offline(struct bfa_fcs_port_ms_s *ms,
					   enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_plogi_sending(struct bfa_fcs_port_ms_s *ms,
						 enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_plogi(struct bfa_fcs_port_ms_s *ms,
					 enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_plogi_retry(struct bfa_fcs_port_ms_s *ms,
					       enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_gmal_sending(struct bfa_fcs_port_ms_s *ms,
						enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_gmal(struct bfa_fcs_port_ms_s *ms,
					enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_gmal_retry(struct bfa_fcs_port_ms_s *ms,
					      enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_gfn_sending(struct bfa_fcs_port_ms_s *ms,
					       enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_gfn(struct bfa_fcs_port_ms_s *ms,
				       enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_gfn_retry(struct bfa_fcs_port_ms_s *ms,
					     enum port_ms_event event);
static void     bfa_fcs_port_ms_sm_online(struct bfa_fcs_port_ms_s *ms,
					  enum port_ms_event event);
/**
 * 		Start in offline state - awaiting NS to send start.
 */
static void
bfa_fcs_port_ms_sm_offline(struct bfa_fcs_port_ms_s *ms,
			   enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_PORT_ONLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_plogi_sending);
		bfa_fcs_port_ms_send_plogi(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_plogi_sending(struct bfa_fcs_port_ms_s *ms,
				 enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_plogi);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
				       &ms->fcxp_wqe);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_plogi(struct bfa_fcs_port_ms_s *ms, enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_plogi_retry);
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ms->port), &ms->timer,
				bfa_fcs_port_ms_timeout, ms,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case MSSM_EVENT_RSP_OK:
		/*
		 * since plogi is done, now invoke MS related sub-modules
		 */
		bfa_fcs_port_fdmi_online(ms);

		/**
		 * if this is a Vport, go to online state.
		 */
		if (ms->port->vport) {
			bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_online);
			break;
		}

		/*
		 * For a base port we need to get the
		 * switch's IP address.
		 */
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gmal_sending);
		bfa_fcs_port_ms_send_gmal(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_fcxp_discard(ms->fcxp);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_plogi_retry(struct bfa_fcs_port_ms_s *ms,
			       enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_plogi_sending);
		bfa_fcs_port_ms_send_plogi(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_timer_stop(&ms->timer);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_online(struct bfa_fcs_port_ms_s *ms,
			  enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		/*
		 * now invoke MS related sub-modules
		 */
		bfa_fcs_port_fdmi_offline(ms);
		break;

	case MSSM_EVENT_PORT_FABRIC_RSCN:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gfn_sending);
		ms->retry_cnt = 0;
		bfa_fcs_port_ms_send_gfn(ms, NULL);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_gmal_sending(struct bfa_fcs_port_ms_s *ms,
				enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gmal);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
				       &ms->fcxp_wqe);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_gmal(struct bfa_fcs_port_ms_s *ms, enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		if (ms->retry_cnt++ < BFA_FCS_MS_CMD_MAX_RETRIES) {
			bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gmal_retry);
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
					&ms->timer, bfa_fcs_port_ms_timeout, ms,
					BFA_FCS_RETRY_TIMEOUT);
		} else {
			bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gfn_sending);
			bfa_fcs_port_ms_send_gfn(ms, NULL);
			ms->retry_cnt = 0;
		}
		break;

	case MSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gfn_sending);
		bfa_fcs_port_ms_send_gfn(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_fcxp_discard(ms->fcxp);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_gmal_retry(struct bfa_fcs_port_ms_s *ms,
			      enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gmal_sending);
		bfa_fcs_port_ms_send_gmal(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_timer_stop(&ms->timer);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 *  ms_pvt MS local functions
 */

static void
bfa_fcs_port_ms_send_gmal(void *ms_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_ms_s *ms = ms_cbarg;
	struct bfa_fcs_port_s *port = ms->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &ms->fcxp_wqe,
				    bfa_fcs_port_ms_send_gmal, ms);
		return;
	}
	ms->fcxp = fcxp;

	len = fc_gmal_req_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				bfa_fcs_port_get_fcid(port),
				bfa_lps_get_peer_nwwn(port->fabric->lps));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_ms_gmal_response,
		      (void *)ms, FC_MAX_PDUSZ, FC_RA_TOV);

	bfa_sm_send_event(ms, MSSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_port_ms_gmal_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			      void *cbarg, bfa_status_t req_status,
			      u32 rsp_len, u32 resid_len,
			      struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_ms_s *ms = (struct bfa_fcs_port_ms_s *)cbarg;
	struct bfa_fcs_port_s *port = ms->port;
	struct ct_hdr_s       *cthdr = NULL;
	struct fcgs_gmal_resp_s *gmal_resp;
	struct fc_gmal_entry_s *gmal_entry;
	u32        num_entries;
	u8        *rsp_str;

	bfa_trc(port->fcs, req_status);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		gmal_resp = (struct fcgs_gmal_resp_s *)(cthdr + 1);
		num_entries = bfa_os_ntohl(gmal_resp->ms_len);
		if (num_entries == 0) {
			bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
			return;
		}
		/*
		 * The response could contain multiple Entries.
		 * Entries for SNMP interface, etc.
		 * We look for the entry with a telnet prefix.
		 * First "http://" entry refers to IP addr
		 */

		gmal_entry = (struct fc_gmal_entry_s *)gmal_resp->ms_ma;
		while (num_entries > 0) {
			if (strncmp
			    (gmal_entry->prefix, CT_GMAL_RESP_PREFIX_HTTP,
			     sizeof(gmal_entry->prefix)) == 0) {

				/*
				 * if the IP address is terminating with a '/',
				 * remove it. *Byte 0 consists of the length
				 * of the string.
				 */
				rsp_str = &(gmal_entry->prefix[0]);
				if (rsp_str[gmal_entry->len - 1] == '/')
					rsp_str[gmal_entry->len - 1] = 0;
				/*
				 * copy IP Address to fabric
				 */
				strncpy(bfa_fcs_port_get_fabric_ipaddr(port),
					gmal_entry->ip_addr,
					BFA_FCS_FABRIC_IPADDR_SZ);
				break;
			} else {
				--num_entries;
				++gmal_entry;
			}
		}

		bfa_sm_send_event(ms, MSSM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
}

static void
bfa_fcs_port_ms_sm_gfn_sending(struct bfa_fcs_port_ms_s *ms,
			       enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gfn);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
				       &ms->fcxp_wqe);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_gfn(struct bfa_fcs_port_ms_s *ms, enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		if (ms->retry_cnt++ < BFA_FCS_MS_CMD_MAX_RETRIES) {
			bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gfn_retry);
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
					&ms->timer, bfa_fcs_port_ms_timeout, ms,
					BFA_FCS_RETRY_TIMEOUT);
		} else {
			bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_online);
			ms->retry_cnt = 0;
		}
		break;

	case MSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_online);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_fcxp_discard(ms->fcxp);
		break;

	default:
		bfa_assert(0);
	}
}

static void
bfa_fcs_port_ms_sm_gfn_retry(struct bfa_fcs_port_ms_s *ms,
			     enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_gfn_sending);
		bfa_fcs_port_ms_send_gfn(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);
		bfa_timer_stop(&ms->timer);
		break;

	default:
		bfa_assert(0);
	}
}

/**
 *  ms_pvt MS local functions
 */

static void
bfa_fcs_port_ms_send_gfn(void *ms_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_ms_s *ms = ms_cbarg;
	struct bfa_fcs_port_s *port = ms->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &ms->fcxp_wqe,
				    bfa_fcs_port_ms_send_gfn, ms);
		return;
	}
	ms->fcxp = fcxp;

	len = fc_gfn_req_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			       bfa_fcs_port_get_fcid(port),
			       bfa_lps_get_peer_nwwn(port->fabric->lps));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_ms_gfn_response,
		      (void *)ms, FC_MAX_PDUSZ, FC_RA_TOV);

	bfa_sm_send_event(ms, MSSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_port_ms_gfn_response(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
			     bfa_status_t req_status, u32 rsp_len,
			       u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_ms_s *ms = (struct bfa_fcs_port_ms_s *)cbarg;
	struct bfa_fcs_port_s *port = ms->port;
	struct ct_hdr_s       *cthdr = NULL;
	wwn_t          *gfn_resp;

	bfa_trc(port->fcs, req_status);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		gfn_resp = (wwn_t *) (cthdr + 1);
		/*
		 * check if it has actually changed
		 */
		if ((memcmp
		     ((void *)&bfa_fcs_port_get_fabric_name(port), gfn_resp,
		      sizeof(wwn_t)) != 0))
			bfa_fcs_fabric_set_fabric_name(port->fabric, *gfn_resp);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
}

/**
 *  ms_pvt MS local functions
 */

static void
bfa_fcs_port_ms_send_plogi(void *ms_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_ms_s *ms = ms_cbarg;
	struct bfa_fcs_port_s *port = ms->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		port->stats.ms_plogi_alloc_wait++;
		bfa_fcxp_alloc_wait(port->fcs->bfa, &ms->fcxp_wqe,
				    bfa_fcs_port_ms_send_plogi, ms);
		return;
	}
	ms->fcxp = fcxp;

	len = fc_plogi_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_os_hton3b(FC_MGMT_SERVER),
			     bfa_fcs_port_get_fcid(port), 0,
			     port->port_cfg.pwwn, port->port_cfg.nwwn,
			     bfa_pport_get_maxfrsize(port->fcs->bfa));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_ms_plogi_response,
		      (void *)ms, FC_MAX_PDUSZ, FC_RA_TOV);

	port->stats.ms_plogi_sent++;
	bfa_sm_send_event(ms, MSSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_port_ms_plogi_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			       void *cbarg, bfa_status_t req_status,
			       u32 rsp_len, u32 resid_len,
			       struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_ms_s *ms = (struct bfa_fcs_port_ms_s *)cbarg;

	struct bfa_fcs_port_s *port = ms->port;
	struct fc_els_cmd_s   *els_cmd;
	struct fc_ls_rjt_s    *ls_rjt;

	bfa_trc(port->fcs, req_status);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		port->stats.ms_plogi_rsp_err++;
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
		return;
	}

	els_cmd = (struct fc_els_cmd_s *) BFA_FCXP_RSP_PLD(fcxp);

	switch (els_cmd->els_code) {

	case FC_ELS_ACC:
		if (rsp_len < sizeof(struct fc_logi_s)) {
			bfa_trc(port->fcs, rsp_len);
			port->stats.ms_plogi_acc_err++;
			bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
			break;
		}
		port->stats.ms_plogi_accepts++;
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_OK);
		break;

	case FC_ELS_LS_RJT:
		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(port->fcs, ls_rjt->reason_code);
		bfa_trc(port->fcs, ls_rjt->reason_code_expl);

		port->stats.ms_rejects++;
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
		break;

	default:
		port->stats.ms_plogi_unknown_rsp++;
		bfa_trc(port->fcs, els_cmd->els_code);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
	}
}

static void
bfa_fcs_port_ms_timeout(void *arg)
{
	struct bfa_fcs_port_ms_s *ms = (struct bfa_fcs_port_ms_s *)arg;

	ms->port->stats.ms_timeouts++;
	bfa_sm_send_event(ms, MSSM_EVENT_TIMEOUT);
}


void
bfa_fcs_port_ms_init(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_ms_s *ms = BFA_FCS_GET_MS_FROM_PORT(port);

	ms->port = port;
	bfa_sm_set_state(ms, bfa_fcs_port_ms_sm_offline);

	/*
	 * Invoke init routines of sub modules.
	 */
	bfa_fcs_port_fdmi_init(ms);
}

void
bfa_fcs_port_ms_offline(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_ms_s *ms = BFA_FCS_GET_MS_FROM_PORT(port);

	ms->port = port;
	bfa_sm_send_event(ms, MSSM_EVENT_PORT_OFFLINE);
}

void
bfa_fcs_port_ms_online(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_ms_s *ms = BFA_FCS_GET_MS_FROM_PORT(port);

	ms->port = port;
	bfa_sm_send_event(ms, MSSM_EVENT_PORT_ONLINE);
}

void
bfa_fcs_port_ms_fabric_rscn(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_ms_s *ms = BFA_FCS_GET_MS_FROM_PORT(port);

	/*
	 * @todo.  Handle this only when in Online state
	 */
	if (bfa_sm_cmp_state(ms, bfa_fcs_port_ms_sm_online))
		bfa_sm_send_event(ms, MSSM_EVENT_PORT_FABRIC_RSCN);
}
