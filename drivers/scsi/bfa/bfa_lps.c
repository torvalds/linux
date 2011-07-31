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
#include <bfi/bfi_lps.h>
#include <cs/bfa_debug.h>
#include <defs/bfa_defs_pci.h>

BFA_TRC_FILE(HAL, LPS);
BFA_MODULE(lps);

#define BFA_LPS_MIN_LPORTS	(1)
#define BFA_LPS_MAX_LPORTS	(256)

/*
 * Maximum Vports supported per physical port or vf.
 */
#define BFA_LPS_MAX_VPORTS_SUPP_CB  255
#define BFA_LPS_MAX_VPORTS_SUPP_CT  190

/**
 * forward declarations
 */
static void bfa_lps_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *ndm_len,
			    u32 *dm_len);
static void bfa_lps_attach(struct bfa_s *bfa, void *bfad,
			   struct bfa_iocfc_cfg_s *cfg,
			   struct bfa_meminfo_s *meminfo,
			   struct bfa_pcidev_s *pcidev);
static void bfa_lps_detach(struct bfa_s *bfa);
static void bfa_lps_start(struct bfa_s *bfa);
static void bfa_lps_stop(struct bfa_s *bfa);
static void bfa_lps_iocdisable(struct bfa_s *bfa);
static void bfa_lps_login_rsp(struct bfa_s *bfa,
			      struct bfi_lps_login_rsp_s *rsp);
static void bfa_lps_logout_rsp(struct bfa_s *bfa,
			       struct bfi_lps_logout_rsp_s *rsp);
static void bfa_lps_reqq_resume(void *lps_arg);
static void bfa_lps_free(struct bfa_lps_s *lps);
static void bfa_lps_send_login(struct bfa_lps_s *lps);
static void bfa_lps_send_logout(struct bfa_lps_s *lps);
static void bfa_lps_login_comp(struct bfa_lps_s *lps);
static void bfa_lps_logout_comp(struct bfa_lps_s *lps);
static void bfa_lps_cvl_event(struct bfa_lps_s *lps);

/**
 *  lps_pvt BFA LPS private functions
 */

enum bfa_lps_event {
	BFA_LPS_SM_LOGIN	= 1,	/* login request from user	*/
	BFA_LPS_SM_LOGOUT	= 2,	/* logout request from user	*/
	BFA_LPS_SM_FWRSP	= 3,	/* f/w response to login/logout	*/
	BFA_LPS_SM_RESUME	= 4,	/* space present in reqq queue	*/
	BFA_LPS_SM_DELETE	= 5,	/* lps delete from user		*/
	BFA_LPS_SM_OFFLINE	= 6,	/* Link is offline		*/
	BFA_LPS_SM_RX_CVL       = 7,	/* Rx clear virtual link        */
};

static void bfa_lps_sm_init(struct bfa_lps_s *lps, enum bfa_lps_event event);
static void bfa_lps_sm_login(struct bfa_lps_s *lps, enum bfa_lps_event event);
static void bfa_lps_sm_loginwait(struct bfa_lps_s *lps,
			enum bfa_lps_event event);
static void bfa_lps_sm_online(struct bfa_lps_s *lps, enum bfa_lps_event event);
static void bfa_lps_sm_logout(struct bfa_lps_s *lps, enum bfa_lps_event event);
static void bfa_lps_sm_logowait(struct bfa_lps_s *lps,
			enum bfa_lps_event event);

/**
 * Init state -- no login
 */
static void
bfa_lps_sm_init(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->lp_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_LOGIN:
		if (bfa_reqq_full(lps->bfa, lps->reqq)) {
			bfa_sm_set_state(lps, bfa_lps_sm_loginwait);
			bfa_reqq_wait(lps->bfa, lps->reqq, &lps->wqe);
		} else {
			bfa_sm_set_state(lps, bfa_lps_sm_login);
			bfa_lps_send_login(lps);
		}
		if (lps->fdisc)
			bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
			BFA_PL_EID_LOGIN, 0, "FDISC Request");
		else
			bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
			BFA_PL_EID_LOGIN, 0, "FLOGI Request");
		break;

	case BFA_LPS_SM_LOGOUT:
		bfa_lps_logout_comp(lps);
		break;

	case BFA_LPS_SM_DELETE:
		bfa_lps_free(lps);
		break;

	case BFA_LPS_SM_RX_CVL:
	case BFA_LPS_SM_OFFLINE:
		break;

	case BFA_LPS_SM_FWRSP:
		/* Could happen when fabric detects loopback and discards
		 * the lps request. Fw will eventually sent out the timeout
		 * Just ignore
		 */
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/**
 * login is in progress -- awaiting response from firmware
 */
