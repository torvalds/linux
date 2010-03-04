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
 *  fabric.c Fabric module implementation.
 */

#include "fcs_fabric.h"
#include "fcs_lport.h"
#include "fcs_vport.h"
#include "fcs_trcmod.h"
#include "fcs_fcxp.h"
#include "fcs_auth.h"
#include "fcs.h"
#include "fcbuild.h"
#include <log/bfa_log_fcs.h>
#include <aen/bfa_aen_port.h>
#include <bfa_svc.h>

BFA_TRC_FILE(FCS, FABRIC);

#define BFA_FCS_FABRIC_RETRY_DELAY	(2000)	/* Milliseconds */
#define BFA_FCS_FABRIC_CLEANUP_DELAY	(10000)	/* Milliseconds */

#define bfa_fcs_fabric_set_opertype(__fabric) do {             \
	if (bfa_pport_get_topology((__fabric)->fcs->bfa)       \
				== BFA_PPORT_TOPOLOGY_P2P)     \
		(__fabric)->oper_type = BFA_PPORT_TYPE_NPORT;  \
	else                                                   \
		(__fabric)->oper_type = BFA_PPORT_TYPE_NLPORT; \
} while (0)

/*
 * forward declarations
 */
static void     bfa_fcs_fabric_init(struct bfa_fcs_fabric_s *fabric);
static void     bfa_fcs_fabric_login(struct bfa_fcs_fabric_s *fabric);
static void     bfa_fcs_fabric_notify_online(struct bfa_fcs_fabric_s *fabric);
static void     bfa_fcs_fabric_notify_offline(struct bfa_fcs_fabric_s *fabric);
static void     bfa_fcs_fabric_delay(void *cbarg);
static void     bfa_fcs_fabric_delete(struct bfa_fcs_fabric_s *fabric);
static void     bfa_fcs_fabric_delete_comp(void *cbarg);
static void     bfa_fcs_fabric_process_uf(struct bfa_fcs_fabric_s *fabric,
					  struct fchs_s *fchs, u16 len);
static void     bfa_fcs_fabric_process_flogi(struct bfa_fcs_fabric_s *fabric,
					     struct fchs_s *fchs, u16 len);
static void     bfa_fcs_fabric_send_flogi_acc(struct bfa_fcs_fabric_s *fabric);
static void     bfa_fcs_fabric_flogiacc_comp(void *fcsarg,
					     struct bfa_fcxp_s *fcxp,
					     void *cbarg, bfa_status_t status,
					     u32 rsp_len,
					     u32 resid_len,
					     struct fchs_s *rspfchs);
/**
 *  fcs_fabric_sm fabric state machine functions
 */

/**
 * Fabric state machine events
 */
enum bfa_fcs_fabric_event {
	BFA_FCS_FABRIC_SM_CREATE = 1,	/*  fabric create from driver */
	BFA_FCS_FABRIC_SM_DELETE = 2,	/*  fabric delete from driver */
	BFA_FCS_FABRIC_SM_LINK_DOWN = 3,	/*  link down from port */
	BFA_FCS_FABRIC_SM_LINK_UP = 4,	/*  link up from port */
	BFA_FCS_FABRIC_SM_CONT_OP = 5,	/*  continue op from flogi/auth */
	BFA_FCS_FABRIC_SM_RETRY_OP = 6,	/*  continue op from flogi/auth */
	BFA_FCS_FABRIC_SM_NO_FABRIC = 7,	/*  no fabric from flogi/auth
						 */
	BFA_FCS_FABRIC_SM_PERF_EVFP = 8,	/*  perform EVFP from
						 *flogi/auth */
	BFA_FCS_FABRIC_SM_ISOLATE = 9,	/*  isolate from EVFP processing */
	BFA_FCS_FABRIC_SM_NO_TAGGING = 10,/*  no VFT tagging from EVFP */
	BFA_FCS_FABRIC_SM_DELAYED = 11,	/*  timeout delay event */
	BFA_FCS_FABRIC_SM_AUTH_FAILED = 12,	/*  authentication failed */
	BFA_FCS_FABRIC_SM_AUTH_SUCCESS = 13,	/*  authentication successful
						 */
	BFA_FCS_FABRIC_SM_DELCOMP = 14,	/*  all vports deleted event */
	BFA_FCS_FABRIC_SM_LOOPBACK = 15,	/*  Received our own FLOGI */
	BFA_FCS_FABRIC_SM_START = 16,	/*  fabric delete from driver */
};

