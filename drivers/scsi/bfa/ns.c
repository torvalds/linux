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
 * @page ns_sm_info VPORT NS State Machine
 *
 * @section ns_sm_interactions VPORT NS State Machine Interactions
 *
 * @section ns_sm VPORT NS State Machine
 * 	img ns_sm.jpg
 */
#include <bfa.h>
#include <bfa_svc.h>
#include <bfa_iocfc.h>
#include "fcs_lport.h"
#include "fcs_rport.h"
#include "fcs_trcmod.h"
#include "fcs_fcxp.h"
#include "fcs.h"
#include "lport_priv.h"

BFA_TRC_FILE(FCS, NS);

/*
 * forward declarations
 */
static void     bfa_fcs_port_ns_send_plogi(void *ns_cbarg,
					   struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_ns_send_rspn_id(void *ns_cbarg,
					     struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_ns_send_rft_id(void *ns_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_ns_send_rff_id(void *ns_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_ns_send_gid_ft(void *ns_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_ns_timeout(void *arg);
static void     bfa_fcs_port_ns_plogi_response(void *fcsarg,
					       struct bfa_fcxp_s *fcxp,
					       void *cbarg,
					       bfa_status_t req_status,
					       u32 rsp_len,
					       u32 resid_len,
					       struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_ns_rspn_id_response(void *fcsarg,
						 struct bfa_fcxp_s *fcxp,
						 void *cbarg,
						 bfa_status_t req_status,
						 u32 rsp_len,
						 u32 resid_len,
						 struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_ns_rft_id_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_ns_rff_id_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_ns_gid_ft_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_ns_process_gidft_pids(struct bfa_fcs_port_s *port,
						   u32 *pid_buf,
						   u32 n_pids);

static void     bfa_fcs_port_ns_boot_target_disc(struct bfa_fcs_port_s *port);
/**
 *  fcs_ns_sm FCS nameserver interface state machine
 */

/**
 * VPort NS State Machine events
 */
enum vport_ns_event {
	NSSM_EVENT_PORT_ONLINE = 1,
	NSSM_EVENT_PORT_OFFLINE = 2,
	NSSM_EVENT_PLOGI_SENT = 3,
	NSSM_EVENT_RSP_OK = 4,
	NSSM_EVENT_RSP_ERROR = 5,
	NSSM_EVENT_TIMEOUT = 6,
	NSSM_EVENT_NS_QUERY = 7,
	NSSM_EVENT_RSPNID_SENT = 8,
	NSSM_EVENT_RFTID_SENT = 9,
	NSSM_EVENT_RFFID_SENT = 10,
	NSSM_EVENT_GIDFT_SENT = 11,
};

static void     bfa_fcs_port_ns_sm_offline(struct bfa_fcs_port_ns_s *ns,
					   enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_plogi_sending(struct bfa_fcs_port_ns_s *ns,
						 enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_plogi(struct bfa_fcs_port_ns_s *ns,
					 enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_plogi_retry(struct bfa_fcs_port_ns_s *ns,
					       enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_sending_rspn_id(struct bfa_fcs_port_ns_s *ns,
						   enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_rspn_id(struct bfa_fcs_port_ns_s *ns,
					   enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_rspn_id_retry(struct bfa_fcs_port_ns_s *ns,
						 enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_sending_rft_id(struct bfa_fcs_port_ns_s *ns,
						  enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_rft_id_retry(struct bfa_fcs_port_ns_s *ns,
						enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_rft_id(struct bfa_fcs_port_ns_s *ns,
					  enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_sending_rff_id(struct bfa_fcs_port_ns_s *ns,
						  enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_rff_id_retry(struct bfa_fcs_port_ns_s *ns,
						enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_rff_id(struct bfa_fcs_port_ns_s *ns,
					  enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_sending_gid_ft(struct bfa_fcs_port_ns_s *ns,
						  enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_gid_ft(struct bfa_fcs_port_ns_s *ns,
					  enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_gid_ft_retry(struct bfa_fcs_port_ns_s *ns,
						enum vport_ns_event event);
static void     bfa_fcs_port_ns_sm_online(struct bfa_fcs_port_ns_s *ns,
					  enum vport_ns_event event);
/**
 * 		Start in offline state - awaiting linkup
 */
static void
bfa_fcs_port_ns_sm_offline(struct bfa_fcs_port_ns_s *ns,
			   enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_PORT_ONLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_plogi_sending);
		bfa_fcs_port_ns_send_plogi(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_plogi_sending(struct bfa_fcs_port_ns_s *ns,
				 enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_PLOGI_SENT:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_plogi);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				       &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_plogi(struct bfa_fcs_port_ns_s *ns,
			 enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_plogi_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port), &ns->timer,
				bfa_fcs_port_ns_timeout, ns,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_rspn_id);
		bfa_fcs_port_ns_send_rspn_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_plogi_retry(struct bfa_fcs_port_ns_s *ns,
			       enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_plogi_sending);
		bfa_fcs_port_ns_send_plogi(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_sending_rspn_id(struct bfa_fcs_port_ns_s *ns,
				   enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSPNID_SENT:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_rspn_id);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				       &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_rspn_id(struct bfa_fcs_port_ns_s *ns,
			   enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_rspn_id_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port), &ns->timer,
				bfa_fcs_port_ns_timeout, ns,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_rft_id);
		bfa_fcs_port_ns_send_rft_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(ns->fcxp);
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_rspn_id_retry(struct bfa_fcs_port_ns_s *ns,
				 enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_rspn_id);
		bfa_fcs_port_ns_send_rspn_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_sending_rft_id(struct bfa_fcs_port_ns_s *ns,
				  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RFTID_SENT:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_rft_id);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				       &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_rft_id(struct bfa_fcs_port_ns_s *ns,
			  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_OK:
		/*
		 * Now move to register FC4 Features
		 */
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_rff_id);
		bfa_fcs_port_ns_send_rff_id(ns, NULL);
		break;

	case NSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_rft_id_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port), &ns->timer,
				bfa_fcs_port_ns_timeout, ns,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_rft_id_retry(struct bfa_fcs_port_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_rft_id);
		bfa_fcs_port_ns_send_rft_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_sending_rff_id(struct bfa_fcs_port_ns_s *ns,
				  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RFFID_SENT:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_rff_id);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				       &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_rff_id(struct bfa_fcs_port_ns_s *ns,
			  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_OK:

		/*
		 * If min cfg mode is enabled, we donot initiate rport
		 * discovery with the fabric. Instead, we will retrieve the
		 * boot targets from HAL/FW.
		 */
		if (__fcs_min_cfg(ns->port->fcs)) {
			bfa_fcs_port_ns_boot_target_disc(ns->port);
			bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_online);
			return;
		}

		/*
		 * If the port role is Initiator Mode issue NS query.
		 * If it is Target Mode, skip this and go to online.
		 */
		if (BFA_FCS_VPORT_IS_INITIATOR_MODE(ns->port)) {
			bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_gid_ft);
			bfa_fcs_port_ns_send_gid_ft(ns, NULL);
		} else if (BFA_FCS_VPORT_IS_TARGET_MODE(ns->port)) {
			bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_online);
		}
		/*
		 * kick off mgmt srvr state machine
		 */
		bfa_fcs_port_ms_online(ns->port);
		break;

	case NSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_rff_id_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port), &ns->timer,
				bfa_fcs_port_ns_timeout, ns,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_rff_id_retry(struct bfa_fcs_port_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_rff_id);
		bfa_fcs_port_ns_send_rff_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}
static void
bfa_fcs_port_ns_sm_sending_gid_ft(struct bfa_fcs_port_ns_s *ns,
				  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_GIDFT_SENT:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_gid_ft);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				       &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_gid_ft(struct bfa_fcs_port_ns_s *ns,
			  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_online);
		break;

	case NSSM_EVENT_RSP_ERROR:
		/*
		 * TBD: for certain reject codes, we don't need to retry
		 */
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_gid_ft_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port), &ns->timer,
				bfa_fcs_port_ns_timeout, ns,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_gid_ft_retry(struct bfa_fcs_port_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_gid_ft);
		bfa_fcs_port_ns_send_gid_ft(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_port_ns_sm_online(struct bfa_fcs_port_ns_s *ns,
			  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
		break;

	case NSSM_EVENT_NS_QUERY:
		/*
		 * If the port role is Initiator Mode issue NS query.
		 * If it is Target Mode, skip this and go to online.
		 */
		if (BFA_FCS_VPORT_IS_INITIATOR_MODE(ns->port)) {
			bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_sending_gid_ft);
			bfa_fcs_port_ns_send_gid_ft(ns, NULL);
		};
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}



/**
 *  ns_pvt Nameserver local functions
 */

static void
bfa_fcs_port_ns_send_plogi(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_ns_s *ns = ns_cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		port->stats.ns_plogi_alloc_wait++;
		bfa_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				    bfa_fcs_port_ns_send_plogi, ns);
		return;
	}
	ns->fcxp = fcxp;

	len = fc_plogi_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_os_hton3b(FC_NAME_SERVER),
			     bfa_fcs_port_get_fcid(port), 0,
			     port->port_cfg.pwwn, port->port_cfg.nwwn,
			     bfa_fcport_get_maxfrsize(port->fcs->bfa));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_ns_plogi_response,
		      (void *)ns, FC_MAX_PDUSZ, FC_RA_TOV);
	port->stats.ns_plogi_sent++;

	bfa_sm_send_event(ns, NSSM_EVENT_PLOGI_SENT);
}

static void
bfa_fcs_port_ns_plogi_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			       void *cbarg, bfa_status_t req_status,
			       u32 rsp_len, u32 resid_len,
			       struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_ns_s *ns = (struct bfa_fcs_port_ns_s *)cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	/* struct fc_logi_s *plogi_resp; */
	struct fc_els_cmd_s   *els_cmd;
	struct fc_ls_rjt_s    *ls_rjt;

	bfa_trc(port->fcs, req_status);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_plogi_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	els_cmd = (struct fc_els_cmd_s *) BFA_FCXP_RSP_PLD(fcxp);

	switch (els_cmd->els_code) {

	case FC_ELS_ACC:
		if (rsp_len < sizeof(struct fc_logi_s)) {
			bfa_trc(port->fcs, rsp_len);
			port->stats.ns_plogi_acc_err++;
			bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
			break;
		}
		port->stats.ns_plogi_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		break;

	case FC_ELS_LS_RJT:
		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(port->fcs, ls_rjt->reason_code);
		bfa_trc(port->fcs, ls_rjt->reason_code_expl);

		port->stats.ns_rejects++;

		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		break;

	default:
		port->stats.ns_plogi_unknown_rsp++;
		bfa_trc(port->fcs, els_cmd->els_code);
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
	}
}

/**
 * Register the symbolic port name.
 */
static void
bfa_fcs_port_ns_send_rspn_id(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_ns_s *ns = ns_cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;
	u8         symbl[256];
	u8        *psymbl = &symbl[0];

	bfa_os_memset(symbl, 0, sizeof(symbl));

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		port->stats.ns_rspnid_alloc_wait++;
		bfa_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				    bfa_fcs_port_ns_send_rspn_id, ns);
		return;
	}
	ns->fcxp = fcxp;

	/*
	 * for V-Port, form a Port Symbolic Name
	 */
	if (port->vport) {
		/**For Vports,
		 *  we append the vport's port symbolic name to that of the base port.
		 */

		strncpy((char *)psymbl,
			(char *)
			&(bfa_fcs_port_get_psym_name
			  (bfa_fcs_get_base_port(port->fcs))),
			strlen((char *)
			       &bfa_fcs_port_get_psym_name(bfa_fcs_get_base_port
							   (port->fcs))));

		/*
		 * Ensure we have a null terminating string.
		 */
		((char *)
		 psymbl)[strlen((char *)
				&bfa_fcs_port_get_psym_name
				(bfa_fcs_get_base_port(port->fcs)))] = 0;

		strncat((char *)psymbl,
			(char *)&(bfa_fcs_port_get_psym_name(port)),
			strlen((char *)&bfa_fcs_port_get_psym_name(port)));
	} else {
		psymbl = (u8 *) &(bfa_fcs_port_get_psym_name(port));
	}

	len = fc_rspnid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			      bfa_fcs_port_get_fcid(port), 0, psymbl);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_ns_rspn_id_response,
		      (void *)ns, FC_MAX_PDUSZ, FC_RA_TOV);

	port->stats.ns_rspnid_sent++;

	bfa_sm_send_event(ns, NSSM_EVENT_RSPNID_SENT);
}

static void
bfa_fcs_port_ns_rspn_id_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				 void *cbarg, bfa_status_t req_status,
				 u32 rsp_len, u32 resid_len,
				 struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_ns_s *ns = (struct bfa_fcs_port_ns_s *)cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct ct_hdr_s       *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_rspnid_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		port->stats.ns_rspnid_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		return;
	}

	port->stats.ns_rspnid_rejects++;
	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
}

/**
 * Register FC4-Types
 * TBD, Need to retrieve this from the OS driver, in case IPFC is enabled ?
 */
static void
bfa_fcs_port_ns_send_rft_id(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_ns_s *ns = ns_cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		port->stats.ns_rftid_alloc_wait++;
		bfa_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				    bfa_fcs_port_ns_send_rft_id, ns);
		return;
	}
	ns->fcxp = fcxp;

	len = fc_rftid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_fcs_port_get_fcid(port), 0,
			     port->port_cfg.roles);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_ns_rft_id_response,
		      (void *)ns, FC_MAX_PDUSZ, FC_RA_TOV);

	port->stats.ns_rftid_sent++;
	bfa_sm_send_event(ns, NSSM_EVENT_RFTID_SENT);
}

static void
bfa_fcs_port_ns_rft_id_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_ns_s *ns = (struct bfa_fcs_port_ns_s *)cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct ct_hdr_s       *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_rftid_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		port->stats.ns_rftid_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		return;
	}

	port->stats.ns_rftid_rejects++;
	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
}

