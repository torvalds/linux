/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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

/*
 *  bfa_fcs.c BFA FCS main
 */

#include "bfa_fcs.h"
#include "bfa_fcbuild.h"
#include "bfad_drv.h"

BFA_TRC_FILE(FCS, FCS);

/*
 * FCS sub-modules
 */
struct bfa_fcs_mod_s {
	void		(*attach) (struct bfa_fcs_s *fcs);
	void		(*modinit) (struct bfa_fcs_s *fcs);
	void		(*modexit) (struct bfa_fcs_s *fcs);
};

#define BFA_FCS_MODULE(_mod) { _mod ## _modinit, _mod ## _modexit }

static struct bfa_fcs_mod_s fcs_modules[] = {
	{ bfa_fcs_port_attach, NULL, NULL },
	{ bfa_fcs_uf_attach, NULL, NULL },
	{ bfa_fcs_fabric_attach, bfa_fcs_fabric_modinit,
	  bfa_fcs_fabric_modexit },
};

/*
 *  fcs_api BFA FCS API
 */

static void
bfa_fcs_exit_comp(void *fcs_cbarg)
{
	struct bfa_fcs_s      *fcs = fcs_cbarg;
	struct bfad_s         *bfad = fcs->bfad;

	complete(&bfad->comp);
}



/*
 *  fcs_api BFA FCS API
 */

/*
 * fcs attach -- called once to initialize data structures at driver attach time
 */
void
bfa_fcs_attach(struct bfa_fcs_s *fcs, struct bfa_s *bfa, struct bfad_s *bfad,
	       bfa_boolean_t min_cfg)
{
	int		i;
	struct bfa_fcs_mod_s  *mod;

	fcs->bfa = bfa;
	fcs->bfad = bfad;
	fcs->min_cfg = min_cfg;

	bfa_attach_fcs(bfa);
	fcbuild_init();

	for (i = 0; i < sizeof(fcs_modules) / sizeof(fcs_modules[0]); i++) {
		mod = &fcs_modules[i];
		if (mod->attach)
			mod->attach(fcs);
	}
}

/*
 * fcs initialization, called once after bfa initialization is complete
 */
void
bfa_fcs_init(struct bfa_fcs_s *fcs)
{
	int		i, npbc_vports;
	struct bfa_fcs_mod_s  *mod;
	struct bfi_pbc_vport_s pbc_vports[BFI_PBC_MAX_VPORTS];

	for (i = 0; i < sizeof(fcs_modules) / sizeof(fcs_modules[0]); i++) {
		mod = &fcs_modules[i];
		if (mod->modinit)
			mod->modinit(fcs);
	}
	/* Initialize pbc vports */
	if (!fcs->min_cfg) {
		npbc_vports =
		    bfa_iocfc_get_pbc_vports(fcs->bfa, pbc_vports);
		for (i = 0; i < npbc_vports; i++)
			bfa_fcb_pbc_vport_create(fcs->bfa->bfad, pbc_vports[i]);
	}
}

/*
 * Start FCS operations.
 */
void
bfa_fcs_start(struct bfa_fcs_s *fcs)
{
	bfa_fcs_fabric_modstart(fcs);
}

/*
 *	brief
 *		FCS driver details initialization.
 *
 *	param[in]		fcs		FCS instance
 *	param[in]		driver_info	Driver Details
 *
 *	return None
 */
void
bfa_fcs_driver_info_init(struct bfa_fcs_s *fcs,
			struct bfa_fcs_driver_info_s *driver_info)
{

	fcs->driver_info = *driver_info;

	bfa_fcs_fabric_psymb_init(&fcs->fabric);
}

/*
 *	brief
 *		FCS FDMI Driver Parameter Initialization
 *
 *	param[in]		fcs		FCS instance
 *	param[in]		fdmi_enable	TRUE/FALSE
 *
 *	return None
 */
void
bfa_fcs_set_fdmi_param(struct bfa_fcs_s *fcs, bfa_boolean_t fdmi_enable)
{

	fcs->fdmi_enabled = fdmi_enable;

}
/*
 *	brief
 *		FCS instance cleanup and exit.
 *
 *	param[in]		fcs			FCS instance
 *	return None
 */