static void     bfa_fcs_fabric_sm_uninit(struct bfa_fcs_fabric_s *fabric,
					 enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_created(struct bfa_fcs_fabric_s *fabric,
					  enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_linkdown(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_flogi(struct bfa_fcs_fabric_s *fabric,
					enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_flogi_retry(struct bfa_fcs_fabric_s *fabric,
					      enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_auth(struct bfa_fcs_fabric_s *fabric,
				       enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_auth_failed(struct bfa_fcs_fabric_s *fabric,
					      enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_loopback(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_nofabric(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_online(struct bfa_fcs_fabric_s *fabric,
					 enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_evfp(struct bfa_fcs_fabric_s *fabric,
				       enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_evfp_done(struct bfa_fcs_fabric_s *fabric,
					    enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_isolated(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void     bfa_fcs_fabric_sm_deleting(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
/**
 *   Beginning state before fabric creation.
 */
static void
bfa_fcs_fabric_sm_uninit(struct bfa_fcs_fabric_s *fabric,
			 enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_CREATE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_created);
		bfa_fcs_fabric_init(fabric);
		bfa_fcs_lport_init(&fabric->bport, &fabric->bport.port_cfg);
		break;

	case BFA_FCS_FABRIC_SM_LINK_UP:
	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   Beginning state before fabric creation.
 */
static void
bfa_fcs_fabric_sm_created(struct bfa_fcs_fabric_s *fabric,
			  enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_START:
		if (bfa_pport_is_linkup(fabric->fcs->bfa)) {
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_flogi);
			bfa_fcs_fabric_login(fabric);
		} else
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		break;

	case BFA_FCS_FABRIC_SM_LINK_UP:
	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_uninit);
		bfa_fcs_modexit_comp(fabric->fcs);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   Link is down, awaiting LINK UP event from port. This is also the
 *   first state at fabric creation.
 */
static void
bfa_fcs_fabric_sm_linkdown(struct bfa_fcs_fabric_s *fabric,
			   enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_LINK_UP:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_flogi);
		bfa_fcs_fabric_login(fabric);
		break;

	case BFA_FCS_FABRIC_SM_RETRY_OP:
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   FLOGI is in progress, awaiting FLOGI reply.
 */
static void
bfa_fcs_fabric_sm_flogi(struct bfa_fcs_fabric_s *fabric,
			enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_CONT_OP:

		bfa_pport_set_tx_bbcredit(fabric->fcs->bfa, fabric->bb_credit);
		fabric->fab_type = BFA_FCS_FABRIC_SWITCHED;

		if (fabric->auth_reqd && fabric->is_auth) {
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_auth);
			bfa_trc(fabric->fcs, event);
		} else {
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_online);
			bfa_fcs_fabric_notify_online(fabric);
		}
		break;

	case BFA_FCS_FABRIC_SM_RETRY_OP:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_flogi_retry);
		bfa_timer_start(fabric->fcs->bfa, &fabric->delay_timer,
				bfa_fcs_fabric_delay, fabric,
				BFA_FCS_FABRIC_RETRY_DELAY);
		break;

	case BFA_FCS_FABRIC_SM_LOOPBACK:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_loopback);
		bfa_lps_discard(fabric->lps);
		bfa_fcs_fabric_set_opertype(fabric);
		break;

	case BFA_FCS_FABRIC_SM_NO_FABRIC:
		fabric->fab_type = BFA_FCS_FABRIC_N2N;
		bfa_pport_set_tx_bbcredit(fabric->fcs->bfa, fabric->bb_credit);
		bfa_fcs_fabric_notify_online(fabric);
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_nofabric);
		break;

	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		bfa_lps_discard(fabric->lps);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_lps_discard(fabric->lps);
		bfa_fcs_fabric_delete(fabric);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}


static void
bfa_fcs_fabric_sm_flogi_retry(struct bfa_fcs_fabric_s *fabric,
			      enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_DELAYED:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_flogi);
		bfa_fcs_fabric_login(fabric);
		break;

	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		bfa_timer_stop(&fabric->delay_timer);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_timer_stop(&fabric->delay_timer);
		bfa_fcs_fabric_delete(fabric);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   Authentication is in progress, awaiting authentication results.
 */
static void
bfa_fcs_fabric_sm_auth(struct bfa_fcs_fabric_s *fabric,
		       enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_AUTH_FAILED:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_auth_failed);
		bfa_lps_discard(fabric->lps);
		break;

	case BFA_FCS_FABRIC_SM_AUTH_SUCCESS:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_online);
		bfa_fcs_fabric_notify_online(fabric);
		break;

	case BFA_FCS_FABRIC_SM_PERF_EVFP:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_evfp);
		break;

	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		bfa_lps_discard(fabric->lps);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   Authentication failed
 */
static void
bfa_fcs_fabric_sm_auth_failed(struct bfa_fcs_fabric_s *fabric,
			      enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		bfa_fcs_fabric_notify_offline(fabric);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   Port is in loopback mode.
 */
static void
bfa_fcs_fabric_sm_loopback(struct bfa_fcs_fabric_s *fabric,
			   enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		bfa_fcs_fabric_notify_offline(fabric);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   There is no attached fabric - private loop or NPort-to-NPort topology.
 */
static void
bfa_fcs_fabric_sm_nofabric(struct bfa_fcs_fabric_s *fabric,
			   enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		bfa_lps_discard(fabric->lps);
		bfa_fcs_fabric_notify_offline(fabric);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	case BFA_FCS_FABRIC_SM_NO_FABRIC:
		bfa_trc(fabric->fcs, fabric->bb_credit);
		bfa_pport_set_tx_bbcredit(fabric->fcs->bfa, fabric->bb_credit);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   Fabric is online - normal operating state.
 */
static void
bfa_fcs_fabric_sm_online(struct bfa_fcs_fabric_s *fabric,
			 enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		bfa_lps_discard(fabric->lps);
		bfa_fcs_fabric_notify_offline(fabric);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	case BFA_FCS_FABRIC_SM_AUTH_FAILED:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_auth_failed);
		bfa_lps_discard(fabric->lps);
		break;

	case BFA_FCS_FABRIC_SM_AUTH_SUCCESS:
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   Exchanging virtual fabric parameters.
 */
static void
bfa_fcs_fabric_sm_evfp(struct bfa_fcs_fabric_s *fabric,
		       enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_CONT_OP:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_evfp_done);
		break;

	case BFA_FCS_FABRIC_SM_ISOLATE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_isolated);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/**
 *   EVFP exchange complete and VFT tagging is enabled.
 */
static void
bfa_fcs_fabric_sm_evfp_done(struct bfa_fcs_fabric_s *fabric,
			    enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);
}

/**
 *   Port is isolated after EVFP exchange due to VF_ID mismatch (N and F).
 */
static void
bfa_fcs_fabric_sm_isolated(struct bfa_fcs_fabric_s *fabric,
			   enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	bfa_log(fabric->fcs->logm, BFA_LOG_FCS_FABRIC_ISOLATED,
		fabric->bport.port_cfg.pwwn, fabric->fcs->port_vfid,
		fabric->event_arg.swp_vfid);
}

/**
 *   Fabric is being deleted, awaiting vport delete completions.
 */
static void
bfa_fcs_fabric_sm_deleting(struct bfa_fcs_fabric_s *fabric,
			   enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_DELCOMP:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_uninit);
		bfa_fcs_modexit_comp(fabric->fcs);
		break;

	case BFA_FCS_FABRIC_SM_LINK_UP:
		break;

	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_fcs_fabric_notify_offline(fabric);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}



/**
 *  fcs_fabric_private fabric private functions
 */

static void
bfa_fcs_fabric_init(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_port_cfg_s *port_cfg = &fabric->bport.port_cfg;

	port_cfg->roles = BFA_PORT_ROLE_FCP_IM;
	port_cfg->nwwn = bfa_ioc_get_nwwn(&fabric->fcs->bfa->ioc);
	port_cfg->pwwn = bfa_ioc_get_pwwn(&fabric->fcs->bfa->ioc);
}

/**
 * Port Symbolic Name Creation for base port.
 */
void
bfa_fcs_fabric_psymb_init(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_port_cfg_s *port_cfg = &fabric->bport.port_cfg;
	struct bfa_adapter_attr_s adapter_attr;
	struct bfa_fcs_driver_info_s *driver_info = &fabric->fcs->driver_info;

	bfa_os_memset((void *)&adapter_attr, 0,
		      sizeof(struct bfa_adapter_attr_s));
	bfa_ioc_get_adapter_attr(&fabric->fcs->bfa->ioc, &adapter_attr);

	/*
	 * Model name/number
	 */
	strncpy((char *)&port_cfg->sym_name, adapter_attr.model,
		BFA_FCS_PORT_SYMBNAME_MODEL_SZ);
	strncat((char *)&port_cfg->sym_name, BFA_FCS_PORT_SYMBNAME_SEPARATOR,
		sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

	/*
	 * Driver Version
	 */
	strncat((char *)&port_cfg->sym_name, (char *)driver_info->version,
		BFA_FCS_PORT_SYMBNAME_VERSION_SZ);
	strncat((char *)&port_cfg->sym_name, BFA_FCS_PORT_SYMBNAME_SEPARATOR,
		sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

	/*
	 * Host machine name
	 */
	strncat((char *)&port_cfg->sym_name,
		(char *)driver_info->host_machine_name,
		BFA_FCS_PORT_SYMBNAME_MACHINENAME_SZ);
	strncat((char *)&port_cfg->sym_name, BFA_FCS_PORT_SYMBNAME_SEPARATOR,
		sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

	/*
	 * Host OS Info :
	 * If OS Patch Info is not there, do not truncate any bytes from the
	 * OS name string and instead copy the entire OS info string (64 bytes).
	 */
	if (driver_info->host_os_patch[0] == '\0') {
		strncat((char *)&port_cfg->sym_name,
			(char *)driver_info->host_os_name, BFA_FCS_OS_STR_LEN);
		strncat((char *)&port_cfg->sym_name,
			BFA_FCS_PORT_SYMBNAME_SEPARATOR,
			sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));
	} else {
		strncat((char *)&port_cfg->sym_name,
			(char *)driver_info->host_os_name,
			BFA_FCS_PORT_SYMBNAME_OSINFO_SZ);
		strncat((char *)&port_cfg->sym_name,
			BFA_FCS_PORT_SYMBNAME_SEPARATOR,
			sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

		/*
		 * Append host OS Patch Info
		 */
		strncat((char *)&port_cfg->sym_name,
			(char *)driver_info->host_os_patch,
			BFA_FCS_PORT_SYMBNAME_OSPATCH_SZ);
	}

	/*
	 * null terminate
	 */
	port_cfg->sym_name.symname[BFA_SYMNAME_MAXLEN - 1] = 0;
}

/**
 * bfa lps login completion callback
 */
void
bfa_cb_lps_flogi_comp(void *bfad, void *uarg, bfa_status_t status)
{
	struct bfa_fcs_fabric_s *fabric = uarg;

	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, status);

	switch (status) {
	case BFA_STATUS_OK:
		fabric->stats.flogi_accepts++;
		break;

	case BFA_STATUS_INVALID_MAC:
		/*
		 * Only for CNA
		 */
		fabric->stats.flogi_acc_err++;
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_RETRY_OP);

		return;

	case BFA_STATUS_EPROTOCOL:
		switch (bfa_lps_get_extstatus(fabric->lps)) {
		case BFA_EPROTO_BAD_ACCEPT:
			fabric->stats.flogi_acc_err++;
			break;

		case BFA_EPROTO_UNKNOWN_RSP:
			fabric->stats.flogi_unknown_rsp++;
			break;

		default:
			break;
		}
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_RETRY_OP);

		return;

	case BFA_STATUS_FABRIC_RJT:
		fabric->stats.flogi_rejects++;
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_RETRY_OP);
		return;

	default:
		fabric->stats.flogi_rsp_err++;
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_RETRY_OP);
		return;
	}

	fabric->bb_credit = bfa_lps_get_peer_bbcredit(fabric->lps);
	bfa_trc(fabric->fcs, fabric->bb_credit);

	if (!bfa_lps_is_brcd_fabric(fabric->lps))
		fabric->fabric_name = bfa_lps_get_peer_nwwn(fabric->lps);

	/*
	 * Check port type. It should be 1 = F-port.
	 */
	if (bfa_lps_is_fport(fabric->lps)) {
		fabric->bport.pid = bfa_lps_get_pid(fabric->lps);
		fabric->is_npiv = bfa_lps_is_npiv_en(fabric->lps);
		fabric->is_auth = bfa_lps_is_authreq(fabric->lps);
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_CONT_OP);
	} else {
		/*
		 * Nport-2-Nport direct attached
		 */
		fabric->bport.port_topo.pn2n.rem_port_wwn =
			bfa_lps_get_peer_pwwn(fabric->lps);
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_NO_FABRIC);
	}

	bfa_trc(fabric->fcs, fabric->bport.pid);
	bfa_trc(fabric->fcs, fabric->is_npiv);
	bfa_trc(fabric->fcs, fabric->is_auth);
}

/**
 * 		Allocate and send FLOGI.
 */
static void
bfa_fcs_fabric_login(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_s   *bfa = fabric->fcs->bfa;
	struct bfa_port_cfg_s *pcfg = &fabric->bport.port_cfg;
	u8         alpa = 0;

	if (bfa_pport_get_topology(bfa) == BFA_PPORT_TOPOLOGY_LOOP)
		alpa = bfa_pport_get_myalpa(bfa);

	bfa_lps_flogi(fabric->lps, fabric, alpa, bfa_pport_get_maxfrsize(bfa),
		      pcfg->pwwn, pcfg->nwwn, fabric->auth_reqd);

	fabric->stats.flogi_sent++;
}

static void
bfa_fcs_fabric_notify_online(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head *qe, *qen;

	bfa_trc(fabric->fcs, fabric->fabric_name);

	bfa_fcs_fabric_set_opertype(fabric);
	fabric->stats.fabric_onlines++;

	/**
	 * notify online event to base and then virtual ports
	 */
	bfa_fcs_port_online(&fabric->bport);

	list_for_each_safe(qe, qen, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *)qe;
		bfa_fcs_vport_online(vport);
	}
}

static void
bfa_fcs_fabric_notify_offline(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head *qe, *qen;

	bfa_trc(fabric->fcs, fabric->fabric_name);
	fabric->stats.fabric_offlines++;

	/**
	 * notify offline event first to vports and then base port.
	 */
	list_for_each_safe(qe, qen, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *)qe;
		bfa_fcs_vport_offline(vport);
	}

	bfa_fcs_port_offline(&fabric->bport);

	fabric->fabric_name = 0;
	fabric->fabric_ip_addr[0] = 0;
}

