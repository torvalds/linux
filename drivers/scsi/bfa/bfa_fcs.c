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

#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfa_fcs.h"
#include "bfa_fcbuild.h"

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
	fcs->num_rport_logins = 0;

	bfa->fcs = BFA_TRUE;
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
	int	i;
	struct bfa_fcs_mod_s  *mod;

	for (i = 0; i < sizeof(fcs_modules) / sizeof(fcs_modules[0]); i++) {
		mod = &fcs_modules[i];
		if (mod->modinit)
			mod->modinit(fcs);
	}
}

/*
 * FCS update cfg - reset the pwwn/nwwn of fabric base logical port
 * with values learned during bfa_init firmware GETATTR REQ.
 */
void
bfa_fcs_update_cfg(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_fabric_s *fabric = &fcs->fabric;
	struct bfa_lport_cfg_s *port_cfg = &fabric->bport.port_cfg;
	struct bfa_ioc_s *ioc = &fabric->fcs->bfa->ioc;

	port_cfg->nwwn = ioc->attr->nwwn;
	port_cfg->pwwn = ioc->attr->pwwn;
}

/*
 * Stop FCS operations.
 */
void
bfa_fcs_stop(struct bfa_fcs_s *fcs)
{
	bfa_wc_init(&fcs->wc, bfa_fcs_exit_comp, fcs);
	bfa_wc_up(&fcs->wc);
	bfa_fcs_fabric_modstop(fcs);
	bfa_wc_wait(&fcs->wc);
}

/*
 * fcs pbc vport initialization
 */