void
bfa_fcs_exit(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_mod_s  *mod;
	int		nmods, i;

	bfa_wc_init(&fcs->wc, bfa_fcs_exit_comp, fcs);

	nmods = sizeof(fcs_modules) / sizeof(fcs_modules[0]);

	for (i = 0; i < nmods; i++) {

		mod = &fcs_modules[i];
		if (mod->modexit) {
			bfa_wc_up(&fcs->wc);
			mod->modexit(fcs);
		}
	}

	bfa_wc_wait(&fcs->wc);
}


void
bfa_fcs_trc_init(struct bfa_fcs_s *fcs, struct bfa_trc_mod_s *trcmod)
{
	fcs->trcmod = trcmod;
}

void
bfa_fcs_modexit_comp(struct bfa_fcs_s *fcs)
{
	bfa_wc_down(&fcs->wc);
}

/*
 * Fabric module implementation.
 */

#define BFA_FCS_FABRIC_RETRY_DELAY	(2000)	/* Milliseconds */
#define BFA_FCS_FABRIC_CLEANUP_DELAY	(10000)	/* Milliseconds */

#define bfa_fcs_fabric_set_opertype(__fabric) do {			\
		if (bfa_fcport_get_topology((__fabric)->fcs->bfa)	\
		    == BFA_PORT_TOPOLOGY_P2P)				\
			(__fabric)->oper_type = BFA_PORT_TYPE_NPORT;	\
		else							\
			(__fabric)->oper_type = BFA_PORT_TYPE_NLPORT;	\
} while (0)

/*
 * forward declarations
 */
static void bfa_fcs_fabric_init(struct bfa_fcs_fabric_s *fabric);
static void bfa_fcs_fabric_login(struct bfa_fcs_fabric_s *fabric);
static void bfa_fcs_fabric_notify_online(struct bfa_fcs_fabric_s *fabric);
static void bfa_fcs_fabric_notify_offline(struct bfa_fcs_fabric_s *fabric);
static void bfa_fcs_fabric_delay(void *cbarg);
static void bfa_fcs_fabric_delete(struct bfa_fcs_fabric_s *fabric);
static void bfa_fcs_fabric_delete_comp(void *cbarg);
static void bfa_fcs_fabric_process_uf(struct bfa_fcs_fabric_s *fabric,
				      struct fchs_s *fchs, u16 len);
static void bfa_fcs_fabric_process_flogi(struct bfa_fcs_fabric_s *fabric,
					 struct fchs_s *fchs, u16 len);
static void bfa_fcs_fabric_send_flogi_acc(struct bfa_fcs_fabric_s *fabric);
static void bfa_fcs_fabric_flogiacc_comp(void *fcsarg,
					 struct bfa_fcxp_s *fcxp, void *cbarg,
					 bfa_status_t status,
					 u32 rsp_len,
					 u32 resid_len,
					 struct fchs_s *rspfchs);
/*
 *  fcs_fabric_sm fabric state machine functions
 */

/*
 * Fabric state machine events
 */
enum bfa_fcs_fabric_event {
	BFA_FCS_FABRIC_SM_CREATE	= 1,	/*  create from driver	      */
	BFA_FCS_FABRIC_SM_DELETE	= 2,	/*  delete from driver	      */
	BFA_FCS_FABRIC_SM_LINK_DOWN	= 3,	/*  link down from port      */
	BFA_FCS_FABRIC_SM_LINK_UP	= 4,	/*  link up from port	      */
	BFA_FCS_FABRIC_SM_CONT_OP	= 5,	/*  flogi/auth continue op   */
	BFA_FCS_FABRIC_SM_RETRY_OP	= 6,	/*  flogi/auth retry op      */
	BFA_FCS_FABRIC_SM_NO_FABRIC	= 7,	/*  from flogi/auth	      */
	BFA_FCS_FABRIC_SM_PERF_EVFP	= 8,	/*  from flogi/auth	      */
	BFA_FCS_FABRIC_SM_ISOLATE	= 9,	/*  from EVFP processing     */
	BFA_FCS_FABRIC_SM_NO_TAGGING	= 10,	/*  no VFT tagging from EVFP */
	BFA_FCS_FABRIC_SM_DELAYED	= 11,	/*  timeout delay event      */
	BFA_FCS_FABRIC_SM_AUTH_FAILED	= 12,	/*  auth failed	      */
	BFA_FCS_FABRIC_SM_AUTH_SUCCESS	= 13,	/*  auth successful	      */
	BFA_FCS_FABRIC_SM_DELCOMP	= 14,	/*  all vports deleted event */
	BFA_FCS_FABRIC_SM_LOOPBACK	= 15,	/*  Received our own FLOGI   */
	BFA_FCS_FABRIC_SM_START		= 16,	/*  from driver	      */
};