static void
bfa_fcs_fabric_delay(void *cbarg)
{
	struct bfa_fcs_fabric_s *fabric = cbarg;

	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_DELAYED);
}

/**
 * Delete all vports and wait for vport delete completions.
 */
static void
bfa_fcs_fabric_delete(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head *qe, *qen;

	list_for_each_safe(qe, qen, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *)qe;
		bfa_fcs_vport_delete(vport);
	}

	bfa_fcs_port_delete(&fabric->bport);
	bfa_wc_wait(&fabric->wc);
}

static void
bfa_fcs_fabric_delete_comp(void *cbarg)
{
	struct bfa_fcs_fabric_s *fabric = cbarg;

	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_DELCOMP);
}



/**
 *  fcs_fabric_public fabric public functions
 */

/**
 *   Attach time initialization
 */
void
bfa_fcs_fabric_attach(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_fabric_s *fabric;

	fabric = &fcs->fabric;
	bfa_os_memset(fabric, 0, sizeof(struct bfa_fcs_fabric_s));

	/**
	 * Initialize base fabric.
	 */
	fabric->fcs = fcs;
	INIT_LIST_HEAD(&fabric->vport_q);
	INIT_LIST_HEAD(&fabric->vf_q);
	fabric->lps = bfa_lps_alloc(fcs->bfa);
	bfa_assert(fabric->lps);

	/**
	 * Initialize fabric delete completion handler. Fabric deletion is complete
	 * when the last vport delete is complete.
	 */
	bfa_wc_init(&fabric->wc, bfa_fcs_fabric_delete_comp, fabric);
	bfa_wc_up(&fabric->wc);	/* For the base port */

	bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_uninit);
	bfa_fcs_lport_attach(&fabric->bport, fabric->fcs, FC_VF_ID_NULL, NULL);
}

