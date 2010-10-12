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
 *  bfa_fcs_vport.c FCS virtual port state machine
 */

#include <bfa.h>
#include <bfa_svc.h>
#include <fcbuild.h>
#include "fcs_fabric.h"
#include "fcs_lport.h"
#include "fcs_vport.h"
#include "fcs_trcmod.h"
#include "fcs.h"
#include <aen/bfa_aen_lport.h>

BFA_TRC_FILE(FCS, VPORT);

#define __vport_fcs(__vp)       ((__vp)->lport.fcs)
#define __vport_pwwn(__vp)      ((__vp)->lport.port_cfg.pwwn)
#define __vport_nwwn(__vp)      ((__vp)->lport.port_cfg.nwwn)
#define __vport_bfa(__vp)       ((__vp)->lport.fcs->bfa)
#define __vport_fcid(__vp)      ((__vp)->lport.pid)
#define __vport_fabric(__vp)    ((__vp)->lport.fabric)
#define __vport_vfid(__vp)      ((__vp)->lport.fabric->vf_id)

#define BFA_FCS_VPORT_MAX_RETRIES  5
/*
 * Forward declarations
 */
static void     bfa_fcs_vport_do_fdisc(struct bfa_fcs_vport_s *vport);
static void     bfa_fcs_vport_timeout(void *vport_arg);
static void     bfa_fcs_vport_do_logo(struct bfa_fcs_vport_s *vport);
static void     bfa_fcs_vport_free(struct bfa_fcs_vport_s *vport);

/**
 *  fcs_vport_sm FCS virtual port state machine
 */

/**
 * VPort State Machine events
 */
enum bfa_fcs_vport_event {
	BFA_FCS_VPORT_SM_CREATE = 1,	/*  vport create event */
	BFA_FCS_VPORT_SM_DELETE = 2,	/*  vport delete event */
	BFA_FCS_VPORT_SM_START = 3,	/*  vport start request */
	BFA_FCS_VPORT_SM_STOP = 4,	/*  stop: unsupported */
	BFA_FCS_VPORT_SM_ONLINE = 5,	/*  fabric online */
	BFA_FCS_VPORT_SM_OFFLINE = 6,	/*  fabric offline event */
	BFA_FCS_VPORT_SM_FRMSENT = 7,	/*  fdisc/logo sent events */
	BFA_FCS_VPORT_SM_RSP_OK = 8,	/*  good response */
	BFA_FCS_VPORT_SM_RSP_ERROR = 9,	/*  error/bad response */
	BFA_FCS_VPORT_SM_TIMEOUT = 10,	/*  delay timer event */
	BFA_FCS_VPORT_SM_DELCOMP = 11,	/*  lport delete completion */
	BFA_FCS_VPORT_SM_RSP_DUP_WWN = 12,	/*  Dup wnn error */
	BFA_FCS_VPORT_SM_RSP_FAILED = 13,	/*  non-retryable failure */
};