static void	bfa_fcs_fabric_sm_uninit(struct bfa_fcs_fabric_s *fabric,
					 enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_created(struct bfa_fcs_fabric_s *fabric,
					  enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_linkdown(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_flogi(struct bfa_fcs_fabric_s *fabric,
					enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_flogi_retry(struct bfa_fcs_fabric_s *fabric,
					      enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_auth(struct bfa_fcs_fabric_s *fabric,
				       enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_auth_failed(struct bfa_fcs_fabric_s *fabric,
					      enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_loopback(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_nofabric(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_online(struct bfa_fcs_fabric_s *fabric,
					 enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_evfp(struct bfa_fcs_fabric_s *fabric,
				       enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_evfp_done(struct bfa_fcs_fabric_s *fabric,
					    enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_isolated(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_deleting(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
/*
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

/*
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
		if (bfa_fcport_is_linkup(fabric->fcs->bfa)) {
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

/*
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

/*
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

		bfa_fcport_set_tx_bbcredit(fabric->fcs->bfa,
					   fabric->bb_credit);
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
		bfa_fcport_set_tx_bbcredit(fabric->fcs->bfa,
					   fabric->bb_credit);
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

/*
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

/*
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

/*
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

/*
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
		bfa_fcport_set_tx_bbcredit(fabric->fcs->bfa,
					   fabric->bb_credit);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/*
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

/*
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

/*
 *   EVFP exchange complete and VFT tagging is enabled.
 */
static void
bfa_fcs_fabric_sm_evfp_done(struct bfa_fcs_fabric_s *fabric,
			    enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);
}

/*
 *   Port is isolated after EVFP exchange due to VF_ID mismatch (N and F).
 */
static void
bfa_fcs_fabric_sm_isolated(struct bfa_fcs_fabric_s *fabric,
			   enum bfa_fcs_fabric_event event)
{
	struct bfad_s *bfad = (struct bfad_s *)fabric->fcs->bfad;
	char	pwwn_ptr[BFA_STRING_32];

	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);
	wwn2str(pwwn_ptr, fabric->bport.port_cfg.pwwn);

	BFA_LOG(KERN_INFO, bfad, bfa_log_level,
		"Port is isolated due to VF_ID mismatch. "
		"PWWN: %s Port VF_ID: %04x switch port VF_ID: %04x.",
		pwwn_ptr, fabric->fcs->port_vfid,
		fabric->event_arg.swp_vfid);
}

/*
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



/*
 *  fcs_fabric_private fabric private functions
 */

static void
bfa_fcs_fabric_init(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_lport_cfg_s *port_cfg = &fabric->bport.port_cfg;

	port_cfg->roles = BFA_LPORT_ROLE_FCP_IM;
	port_cfg->nwwn = bfa_ioc_get_nwwn(&fabric->fcs->bfa->ioc);
	port_cfg->pwwn = bfa_ioc_get_pwwn(&fabric->fcs->bfa->ioc);
}

/*
 * Port Symbolic Name Creation for base port.
 */
void
bfa_fcs_fabric_psymb_init(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_lport_cfg_s *port_cfg = &fabric->bport.port_cfg;
	char model[BFA_ADAPTER_MODEL_NAME_LEN] = {0};
	struct bfa_fcs_driver_info_s *driver_info = &fabric->fcs->driver_info;

	bfa_ioc_get_adapter_model(&fabric->fcs->bfa->ioc, model);

	/* Model name/number */
	strncpy((char *)&port_cfg->sym_name, model,
		BFA_FCS_PORT_SYMBNAME_MODEL_SZ);
	strncat((char *)&port_cfg->sym_name, BFA_FCS_PORT_SYMBNAME_SEPARATOR,
		sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

	/* Driver Version */
	strncat((char *)&port_cfg->sym_name, (char *)driver_info->version,
		BFA_FCS_PORT_SYMBNAME_VERSION_SZ);
	strncat((char *)&port_cfg->sym_name, BFA_FCS_PORT_SYMBNAME_SEPARATOR,
		sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

	/* Host machine name */
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
			(char *)driver_info->host_os_name,
			BFA_FCS_OS_STR_LEN);
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

		/* Append host OS Patch Info */
		strncat((char *)&port_cfg->sym_name,
			(char *)driver_info->host_os_patch,
			BFA_FCS_PORT_SYMBNAME_OSPATCH_SZ);
	}

	/* null terminate */
	port_cfg->sym_name.symname[BFA_SYMNAME_MAXLEN - 1] = 0;
}

/*
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
		/* Only for CNA */
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
		fabric->fabric_name =  bfa_lps_get_peer_nwwn(fabric->lps);

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
/*
 *		Allocate and send FLOGI.
 */
static void
bfa_fcs_fabric_login(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_s		*bfa = fabric->fcs->bfa;
	struct bfa_lport_cfg_s	*pcfg = &fabric->bport.port_cfg;
	u8			alpa = 0;

	if (bfa_fcport_get_topology(bfa) == BFA_PORT_TOPOLOGY_LOOP)
		alpa = bfa_fcport_get_myalpa(bfa);

	bfa_lps_flogi(fabric->lps, fabric, alpa, bfa_fcport_get_maxfrsize(bfa),
		      pcfg->pwwn, pcfg->nwwn, fabric->auth_reqd);

	fabric->stats.flogi_sent++;
}

static void
bfa_fcs_fabric_notify_online(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head	      *qe, *qen;

	bfa_trc(fabric->fcs, fabric->fabric_name);

	bfa_fcs_fabric_set_opertype(fabric);
	fabric->stats.fabric_onlines++;

	/*
	 * notify online event to base and then virtual ports
	 */
	bfa_fcs_lport_online(&fabric->bport);

	list_for_each_safe(qe, qen, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *) qe;
		bfa_fcs_vport_online(vport);
	}
}

static void
bfa_fcs_fabric_notify_offline(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head	      *qe, *qen;

	bfa_trc(fabric->fcs, fabric->fabric_name);
	fabric->stats.fabric_offlines++;

	/*
	 * notify offline event first to vports and then base port.
	 */
	list_for_each_safe(qe, qen, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *) qe;
		bfa_fcs_vport_offline(vport);
	}

	bfa_fcs_lport_offline(&fabric->bport);

	fabric->fabric_name = 0;
	fabric->fabric_ip_addr[0] = 0;
}

static void
bfa_fcs_fabric_delay(void *cbarg)
{
	struct bfa_fcs_fabric_s *fabric = cbarg;

	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_DELAYED);
}

/*
 * Delete all vports and wait for vport delete completions.
 */
static void
bfa_fcs_fabric_delete(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head	      *qe, *qen;

	list_for_each_safe(qe, qen, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *) qe;
		bfa_fcs_vport_fcs_delete(vport);
	}

	bfa_fcs_lport_delete(&fabric->bport);
	bfa_wc_wait(&fabric->wc);
}

static void
bfa_fcs_fabric_delete_comp(void *cbarg)
{
	struct bfa_fcs_fabric_s *fabric = cbarg;

	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_DELCOMP);
}

/*
 *  fcs_fabric_public fabric public functions
 */

/*
 * Attach time initialization.
 */
void
bfa_fcs_fabric_attach(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_fabric_s *fabric;

	fabric = &fcs->fabric;
	memset(fabric, 0, sizeof(struct bfa_fcs_fabric_s));

	/*
	 * Initialize base fabric.
	 */
	fabric->fcs = fcs;
	INIT_LIST_HEAD(&fabric->vport_q);
	INIT_LIST_HEAD(&fabric->vf_q);
	fabric->lps = bfa_lps_alloc(fcs->bfa);
	bfa_assert(fabric->lps);

	/*
	 * Initialize fabric delete completion handler. Fabric deletion is
	 * complete when the last vport delete is complete.
	 */
	bfa_wc_init(&fabric->wc, bfa_fcs_fabric_delete_comp, fabric);
	bfa_wc_up(&fabric->wc); /* For the base port */

	bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_uninit);
	bfa_fcs_lport_attach(&fabric->bport, fabric->fcs, FC_VF_ID_NULL, NULL);
}

void
bfa_fcs_fabric_modinit(struct bfa_fcs_s *fcs)
{
	bfa_sm_send_event(&fcs->fabric, BFA_FCS_FABRIC_SM_CREATE);
	bfa_trc(fcs, 0);
}

/*
 *   Module cleanup
 */
void
bfa_fcs_fabric_modexit(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_fabric_s *fabric;

	bfa_trc(fcs, 0);

	/*
	 * Cleanup base fabric.
	 */
	fabric = &fcs->fabric;
	bfa_lps_delete(fabric->lps);
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_DELETE);
}

/*
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

/*
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

bfa_boolean_t
bfa_fcs_fabric_is_auth_failed(struct bfa_fcs_fabric_s *fabric)
{
	return bfa_sm_cmp_state(fabric, bfa_fcs_fabric_sm_auth_failed);
}

enum bfa_port_type
bfa_fcs_fabric_port_type(struct bfa_fcs_fabric_s *fabric)
{
	return fabric->oper_type;
}

/*
 *   Link up notification from BFA physical port module.
 */
void
bfa_fcs_fabric_link_up(struct bfa_fcs_fabric_s *fabric)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_LINK_UP);
}

/*
 *   Link down notification from BFA physical port module.
 */
void
bfa_fcs_fabric_link_down(struct bfa_fcs_fabric_s *fabric)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_LINK_DOWN);
}

/*
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
	/*
	 * - add vport to fabric's vport_q
	 */
	bfa_trc(fabric->fcs, fabric->vf_id);

	list_add_tail(&vport->qe, &fabric->vport_q);
	fabric->num_vports++;
	bfa_wc_up(&fabric->wc);
}