void
bfa_fcs_fabric_modinit(struct bfa_fcs_s *fcs)
{
	bfa_sm_send_event(&fcs->fabric, BFA_FCS_FABRIC_SM_CREATE);
	bfa_trc(fcs, 0);
}

/**
 *   Module cleanup
 */
void
bfa_fcs_fabric_modexit(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_fabric_s *fabric;

	bfa_trc(fcs, 0);

	/**
	 * Cleanup base fabric.
	 */
	fabric = &fcs->fabric;
	bfa_lps_delete(fabric->lps);
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_DELETE);
}

/**
 * Fabric module start -- kick starts FCS actions
 */
void
bfa_fcs_fabric_modstart(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_fabric_s *fabric;

	bfa_trc(fcs, 0);
	fabric = &fcs->fabric;
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_START);
}

/**
 *   Suspend fabric activity as part of driver suspend.
 */
void
bfa_fcs_fabric_modsusp(struct bfa_fcs_s *fcs)
{
}

bfa_boolean_t
bfa_fcs_fabric_is_loopback(struct bfa_fcs_fabric_s *fabric)
{
	return bfa_sm_cmp_state(fabric, bfa_fcs_fabric_sm_loopback);
}

enum bfa_pport_type
bfa_fcs_fabric_port_type(struct bfa_fcs_fabric_s *fabric)
{
	return fabric->oper_type;
}

