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
#include <cs/bfa_debug.h>
#include <bfi/bfi_rport.h>
#include "bfa_intr_priv.h"

BFA_TRC_FILE(HAL, RPORT);
BFA_MODULE(rport);

#define bfa_rport_offline_cb(__rp) do {				\
	if ((__rp)->bfa->fcs)						\
		bfa_cb_rport_offline((__rp)->rport_drv);      \
	else {								\
		bfa_cb_queue((__rp)->bfa, &(__rp)->hcb_qe,		\
				__bfa_cb_rport_offline, (__rp));      \
	}								\
} while (0)

#define bfa_rport_online_cb(__rp) do {				\
	if ((__rp)->bfa->fcs)						\
		bfa_cb_rport_online((__rp)->rport_drv);      \
	else {								\
		bfa_cb_queue((__rp)->bfa, &(__rp)->hcb_qe,		\
				  __bfa_cb_rport_online, (__rp));      \
		}							\
} while (0)

/*
 * forward declarations
 */
static struct bfa_rport_s *bfa_rport_alloc(struct bfa_rport_mod_s *rp_mod);
static void bfa_rport_free(struct bfa_rport_s *rport);
static bfa_boolean_t bfa_rport_send_fwcreate(struct bfa_rport_s *rp);
static bfa_boolean_t bfa_rport_send_fwdelete(struct bfa_rport_s *rp);
static bfa_boolean_t bfa_rport_send_fwspeed(struct bfa_rport_s *rp);
static void __bfa_cb_rport_online(void *cbarg, bfa_boolean_t complete);
static void __bfa_cb_rport_offline(void *cbarg, bfa_boolean_t complete);

/**
 *  bfa_rport_sm BFA rport state machine
 */


enum bfa_rport_event {
	BFA_RPORT_SM_CREATE	= 1,	/*  rport create event		*/
	BFA_RPORT_SM_DELETE	= 2,	/*  deleting an existing rport */
	BFA_RPORT_SM_ONLINE	= 3,	/*  rport is online		*/
	BFA_RPORT_SM_OFFLINE	= 4,	/*  rport is offline		*/
	BFA_RPORT_SM_FWRSP	= 5,	/*  firmware response		*/
	BFA_RPORT_SM_HWFAIL	= 6,	/*  IOC h/w failure		*/
	BFA_RPORT_SM_QOS_SCN	= 7,	/*  QoS SCN from firmware	*/
	BFA_RPORT_SM_SET_SPEED	= 8,	/*  Set Rport Speed 		*/
	BFA_RPORT_SM_QRESUME	= 9,	/*  space in requeue queue	*/
};