static void     bfa_fcs_vport_sm_uninit(struct bfa_fcs_vport_s *vport,
					enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_created(struct bfa_fcs_vport_s *vport,
					 enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_offline(struct bfa_fcs_vport_s *vport,
					 enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_fdisc(struct bfa_fcs_vport_s *vport,
				       enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_fdisc_retry(struct bfa_fcs_vport_s *vport,
					     enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_online(struct bfa_fcs_vport_s *vport,
					enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_deleting(struct bfa_fcs_vport_s *vport,
					  enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_cleanup(struct bfa_fcs_vport_s *vport,
					 enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_logo(struct bfa_fcs_vport_s *vport,
				      enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_error(struct bfa_fcs_vport_s *vport,
				       enum bfa_fcs_vport_event event);

static struct bfa_sm_table_s vport_sm_table[] = {
	{BFA_SM(bfa_fcs_vport_sm_uninit), BFA_FCS_VPORT_UNINIT},
	{BFA_SM(bfa_fcs_vport_sm_created), BFA_FCS_VPORT_CREATED},
	{BFA_SM(bfa_fcs_vport_sm_offline), BFA_FCS_VPORT_OFFLINE},
	{BFA_SM(bfa_fcs_vport_sm_fdisc), BFA_FCS_VPORT_FDISC},
	{BFA_SM(bfa_fcs_vport_sm_fdisc_retry), BFA_FCS_VPORT_FDISC_RETRY},
	{BFA_SM(bfa_fcs_vport_sm_online), BFA_FCS_VPORT_ONLINE},
	{BFA_SM(bfa_fcs_vport_sm_deleting), BFA_FCS_VPORT_DELETING},
	{BFA_SM(bfa_fcs_vport_sm_cleanup), BFA_FCS_VPORT_CLEANUP},
	{BFA_SM(bfa_fcs_vport_sm_logo), BFA_FCS_VPORT_LOGO},
	{BFA_SM(bfa_fcs_vport_sm_error), BFA_FCS_VPORT_ERROR}
};

/**
 * Beginning state.
 */
static void
bfa_fcs_vport_sm_uninit(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_CREATE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_created);
		bfa_fcs_fabric_addvport(__vport_fabric(vport), vport);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/**
 * Created state - a start event is required to start up the state machine.
 */
static void
bfa_fcs_vport_sm_created(struct bfa_fcs_vport_s *vport,
			 enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_START:
		if (bfa_fcs_fabric_is_online(__vport_fabric(vport))
		    && bfa_fcs_fabric_npiv_capable(__vport_fabric(vport))) {
			bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc);
			bfa_fcs_vport_do_fdisc(vport);
		} else {
			/**
			 * Fabric is offline or not NPIV capable, stay in
			 * offline state.
			 */
			vport->vport_stats.fab_no_npiv++;
			bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		}
		break;

	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_fcs_port_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_ONLINE:
	case BFA_FCS_VPORT_SM_OFFLINE:
		/**
		 * Ignore ONLINE/OFFLINE events from fabric till vport is started.
		 */
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/**
 * Offline state - awaiting ONLINE event from fabric SM.
 */
static void
bfa_fcs_vport_sm_offline(struct bfa_fcs_vport_s *vport,
			 enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_fcs_port_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_ONLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc);
		vport->fdisc_retries = 0;
		bfa_fcs_vport_do_fdisc(vport);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		/*
		 * This can happen if the vport couldn't be initialzied due
		 * the fact that the npiv was not enabled on the switch. In
		 * that case we will put the vport in offline state. However,
		 * the link can go down and cause the this event to be sent when
		 * we are already offline. Ignore it.
		 */
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/**
 * FDISC is sent and awaiting reply from fabric.
 */
static void
bfa_fcs_vport_sm_fdisc(struct bfa_fcs_vport_s *vport,
		       enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_lps_discard(vport->lps);
		bfa_fcs_port_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		bfa_lps_discard(vport->lps);
		break;

	case BFA_FCS_VPORT_SM_RSP_OK:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_online);
		bfa_fcs_port_online(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_RSP_ERROR:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc_retry);
		bfa_timer_start(__vport_bfa(vport), &vport->timer,
				bfa_fcs_vport_timeout, vport,
				BFA_FCS_RETRY_TIMEOUT);
		break;

	case BFA_FCS_VPORT_SM_RSP_FAILED:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		break;

	case BFA_FCS_VPORT_SM_RSP_DUP_WWN:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_error);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/**
 * FDISC attempt failed - a timer is active to retry FDISC.
 */
static void
bfa_fcs_vport_sm_fdisc_retry(struct bfa_fcs_vport_s *vport,
			     enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_timer_stop(&vport->timer);
		bfa_fcs_port_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		bfa_timer_stop(&vport->timer);
		break;

	case BFA_FCS_VPORT_SM_TIMEOUT:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc);
		vport->vport_stats.fdisc_retries++;
		vport->fdisc_retries++;
		bfa_fcs_vport_do_fdisc(vport);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/**
 * Vport is online (FDISC is complete).
 */
static void
bfa_fcs_vport_sm_online(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_deleting);
		bfa_fcs_port_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		bfa_lps_discard(vport->lps);
		bfa_fcs_port_offline(&vport->lport);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/**
 * Vport is being deleted - awaiting lport delete completion to send
 * LOGO to fabric.
 */
static void
bfa_fcs_vport_sm_deleting(struct bfa_fcs_vport_s *vport,
			  enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		break;

	case BFA_FCS_VPORT_SM_DELCOMP:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_logo);
		bfa_fcs_vport_do_logo(vport);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/**
 * Error State.
 * This state will be set when the Vport Creation fails due to errors like
 * Dup WWN. In this state only operation allowed is a Vport Delete.
 */
static void
bfa_fcs_vport_sm_error(struct bfa_fcs_vport_s *vport,
		       enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_fcs_port_delete(&vport->lport);

		break;

	default:
		bfa_trc(__vport_fcs(vport), event);
	}
}

/**
 * Lport cleanup is in progress since vport is being deleted. Fabric is
 * offline, so no LOGO is needed to complete vport deletion.
 */
static void
bfa_fcs_vport_sm_cleanup(struct bfa_fcs_vport_s *vport,
			 enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELCOMP:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_uninit);
		bfa_fcs_vport_free(vport);
		break;

	case BFA_FCS_VPORT_SM_DELETE:
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/**
 * LOGO is sent to fabric. Vport delete is in progress. Lport delete cleanup
 * is done.
 */
static void
bfa_fcs_vport_sm_logo(struct bfa_fcs_vport_s *vport,
		      enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_lps_discard(vport->lps);
		/*
		 * !!! fall through !!!
		 */

	case BFA_FCS_VPORT_SM_RSP_OK:
	case BFA_FCS_VPORT_SM_RSP_ERROR:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_uninit);
		bfa_fcs_vport_free(vport);
		break;

	case BFA_FCS_VPORT_SM_DELETE:
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}



/**
 *  fcs_vport_private FCS virtual port private functions
 */

/**
 * Send AEN notification
 */
static void
bfa_fcs_vport_aen_post(bfa_fcs_lport_t *port, enum bfa_lport_aen_event event)
{
	union bfa_aen_data_u aen_data;
	struct bfa_log_mod_s *logmod = port->fcs->logm;
	enum bfa_port_role role = port->port_cfg.roles;
	wwn_t           lpwwn = bfa_fcs_port_get_pwwn(port);
	char            lpwwn_ptr[BFA_STRING_32];
	char           *role_str[BFA_PORT_ROLE_FCP_MAX / 2 + 1] =
		{ "Initiator", "Target", "IPFC" };

	wwn2str(lpwwn_ptr, lpwwn);

	bfa_assert(role <= BFA_PORT_ROLE_FCP_MAX);

	bfa_log(logmod, BFA_LOG_CREATE_ID(BFA_AEN_CAT_LPORT, event), lpwwn_ptr,
			role_str[role/2]);

	aen_data.lport.vf_id = port->fabric->vf_id;
	aen_data.lport.roles = role;
	aen_data.lport.ppwwn =
		bfa_fcs_port_get_pwwn(bfa_fcs_get_base_port(port->fcs));
	aen_data.lport.lpwwn = lpwwn;
}

/**
 * This routine will be called to send a FDISC command.
 */
static void
bfa_fcs_vport_do_fdisc(struct bfa_fcs_vport_s *vport)
{
	bfa_lps_fdisc(vport->lps, vport,
		      bfa_fcport_get_maxfrsize(__vport_bfa(vport)),
		      __vport_pwwn(vport), __vport_nwwn(vport));
	vport->vport_stats.fdisc_sent++;
}

static void
bfa_fcs_vport_fdisc_rejected(struct bfa_fcs_vport_s *vport)
{
	u8         lsrjt_rsn = bfa_lps_get_lsrjt_rsn(vport->lps);
	u8         lsrjt_expl = bfa_lps_get_lsrjt_expl(vport->lps);

	bfa_trc(__vport_fcs(vport), lsrjt_rsn);
	bfa_trc(__vport_fcs(vport), lsrjt_expl);

	/*
	 * For certain reason codes, we don't want to retry.
	 */
	switch (bfa_lps_get_lsrjt_expl(vport->lps)) {
	case FC_LS_RJT_EXP_INV_PORT_NAME:	/* by brocade */
	case FC_LS_RJT_EXP_INVALID_NPORT_ID:	/* by Cisco */
		if (vport->fdisc_retries < BFA_FCS_VPORT_MAX_RETRIES)
			bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
		else {
			bfa_fcs_vport_aen_post(&vport->lport,
					       BFA_LPORT_AEN_NPIV_DUP_WWN);
			bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_DUP_WWN);
		}
		break;

	case FC_LS_RJT_EXP_INSUFF_RES:
		/*
		 * This means max logins per port/switch setting on the
		 * switch was exceeded.
		 */
		if (vport->fdisc_retries < BFA_FCS_VPORT_MAX_RETRIES)
			bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
		else {
			bfa_fcs_vport_aen_post(&vport->lport,
					       BFA_LPORT_AEN_NPIV_FABRIC_MAX);
			bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_FAILED);
		}
		break;

	default:
		if (vport->fdisc_retries == 0)	/* Print only once */
			bfa_fcs_vport_aen_post(&vport->lport,
					       BFA_LPORT_AEN_NPIV_UNKNOWN);
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
	}
}

/**
 * 	Called to send a logout to the fabric. Used when a V-Port is
 * 	deleted/stopped.
 */
static void
bfa_fcs_vport_do_logo(struct bfa_fcs_vport_s *vport)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));

	vport->vport_stats.logo_sent++;
	bfa_lps_fdisclogo(vport->lps);
}

/**
 *     This routine will be called by bfa_timer on timer timeouts.
 *
 * 	param[in] 	vport 		- pointer to bfa_fcs_vport_t.
 * 	param[out]	vport_status 	- pointer to return vport status in
 *
 * 	return
 * 		void
 *
* 	Special Considerations:
 *
 * 	note
 */
static void
bfa_fcs_vport_timeout(void *vport_arg)
{
	struct bfa_fcs_vport_s *vport = (struct bfa_fcs_vport_s *)vport_arg;

	vport->vport_stats.fdisc_timeouts++;
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_TIMEOUT);
}