/**
 *   Link up notification from BFA physical port module.
 */
void
bfa_fcs_fabric_link_up(struct bfa_fcs_fabric_s *fabric)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_LINK_UP);
}

/**
 *   Link down notification from BFA physical port module.
 */
void
bfa_fcs_fabric_link_down(struct bfa_fcs_fabric_s *fabric)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_LINK_DOWN);
}

/**
 *   A child vport is being created in the fabric.
 *
 *   Call from vport module at vport creation. A list of base port and vports
 *   belonging to a fabric is maintained to propagate link events.
 *
 *   param[in] fabric - Fabric instance. This can be a base fabric or vf.
 *   param[in] vport  - Vport being created.
 *
 *   @return None (always succeeds)
 */
void
bfa_fcs_fabric_addvport(struct bfa_fcs_fabric_s *fabric,
			struct bfa_fcs_vport_s *vport)
{
	/**
	 * - add vport to fabric's vport_q
	 */
	bfa_trc(fabric->fcs, fabric->vf_id);

	list_add_tail(&vport->qe, &fabric->vport_q);
	fabric->num_vports++;
	bfa_wc_up(&fabric->wc);
}

/**
 *   A child vport is being deleted from fabric.
 *
 *   Vport is being deleted.
 */
void
bfa_fcs_fabric_delvport(struct bfa_fcs_fabric_s *fabric,
			struct bfa_fcs_vport_s *vport)
{
	list_del(&vport->qe);
	fabric->num_vports--;
	bfa_wc_down(&fabric->wc);
}