/**
* Register FC4-Features : Should be done after RFT_ID
 */
static void
bfa_fcs_port_ns_send_rff_id(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_ns_s *ns = ns_cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;
	u8         fc4_ftrs = 0;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		port->stats.ns_rffid_alloc_wait++;
		bfa_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				    bfa_fcs_port_ns_send_rff_id, ns);
		return;
	}
	ns->fcxp = fcxp;

	if (BFA_FCS_VPORT_IS_INITIATOR_MODE(ns->port))
		fc4_ftrs = FC_GS_FCP_FC4_FEATURE_INITIATOR;
	else if (BFA_FCS_VPORT_IS_TARGET_MODE(ns->port))
		fc4_ftrs = FC_GS_FCP_FC4_FEATURE_TARGET;

	len = fc_rffid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_fcs_port_get_fcid(port), 0, FC_TYPE_FCP,
			     fc4_ftrs);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_ns_rff_id_response,
		      (void *)ns, FC_MAX_PDUSZ, FC_RA_TOV);

	port->stats.ns_rffid_sent++;
	bfa_sm_send_event(ns, NSSM_EVENT_RFFID_SENT);
}

static void
bfa_fcs_port_ns_rff_id_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_ns_s *ns = (struct bfa_fcs_port_ns_s *)cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct ct_hdr_s       *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_rffid_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		port->stats.ns_rffid_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		return;
	}

	port->stats.ns_rffid_rejects++;
	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);

	if (cthdr->reason_code == CT_RSN_NOT_SUPP) {
		/*
		 * if this command is not supported, we don't retry
		 */
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
	} else {
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
	}
}