static void
bfa_fcs_vport_free(struct bfa_fcs_vport_s *vport)
{
	bfa_fcs_fabric_delvport(__vport_fabric(vport), vport);
	bfa_fcb_vport_delete(vport->vport_drv);
	bfa_lps_delete(vport->lps);
}



/**
 *  fcs_vport_public FCS virtual port public interfaces
 */

/**
 * Online notification from fabric SM.
 */
void
bfa_fcs_vport_online(struct bfa_fcs_vport_s *vport)
{
	vport->vport_stats.fab_online++;
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_ONLINE);
}

/**
 * Offline notification from fabric SM.
 */
void
bfa_fcs_vport_offline(struct bfa_fcs_vport_s *vport)
{
	vport->vport_stats.fab_offline++;
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_OFFLINE);
}

/**
 * Cleanup notification from fabric SM on link timer expiry.
 */
void
bfa_fcs_vport_cleanup(struct bfa_fcs_vport_s *vport)
{
	vport->vport_stats.fab_cleanup++;
}

/**
 * delete notification from fabric SM. To be invoked from within FCS.
 */
void
bfa_fcs_vport_fcs_delete(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_DELETE);
}

/**
 * Delete completion callback from associated lport
 */
void
bfa_fcs_vport_delete_comp(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_DELCOMP);
}