/*
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

/*
 *   Base port is deleted.
 */
void
bfa_fcs_fabric_port_delete_comp(struct bfa_fcs_fabric_s *fabric)
{
	bfa_wc_down(&fabric->wc);
}


/*
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

/*
 *	brief
 *
 */
bfa_status_t
bfa_fcs_fabric_addvf(struct bfa_fcs_fabric_s *vf, struct bfa_fcs_s *fcs,
		     struct bfa_lport_cfg_s *port_cfg, struct bfad_vf_s *vf_drv)
{
	bfa_sm_set_state(vf, bfa_fcs_fabric_sm_uninit);
	return BFA_STATUS_OK;
}

/*
 * Lookup for a vport withing a fabric given its pwwn
 */
struct bfa_fcs_vport_s *
bfa_fcs_fabric_vport_lookup(struct bfa_fcs_fabric_s *fabric, wwn_t pwwn)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head	      *qe;

	list_for_each(qe, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *) qe;
		if (bfa_fcs_lport_get_pwwn(&vport->lport) == pwwn)
			return vport;
	}

	return NULL;
}

/*
 *    In a given fabric, return the number of lports.
 *
 *   param[in] fabric - Fabric instance. This can be a base fabric or vf.
 *
 *   @return : 1 or more.
 */
u16
bfa_fcs_fabric_vport_count(struct bfa_fcs_fabric_s *fabric)
{
	return fabric->num_vports;
}