void
bfa_fcs_pbc_vport_init(struct bfa_fcs_s *fcs)
{
	int i, npbc_vports;
	struct bfi_pbc_vport_s pbc_vports[BFI_PBC_MAX_VPORTS];

	/* Initialize pbc vports */
	if (!fcs->min_cfg) {
		npbc_vports =
			bfa_iocfc_get_pbc_vports(fcs->bfa, pbc_vports);
		for (i = 0; i < npbc_vports; i++)
			bfa_fcb_pbc_vport_create(fcs->bfa->bfad, pbc_vports[i]);
	}
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
	bfa_fcs_fabric_nsymb_init(&fcs->fabric);
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


/*
 * Fabric module implementation.
 */

#define BFA_FCS_FABRIC_RETRY_DELAY	(2000)	/* Milliseconds */
#define BFA_FCS_FABRIC_CLEANUP_DELAY	(10000)	/* Milliseconds */

#define bfa_fcs_fabric_set_opertype(__fabric) do {			\
	if (bfa_fcport_get_topology((__fabric)->fcs->bfa)		\
				== BFA_PORT_TOPOLOGY_P2P) {		\
		if (fabric->fab_type == BFA_FCS_FABRIC_SWITCHED)	\
			(__fabric)->oper_type = BFA_PORT_TYPE_NPORT;	\
		else							\
			(__fabric)->oper_type = BFA_PORT_TYPE_P2P;	\
	} else								\
		(__fabric)->oper_type = BFA_PORT_TYPE_NLPORT;		\
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
static void bfa_fcs_fabric_stop(struct bfa_fcs_fabric_s *fabric);
static void bfa_fcs_fabric_stop_comp(void *cbarg);
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
static u8 bfa_fcs_fabric_oper_bbscn(struct bfa_fcs_fabric_s *fabric);
static bfa_boolean_t bfa_fcs_fabric_is_bbscn_enabled(
				struct bfa_fcs_fabric_s *fabric);

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
static void	bfa_fcs_fabric_sm_nofabric(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_evfp(struct bfa_fcs_fabric_s *fabric,
				       enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_evfp_done(struct bfa_fcs_fabric_s *fabric,
					    enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_isolated(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_deleting(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_stopping(struct bfa_fcs_fabric_s *fabric,
					   enum bfa_fcs_fabric_event event);
static void	bfa_fcs_fabric_sm_cleanup(struct bfa_fcs_fabric_s *fabric,
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
	struct bfa_s	*bfa = fabric->fcs->bfa;

	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_START:
		if (!bfa_fcport_is_linkup(fabric->fcs->bfa)) {
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
			break;
		}
		if (bfa_fcport_get_topology(bfa) ==
				BFA_PORT_TOPOLOGY_LOOP) {
			fabric->fab_type = BFA_FCS_FABRIC_LOOP;
			fabric->bport.pid = bfa_fcport_get_myalpa(bfa);
			fabric->bport.pid = bfa_hton3b(fabric->bport.pid);
			bfa_sm_set_state(fabric,
					bfa_fcs_fabric_sm_online);
			bfa_fcs_fabric_set_opertype(fabric);
			bfa_fcs_lport_online(&fabric->bport);
		} else {
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_flogi);
			bfa_fcs_fabric_login(fabric);
		}
		break;

	case BFA_FCS_FABRIC_SM_LINK_UP:
	case BFA_FCS_FABRIC_SM_LINK_DOWN:
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
 *   Link is down, awaiting LINK UP event from port. This is also the
 *   first state at fabric creation.
 */
static void
bfa_fcs_fabric_sm_linkdown(struct bfa_fcs_fabric_s *fabric,
			   enum bfa_fcs_fabric_event event)
{
	struct bfa_s	*bfa = fabric->fcs->bfa;

	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_LINK_UP:
		if (bfa_fcport_get_topology(bfa) != BFA_PORT_TOPOLOGY_LOOP) {
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_flogi);
			bfa_fcs_fabric_login(fabric);
			break;
		}
		fabric->fab_type = BFA_FCS_FABRIC_LOOP;
		fabric->bport.pid = bfa_fcport_get_myalpa(bfa);
		fabric->bport.pid = bfa_hton3b(fabric->bport.pid);
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_online);
		bfa_fcs_fabric_set_opertype(fabric);
		bfa_fcs_lport_online(&fabric->bport);
		break;

	case BFA_FCS_FABRIC_SM_RETRY_OP:
	case BFA_FCS_FABRIC_SM_LOOPBACK:
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	case BFA_FCS_FABRIC_SM_STOP:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_cleanup);
		bfa_fcs_fabric_stop(fabric);
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
					   fabric->bb_credit,
					   bfa_fcs_fabric_oper_bbscn(fabric));
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
		bfa_sm_send_event(fabric->lps, BFA_LPS_SM_OFFLINE);
		bfa_fcs_fabric_set_opertype(fabric);
		break;

	case BFA_FCS_FABRIC_SM_NO_FABRIC:
		fabric->fab_type = BFA_FCS_FABRIC_N2N;
		bfa_fcport_set_tx_bbcredit(fabric->fcs->bfa,
					   fabric->bb_credit,
					   bfa_fcs_fabric_oper_bbscn(fabric));
		bfa_fcs_fabric_notify_online(fabric);
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_nofabric);
		break;

	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		bfa_sm_send_event(fabric->lps, BFA_LPS_SM_OFFLINE);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_sm_send_event(fabric->lps, BFA_LPS_SM_OFFLINE);
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
		bfa_sm_send_event(fabric->lps, BFA_LPS_SM_OFFLINE);
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
		bfa_sm_send_event(fabric->lps, BFA_LPS_SM_OFFLINE);
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
void
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
void
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
		bfa_sm_send_event(fabric->lps, BFA_LPS_SM_OFFLINE);
		bfa_fcs_fabric_notify_offline(fabric);
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	case BFA_FCS_FABRIC_SM_NO_FABRIC:
		bfa_trc(fabric->fcs, fabric->bb_credit);
		bfa_fcport_set_tx_bbcredit(fabric->fcs->bfa,
					   fabric->bb_credit,
					   bfa_fcs_fabric_oper_bbscn(fabric));
		break;

	case BFA_FCS_FABRIC_SM_RETRY_OP:
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/*
 *   Fabric is online - normal operating state.
 */
void
bfa_fcs_fabric_sm_online(struct bfa_fcs_fabric_s *fabric,
			 enum bfa_fcs_fabric_event event)
{
	struct bfa_s	*bfa = fabric->fcs->bfa;

	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_linkdown);
		if (bfa_fcport_get_topology(bfa) == BFA_PORT_TOPOLOGY_LOOP) {
			bfa_fcs_lport_offline(&fabric->bport);
		} else {
			bfa_sm_send_event(fabric->lps, BFA_LPS_SM_OFFLINE);
			bfa_fcs_fabric_notify_offline(fabric);
		}
		break;

	case BFA_FCS_FABRIC_SM_DELETE:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_deleting);
		bfa_fcs_fabric_delete(fabric);
		break;

	case BFA_FCS_FABRIC_SM_STOP:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_stopping);
		bfa_fcs_fabric_stop(fabric);
		break;

	case BFA_FCS_FABRIC_SM_AUTH_FAILED:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_auth_failed);
		bfa_sm_send_event(fabric->lps, BFA_LPS_SM_OFFLINE);
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
		bfa_wc_down(&fabric->fcs->wc);
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
 * Fabric is being stopped, awaiting vport stop completions.
 */