/**
 *  fcs_vport_api Virtual port API
 */

/**
 *  	Use this function to instantiate a new FCS vport object. This
 * 	function will not trigger any HW initialization process (which will be
 * 	done in vport_start() call)
 *
 * 	param[in] vport	- 	pointer to bfa_fcs_vport_t. This space
 * 					needs to be allocated by the driver.
 * 	param[in] fcs 		- 	FCS instance
 * 	param[in] vport_cfg	- 	vport configuration
 * 	param[in] vf_id    	- 	VF_ID if vport is created within a VF.
 *                          		FC_VF_ID_NULL to specify base fabric.
 * 	param[in] vport_drv 	- 	Opaque handle back to the driver's vport
 * 					structure
 *
 * 	retval BFA_STATUS_OK - on success.
 * 	retval BFA_STATUS_FAILED - on failure.
 */
bfa_status_t
bfa_fcs_vport_create(struct bfa_fcs_vport_s *vport, struct bfa_fcs_s *fcs,
		     u16 vf_id, struct bfa_port_cfg_s *vport_cfg,
		     struct bfad_vport_s *vport_drv)
{
	if (vport_cfg->pwwn == 0)
		return BFA_STATUS_INVALID_WWN;

	if (bfa_fcs_port_get_pwwn(&fcs->fabric.bport) == vport_cfg->pwwn)
		return BFA_STATUS_VPORT_WWN_BP;