/*
 *  Get OUI of the attached switch.
 *
 *  Note : Use of this function should be avoided as much as possible.
 *         This function should be used only if there is any requirement
*          to check for FOS version below 6.3.
 *         To check if the attached fabric is a brocade fabric, use
 *         bfa_lps_is_brcd_fabric() which works for FOS versions 6.3
 *         or above only.
 */

u16
bfa_fcs_fabric_get_switch_oui(struct bfa_fcs_fabric_s *fabric)
{
	wwn_t fab_nwwn;
	u8 *tmp;
	u16 oui;

	fab_nwwn = bfa_lps_get_peer_nwwn(fabric->lps);

	tmp = (u8 *)&fab_nwwn;
	oui = (tmp[3] << 8) | tmp[4];

	return oui;
}
/*
 *		Unsolicited frame receive handling.
 */
void
bfa_fcs_fabric_uf_recv(struct bfa_fcs_fabric_s *fabric, struct fchs_s *fchs,
		       u16 len)
{
	u32	pid = fchs->d_id;
	struct bfa_fcs_vport_s *vport;
	struct list_head	      *qe;
	struct fc_els_cmd_s *els_cmd = (struct fc_els_cmd_s *) (fchs + 1);
	struct fc_logi_s *flogi = (struct fc_logi_s *) els_cmd;

	bfa_trc(fabric->fcs, len);
	bfa_trc(fabric->fcs, pid);

	/*
	 * Look for our own FLOGI frames being looped back. This means an
	 * external loopback cable is in place. Our own FLOGI frames are
	 * sometimes looped back when switch port gets temporarily bypassed.
	 */
	if ((pid == bfa_os_ntoh3b(FC_FABRIC_PORT)) &&
	    (els_cmd->els_code == FC_ELS_FLOGI) &&
	    (flogi->port_name == bfa_fcs_lport_get_pwwn(&fabric->bport))) {
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_LOOPBACK);
		return;
	}

	/*
	 * FLOGI/EVFP exchanges should be consumed by base fabric.
	 */
	if (fchs->d_id == bfa_os_hton3b(FC_FABRIC_PORT)) {
		bfa_trc(fabric->fcs, pid);
		bfa_fcs_fabric_process_uf(fabric, fchs, len);
		return;
	}

	if (fabric->bport.pid == pid) {
		/*
		 * All authentication frames should be routed to auth
		 */
		bfa_trc(fabric->fcs, els_cmd->els_code);
		if (els_cmd->els_code == FC_ELS_AUTH) {
			bfa_trc(fabric->fcs, els_cmd->els_code);
			return;
		}

		bfa_trc(fabric->fcs, *(u8 *) ((u8 *) fchs));
		bfa_fcs_lport_uf_recv(&fabric->bport, fchs, len);
		return;
	}

	/*
	 * look for a matching local port ID
	 */
	list_for_each(qe, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *) qe;
		if (vport->lport.pid == pid) {
			bfa_fcs_lport_uf_recv(&vport->lport, fchs, len);
			return;
		}
	}
	bfa_trc(fabric->fcs, els_cmd->els_code);
	bfa_fcs_lport_uf_recv(&fabric->bport, fchs, len);
}