/**
 *   Base port is deleted.
 */
void
bfa_fcs_fabric_port_delete_comp(struct bfa_fcs_fabric_s *fabric)
{
	bfa_wc_down(&fabric->wc);
}

/**
 *    Check if fabric is online.
 *
 *   param[in] fabric - Fabric instance. This can be a base fabric or vf.
 *
 *   @return  TRUE/FALSE
 */
int
bfa_fcs_fabric_is_online(struct bfa_fcs_fabric_s *fabric)
{
	return bfa_sm_cmp_state(fabric, bfa_fcs_fabric_sm_online);
}


bfa_status_t
bfa_fcs_fabric_addvf(struct bfa_fcs_fabric_s *vf, struct bfa_fcs_s *fcs,
		     struct bfa_port_cfg_s *port_cfg,
		     struct bfad_vf_s *vf_drv)
{
	bfa_sm_set_state(vf, bfa_fcs_fabric_sm_uninit);
	return BFA_STATUS_OK;
}

/**
 * Lookup for a vport withing a fabric given its pwwn
 */
struct bfa_fcs_vport_s *
bfa_fcs_fabric_vport_lookup(struct bfa_fcs_fabric_s *fabric, wwn_t pwwn)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head *qe;

	list_for_each(qe, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *)qe;
		if (bfa_fcs_port_get_pwwn(&vport->lport) == pwwn)
			return vport;
	}

	return NULL;
}

/**
 *    In a given fabric, return the number of lports.
 *
 *   param[in] fabric - Fabric instance. This can be a base fabric or vf.
 *
*    @return : 1 or more.
 */