/**
 * Query Fabric for FC4-Types Devices.
 *
*  TBD : Need to use a local (FCS private) response buffer, since the response
 * can be larger than 2K.
 */
static void
bfa_fcs_port_ns_send_gid_ft(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_ns_s *ns = ns_cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct fchs_s          fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		port->stats.ns_gidft_alloc_wait++;
		bfa_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				    bfa_fcs_port_ns_send_gid_ft, ns);
		return;
	}
	ns->fcxp = fcxp;

	/*
	 * This query is only initiated for FCP initiator mode.
	 */
	len = fc_gid_ft_build(&fchs, bfa_fcxp_get_reqbuf(fcxp), ns->port->pid,
			      FC_TYPE_FCP);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, bfa_fcs_port_ns_gid_ft_response,
		      (void *)ns, bfa_fcxp_get_maxrsp(port->fcs->bfa),
		      FC_RA_TOV);

	port->stats.ns_gidft_sent++;

	bfa_sm_send_event(ns, NSSM_EVENT_GIDFT_SENT);
}

static void
bfa_fcs_port_ns_gid_ft_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_ns_s *ns = (struct bfa_fcs_port_ns_s *)cbarg;
	struct bfa_fcs_port_s *port = ns->port;
	struct ct_hdr_s       *cthdr = NULL;
	u32        n_pids;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_gidft_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	if (resid_len != 0) {
		/*
		 * TBD : we will need to allocate a larger buffer & retry the
		 * command
		 */
		bfa_trc(port->fcs, rsp_len);
		bfa_trc(port->fcs, resid_len);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	switch (cthdr->cmd_rsp_code) {

	case CT_RSP_ACCEPT:

		port->stats.ns_gidft_accepts++;
		n_pids = (fc_get_ctresp_pyld_len(rsp_len) / sizeof(u32));
		bfa_trc(port->fcs, n_pids);
		bfa_fcs_port_ns_process_gidft_pids(port,
						   (u32 *) (cthdr + 1),
						   n_pids);
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		break;

	case CT_RSP_REJECT:

		/*
		 * Check the reason code  & explanation.
		 * There may not have been any FC4 devices in the fabric
		 */
		port->stats.ns_gidft_rejects++;
		bfa_trc(port->fcs, cthdr->reason_code);
		bfa_trc(port->fcs, cthdr->exp_code);

		if ((cthdr->reason_code == CT_RSN_UNABLE_TO_PERF)
		    && (cthdr->exp_code == CT_NS_EXP_FT_NOT_REG)) {

			bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		} else {
			/*
			 * for all other errors, retry
			 */
			bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		}
		break;

	default:
		port->stats.ns_gidft_unknown_rsp++;
		bfa_trc(port->fcs, cthdr->cmd_rsp_code);
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
	}
}