/*
 *		Unsolicited frames to be processed by fabric.
 */
static void
bfa_fcs_fabric_process_uf(struct bfa_fcs_fabric_s *fabric, struct fchs_s *fchs,
			  u16 len)
{
	struct fc_els_cmd_s *els_cmd = (struct fc_els_cmd_s *) (fchs + 1);

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

/*
 *	Process	incoming FLOGI
 */
static void
bfa_fcs_fabric_process_flogi(struct bfa_fcs_fabric_s *fabric,
			struct fchs_s *fchs, u16 len)
{
	struct fc_logi_s *flogi = (struct fc_logi_s *) (fchs + 1);
	struct bfa_fcs_lport_s *bport = &fabric->bport;

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

	fabric->bb_credit = be16_to_cpu(flogi->csp.bbcred);
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
	struct bfa_lport_cfg_s *pcfg = &fabric->bport.port_cfg;
	struct bfa_fcs_lport_n2n_s *n2n_port = &fabric->bport.port_topo.pn2n;
	struct bfa_s	  *bfa = fabric->fcs->bfa;
	struct bfa_fcxp_s *fcxp;
	u16	reqlen;
	struct fchs_s	fchs;

	fcxp = bfa_fcs_fcxp_alloc(fabric->fcs);
	/*
	 * Do not expect this failure -- expect remote node to retry
	 */
	if (!fcxp)
		return;

	reqlen = fc_flogi_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				    bfa_os_hton3b(FC_FABRIC_PORT),
				    n2n_port->reply_oxid, pcfg->pwwn,
				    pcfg->nwwn,
				    bfa_fcport_get_maxfrsize(bfa),
				    bfa_fcport_get_rx_bbcredit(bfa));

	bfa_fcxp_send(fcxp, NULL, fabric->vf_id, bfa_lps_get_tag(fabric->lps),
		      BFA_FALSE, FC_CLASS_3,
		      reqlen, &fchs, bfa_fcs_fabric_flogiacc_comp, fabric,
		      FC_MAX_PDUSZ, 0);
}

/*
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
 * @param[in] wwn_t - new fabric name
 *
 * @return - none
 */
void
bfa_fcs_fabric_set_fabric_name(struct bfa_fcs_fabric_s *fabric,
			       wwn_t fabric_name)
{
	struct bfad_s *bfad = (struct bfad_s *)fabric->fcs->bfad;
	char	pwwn_ptr[BFA_STRING_32];
	char	fwwn_ptr[BFA_STRING_32];

	bfa_trc(fabric->fcs, fabric_name);

	if (fabric->fabric_name == 0) {
		/*
		 * With BRCD switches, we don't get Fabric Name in FLOGI.
		 * Don't generate a fabric name change event in this case.
		 */
		fabric->fabric_name = fabric_name;
	} else {
		fabric->fabric_name = fabric_name;
		wwn2str(pwwn_ptr, bfa_fcs_lport_get_pwwn(&fabric->bport));
		wwn2str(fwwn_ptr,
			bfa_fcs_lport_get_fabric_name(&fabric->bport));
		BFA_LOG(KERN_WARNING, bfad, bfa_log_level,
			"Base port WWN = %s Fabric WWN = %s\n",
			pwwn_ptr, fwwn_ptr);
	}
}