static void
bfa_lps_sm_login(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->lp_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_FWRSP:
		if (lps->status == BFA_STATUS_OK) {
			bfa_sm_set_state(lps, bfa_lps_sm_online);
			if (lps->fdisc)
				bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
				BFA_PL_EID_LOGIN, 0, "FDISC Accept");
			else
				bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
				BFA_PL_EID_LOGIN, 0, "FLOGI Accept");
		} else {
			bfa_sm_set_state(lps, bfa_lps_sm_init);
			if (lps->fdisc)
				bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
				BFA_PL_EID_LOGIN, 0,
				"FDISC Fail (RJT or timeout)");
			else
				bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
				BFA_PL_EID_LOGIN, 0,
				"FLOGI Fail (RJT or timeout)");
		}
		bfa_lps_login_comp(lps);
		break;

	case BFA_LPS_SM_OFFLINE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/**
 * login pending - awaiting space in request queue
 */
static void
bfa_lps_sm_loginwait(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->lp_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_RESUME:
		bfa_sm_set_state(lps, bfa_lps_sm_login);
		break;

	case BFA_LPS_SM_OFFLINE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		bfa_reqq_wcancel(&lps->wqe);
		break;

	case BFA_LPS_SM_RX_CVL:
		/*
		 * Login was not even sent out; so when getting out
		 * of this state, it will appear like a login retry
		 * after Clear virtual link
		 */
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/**
 * login complete
 */
static void
bfa_lps_sm_online(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->lp_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_LOGOUT:
		if (bfa_reqq_full(lps->bfa, lps->reqq)) {
			bfa_sm_set_state(lps, bfa_lps_sm_logowait);
			bfa_reqq_wait(lps->bfa, lps->reqq, &lps->wqe);
		} else {
			bfa_sm_set_state(lps, bfa_lps_sm_logout);
			bfa_lps_send_logout(lps);
		}
		bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
			BFA_PL_EID_LOGO, 0, "Logout");
		break;

	case BFA_LPS_SM_RX_CVL:
		bfa_sm_set_state(lps, bfa_lps_sm_init);

		/* Let the vport module know about this event */
		bfa_lps_cvl_event(lps);
		bfa_plog_str(lps->bfa->plog, BFA_PL_MID_LPS,
			BFA_PL_EID_FIP_FCF_CVL, 0, "FCF Clear Virt. Link Rx");
		break;

	case BFA_LPS_SM_OFFLINE:
	case BFA_LPS_SM_DELETE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/**
 * logout in progress - awaiting firmware response
 */
static void
bfa_lps_sm_logout(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->lp_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_FWRSP:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		bfa_lps_logout_comp(lps);
		break;

	case BFA_LPS_SM_OFFLINE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}

/**
 * logout pending -- awaiting space in request queue
 */
static void
bfa_lps_sm_logowait(struct bfa_lps_s *lps, enum bfa_lps_event event)
{
	bfa_trc(lps->bfa, lps->lp_tag);
	bfa_trc(lps->bfa, event);

	switch (event) {
	case BFA_LPS_SM_RESUME:
		bfa_sm_set_state(lps, bfa_lps_sm_logout);
		bfa_lps_send_logout(lps);
		break;

	case BFA_LPS_SM_OFFLINE:
		bfa_sm_set_state(lps, bfa_lps_sm_init);
		bfa_reqq_wcancel(&lps->wqe);
		break;

	default:
		bfa_sm_fault(lps->bfa, event);
	}
}



/**
 *  lps_pvt BFA LPS private functions
 */

/**
 * return memory requirement
 */
static void
bfa_lps_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *ndm_len, u32 *dm_len)
{
	if (cfg->drvcfg.min_cfg)
		*ndm_len += sizeof(struct bfa_lps_s) * BFA_LPS_MIN_LPORTS;
	else
		*ndm_len += sizeof(struct bfa_lps_s) * BFA_LPS_MAX_LPORTS;
}