	if (bfa_fcs_vport_lookup(fcs, vf_id, vport_cfg->pwwn) != NULL)
		return BFA_STATUS_VPORT_EXISTS;

	if (bfa_fcs_fabric_vport_count(&fcs->fabric) ==
		bfa_lps_get_max_vport(fcs->bfa))
		return BFA_STATUS_VPORT_MAX;

	vport->lps = bfa_lps_alloc(fcs->bfa);
	if (!vport->lps)
		return BFA_STATUS_VPORT_MAX;

	vport->vport_drv = vport_drv;
	vport_cfg->preboot_vp = BFA_FALSE;
	bfa_sm_set_state(vport, bfa_fcs_vport_sm_uninit);

	bfa_fcs_lport_attach(&vport->lport, fcs, vf_id, vport);
	bfa_fcs_lport_init(&vport->lport, vport_cfg);

	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_CREATE);

	return BFA_STATUS_OK;
}

/**
 *      Use this function to instantiate a new FCS PBC vport object. This
 *      function will not trigger any HW initialization process (which will be
 *      done in vport_start() call)
 *
 *      param[in] vport        -       pointer to bfa_fcs_vport_t. This space
 *                                      needs to be allocated by the driver.
 *      param[in] fcs          -       FCS instance
 *      param[in] vport_cfg    -       vport configuration
 *      param[in] vf_id        -       VF_ID if vport is created within a VF.
 *                                      FC_VF_ID_NULL to specify base fabric.
 *      param[in] vport_drv    -       Opaque handle back to the driver's vport
 *                                      structure
 *
 *      retval BFA_STATUS_OK - on success.
 *      retval BFA_STATUS_FAILED - on failure.
 */
bfa_status_t
bfa_fcs_pbc_vport_create(struct bfa_fcs_vport_s *vport, struct bfa_fcs_s *fcs,
			uint16_t vf_id, struct bfa_port_cfg_s *vport_cfg,
			struct bfad_vport_s *vport_drv)
{
	bfa_status_t rc;

	rc = bfa_fcs_vport_create(vport, fcs, vf_id, vport_cfg, vport_drv);
	vport->lport.port_cfg.preboot_vp = BFA_TRUE;

	return rc;
}

/**
 *  	Use this function initialize the vport.
 *
 *  @param[in] vport - pointer to bfa_fcs_vport_t.
 *
 *  @returns None
 */
bfa_status_t
bfa_fcs_vport_start(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_START);

	return BFA_STATUS_OK;
}

/**
 *  	Use this function quiese the vport object. This function will return
 * 	immediately, when the vport is actually stopped, the
 * 	bfa_drv_vport_stop_cb() will be called.
 *
 * 	param[in] vport - pointer to bfa_fcs_vport_t.
 *
 * 	return None
 */
bfa_status_t
bfa_fcs_vport_stop(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_STOP);

	return BFA_STATUS_OK;
}

/**
 *  	Use this function to delete a vport object. Fabric object should
 * 		be stopped before this function call.
 *
 *	Donot invoke this from within FCS
 *
 * 	param[in] vport - pointer to bfa_fcs_vport_t.
 *
 * 	return     None
 */
bfa_status_t
bfa_fcs_vport_delete(struct bfa_fcs_vport_s *vport)
{
	if (vport->lport.port_cfg.preboot_vp)
		return BFA_STATUS_PBC;

	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_DELETE);

	return BFA_STATUS_OK;
}