/*
 *	Returns FCS vf structure for a given vf_id.
 *
 *	param[in]	vf_id - VF_ID
 *
 *	return
 *	If lookup succeeds, retuns fcs vf object, otherwise returns NULL
 */
bfa_fcs_vf_t   *
bfa_fcs_vf_lookup(struct bfa_fcs_s *fcs, u16 vf_id)
{
	bfa_trc(fcs, vf_id);
	if (vf_id == FC_VF_ID_NULL)
		return &fcs->fabric;

	return NULL;
}

/*
 * BFA FCS PPORT ( physical port)
 */
static void
bfa_fcs_port_event_handler(void *cbarg, enum bfa_port_linkstate event)
{
	struct bfa_fcs_s      *fcs = cbarg;

	bfa_trc(fcs, event);

	switch (event) {
	case BFA_PORT_LINKUP:
		bfa_fcs_fabric_link_up(&fcs->fabric);
		break;

	case BFA_PORT_LINKDOWN:
		bfa_fcs_fabric_link_down(&fcs->fabric);
		break;

	default:
		bfa_assert(0);
	}
}

void
bfa_fcs_port_attach(struct bfa_fcs_s *fcs)
{
	bfa_fcport_event_register(fcs->bfa, bfa_fcs_port_event_handler, fcs);
}

/*
 * BFA FCS UF ( Unsolicited Frames)
 */

/*
 *		BFA callback for unsolicited frame receive handler.
 *
 * @param[in]		cbarg		callback arg for receive handler
 * @param[in]		uf		unsolicited frame descriptor
 *
 * @return None
 */
static void
bfa_fcs_uf_recv(void *cbarg, struct bfa_uf_s *uf)
{
	struct bfa_fcs_s	*fcs = (struct bfa_fcs_s *) cbarg;
	struct fchs_s	*fchs = bfa_uf_get_frmbuf(uf);
	u16	len = bfa_uf_get_frmlen(uf);
	struct fc_vft_s *vft;
	struct bfa_fcs_fabric_s *fabric;

	/*
	 * check for VFT header
	 */
	if (fchs->routing == FC_RTG_EXT_HDR &&
	    fchs->cat_info == FC_CAT_VFT_HDR) {
		bfa_stats(fcs, uf.tagged);
		vft = bfa_uf_get_frmbuf(uf);
		if (fcs->port_vfid == vft->vf_id)
			fabric = &fcs->fabric;
		else
			fabric = bfa_fcs_vf_lookup(fcs, (u16) vft->vf_id);

		/*
		 * drop frame if vfid is unknown
		 */
		if (!fabric) {
			bfa_assert(0);
			bfa_stats(fcs, uf.vfid_unknown);
			bfa_uf_free(uf);
			return;
		}

		/*
		 * skip vft header
		 */
		fchs = (struct fchs_s *) (vft + 1);
		len -= sizeof(struct fc_vft_s);

		bfa_trc(fcs, vft->vf_id);
	} else {
		bfa_stats(fcs, uf.untagged);
		fabric = &fcs->fabric;
	}

	bfa_trc(fcs, ((u32 *) fchs)[0]);
	bfa_trc(fcs, ((u32 *) fchs)[1]);
	bfa_trc(fcs, ((u32 *) fchs)[2]);
	bfa_trc(fcs, ((u32 *) fchs)[3]);
	bfa_trc(fcs, ((u32 *) fchs)[4]);
	bfa_trc(fcs, ((u32 *) fchs)[5]);
	bfa_trc(fcs, len);

	bfa_fcs_fabric_uf_recv(fabric, fchs, len);
	bfa_uf_free(uf);
}

void
bfa_fcs_uf_attach(struct bfa_fcs_s *fcs)
{
	bfa_uf_recv_register(fcs->bfa, bfa_fcs_uf_recv, fcs);
}