static void
bfa_fcs_fabric_sm_stopping(struct bfa_fcs_fabric_s *fabric,
			   enum bfa_fcs_fabric_event event)
{
	struct bfa_s	*bfa = fabric->fcs->bfa;

	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_STOPCOMP:
		if (bfa_fcport_get_topology(bfa) == BFA_PORT_TOPOLOGY_LOOP) {
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_created);
		} else {
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_cleanup);
			bfa_sm_send_event(fabric->lps, BFA_LPS_SM_LOGOUT);
		}
		break;

	case BFA_FCS_FABRIC_SM_LINK_UP:
		break;

	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		if (bfa_fcport_get_topology(bfa) == BFA_PORT_TOPOLOGY_LOOP)
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_created);
		else
			bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_cleanup);
		break;

	default:
		bfa_sm_fault(fabric->fcs, event);
	}
}

/*
 * Fabric is being stopped, cleanup without FLOGO
 */
static void
bfa_fcs_fabric_sm_cleanup(struct bfa_fcs_fabric_s *fabric,
			  enum bfa_fcs_fabric_event event)
{
	bfa_trc(fabric->fcs, fabric->bport.port_cfg.pwwn);
	bfa_trc(fabric->fcs, event);

	switch (event) {
	case BFA_FCS_FABRIC_SM_STOPCOMP:
	case BFA_FCS_FABRIC_SM_LOGOCOMP:
		bfa_sm_set_state(fabric, bfa_fcs_fabric_sm_created);
		bfa_wc_down(&(fabric->fcs)->wc);
		break;

	case BFA_FCS_FABRIC_SM_LINK_DOWN:
		/*
		 * Ignore - can get this event if we get notified about IOC down
		 * before the fabric completion callbk is done.
		 */
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
	port_cfg->nwwn = fabric->fcs->bfa->ioc.attr->nwwn;
	port_cfg->pwwn = fabric->fcs->bfa->ioc.attr->pwwn;
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
 * Node Symbolic Name Creation for base port and all vports
 */
void
bfa_fcs_fabric_nsymb_init(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_lport_cfg_s *port_cfg = &fabric->bport.port_cfg;
	char model[BFA_ADAPTER_MODEL_NAME_LEN] = {0};
	struct bfa_fcs_driver_info_s *driver_info = &fabric->fcs->driver_info;

	bfa_ioc_get_adapter_model(&fabric->fcs->bfa->ioc, model);

	/* Model name/number */
	strncpy((char *)&port_cfg->node_sym_name, model,
		BFA_FCS_PORT_SYMBNAME_MODEL_SZ);
	strncat((char *)&port_cfg->node_sym_name,
			BFA_FCS_PORT_SYMBNAME_SEPARATOR,
			sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

	/* Driver Version */
	strncat((char *)&port_cfg->node_sym_name, (char *)driver_info->version,
		BFA_FCS_PORT_SYMBNAME_VERSION_SZ);
	strncat((char *)&port_cfg->node_sym_name,
			BFA_FCS_PORT_SYMBNAME_SEPARATOR,
			sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

	/* Host machine name */
	strncat((char *)&port_cfg->node_sym_name,
		(char *)driver_info->host_machine_name,
		BFA_FCS_PORT_SYMBNAME_MACHINENAME_SZ);
	strncat((char *)&port_cfg->node_sym_name,
			BFA_FCS_PORT_SYMBNAME_SEPARATOR,
			sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));

	/* null terminate */
	port_cfg->node_sym_name.symname[BFA_SYMNAME_MAXLEN - 1] = 0;
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
		switch (fabric->lps->ext_status) {
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
		if (fabric->lps->lsrjt_rsn == FC_LS_RJT_RSN_LOGICAL_ERROR &&
		    fabric->lps->lsrjt_expl == FC_LS_RJT_EXP_NO_ADDL_INFO)
			fabric->fcs->bbscn_flogi_rjt = BFA_TRUE;

		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_RETRY_OP);
		return;

	default:
		fabric->stats.flogi_rsp_err++;
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_RETRY_OP);
		return;
	}

	fabric->bb_credit = fabric->lps->pr_bbcred;
	bfa_trc(fabric->fcs, fabric->bb_credit);

	if (!(fabric->lps->brcd_switch))
		fabric->fabric_name =  fabric->lps->pr_nwwn;

	/*
	 * Check port type. It should be 1 = F-port.
	 */
	if (fabric->lps->fport) {
		fabric->bport.pid = fabric->lps->lp_pid;
		fabric->is_npiv = fabric->lps->npiv_en;
		fabric->is_auth = fabric->lps->auth_req;
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_CONT_OP);
	} else {
		/*
		 * Nport-2-Nport direct attached
		 */
		fabric->bport.port_topo.pn2n.rem_port_wwn =
			fabric->lps->pr_pwwn;
		fabric->fab_type = BFA_FCS_FABRIC_N2N;
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
	u8			alpa = 0, bb_scn = 0;

	if (bfa_fcs_fabric_is_bbscn_enabled(fabric) &&
	    (!fabric->fcs->bbscn_flogi_rjt))
		bb_scn = BFA_FCS_PORT_DEF_BB_SCN;

	bfa_lps_flogi(fabric->lps, fabric, alpa, bfa_fcport_get_maxfrsize(bfa),
		      pcfg->pwwn, pcfg->nwwn, fabric->auth_reqd, bb_scn);

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
 * Stop all vports and wait for vport stop completions.
 */
static void
bfa_fcs_fabric_stop(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_fcs_vport_s *vport;
	struct list_head	*qe, *qen;

	bfa_wc_init(&fabric->stop_wc, bfa_fcs_fabric_stop_comp, fabric);

	list_for_each_safe(qe, qen, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *) qe;
		bfa_wc_up(&fabric->stop_wc);
		bfa_fcs_vport_fcs_stop(vport);
	}

	bfa_wc_up(&fabric->stop_wc);
	bfa_fcs_lport_stop(&fabric->bport);
	bfa_wc_wait(&fabric->stop_wc);
}

/*
 * Computes operating BB_SCN value
 */
static u8
bfa_fcs_fabric_oper_bbscn(struct bfa_fcs_fabric_s *fabric)
{
	u8	pr_bbscn = fabric->lps->pr_bbscn;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(fabric->fcs->bfa);

	if (!(fcport->cfg.bb_scn_state && pr_bbscn))
		return 0;

	/* return max of local/remote bb_scn values */
	return ((pr_bbscn > BFA_FCS_PORT_DEF_BB_SCN) ?
		pr_bbscn : BFA_FCS_PORT_DEF_BB_SCN);
}

/*
 * Check if BB_SCN can be enabled.
 */
static bfa_boolean_t
bfa_fcs_fabric_is_bbscn_enabled(struct bfa_fcs_fabric_s *fabric)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(fabric->fcs->bfa);