/**
 * bfa module attach at initialization time
 */
static void
bfa_lps_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;
	int			i;

	bfa_os_memset(mod, 0, sizeof(struct bfa_lps_mod_s));
	mod->num_lps = BFA_LPS_MAX_LPORTS;
	if (cfg->drvcfg.min_cfg)
		mod->num_lps = BFA_LPS_MIN_LPORTS;
	else
		mod->num_lps = BFA_LPS_MAX_LPORTS;
	mod->lps_arr = lps = (struct bfa_lps_s *) bfa_meminfo_kva(meminfo);

	bfa_meminfo_kva(meminfo) += mod->num_lps * sizeof(struct bfa_lps_s);

	INIT_LIST_HEAD(&mod->lps_free_q);
	INIT_LIST_HEAD(&mod->lps_active_q);

	for (i = 0; i < mod->num_lps; i++, lps++) {
		lps->bfa	= bfa;
		lps->lp_tag	= (u8) i;
		lps->reqq	= BFA_REQQ_LPS;
		bfa_reqq_winit(&lps->wqe, bfa_lps_reqq_resume, lps);
		list_add_tail(&lps->qe, &mod->lps_free_q);
	}
}

static void
bfa_lps_detach(struct bfa_s *bfa)
{
}

static void
bfa_lps_start(struct bfa_s *bfa)
{
}

static void
bfa_lps_stop(struct bfa_s *bfa)
{
}

/**
 * IOC in disabled state -- consider all lps offline
 */
static void
bfa_lps_iocdisable(struct bfa_s *bfa)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;
	struct list_head		*qe, *qen;

	list_for_each_safe(qe, qen, &mod->lps_active_q) {
		lps = (struct bfa_lps_s *) qe;
		bfa_sm_send_event(lps, BFA_LPS_SM_OFFLINE);
	}
}

/**
 * Firmware login response
 */
static void
bfa_lps_login_rsp(struct bfa_s *bfa, struct bfi_lps_login_rsp_s *rsp)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;

	bfa_assert(rsp->lp_tag < mod->num_lps);
	lps = BFA_LPS_FROM_TAG(mod, rsp->lp_tag);

	lps->status = rsp->status;
	switch (rsp->status) {
	case BFA_STATUS_OK:
		lps->fport	= rsp->f_port;
		lps->npiv_en	= rsp->npiv_en;
		lps->lp_pid	= rsp->lp_pid;
		lps->pr_bbcred	= bfa_os_ntohs(rsp->bb_credit);
		lps->pr_pwwn	= rsp->port_name;
		lps->pr_nwwn	= rsp->node_name;
		lps->auth_req	= rsp->auth_req;
		lps->lp_mac	= rsp->lp_mac;
		lps->brcd_switch = rsp->brcd_switch;
		lps->fcf_mac	= rsp->fcf_mac;

		break;

	case BFA_STATUS_FABRIC_RJT:
		lps->lsrjt_rsn = rsp->lsrjt_rsn;
		lps->lsrjt_expl = rsp->lsrjt_expl;

		break;

	case BFA_STATUS_EPROTOCOL:
		lps->ext_status = rsp->ext_status;

		break;

	default:
		/* Nothing to do with other status */
		break;
	}

	bfa_sm_send_event(lps, BFA_LPS_SM_FWRSP);
}

/**
 * Firmware logout response
 */
static void
bfa_lps_logout_rsp(struct bfa_s *bfa, struct bfi_lps_logout_rsp_s *rsp)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;

	bfa_assert(rsp->lp_tag < mod->num_lps);
	lps = BFA_LPS_FROM_TAG(mod, rsp->lp_tag);

	bfa_sm_send_event(lps, BFA_LPS_SM_FWRSP);
}