static void	bfa_rport_sm_uninit(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void	bfa_rport_sm_created(struct bfa_rport_s *rp,
					 enum bfa_rport_event event);
static void	bfa_rport_sm_fwcreate(struct bfa_rport_s *rp,
					  enum bfa_rport_event event);
static void	bfa_rport_sm_online(struct bfa_rport_s *rp,
					enum bfa_rport_event event);
static void	bfa_rport_sm_fwdelete(struct bfa_rport_s *rp,
					  enum bfa_rport_event event);
static void	bfa_rport_sm_offline(struct bfa_rport_s *rp,
					 enum bfa_rport_event event);
static void	bfa_rport_sm_deleting(struct bfa_rport_s *rp,
					  enum bfa_rport_event event);
static void	bfa_rport_sm_offline_pending(struct bfa_rport_s *rp,
					  enum bfa_rport_event event);
static void	bfa_rport_sm_delete_pending(struct bfa_rport_s *rp,
					  enum bfa_rport_event event);
static void	bfa_rport_sm_iocdisable(struct bfa_rport_s *rp,
					    enum bfa_rport_event event);
static void	bfa_rport_sm_fwcreate_qfull(struct bfa_rport_s *rp,
					  enum bfa_rport_event event);
static void	bfa_rport_sm_fwdelete_qfull(struct bfa_rport_s *rp,
					  enum bfa_rport_event event);
static void	bfa_rport_sm_deleting_qfull(struct bfa_rport_s *rp,
					  enum bfa_rport_event event);

/**
 * Beginning state, only online event expected.
 */
static void
bfa_rport_sm_uninit(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_CREATE:
		bfa_stats(rp, sm_un_cr);
		bfa_sm_set_state(rp, bfa_rport_sm_created);
		break;

	default:
		bfa_stats(rp, sm_un_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

static void
bfa_rport_sm_created(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_ONLINE:
		bfa_stats(rp, sm_cr_on);
		if (bfa_rport_send_fwcreate(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate_qfull);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_cr_del);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_cr_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	default:
		bfa_stats(rp, sm_cr_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * Waiting for rport create response from firmware.
 */
static void
bfa_rport_sm_fwcreate(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_fwc_rsp);
		bfa_sm_set_state(rp, bfa_rport_sm_online);
		bfa_rport_online_cb(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_fwc_del);
		bfa_sm_set_state(rp, bfa_rport_sm_delete_pending);
		break;

	case BFA_RPORT_SM_OFFLINE:
		bfa_stats(rp, sm_fwc_off);
		bfa_sm_set_state(rp, bfa_rport_sm_offline_pending);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_fwc_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	default:
		bfa_stats(rp, sm_fwc_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * Request queue is full, awaiting queue resume to send create request.
 */
static void
bfa_rport_sm_fwcreate_qfull(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_QRESUME:
		bfa_sm_set_state(rp, bfa_rport_sm_fwcreate);
		bfa_rport_send_fwcreate(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_fwc_del);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_reqq_wcancel(&rp->reqq_wait);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_OFFLINE:
		bfa_stats(rp, sm_fwc_off);
		bfa_sm_set_state(rp, bfa_rport_sm_offline);
		bfa_reqq_wcancel(&rp->reqq_wait);
		bfa_rport_offline_cb(rp);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_fwc_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		bfa_reqq_wcancel(&rp->reqq_wait);
		break;

	default:
		bfa_stats(rp, sm_fwc_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * Online state - normal parking state.
 */
static void
bfa_rport_sm_online(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	struct bfi_rport_qos_scn_s *qos_scn;

	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_OFFLINE:
		bfa_stats(rp, sm_on_off);
		if (bfa_rport_send_fwdelete(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwdelete);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwdelete_qfull);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_on_del);
		if (bfa_rport_send_fwdelete(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_deleting);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_deleting_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_on_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	case BFA_RPORT_SM_SET_SPEED:
		bfa_rport_send_fwspeed(rp);
		break;

	case BFA_RPORT_SM_QOS_SCN:
		qos_scn = (struct bfi_rport_qos_scn_s *) rp->event_arg.fw_msg;
		rp->qos_attr = qos_scn->new_qos_attr;
		bfa_trc(rp->bfa, qos_scn->old_qos_attr.qos_flow_id);
		bfa_trc(rp->bfa, qos_scn->new_qos_attr.qos_flow_id);
		bfa_trc(rp->bfa, qos_scn->old_qos_attr.qos_priority);
		bfa_trc(rp->bfa, qos_scn->new_qos_attr.qos_priority);

		qos_scn->old_qos_attr.qos_flow_id  =
			bfa_os_ntohl(qos_scn->old_qos_attr.qos_flow_id);
		qos_scn->new_qos_attr.qos_flow_id  =
			bfa_os_ntohl(qos_scn->new_qos_attr.qos_flow_id);
		qos_scn->old_qos_attr.qos_priority =
			bfa_os_ntohl(qos_scn->old_qos_attr.qos_priority);
		qos_scn->new_qos_attr.qos_priority =
			bfa_os_ntohl(qos_scn->new_qos_attr.qos_priority);

		if (qos_scn->old_qos_attr.qos_flow_id !=
			qos_scn->new_qos_attr.qos_flow_id)
			bfa_cb_rport_qos_scn_flowid(rp->rport_drv,
						    qos_scn->old_qos_attr,
						    qos_scn->new_qos_attr);
		if (qos_scn->old_qos_attr.qos_priority !=
			qos_scn->new_qos_attr.qos_priority)
			bfa_cb_rport_qos_scn_prio(rp->rport_drv,
						  qos_scn->old_qos_attr,
						  qos_scn->new_qos_attr);
		break;

	default:
		bfa_stats(rp, sm_on_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * Firmware rport is being deleted - awaiting f/w response.
 */
static void
bfa_rport_sm_fwdelete(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_fwd_rsp);
		bfa_sm_set_state(rp, bfa_rport_sm_offline);
		bfa_rport_offline_cb(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_fwd_del);
		bfa_sm_set_state(rp, bfa_rport_sm_deleting);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_fwd_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		bfa_rport_offline_cb(rp);
		break;

	default:
		bfa_stats(rp, sm_fwd_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

static void
bfa_rport_sm_fwdelete_qfull(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_QRESUME:
		bfa_sm_set_state(rp, bfa_rport_sm_fwdelete);
		bfa_rport_send_fwdelete(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_fwd_del);
		bfa_sm_set_state(rp, bfa_rport_sm_deleting_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_fwd_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		bfa_reqq_wcancel(&rp->reqq_wait);
		bfa_rport_offline_cb(rp);
		break;

	default:
		bfa_stats(rp, sm_fwd_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * Offline state.
 */
static void
bfa_rport_sm_offline(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_off_del);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_ONLINE:
		bfa_stats(rp, sm_off_on);
		if (bfa_rport_send_fwcreate(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_off_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	default:
		bfa_stats(rp, sm_off_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * Rport is deleted, waiting for firmware response to delete.
 */
static void
bfa_rport_sm_deleting(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_del_fwrsp);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_del_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	default:
		bfa_sm_fault(rp->bfa, event);
	}
}

static void
bfa_rport_sm_deleting_qfull(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_QRESUME:
		bfa_stats(rp, sm_del_fwrsp);
		bfa_sm_set_state(rp, bfa_rport_sm_deleting);
		bfa_rport_send_fwdelete(rp);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_del_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_reqq_wcancel(&rp->reqq_wait);
		bfa_rport_free(rp);
		break;

	default:
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * Waiting for rport create response from firmware. A delete is pending.
 */
static void
bfa_rport_sm_delete_pending(struct bfa_rport_s *rp,
				enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_delp_fwrsp);
		if (bfa_rport_send_fwdelete(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_deleting);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_deleting_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_delp_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	default:
		bfa_stats(rp, sm_delp_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * Waiting for rport create response from firmware. Rport offline is pending.
 */
static void
bfa_rport_sm_offline_pending(struct bfa_rport_s *rp,
				 enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_FWRSP:
		bfa_stats(rp, sm_offp_fwrsp);
		if (bfa_rport_send_fwdelete(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwdelete);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwdelete_qfull);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_offp_del);
		bfa_sm_set_state(rp, bfa_rport_sm_delete_pending);
		break;

	case BFA_RPORT_SM_HWFAIL:
		bfa_stats(rp, sm_offp_hwf);
		bfa_sm_set_state(rp, bfa_rport_sm_iocdisable);
		break;

	default:
		bfa_stats(rp, sm_offp_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}

/**
 * IOC h/w failed.
 */
static void
bfa_rport_sm_iocdisable(struct bfa_rport_s *rp, enum bfa_rport_event event)
{
	bfa_trc(rp->bfa, rp->rport_tag);
	bfa_trc(rp->bfa, event);

	switch (event) {
	case BFA_RPORT_SM_OFFLINE:
		bfa_stats(rp, sm_iocd_off);
		bfa_rport_offline_cb(rp);
		break;

	case BFA_RPORT_SM_DELETE:
		bfa_stats(rp, sm_iocd_del);
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);
		bfa_rport_free(rp);
		break;

	case BFA_RPORT_SM_ONLINE:
		bfa_stats(rp, sm_iocd_on);
		if (bfa_rport_send_fwcreate(rp))
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate);
		else
			bfa_sm_set_state(rp, bfa_rport_sm_fwcreate_qfull);
		break;

	case BFA_RPORT_SM_HWFAIL:
		break;

	default:
		bfa_stats(rp, sm_iocd_unexp);
		bfa_sm_fault(rp->bfa, event);
	}
}



/**
 *  bfa_rport_private BFA rport private functions
 */

static void
__bfa_cb_rport_online(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_rport_s *rp = cbarg;

	if (complete)
		bfa_cb_rport_online(rp->rport_drv);
}

static void
__bfa_cb_rport_offline(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_rport_s *rp = cbarg;

	if (complete)
		bfa_cb_rport_offline(rp->rport_drv);
}

static void
bfa_rport_qresume(void *cbarg)
{
	struct bfa_rport_s	*rp = cbarg;

	bfa_sm_send_event(rp, BFA_RPORT_SM_QRESUME);
}

static void
bfa_rport_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len,
		u32 *dm_len)
{
	if (cfg->fwcfg.num_rports < BFA_RPORT_MIN)
		cfg->fwcfg.num_rports = BFA_RPORT_MIN;

	*km_len += cfg->fwcfg.num_rports * sizeof(struct bfa_rport_s);
}

static void
bfa_rport_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		     struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	struct bfa_rport_mod_s *mod = BFA_RPORT_MOD(bfa);
	struct bfa_rport_s *rp;
	u16        i;

	INIT_LIST_HEAD(&mod->rp_free_q);
	INIT_LIST_HEAD(&mod->rp_active_q);

	rp = (struct bfa_rport_s *) bfa_meminfo_kva(meminfo);
	mod->rps_list = rp;
	mod->num_rports = cfg->fwcfg.num_rports;

	bfa_assert(mod->num_rports
		   && !(mod->num_rports & (mod->num_rports - 1)));

	for (i = 0; i < mod->num_rports; i++, rp++) {
		bfa_os_memset(rp, 0, sizeof(struct bfa_rport_s));
		rp->bfa = bfa;
		rp->rport_tag = i;
		bfa_sm_set_state(rp, bfa_rport_sm_uninit);

		/**
		 *  - is unused
		 */
		if (i)
			list_add_tail(&rp->qe, &mod->rp_free_q);

		bfa_reqq_winit(&rp->reqq_wait, bfa_rport_qresume, rp);
	}

	/**
	 * consume memory
	 */
	bfa_meminfo_kva(meminfo) = (u8 *) rp;
}

static void
bfa_rport_detach(struct bfa_s *bfa)
{
}

static void
bfa_rport_start(struct bfa_s *bfa)
{
}

static void
bfa_rport_stop(struct bfa_s *bfa)
{
}

static void
bfa_rport_iocdisable(struct bfa_s *bfa)
{
	struct bfa_rport_mod_s *mod = BFA_RPORT_MOD(bfa);
	struct bfa_rport_s *rport;
	struct list_head        *qe, *qen;

	list_for_each_safe(qe, qen, &mod->rp_active_q) {
		rport = (struct bfa_rport_s *) qe;
		bfa_sm_send_event(rport, BFA_RPORT_SM_HWFAIL);
	}
}

static struct bfa_rport_s *
bfa_rport_alloc(struct bfa_rport_mod_s *mod)
{
	struct bfa_rport_s *rport;

	bfa_q_deq(&mod->rp_free_q, &rport);
	if (rport)
		list_add_tail(&rport->qe, &mod->rp_active_q);

	return rport;
}

static void
bfa_rport_free(struct bfa_rport_s *rport)
{
	struct bfa_rport_mod_s *mod = BFA_RPORT_MOD(rport->bfa);

	bfa_assert(bfa_q_is_on_q(&mod->rp_active_q, rport));
	list_del(&rport->qe);
	list_add_tail(&rport->qe, &mod->rp_free_q);
}

static bfa_boolean_t
bfa_rport_send_fwcreate(struct bfa_rport_s *rp)
{
	struct bfi_rport_create_req_s *m;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(rp->bfa, BFA_REQQ_RPORT);
	if (!m) {
		bfa_reqq_wait(rp->bfa, BFA_REQQ_RPORT, &rp->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_RPORT, BFI_RPORT_H2I_CREATE_REQ,
			bfa_lpuid(rp->bfa));
	m->bfa_handle = rp->rport_tag;
	m->max_frmsz = bfa_os_htons(rp->rport_info.max_frmsz);
	m->pid = rp->rport_info.pid;
	m->lp_tag = rp->rport_info.lp_tag;
	m->local_pid = rp->rport_info.local_pid;
	m->fc_class = rp->rport_info.fc_class;
	m->vf_en = rp->rport_info.vf_en;
	m->vf_id = rp->rport_info.vf_id;
	m->cisc = rp->rport_info.cisc;

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(rp->bfa, BFA_REQQ_RPORT);
	return BFA_TRUE;
}

static bfa_boolean_t
bfa_rport_send_fwdelete(struct bfa_rport_s *rp)
{
	struct bfi_rport_delete_req_s *m;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(rp->bfa, BFA_REQQ_RPORT);
	if (!m) {
		bfa_reqq_wait(rp->bfa, BFA_REQQ_RPORT, &rp->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_RPORT, BFI_RPORT_H2I_DELETE_REQ,
			bfa_lpuid(rp->bfa));
	m->fw_handle = rp->fw_handle;

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(rp->bfa, BFA_REQQ_RPORT);
	return BFA_TRUE;
}

static bfa_boolean_t
bfa_rport_send_fwspeed(struct bfa_rport_s *rp)
{
	struct bfa_rport_speed_req_s *m;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(rp->bfa, BFA_REQQ_RPORT);
	if (!m) {
		bfa_trc(rp->bfa, rp->rport_info.speed);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_RPORT, BFI_RPORT_H2I_SET_SPEED_REQ,
			bfa_lpuid(rp->bfa));
	m->fw_handle = rp->fw_handle;
	m->speed = (u8)rp->rport_info.speed;

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(rp->bfa, BFA_REQQ_RPORT);
	return BFA_TRUE;
}



/**
 *  bfa_rport_public
 */

/**
 * 		Rport interrupt processing.
 */
void
bfa_rport_isr(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	union bfi_rport_i2h_msg_u msg;
	struct bfa_rport_s *rp;

	bfa_trc(bfa, m->mhdr.msg_id);

	msg.msg = m;

	switch (m->mhdr.msg_id) {
	case BFI_RPORT_I2H_CREATE_RSP:
		rp = BFA_RPORT_FROM_TAG(bfa, msg.create_rsp->bfa_handle);
		rp->fw_handle = msg.create_rsp->fw_handle;
		rp->qos_attr = msg.create_rsp->qos_attr;
		bfa_assert(msg.create_rsp->status == BFA_STATUS_OK);
		bfa_sm_send_event(rp, BFA_RPORT_SM_FWRSP);
		break;

	case BFI_RPORT_I2H_DELETE_RSP:
		rp = BFA_RPORT_FROM_TAG(bfa, msg.delete_rsp->bfa_handle);
		bfa_assert(msg.delete_rsp->status == BFA_STATUS_OK);
		bfa_sm_send_event(rp, BFA_RPORT_SM_FWRSP);
		break;

	case BFI_RPORT_I2H_QOS_SCN:
		rp = BFA_RPORT_FROM_TAG(bfa, msg.qos_scn_evt->bfa_handle);
		rp->event_arg.fw_msg = msg.qos_scn_evt;
		bfa_sm_send_event(rp, BFA_RPORT_SM_QOS_SCN);
		break;

	default:
		bfa_trc(bfa, m->mhdr.msg_id);
		bfa_assert(0);
	}
}



/**
 *  bfa_rport_api
 */

struct bfa_rport_s *
bfa_rport_create(struct bfa_s *bfa, void *rport_drv)
{
	struct bfa_rport_s *rp;

	rp = bfa_rport_alloc(BFA_RPORT_MOD(bfa));

	if (rp == NULL)
		return NULL;

	rp->bfa = bfa;
	rp->rport_drv = rport_drv;
	bfa_rport_clear_stats(rp);

	bfa_assert(bfa_sm_cmp_state(rp, bfa_rport_sm_uninit));
	bfa_sm_send_event(rp, BFA_RPORT_SM_CREATE);

	return rp;
}

void
bfa_rport_delete(struct bfa_rport_s *rport)
{
	bfa_sm_send_event(rport, BFA_RPORT_SM_DELETE);
}

void
bfa_rport_online(struct bfa_rport_s *rport, struct bfa_rport_info_s *rport_info)
{
	bfa_assert(rport_info->max_frmsz != 0);

	/**
	 * Some JBODs are seen to be not setting PDU size correctly in PLOGI
	 * responses. Default to minimum size.
	 */
	if (rport_info->max_frmsz == 0) {
		bfa_trc(rport->bfa, rport->rport_tag);
		rport_info->max_frmsz = FC_MIN_PDUSZ;
	}

	bfa_os_assign(rport->rport_info, *rport_info);
	bfa_sm_send_event(rport, BFA_RPORT_SM_ONLINE);
}

void
bfa_rport_offline(struct bfa_rport_s *rport)
{
	bfa_sm_send_event(rport, BFA_RPORT_SM_OFFLINE);
}

void
bfa_rport_speed(struct bfa_rport_s *rport, enum bfa_pport_speed speed)
{
	bfa_assert(speed != 0);
	bfa_assert(speed != BFA_PPORT_SPEED_AUTO);

	rport->rport_info.speed = speed;
	bfa_sm_send_event(rport, BFA_RPORT_SM_SET_SPEED);
}

void
bfa_rport_get_stats(struct bfa_rport_s *rport,
	struct bfa_rport_hal_stats_s *stats)
{
	*stats = rport->stats;
}

void
bfa_rport_get_qos_attr(struct bfa_rport_s *rport,
					struct bfa_rport_qos_attr_s *qos_attr)
{
	qos_attr->qos_priority  = bfa_os_ntohl(rport->qos_attr.qos_priority);
	qos_attr->qos_flow_id  = bfa_os_ntohl(rport->qos_attr.qos_flow_id);

}

void
bfa_rport_clear_stats(struct bfa_rport_s *rport)
{
	bfa_os_memset(&rport->stats, 0, sizeof(rport->stats));
}