/**
 *     This routine will be called by bfa_timer on timer timeouts.
 *
 * 	param[in] 	port 	- pointer to bfa_fcs_port_t.
 *
 * 	return
 * 		void
 *
* 	Special Considerations:
 *
 * 	note
 */
static void
bfa_fcs_port_ns_timeout(void *arg)
{
	struct bfa_fcs_port_ns_s *ns = (struct bfa_fcs_port_ns_s *)arg;

	ns->port->stats.ns_timeouts++;
	bfa_sm_send_event(ns, NSSM_EVENT_TIMEOUT);
}

/*
 * Process the PID list in GID_FT response
 */
static void
bfa_fcs_port_ns_process_gidft_pids(struct bfa_fcs_port_s *port,
				   u32 *pid_buf, u32 n_pids)
{
	struct fcgs_gidft_resp_s *gidft_entry;
	struct bfa_fcs_rport_s *rport;
	u32        ii;

	for (ii = 0; ii < n_pids; ii++) {
		gidft_entry = (struct fcgs_gidft_resp_s *) &pid_buf[ii];

		if (gidft_entry->pid == port->pid)
			continue;

		/*
		 * Check if this rport already exists
		 */
		rport = bfa_fcs_port_get_rport_by_pid(port, gidft_entry->pid);
		if (rport == NULL) {
			/*
			 * this is a new device. create rport
			 */
			rport = bfa_fcs_rport_create(port, gidft_entry->pid);
		} else {
			/*
			 * this rport already exists
			 */
			bfa_fcs_rport_scn(rport);
		}

		bfa_trc(port->fcs, gidft_entry->pid);

		/*
		 * if the last entry bit is set, bail out.
		 */
		if (gidft_entry->last)
			return;
	}
}