/**
 * Firmware received a Clear virtual link request (for FCoE)
 */
static void
bfa_lps_rx_cvl_event(struct bfa_s *bfa, struct bfi_lps_cvl_event_s *cvl)
{
	struct bfa_lps_mod_s    *mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s        *lps;

	lps = BFA_LPS_FROM_TAG(mod, cvl->lp_tag);

	bfa_sm_send_event(lps, BFA_LPS_SM_RX_CVL);
}

/**
 * Space is available in request queue, resume queueing request to firmware.
 */
static void
bfa_lps_reqq_resume(void *lps_arg)
{
	struct bfa_lps_s	*lps = lps_arg;

	bfa_sm_send_event(lps, BFA_LPS_SM_RESUME);
}

/**
 * lps is freed -- triggered by vport delete
 */
static void
bfa_lps_free(struct bfa_lps_s *lps)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(lps->bfa);

	list_del(&lps->qe);
	list_add_tail(&lps->qe, &mod->lps_free_q);
}

/**
 * send login request to firmware
 */
static void
bfa_lps_send_login(struct bfa_lps_s *lps)
{
	struct bfi_lps_login_req_s	*m;

	m = bfa_reqq_next(lps->bfa, lps->reqq);
	bfa_assert(m);

	bfi_h2i_set(m->mh, BFI_MC_LPS, BFI_LPS_H2I_LOGIN_REQ,
			bfa_lpuid(lps->bfa));

	m->lp_tag	= lps->lp_tag;
	m->alpa		= lps->alpa;
	m->pdu_size	= bfa_os_htons(lps->pdusz);
	m->pwwn		= lps->pwwn;
	m->nwwn		= lps->nwwn;
	m->fdisc	= lps->fdisc;
	m->auth_en	= lps->auth_en;

	bfa_reqq_produce(lps->bfa, lps->reqq);
}

/**
 * send logout request to firmware
 */
static void
bfa_lps_send_logout(struct bfa_lps_s *lps)
{
	struct bfi_lps_logout_req_s *m;

	m = bfa_reqq_next(lps->bfa, lps->reqq);
	bfa_assert(m);

	bfi_h2i_set(m->mh, BFI_MC_LPS, BFI_LPS_H2I_LOGOUT_REQ,
			bfa_lpuid(lps->bfa));

	m->lp_tag    = lps->lp_tag;
	m->port_name = lps->pwwn;
	bfa_reqq_produce(lps->bfa, lps->reqq);
}

/**
 * Indirect login completion handler for non-fcs
 */
static void
bfa_lps_login_comp_cb(void *arg, bfa_boolean_t complete)
{
	struct bfa_lps_s *lps	= arg;

	if (!complete)
		return;

	if (lps->fdisc)
		bfa_cb_lps_fdisc_comp(lps->bfa->bfad, lps->uarg, lps->status);
	else
		bfa_cb_lps_flogi_comp(lps->bfa->bfad, lps->uarg, lps->status);
}

/**
 * Login completion handler -- direct call for fcs, queue for others
 */
static void
bfa_lps_login_comp(struct bfa_lps_s *lps)
{
	if (!lps->bfa->fcs) {
		bfa_cb_queue(lps->bfa, &lps->hcb_qe,
				bfa_lps_login_comp_cb, lps);
		return;
	}

	if (lps->fdisc)
		bfa_cb_lps_fdisc_comp(lps->bfa->bfad, lps->uarg, lps->status);
	else
		bfa_cb_lps_flogi_comp(lps->bfa->bfad, lps->uarg, lps->status);
}

/**
 * Indirect logout completion handler for non-fcs
 */
static void
bfa_lps_logout_comp_cb(void *arg, bfa_boolean_t complete)
{
	struct bfa_lps_s *lps	= arg;

	if (!complete)
		return;

	if (lps->fdisc)
		bfa_cb_lps_fdisclogo_comp(lps->bfa->bfad, lps->uarg);
	else
		bfa_cb_lps_flogo_comp(lps->bfa->bfad, lps->uarg);
}

/**
 * Logout completion handler -- direct call for fcs, queue for others
 */