u16
bfa_fcs_fabric_vport_count(struct bfa_fcs_fabric_s *fabric)
{
	return fabric->num_vports;
}

/**
 * 		Unsolicited frame receive handling.
 */
void
bfa_fcs_fabric_uf_recv(struct bfa_fcs_fabric_s *fabric, struct fchs_s *fchs,
		       u16 len)
{
	u32        pid = fchs->d_id;
	struct bfa_fcs_vport_s *vport;
	struct list_head *qe;
	struct fc_els_cmd_s   *els_cmd = (struct fc_els_cmd_s *) (fchs + 1);
	struct fc_logi_s     *flogi = (struct fc_logi_s *) els_cmd;

	bfa_trc(fabric->fcs, len);
	bfa_trc(fabric->fcs, pid);

	/**
	 * Look for our own FLOGI frames being looped back. This means an
	 * external loopback cable is in place. Our own FLOGI frames are
	 * sometimes looped back when switch port gets temporarily bypassed.
	 */
	if ((pid == bfa_os_ntoh3b(FC_FABRIC_PORT))
	    && (els_cmd->els_code == FC_ELS_FLOGI)
	    && (flogi->port_name == bfa_fcs_port_get_pwwn(&fabric->bport))) {
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_LOOPBACK);
		return;
	}

	/**
	 * FLOGI/EVFP exchanges should be consumed by base fabric.
	 */
	if (fchs->d_id == bfa_os_hton3b(FC_FABRIC_PORT)) {
		bfa_trc(fabric->fcs, pid);
		bfa_fcs_fabric_process_uf(fabric, fchs, len);
		return;
	}

	if (fabric->bport.pid == pid) {
		/**
		 * All authentication frames should be routed to auth
		 */
		bfa_trc(fabric->fcs, els_cmd->els_code);
		if (els_cmd->els_code == FC_ELS_AUTH) {
			bfa_trc(fabric->fcs, els_cmd->els_code);
			fabric->auth.response = (u8 *) els_cmd;
			return;
		}

		bfa_trc(fabric->fcs, *(u8 *) ((u8 *) fchs));
		bfa_fcs_port_uf_recv(&fabric->bport, fchs, len);
		return;
	}

	/**
	 * look for a matching local port ID
	 */
	list_for_each(qe, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *)qe;
		if (vport->lport.pid == pid) {
			bfa_fcs_port_uf_recv(&vport->lport, fchs, len);
			return;
		}
	}
	bfa_trc(fabric->fcs, els_cmd->els_code);
	bfa_fcs_port_uf_recv(&fabric->bport, fchs, len);
}

/**
 * 		Unsolicited frames to be processed by fabric.
 */
static void
bfa_fcs_fabric_process_uf(struct bfa_fcs_fabric_s *fabric, struct fchs_s *fchs,
			  u16 len)
{
	struct fc_els_cmd_s   *els_cmd = (struct fc_els_cmd_s *) (fchs + 1);

	bfa_trc(fabric->fcs, els_cmd->els_code);

	switch (els_cmd->els_code) {
	case FC_ELS_FLOGI:
		bfa_fcs_fabric_process_flogi(fabric, fchs, len);
		break;

	default:
		/*
		 * need to generate a LS_RJT
		 */
		break;
	}
}

/**
 * 	Process	incoming FLOGI
 */
static void
bfa_fcs_fabric_process_flogi(struct bfa_fcs_fabric_s *fabric,
			struct fchs_s *fchs, u16 len)
{
	struct fc_logi_s     *flogi = (struct fc_logi_s *) (fchs + 1);
	struct bfa_fcs_port_s *bport = &fabric->bport;

	bfa_trc(fabric->fcs, fchs->s_id);

	fabric->stats.flogi_rcvd++;
	/*
	 * Check port type. It should be 0 = n-port.
	 */
	if (flogi->csp.port_type) {
		/*
		 * @todo: may need to send a LS_RJT
		 */
		bfa_trc(fabric->fcs, flogi->port_name);
		fabric->stats.flogi_rejected++;
		return;
	}

	fabric->bb_credit = bfa_os_ntohs(flogi->csp.bbcred);
	bport->port_topo.pn2n.rem_port_wwn = flogi->port_name;
	bport->port_topo.pn2n.reply_oxid = fchs->ox_id;