/**
 *  	Use this function to get vport's current status info.
 *
 * 	param[in] 	vport 		pointer to bfa_fcs_vport_t.
 * 	param[out]	attr 		pointer to return vport attributes
 *
 * 	return None
 */
void
bfa_fcs_vport_get_attr(struct bfa_fcs_vport_s *vport,
		       struct bfa_vport_attr_s *attr)
{
	if (vport == NULL || attr == NULL)
		return;

	bfa_os_memset(attr, 0, sizeof(struct bfa_vport_attr_s));

	bfa_fcs_port_get_attr(&vport->lport, &attr->port_attr);
	attr->vport_state = bfa_sm_to_state(vport_sm_table, vport->sm);
}

/**
 *  	Use this function to get vport's statistics.
 *
 * 	param[in] 	vport 		pointer to bfa_fcs_vport_t.
 * 	param[out]	stats		pointer to return vport statistics in
 *
 * 	return None
 */
void
bfa_fcs_vport_get_stats(struct bfa_fcs_vport_s *vport,
			struct bfa_vport_stats_s *stats)
{
	*stats = vport->vport_stats;
}

/**
 *  	Use this function to clear vport's statistics.
 *
 * 	param[in] 	vport 		pointer to bfa_fcs_vport_t.
 *
 * 	return None
 */
void
bfa_fcs_vport_clr_stats(struct bfa_fcs_vport_s *vport)
{
	bfa_os_memset(&vport->vport_stats, 0, sizeof(struct bfa_vport_stats_s));
}

/**
 *      Lookup a virtual port. Excludes base port from lookup.
 */
struct bfa_fcs_vport_s *
bfa_fcs_vport_lookup(struct bfa_fcs_s *fcs, u16 vf_id, wwn_t vpwwn)
{
	struct bfa_fcs_vport_s *vport;
	struct bfa_fcs_fabric_s *fabric;

	bfa_trc(fcs, vf_id);
	bfa_trc(fcs, vpwwn);

	fabric = bfa_fcs_vf_lookup(fcs, vf_id);
	if (!fabric) {
		bfa_trc(fcs, vf_id);
		return NULL;
	}

	vport = bfa_fcs_fabric_vport_lookup(fabric, vpwwn);
	return vport;
}

/**
 * FDISC Response
 */
void
bfa_cb_lps_fdisc_comp(void *bfad, void *uarg, bfa_status_t status)
{
	struct bfa_fcs_vport_s *vport = uarg;

	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), status);

	switch (status) {
	case BFA_STATUS_OK:
		/*
		 * Initialize the V-Port fields
		 */
		__vport_fcid(vport) = bfa_lps_get_pid(vport->lps);
		vport->vport_stats.fdisc_accepts++;
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_OK);
		break;

	case BFA_STATUS_INVALID_MAC:
		/*
		 * Only for CNA
		 */
		vport->vport_stats.fdisc_acc_bad++;
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);

		break;

	case BFA_STATUS_EPROTOCOL:
		switch (bfa_lps_get_extstatus(vport->lps)) {
		case BFA_EPROTO_BAD_ACCEPT:
			vport->vport_stats.fdisc_acc_bad++;
			break;

		case BFA_EPROTO_UNKNOWN_RSP:
			vport->vport_stats.fdisc_unknown_rsp++;
			break;

		default:
			break;
		}

		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
		break;

	case BFA_STATUS_FABRIC_RJT:
		vport->vport_stats.fdisc_rejects++;
		bfa_fcs_vport_fdisc_rejected(vport);
		break;

	default:
		vport->vport_stats.fdisc_rsp_err++;
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
	}
}

/**
 * LOGO response
 */
void
bfa_cb_lps_fdisclogo_comp(void *bfad, void *uarg)
{
	struct bfa_fcs_vport_s *vport = uarg;
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_OK);
}

/**
 * Received clear virtual link
 */
void
bfa_cb_lps_cvl_event(void *bfad, void *uarg)
{
	struct bfa_fcs_vport_s *vport = uarg;

	/* Send an Offline followed by an ONLINE */
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_OFFLINE);
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_ONLINE);
}