static void
bfa_lps_logout_comp(struct bfa_lps_s *lps)
{
	if (!lps->bfa->fcs) {
		bfa_cb_queue(lps->bfa, &lps->hcb_qe,
				bfa_lps_logout_comp_cb, lps);
		return;
	}
	if (lps->fdisc)
		bfa_cb_lps_fdisclogo_comp(lps->bfa->bfad, lps->uarg);
	else
		bfa_cb_lps_flogo_comp(lps->bfa->bfad, lps->uarg);
}

/**
 * Clear virtual link completion handler for non-fcs
 */
static void
bfa_lps_cvl_event_cb(void *arg, bfa_boolean_t complete)
{
	struct bfa_lps_s *lps   = arg;

	if (!complete)
		return;

	/* Clear virtual link to base port will result in link down */
	if (lps->fdisc)
		bfa_cb_lps_cvl_event(lps->bfa->bfad, lps->uarg);
}

/**
 * Received Clear virtual link event --direct call for fcs,
 * queue for others
 */
static void
bfa_lps_cvl_event(struct bfa_lps_s *lps)
{
	if (!lps->bfa->fcs) {
		bfa_cb_queue(lps->bfa, &lps->hcb_qe, bfa_lps_cvl_event_cb,
				lps);
		return;
	}

	/* Clear virtual link to base port will result in link down */
	if (lps->fdisc)
		bfa_cb_lps_cvl_event(lps->bfa->bfad, lps->uarg);
}

u32
bfa_lps_get_max_vport(struct bfa_s *bfa)
{
	if (bfa_ioc_devid(&bfa->ioc) == BFA_PCI_DEVICE_ID_CT)
		return BFA_LPS_MAX_VPORTS_SUPP_CT;
	else
		return BFA_LPS_MAX_VPORTS_SUPP_CB;
}

/**
 *  lps_public BFA LPS public functions
 */

/**
 * Allocate a lport srvice tag.
 */
struct bfa_lps_s  *
bfa_lps_alloc(struct bfa_s *bfa)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps = NULL;

	bfa_q_deq(&mod->lps_free_q, &lps);

	if (lps == NULL)
		return NULL;

	list_add_tail(&lps->qe, &mod->lps_active_q);

	bfa_sm_set_state(lps, bfa_lps_sm_init);
	return lps;
}

/**
 * Free lport service tag. This can be called anytime after an alloc.
 * No need to wait for any pending login/logout completions.
 */
void
bfa_lps_delete(struct bfa_lps_s *lps)
{
	bfa_sm_send_event(lps, BFA_LPS_SM_DELETE);
}

/**
 * Initiate a lport login.
 */
void
bfa_lps_flogi(struct bfa_lps_s *lps, void *uarg, u8 alpa, u16 pdusz,
	wwn_t pwwn, wwn_t nwwn, bfa_boolean_t auth_en)
{
	lps->uarg	= uarg;
	lps->alpa	= alpa;
	lps->pdusz	= pdusz;
	lps->pwwn	= pwwn;
	lps->nwwn	= nwwn;
	lps->fdisc	= BFA_FALSE;
	lps->auth_en	= auth_en;
	bfa_sm_send_event(lps, BFA_LPS_SM_LOGIN);
}

/**
 * Initiate a lport fdisc login.
 */
void
bfa_lps_fdisc(struct bfa_lps_s *lps, void *uarg, u16 pdusz, wwn_t pwwn,
	wwn_t nwwn)
{
	lps->uarg	= uarg;
	lps->alpa	= 0;
	lps->pdusz	= pdusz;
	lps->pwwn	= pwwn;
	lps->nwwn	= nwwn;
	lps->fdisc	= BFA_TRUE;
	lps->auth_en	= BFA_FALSE;
	bfa_sm_send_event(lps, BFA_LPS_SM_LOGIN);
}

/**
 * Initiate a lport logout (flogi).
 */
void
bfa_lps_flogo(struct bfa_lps_s *lps)
{
	bfa_sm_send_event(lps, BFA_LPS_SM_LOGOUT);
}

/**
 * Initiate a lport FDSIC logout.
 */