	/*
	 * Send a Flogi Acc
	 */
	bfa_fcs_fabric_send_flogi_acc(fabric);
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_NO_FABRIC);
}

static void
bfa_fcs_fabric_send_flogi_acc(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_port_cfg_s *pcfg = &fabric->bport.port_cfg;
	struct bfa_fcs_port_n2n_s *n2n_port = &fabric->bport.port_topo.pn2n;
	struct bfa_s   *bfa = fabric->fcs->bfa;
	struct bfa_fcxp_s *fcxp;
	u16        reqlen;
	struct fchs_s          fchs;

	fcxp = bfa_fcs_fcxp_alloc(fabric->fcs);
	/**
	 * Do not expect this failure -- expect remote node to retry
	 */
	if (!fcxp)
		return;

	reqlen = fc_flogi_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				    bfa_os_hton3b(FC_FABRIC_PORT),
				    n2n_port->reply_oxid, pcfg->pwwn,
				    pcfg->nwwn, bfa_pport_get_maxfrsize(bfa),
				    bfa_pport_get_rx_bbcredit(bfa));

	bfa_fcxp_send(fcxp, NULL, fabric->vf_id, bfa_lps_get_tag(fabric->lps),
			BFA_FALSE, FC_CLASS_3, reqlen, &fchs,
			bfa_fcs_fabric_flogiacc_comp, fabric,
			FC_MAX_PDUSZ, 0); /* Timeout 0 indicates no
					   * response expected
					   */
}

/**
 *   Flogi Acc completion callback.
 */
static void
bfa_fcs_fabric_flogiacc_comp(void *fcsarg, struct bfa_fcxp_s *fcxp, void *cbarg,
			     bfa_status_t status, u32 rsp_len,
			     u32 resid_len, struct fchs_s *rspfchs)
{
	struct bfa_fcs_fabric_s *fabric = cbarg;

	bfa_trc(fabric->fcs, status);
}

/*
 *
 * @param[in] fabric - fabric
 * @param[in] result - 1
 *
 * @return - none
 */
void
bfa_fcs_auth_finished(struct bfa_fcs_fabric_s *fabric, enum auth_status status)
{
	bfa_trc(fabric->fcs, status);

	if (status == FC_AUTH_STATE_SUCCESS)
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_AUTH_SUCCESS);
	else
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_AUTH_FAILED);
}

/**
 * Send AEN notification
 */
static void
bfa_fcs_fabric_aen_post(struct bfa_fcs_port_s *port,
			enum bfa_port_aen_event event)
{
	union bfa_aen_data_u aen_data;
	struct bfa_log_mod_s *logmod = port->fcs->logm;
	wwn_t           pwwn = bfa_fcs_port_get_pwwn(port);
	wwn_t           fwwn = bfa_fcs_port_get_fabric_name(port);
	char            pwwn_ptr[BFA_STRING_32];
	char            fwwn_ptr[BFA_STRING_32];

	wwn2str(pwwn_ptr, pwwn);
	wwn2str(fwwn_ptr, fwwn);

	switch (event) {
	case BFA_PORT_AEN_FABRIC_NAME_CHANGE:
		bfa_log(logmod, BFA_AEN_PORT_FABRIC_NAME_CHANGE, pwwn_ptr,
			fwwn_ptr);
		break;
	default:
		break;
	}

	aen_data.port.pwwn = pwwn;
	aen_data.port.fwwn = fwwn;
}

/*
 *
 * @param[in] fabric - fabric
 * @param[in] wwn_t - new fabric name
 *
 * @return - none
 */
void
bfa_fcs_fabric_set_fabric_name(struct bfa_fcs_fabric_s *fabric,
			       wwn_t fabric_name)
{
	bfa_trc(fabric->fcs, fabric_name);

	if (fabric->fabric_name == 0) {
		/*
		 * With BRCD switches, we don't get Fabric Name in FLOGI.
		 * Don't generate a fabric name change event in this case.
		 */
		fabric->fabric_name = fabric_name;
	} else {
		fabric->fabric_name = fabric_name;
		/*
		 * Generate a Event
		 */
		bfa_fcs_fabric_aen_post(&fabric->bport,
					BFA_PORT_AEN_FABRIC_NAME_CHANGE);
	}

}

/**
 * Not used by FCS.
 */
void
bfa_cb_lps_flogo_comp(void *bfad, void *uarg)
{
}