	if (bfa_ioc_get_fcmode(&fabric->fcs->bfa->ioc) &&
			fcport->cfg.bb_scn_state &&
			!bfa_fcport_is_qos_enabled(fabric->fcs->bfa) &&
			!bfa_fcport_is_trunk_enabled(fabric->fcs->bfa))
		return BFA_TRUE;
	else
		return BFA_FALSE;
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

static void
bfa_fcs_fabric_stop_comp(void *cbarg)
{
	struct bfa_fcs_fabric_s *fabric = cbarg;

	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_STOPCOMP);
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
	WARN_ON(!fabric->lps);

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
 * Fabric module stop -- stop FCS actions
 */
void
bfa_fcs_fabric_modstop(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_fabric_s *fabric;

	bfa_trc(fcs, 0);
	fabric = &fcs->fabric;
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_STOP);
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
	fabric->fcs->bbscn_flogi_rjt = BFA_FALSE;
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
 * Lookup for a vport within a fabric given its pwwn
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

	fab_nwwn = fabric->lps->pr_nwwn;

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
	if ((pid == bfa_ntoh3b(FC_FABRIC_PORT)) &&
	    (els_cmd->els_code == FC_ELS_FLOGI) &&
	    (flogi->port_name == bfa_fcs_lport_get_pwwn(&fabric->bport))) {
		bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_LOOPBACK);
		return;
	}

	/*
	 * FLOGI/EVFP exchanges should be consumed by base fabric.
	 */
	if (fchs->d_id == bfa_hton3b(FC_FABRIC_PORT)) {
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

	if (!bfa_fcs_fabric_is_switched(fabric))
		bfa_fcs_lport_uf_recv(&fabric->bport, fchs, len);

	bfa_trc(fabric->fcs, fchs->type);
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
	fabric->lps->pr_bbscn = (be16_to_cpu(flogi->csp.rxsz) >> 12);
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

	fcxp = bfa_fcs_fcxp_alloc(fabric->fcs, BFA_FALSE);
	/*
	 * Do not expect this failure -- expect remote node to retry
	 */
	if (!fcxp)
		return;

	reqlen = fc_flogi_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				    bfa_hton3b(FC_FABRIC_PORT),
				    n2n_port->reply_oxid, pcfg->pwwn,
				    pcfg->nwwn,
				    bfa_fcport_get_maxfrsize(bfa),
				    bfa_fcport_get_rx_bbcredit(bfa),
				    bfa_fcs_fabric_oper_bbscn(fabric));

	bfa_fcxp_send(fcxp, NULL, fabric->vf_id, fabric->lps->bfa_tag,
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
 * Send AEN notification
 */
static void
bfa_fcs_fabric_aen_post(struct bfa_fcs_lport_s *port,
			enum bfa_port_aen_event event)
{
	struct bfad_s *bfad = (struct bfad_s *)port->fabric->fcs->bfad;
	struct bfa_aen_entry_s  *aen_entry;

	bfad_get_aen_entry(bfad, aen_entry);
	if (!aen_entry)
		return;

	aen_entry->aen_data.port.pwwn = bfa_fcs_lport_get_pwwn(port);
	aen_entry->aen_data.port.fwwn = bfa_fcs_lport_get_fabric_name(port);

	/* Send the AEN notification */
	bfad_im_post_vendor_event(aen_entry, bfad, ++port->fcs->fcs_aen_seq,
				  BFA_AEN_CAT_PORT, event);
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
		bfa_fcs_fabric_aen_post(&fabric->bport,
				BFA_PORT_AEN_FABRIC_NAME_CHANGE);
	}
}