void
bfa_lps_fdisclogo(struct bfa_lps_s *lps)
{
	bfa_sm_send_event(lps, BFA_LPS_SM_LOGOUT);
}

/**
 * Discard a pending login request -- should be called only for
 * link down handling.
 */
void
bfa_lps_discard(struct bfa_lps_s *lps)
{
	bfa_sm_send_event(lps, BFA_LPS_SM_OFFLINE);
}

/**
 * Return lport services tag
 */
u8
bfa_lps_get_tag(struct bfa_lps_s *lps)
{
	return lps->lp_tag;
}

/**
 * Return lport services tag given the pid
 */
u8
bfa_lps_get_tag_from_pid(struct bfa_s *bfa, u32 pid)
{
	struct bfa_lps_mod_s	*mod = BFA_LPS_MOD(bfa);
	struct bfa_lps_s	*lps;
	int			i;

	for (i = 0, lps = mod->lps_arr; i < mod->num_lps; i++, lps++) {
		if (lps->lp_pid == pid)
			return lps->lp_tag;
	}

	/* Return base port tag anyway */
	return 0;
}

/**
 * return if fabric login indicates support for NPIV
 */
bfa_boolean_t
bfa_lps_is_npiv_en(struct bfa_lps_s *lps)
{
	return lps->npiv_en;
}

/**
 * Return TRUE if attached to F-Port, else return FALSE
 */
bfa_boolean_t
bfa_lps_is_fport(struct bfa_lps_s *lps)
{
	return lps->fport;
}

/**
 * Return TRUE if attached to a Brocade Fabric
 */
bfa_boolean_t
bfa_lps_is_brcd_fabric(struct bfa_lps_s *lps)
{
	return lps->brcd_switch;
}
/**
 * return TRUE if authentication is required
 */
bfa_boolean_t
bfa_lps_is_authreq(struct bfa_lps_s *lps)
{
	return lps->auth_req;
}

bfa_eproto_status_t
bfa_lps_get_extstatus(struct bfa_lps_s *lps)
{
	return lps->ext_status;
}

/**
 * return port id assigned to the lport
 */
u32
bfa_lps_get_pid(struct bfa_lps_s *lps)
{
	return lps->lp_pid;
}

/**
 * Return bb_credit assigned in FLOGI response
 */
u16
bfa_lps_get_peer_bbcredit(struct bfa_lps_s *lps)
{
	return lps->pr_bbcred;
}

/**
 * Return peer port name
 */
wwn_t
bfa_lps_get_peer_pwwn(struct bfa_lps_s *lps)
{
	return lps->pr_pwwn;
}

/**
 * Return peer node name
 */
wwn_t
bfa_lps_get_peer_nwwn(struct bfa_lps_s *lps)
{
	return lps->pr_nwwn;
}

/**
 * return reason code if login request is rejected
 */
u8
bfa_lps_get_lsrjt_rsn(struct bfa_lps_s *lps)
{
	return lps->lsrjt_rsn;
}

/**
 * return explanation code if login request is rejected
 */
u8
bfa_lps_get_lsrjt_expl(struct bfa_lps_s *lps)
{
	return lps->lsrjt_expl;
}

/**
 * Return fpma/spma MAC for lport
 */
struct mac_s
bfa_lps_get_lp_mac(struct bfa_lps_s *lps)
{
	return lps->lp_mac;
}

/**
 * LPS firmware message class handler.
 */
void
bfa_lps_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	union bfi_lps_i2h_msg_u	msg;

	bfa_trc(bfa, m->mhdr.msg_id);
	msg.msg = m;

	switch (m->mhdr.msg_id) {
	case BFI_LPS_H2I_LOGIN_RSP:
		bfa_lps_login_rsp(bfa, msg.login_rsp);
		break;

	case BFI_LPS_H2I_LOGOUT_RSP:
		bfa_lps_logout_rsp(bfa, msg.logout_rsp);
		break;

	case BFI_LPS_H2I_CVL_EVENT:
		bfa_lps_rx_cvl_event(bfa, msg.cvl_event);
		break;

	default:
		bfa_trc(bfa, m->mhdr.msg_id);
		bfa_assert(0);
	}
}