/**
 *  fcs_ns_public FCS nameserver public interfaces
 */

/*
 * Functions called by port/fab.
 * These will send relevant Events to the ns state machine.
 */
void
bfa_fcs_port_ns_init(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_ns_s *ns = BFA_FCS_GET_NS_FROM_PORT(port);

	ns->port = port;
	bfa_sm_set_state(ns, bfa_fcs_port_ns_sm_offline);
}

void
bfa_fcs_port_ns_offline(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_ns_s *ns = BFA_FCS_GET_NS_FROM_PORT(port);

	ns->port = port;
	bfa_sm_send_event(ns, NSSM_EVENT_PORT_OFFLINE);
}

void
bfa_fcs_port_ns_online(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_ns_s *ns = BFA_FCS_GET_NS_FROM_PORT(port);

	ns->port = port;
	bfa_sm_send_event(ns, NSSM_EVENT_PORT_ONLINE);
}

void
bfa_fcs_port_ns_query(struct bfa_fcs_port_s *port)
{
	struct bfa_fcs_port_ns_s *ns = BFA_FCS_GET_NS_FROM_PORT(port);

	bfa_trc(port->fcs, port->pid);
	bfa_sm_send_event(ns, NSSM_EVENT_NS_QUERY);
}

static void
bfa_fcs_port_ns_boot_target_disc(struct bfa_fcs_port_s *port)
{

	struct bfa_fcs_rport_s *rport;
	u8         nwwns;
	wwn_t          *wwns;
	int             ii;

	bfa_iocfc_get_bootwwns(port->fcs->bfa, &nwwns, &wwns);

	for (ii = 0; ii < nwwns; ++ii) {
		rport = bfa_fcs_rport_create_by_wwn(port, wwns[ii]);
		bfa_assert(rport);
	}
}