void
bfa_cb_lps_flogo_comp(void *bfad, void *uarg)
{
	struct bfa_fcs_fabric_s *fabric = uarg;
	bfa_sm_send_event(fabric, BFA_FCS_FABRIC_SM_LOGOCOMP);
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
 *	Return the list of local logical ports present in the given VF.
 *
 *	@param[in]	vf	vf for which logical ports are returned
 *	@param[out]	lpwwn	returned logical port wwn list
 *	@param[in,out]	nlports in:size of lpwwn list;
 *				out:total elements present,
 *				actual elements returned is limited by the size
 */
void
bfa_fcs_vf_get_ports(bfa_fcs_vf_t *vf, wwn_t lpwwn[], int *nlports)
{
	struct list_head *qe;
	struct bfa_fcs_vport_s *vport;
	int	i = 0;
	struct bfa_fcs_s	*fcs;

	if (vf == NULL || lpwwn == NULL || *nlports == 0)
		return;

	fcs = vf->fcs;

	bfa_trc(fcs, vf->vf_id);
	bfa_trc(fcs, (uint32_t) *nlports);

	lpwwn[i++] = vf->bport.port_cfg.pwwn;

	list_for_each(qe, &vf->vport_q) {
		if (i >= *nlports)
			break;

		vport = (struct bfa_fcs_vport_s *) qe;
		lpwwn[i++] = vport->lport.port_cfg.pwwn;
	}

	bfa_trc(fcs, i);
	*nlports = i;
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
		WARN_ON(1);
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
			WARN_ON(1);
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
