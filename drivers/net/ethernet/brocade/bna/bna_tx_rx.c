/*
 * Linux network driver for Brocade Converged Network Adapter.
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
 * Copyright (c) 2005-2011 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */
#include "bna.h"
#include "bfi.h"

/* IB */
static void
bna_ib_coalescing_timeo_set(struct bna_ib *ib, u8 coalescing_timeo)
{
	ib->coalescing_timeo = coalescing_timeo;
	ib->door_bell.doorbell_ack = BNA_DOORBELL_IB_INT_ACK(
				(u32)ib->coalescing_timeo, 0);
}

/* RXF */

#define bna_rxf_vlan_cfg_soft_reset(rxf)				\
do {									\
	(rxf)->vlan_pending_bitmask = (u8)BFI_VLAN_BMASK_ALL;		\
	(rxf)->vlan_strip_pending = true;				\
} while (0)

#define bna_rxf_rss_cfg_soft_reset(rxf)					\
do {									\
	if ((rxf)->rss_status == BNA_STATUS_T_ENABLED)			\
		(rxf)->rss_pending = (BNA_RSS_F_RIT_PENDING |		\
				BNA_RSS_F_CFG_PENDING |			\
				BNA_RSS_F_STATUS_PENDING);		\
} while (0)

static int bna_rxf_cfg_apply(struct bna_rxf *rxf);
static void bna_rxf_cfg_reset(struct bna_rxf *rxf);
static int bna_rxf_fltr_clear(struct bna_rxf *rxf);
static int bna_rxf_ucast_cfg_apply(struct bna_rxf *rxf);
static int bna_rxf_promisc_cfg_apply(struct bna_rxf *rxf);
static int bna_rxf_allmulti_cfg_apply(struct bna_rxf *rxf);
static int bna_rxf_vlan_strip_cfg_apply(struct bna_rxf *rxf);
static int bna_rxf_ucast_cfg_reset(struct bna_rxf *rxf,
					enum bna_cleanup_type cleanup);
static int bna_rxf_promisc_cfg_reset(struct bna_rxf *rxf,
					enum bna_cleanup_type cleanup);
static int bna_rxf_allmulti_cfg_reset(struct bna_rxf *rxf,
					enum bna_cleanup_type cleanup);

bfa_fsm_state_decl(bna_rxf, stopped, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, paused, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, cfg_wait, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, started, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, fltr_clr_wait, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, last_resp_wait, struct bna_rxf,
			enum bna_rxf_event);

static void
bna_rxf_sm_stopped_entry(struct bna_rxf *rxf)
{
	call_rxf_stop_cbfn(rxf);
}

static void
bna_rxf_sm_stopped(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_START:
		if (rxf->flags & BNA_RXF_F_PAUSED) {
			bfa_fsm_set_state(rxf, bna_rxf_sm_paused);
			call_rxf_start_cbfn(rxf);
		} else
			bfa_fsm_set_state(rxf, bna_rxf_sm_cfg_wait);
		break;

	case RXF_E_STOP:
		call_rxf_stop_cbfn(rxf);
		break;

	case RXF_E_FAIL:
		/* No-op */
		break;

	case RXF_E_CONFIG:
		call_rxf_cam_fltr_cbfn(rxf);
		break;

	case RXF_E_PAUSE:
		rxf->flags |= BNA_RXF_F_PAUSED;
		call_rxf_pause_cbfn(rxf);
		break;

	case RXF_E_RESUME:
		rxf->flags &= ~BNA_RXF_F_PAUSED;
		call_rxf_resume_cbfn(rxf);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_rxf_sm_paused_entry(struct bna_rxf *rxf)
{
	call_rxf_pause_cbfn(rxf);
}

static void
bna_rxf_sm_paused(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_STOP:
	case RXF_E_FAIL:
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_CONFIG:
		call_rxf_cam_fltr_cbfn(rxf);
		break;

	case RXF_E_RESUME:
		rxf->flags &= ~BNA_RXF_F_PAUSED;
		bfa_fsm_set_state(rxf, bna_rxf_sm_cfg_wait);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_rxf_sm_cfg_wait_entry(struct bna_rxf *rxf)
{
	if (!bna_rxf_cfg_apply(rxf)) {
		/* No more pending config updates */
		bfa_fsm_set_state(rxf, bna_rxf_sm_started);
	}
}

static void
bna_rxf_sm_cfg_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_STOP:
		bfa_fsm_set_state(rxf, bna_rxf_sm_last_resp_wait);
		break;

	case RXF_E_FAIL:
		bna_rxf_cfg_reset(rxf);
		call_rxf_start_cbfn(rxf);
		call_rxf_cam_fltr_cbfn(rxf);
		call_rxf_resume_cbfn(rxf);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_CONFIG:
		/* No-op */
		break;

	case RXF_E_PAUSE:
		rxf->flags |= BNA_RXF_F_PAUSED;
		call_rxf_start_cbfn(rxf);
		bfa_fsm_set_state(rxf, bna_rxf_sm_fltr_clr_wait);
		break;

	case RXF_E_FW_RESP:
		if (!bna_rxf_cfg_apply(rxf)) {
			/* No more pending config updates */
			bfa_fsm_set_state(rxf, bna_rxf_sm_started);
		}
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_rxf_sm_started_entry(struct bna_rxf *rxf)
{
	call_rxf_start_cbfn(rxf);
	call_rxf_cam_fltr_cbfn(rxf);
	call_rxf_resume_cbfn(rxf);
}

static void
bna_rxf_sm_started(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_STOP:
	case RXF_E_FAIL:
		bna_rxf_cfg_reset(rxf);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_CONFIG:
		bfa_fsm_set_state(rxf, bna_rxf_sm_cfg_wait);
		break;

	case RXF_E_PAUSE:
		rxf->flags |= BNA_RXF_F_PAUSED;
		if (!bna_rxf_fltr_clear(rxf))
			bfa_fsm_set_state(rxf, bna_rxf_sm_paused);
		else
			bfa_fsm_set_state(rxf, bna_rxf_sm_fltr_clr_wait);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_rxf_sm_fltr_clr_wait_entry(struct bna_rxf *rxf)
{
}

static void
bna_rxf_sm_fltr_clr_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_FAIL:
		bna_rxf_cfg_reset(rxf);
		call_rxf_pause_cbfn(rxf);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_FW_RESP:
		if (!bna_rxf_fltr_clear(rxf)) {
			/* No more pending CAM entries to clear */
			bfa_fsm_set_state(rxf, bna_rxf_sm_paused);
		}
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_rxf_sm_last_resp_wait_entry(struct bna_rxf *rxf)
{
}

static void
bna_rxf_sm_last_resp_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_FAIL:
	case RXF_E_FW_RESP:
		bna_rxf_cfg_reset(rxf);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_bfi_ucast_req(struct bna_rxf *rxf, struct bna_mac *mac,
		enum bfi_enet_h2i_msgs req_type)
{
	struct bfi_enet_ucast_req *req = &rxf->bfi_enet_cmd.ucast_req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET, req_type, 0, rxf->rx->rid);
	req->mh.num_entries = htons(
	bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_ucast_req)));
	memcpy(&req->mac_addr, &mac->addr, sizeof(mac_t));
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_ucast_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_mcast_add_req(struct bna_rxf *rxf, struct bna_mac *mac)
{
	struct bfi_enet_mcast_add_req *req =
		&rxf->bfi_enet_cmd.mcast_add_req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET, BFI_ENET_H2I_MAC_MCAST_ADD_REQ,
		0, rxf->rx->rid);
	req->mh.num_entries = htons(
	bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_mcast_add_req)));
	memcpy(&req->mac_addr, &mac->addr, sizeof(mac_t));
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_mcast_add_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_mcast_del_req(struct bna_rxf *rxf, u16 handle)
{
	struct bfi_enet_mcast_del_req *req =
		&rxf->bfi_enet_cmd.mcast_del_req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET, BFI_ENET_H2I_MAC_MCAST_DEL_REQ,
		0, rxf->rx->rid);
	req->mh.num_entries = htons(
	bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_mcast_del_req)));
	req->handle = htons(handle);
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_mcast_del_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_mcast_filter_req(struct bna_rxf *rxf, enum bna_status status)
{
	struct bfi_enet_enable_req *req = &rxf->bfi_enet_cmd.req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_MAC_MCAST_FILTER_REQ, 0, rxf->rx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_enable_req)));
	req->enable = status;
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_enable_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_rx_promisc_req(struct bna_rxf *rxf, enum bna_status status)
{
	struct bfi_enet_enable_req *req = &rxf->bfi_enet_cmd.req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_RX_PROMISCUOUS_REQ, 0, rxf->rx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_enable_req)));
	req->enable = status;
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_enable_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_rx_vlan_filter_set(struct bna_rxf *rxf, u8 block_idx)
{
	struct bfi_enet_rx_vlan_req *req = &rxf->bfi_enet_cmd.vlan_req;
	int i;
	int j;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_RX_VLAN_SET_REQ, 0, rxf->rx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_rx_vlan_req)));
	req->block_idx = block_idx;
	for (i = 0; i < (BFI_ENET_VLAN_BLOCK_SIZE / 32); i++) {
		j = (block_idx * (BFI_ENET_VLAN_BLOCK_SIZE / 32)) + i;
		if (rxf->vlan_filter_status == BNA_STATUS_T_ENABLED)
			req->bit_mask[i] =
				htonl(rxf->vlan_filter_table[j]);
		else
			req->bit_mask[i] = 0xFFFFFFFF;
	}
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_rx_vlan_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_vlan_strip_enable(struct bna_rxf *rxf)
{
	struct bfi_enet_enable_req *req = &rxf->bfi_enet_cmd.req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_RX_VLAN_STRIP_ENABLE_REQ, 0, rxf->rx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_enable_req)));
	req->enable = rxf->vlan_strip_status;
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_enable_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_rit_cfg(struct bna_rxf *rxf)
{
	struct bfi_enet_rit_req *req = &rxf->bfi_enet_cmd.rit_req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_RIT_CFG_REQ, 0, rxf->rx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_rit_req)));
	req->size = htons(rxf->rit_size);
	memcpy(&req->table[0], rxf->rit, rxf->rit_size);
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_rit_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_rss_cfg(struct bna_rxf *rxf)
{
	struct bfi_enet_rss_cfg_req *req = &rxf->bfi_enet_cmd.rss_req;
	int i;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_RSS_CFG_REQ, 0, rxf->rx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_rss_cfg_req)));
	req->cfg.type = rxf->rss_cfg.hash_type;
	req->cfg.mask = rxf->rss_cfg.hash_mask;
	for (i = 0; i < BFI_ENET_RSS_KEY_LEN; i++)
		req->cfg.key[i] =
			htonl(rxf->rss_cfg.toeplitz_hash_key[i]);
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_rss_cfg_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

static void
bna_bfi_rss_enable(struct bna_rxf *rxf)
{
	struct bfi_enet_enable_req *req = &rxf->bfi_enet_cmd.req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_RSS_ENABLE_REQ, 0, rxf->rx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_enable_req)));
	req->enable = rxf->rss_status;
	bfa_msgq_cmd_set(&rxf->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_enable_req), &req->mh);
	bfa_msgq_cmd_post(&rxf->rx->bna->msgq, &rxf->msgq_cmd);
}

/* This function gets the multicast MAC that has already been added to CAM */
static struct bna_mac *
bna_rxf_mcmac_get(struct bna_rxf *rxf, u8 *mac_addr)
{
	struct bna_mac *mac;
	struct list_head *qe;

	list_for_each(qe, &rxf->mcast_active_q) {
		mac = (struct bna_mac *)qe;
		if (BNA_MAC_IS_EQUAL(&mac->addr, mac_addr))
			return mac;
	}

	list_for_each(qe, &rxf->mcast_pending_del_q) {
		mac = (struct bna_mac *)qe;
		if (BNA_MAC_IS_EQUAL(&mac->addr, mac_addr))
			return mac;
	}

	return NULL;
}

static struct bna_mcam_handle *
bna_rxf_mchandle_get(struct bna_rxf *rxf, int handle)
{
	struct bna_mcam_handle *mchandle;
	struct list_head *qe;

	list_for_each(qe, &rxf->mcast_handle_q) {
		mchandle = (struct bna_mcam_handle *)qe;
		if (mchandle->handle == handle)
			return mchandle;
	}

	return NULL;
}

static void
bna_rxf_mchandle_attach(struct bna_rxf *rxf, u8 *mac_addr, int handle)
{
	struct bna_mac *mcmac;
	struct bna_mcam_handle *mchandle;

	mcmac = bna_rxf_mcmac_get(rxf, mac_addr);
	mchandle = bna_rxf_mchandle_get(rxf, handle);
	if (mchandle == NULL) {
		mchandle = bna_mcam_mod_handle_get(&rxf->rx->bna->mcam_mod);
		mchandle->handle = handle;
		mchandle->refcnt = 0;
		list_add_tail(&mchandle->qe, &rxf->mcast_handle_q);
	}
	mchandle->refcnt++;
	mcmac->handle = mchandle;
}

static int
bna_rxf_mcast_del(struct bna_rxf *rxf, struct bna_mac *mac,
		enum bna_cleanup_type cleanup)
{
	struct bna_mcam_handle *mchandle;
	int ret = 0;

	mchandle = mac->handle;
	if (mchandle == NULL)
		return ret;

	mchandle->refcnt--;
	if (mchandle->refcnt == 0) {
		if (cleanup == BNA_HARD_CLEANUP) {
			bna_bfi_mcast_del_req(rxf, mchandle->handle);
			ret = 1;
		}
		list_del(&mchandle->qe);
		bfa_q_qe_init(&mchandle->qe);
		bna_mcam_mod_handle_put(&rxf->rx->bna->mcam_mod, mchandle);
	}
	mac->handle = NULL;

	return ret;
}

static int
bna_rxf_mcast_cfg_apply(struct bna_rxf *rxf)
{
	struct bna_mac *mac = NULL;
	struct list_head *qe;
	int ret;

	/* Delete multicast entries previousely added */
	while (!list_empty(&rxf->mcast_pending_del_q)) {
		bfa_q_deq(&rxf->mcast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		ret = bna_rxf_mcast_del(rxf, mac, BNA_HARD_CLEANUP);
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
		if (ret)
			return ret;
	}

	/* Add multicast entries */
	if (!list_empty(&rxf->mcast_pending_add_q)) {
		bfa_q_deq(&rxf->mcast_pending_add_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		list_add_tail(&mac->qe, &rxf->mcast_active_q);
		bna_bfi_mcast_add_req(rxf, mac);
		return 1;
	}

	return 0;
}

static int
bna_rxf_vlan_cfg_apply(struct bna_rxf *rxf)
{
	u8 vlan_pending_bitmask;
	int block_idx = 0;

	if (rxf->vlan_pending_bitmask) {
		vlan_pending_bitmask = rxf->vlan_pending_bitmask;
		while (!(vlan_pending_bitmask & 0x1)) {
			block_idx++;
			vlan_pending_bitmask >>= 1;
		}
		rxf->vlan_pending_bitmask &= ~(1 << block_idx);
		bna_bfi_rx_vlan_filter_set(rxf, block_idx);
		return 1;
	}

	return 0;
}

static int
bna_rxf_mcast_cfg_reset(struct bna_rxf *rxf, enum bna_cleanup_type cleanup)
{
	struct list_head *qe;
	struct bna_mac *mac;
	int ret;

	/* Throw away delete pending mcast entries */
	while (!list_empty(&rxf->mcast_pending_del_q)) {
		bfa_q_deq(&rxf->mcast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		ret = bna_rxf_mcast_del(rxf, mac, cleanup);
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
		if (ret)
			return ret;
	}

	/* Move active mcast entries to pending_add_q */
	while (!list_empty(&rxf->mcast_active_q)) {
		bfa_q_deq(&rxf->mcast_active_q, &qe);
		bfa_q_qe_init(qe);
		list_add_tail(qe, &rxf->mcast_pending_add_q);
		mac = (struct bna_mac *)qe;
		if (bna_rxf_mcast_del(rxf, mac, cleanup))
			return 1;
	}

	return 0;
}

static int
bna_rxf_rss_cfg_apply(struct bna_rxf *rxf)
{
	if (rxf->rss_pending) {
		if (rxf->rss_pending & BNA_RSS_F_RIT_PENDING) {
			rxf->rss_pending &= ~BNA_RSS_F_RIT_PENDING;
			bna_bfi_rit_cfg(rxf);
			return 1;
		}

		if (rxf->rss_pending & BNA_RSS_F_CFG_PENDING) {
			rxf->rss_pending &= ~BNA_RSS_F_CFG_PENDING;
			bna_bfi_rss_cfg(rxf);
			return 1;
		}

		if (rxf->rss_pending & BNA_RSS_F_STATUS_PENDING) {
			rxf->rss_pending &= ~BNA_RSS_F_STATUS_PENDING;
			bna_bfi_rss_enable(rxf);
			return 1;
		}
	}

	return 0;
}

static int
bna_rxf_cfg_apply(struct bna_rxf *rxf)
{
	if (bna_rxf_ucast_cfg_apply(rxf))
		return 1;

	if (bna_rxf_mcast_cfg_apply(rxf))
		return 1;

	if (bna_rxf_promisc_cfg_apply(rxf))
		return 1;

	if (bna_rxf_allmulti_cfg_apply(rxf))
		return 1;

	if (bna_rxf_vlan_cfg_apply(rxf))
		return 1;

	if (bna_rxf_vlan_strip_cfg_apply(rxf))
		return 1;

	if (bna_rxf_rss_cfg_apply(rxf))
		return 1;

	return 0;
}

/* Only software reset */
static int
bna_rxf_fltr_clear(struct bna_rxf *rxf)
{
	if (bna_rxf_ucast_cfg_reset(rxf, BNA_HARD_CLEANUP))
		return 1;

	if (bna_rxf_mcast_cfg_reset(rxf, BNA_HARD_CLEANUP))
		return 1;

	if (bna_rxf_promisc_cfg_reset(rxf, BNA_HARD_CLEANUP))
		return 1;

	if (bna_rxf_allmulti_cfg_reset(rxf, BNA_HARD_CLEANUP))
		return 1;

	return 0;
}

static void
bna_rxf_cfg_reset(struct bna_rxf *rxf)
{
	bna_rxf_ucast_cfg_reset(rxf, BNA_SOFT_CLEANUP);
	bna_rxf_mcast_cfg_reset(rxf, BNA_SOFT_CLEANUP);
	bna_rxf_promisc_cfg_reset(rxf, BNA_SOFT_CLEANUP);
	bna_rxf_allmulti_cfg_reset(rxf, BNA_SOFT_CLEANUP);
	bna_rxf_vlan_cfg_soft_reset(rxf);
	bna_rxf_rss_cfg_soft_reset(rxf);
}

static void
bna_rit_init(struct bna_rxf *rxf, int rit_size)
{
	struct bna_rx *rx = rxf->rx;
	struct bna_rxp *rxp;
	struct list_head *qe;
	int offset = 0;

	rxf->rit_size = rit_size;
	list_for_each(qe, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe;
		rxf->rit[offset] = rxp->cq.ccb->id;
		offset++;
	}

}

void
bna_bfi_rxf_cfg_rsp(struct bna_rxf *rxf, struct bfi_msgq_mhdr *msghdr)
{
	bfa_fsm_send_event(rxf, RXF_E_FW_RESP);
}

void
bna_bfi_rxf_mcast_add_rsp(struct bna_rxf *rxf,
			struct bfi_msgq_mhdr *msghdr)
{
	struct bfi_enet_mcast_add_req *req =
		&rxf->bfi_enet_cmd.mcast_add_req;
	struct bfi_enet_mcast_add_rsp *rsp =
		(struct bfi_enet_mcast_add_rsp *)msghdr;

	bna_rxf_mchandle_attach(rxf, (u8 *)&req->mac_addr,
		ntohs(rsp->handle));
	bfa_fsm_send_event(rxf, RXF_E_FW_RESP);
}

static void
bna_rxf_init(struct bna_rxf *rxf,
		struct bna_rx *rx,
		struct bna_rx_config *q_config,
		struct bna_res_info *res_info)
{
	rxf->rx = rx;

	INIT_LIST_HEAD(&rxf->ucast_pending_add_q);
	INIT_LIST_HEAD(&rxf->ucast_pending_del_q);
	rxf->ucast_pending_set = 0;
	rxf->ucast_active_set = 0;
	INIT_LIST_HEAD(&rxf->ucast_active_q);
	rxf->ucast_pending_mac = NULL;

	INIT_LIST_HEAD(&rxf->mcast_pending_add_q);
	INIT_LIST_HEAD(&rxf->mcast_pending_del_q);
	INIT_LIST_HEAD(&rxf->mcast_active_q);
	INIT_LIST_HEAD(&rxf->mcast_handle_q);

	if (q_config->paused)
		rxf->flags |= BNA_RXF_F_PAUSED;

	rxf->rit = (u8 *)
		res_info[BNA_RX_RES_MEM_T_RIT].res_u.mem_info.mdl[0].kva;
	bna_rit_init(rxf, q_config->num_paths);

	rxf->rss_status = q_config->rss_status;
	if (rxf->rss_status == BNA_STATUS_T_ENABLED) {
		rxf->rss_cfg = q_config->rss_config;
		rxf->rss_pending |= BNA_RSS_F_CFG_PENDING;
		rxf->rss_pending |= BNA_RSS_F_RIT_PENDING;
		rxf->rss_pending |= BNA_RSS_F_STATUS_PENDING;
	}

	rxf->vlan_filter_status = BNA_STATUS_T_DISABLED;
	memset(rxf->vlan_filter_table, 0,
			(sizeof(u32) * (BFI_ENET_VLAN_ID_MAX / 32)));
	rxf->vlan_filter_table[0] |= 1; /* for pure priority tagged frames */
	rxf->vlan_pending_bitmask = (u8)BFI_VLAN_BMASK_ALL;

	rxf->vlan_strip_status = q_config->vlan_strip_status;

	bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
}

static void
bna_rxf_uninit(struct bna_rxf *rxf)
{
	struct bna_mac *mac;

	rxf->ucast_pending_set = 0;
	rxf->ucast_active_set = 0;

	while (!list_empty(&rxf->ucast_pending_add_q)) {
		bfa_q_deq(&rxf->ucast_pending_add_q, &mac);
		bfa_q_qe_init(&mac->qe);
		bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod, mac);
	}

	if (rxf->ucast_pending_mac) {
		bfa_q_qe_init(&rxf->ucast_pending_mac->qe);
		bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod,
			rxf->ucast_pending_mac);
		rxf->ucast_pending_mac = NULL;
	}

	while (!list_empty(&rxf->mcast_pending_add_q)) {
		bfa_q_deq(&rxf->mcast_pending_add_q, &mac);
		bfa_q_qe_init(&mac->qe);
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
	}

	rxf->rxmode_pending = 0;
	rxf->rxmode_pending_bitmask = 0;
	if (rxf->rx->bna->promisc_rid == rxf->rx->rid)
		rxf->rx->bna->promisc_rid = BFI_INVALID_RID;
	if (rxf->rx->bna->default_mode_rid == rxf->rx->rid)
		rxf->rx->bna->default_mode_rid = BFI_INVALID_RID;

	rxf->rss_pending = 0;
	rxf->vlan_strip_pending = false;

	rxf->flags = 0;

	rxf->rx = NULL;
}

static void
bna_rx_cb_rxf_started(struct bna_rx *rx)
{
	bfa_fsm_send_event(rx, RX_E_RXF_STARTED);
}

static void
bna_rxf_start(struct bna_rxf *rxf)
{
	rxf->start_cbfn = bna_rx_cb_rxf_started;
	rxf->start_cbarg = rxf->rx;
	bfa_fsm_send_event(rxf, RXF_E_START);
}

static void
bna_rx_cb_rxf_stopped(struct bna_rx *rx)
{
	bfa_fsm_send_event(rx, RX_E_RXF_STOPPED);
}

static void
bna_rxf_stop(struct bna_rxf *rxf)
{
	rxf->stop_cbfn = bna_rx_cb_rxf_stopped;
	rxf->stop_cbarg = rxf->rx;
	bfa_fsm_send_event(rxf, RXF_E_STOP);
}

static void
bna_rxf_fail(struct bna_rxf *rxf)
{
	bfa_fsm_send_event(rxf, RXF_E_FAIL);
}

enum bna_cb_status
bna_rx_ucast_set(struct bna_rx *rx, u8 *ucmac,
		 void (*cbfn)(struct bnad *, struct bna_rx *))
{
	struct bna_rxf *rxf = &rx->rxf;

	if (rxf->ucast_pending_mac == NULL) {
		rxf->ucast_pending_mac =
				bna_ucam_mod_mac_get(&rxf->rx->bna->ucam_mod);
		if (rxf->ucast_pending_mac == NULL)
			return BNA_CB_UCAST_CAM_FULL;
		bfa_q_qe_init(&rxf->ucast_pending_mac->qe);
	}

	memcpy(rxf->ucast_pending_mac->addr, ucmac, ETH_ALEN);
	rxf->ucast_pending_set = 1;
	rxf->cam_fltr_cbfn = cbfn;
	rxf->cam_fltr_cbarg = rx->bna->bnad;

	bfa_fsm_send_event(rxf, RXF_E_CONFIG);

	return BNA_CB_SUCCESS;
}

enum bna_cb_status
bna_rx_mcast_add(struct bna_rx *rx, u8 *addr,
		 void (*cbfn)(struct bnad *, struct bna_rx *))
{
	struct bna_rxf *rxf = &rx->rxf;
	struct bna_mac *mac;

	/* Check if already added or pending addition */
	if (bna_mac_find(&rxf->mcast_active_q, addr) ||
		bna_mac_find(&rxf->mcast_pending_add_q, addr)) {
		if (cbfn)
			cbfn(rx->bna->bnad, rx);
		return BNA_CB_SUCCESS;
	}

	mac = bna_mcam_mod_mac_get(&rxf->rx->bna->mcam_mod);
	if (mac == NULL)
		return BNA_CB_MCAST_LIST_FULL;
	bfa_q_qe_init(&mac->qe);
	memcpy(mac->addr, addr, ETH_ALEN);
	list_add_tail(&mac->qe, &rxf->mcast_pending_add_q);

	rxf->cam_fltr_cbfn = cbfn;
	rxf->cam_fltr_cbarg = rx->bna->bnad;

	bfa_fsm_send_event(rxf, RXF_E_CONFIG);

	return BNA_CB_SUCCESS;
}

enum bna_cb_status
bna_rx_mcast_listset(struct bna_rx *rx, int count, u8 *mclist,
		     void (*cbfn)(struct bnad *, struct bna_rx *))
{
	struct bna_rxf *rxf = &rx->rxf;
	struct list_head list_head;
	struct list_head *qe;
	u8 *mcaddr;
	struct bna_mac *mac;
	int i;

	/* Allocate nodes */
	INIT_LIST_HEAD(&list_head);
	for (i = 0, mcaddr = mclist; i < count; i++) {
		mac = bna_mcam_mod_mac_get(&rxf->rx->bna->mcam_mod);
		if (mac == NULL)
			goto err_return;
		bfa_q_qe_init(&mac->qe);
		memcpy(mac->addr, mcaddr, ETH_ALEN);
		list_add_tail(&mac->qe, &list_head);

		mcaddr += ETH_ALEN;
	}

	/* Purge the pending_add_q */
	while (!list_empty(&rxf->mcast_pending_add_q)) {
		bfa_q_deq(&rxf->mcast_pending_add_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
	}

	/* Schedule active_q entries for deletion */
	while (!list_empty(&rxf->mcast_active_q)) {
		bfa_q_deq(&rxf->mcast_active_q, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);
		list_add_tail(&mac->qe, &rxf->mcast_pending_del_q);
	}

	/* Add the new entries */
	while (!list_empty(&list_head)) {
		bfa_q_deq(&list_head, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);
		list_add_tail(&mac->qe, &rxf->mcast_pending_add_q);
	}

	rxf->cam_fltr_cbfn = cbfn;
	rxf->cam_fltr_cbarg = rx->bna->bnad;
	bfa_fsm_send_event(rxf, RXF_E_CONFIG);

	return BNA_CB_SUCCESS;

err_return:
	while (!list_empty(&list_head)) {
		bfa_q_deq(&list_head, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
	}

	return BNA_CB_MCAST_LIST_FULL;
}

void
bna_rx_vlan_add(struct bna_rx *rx, int vlan_id)
{
	struct bna_rxf *rxf = &rx->rxf;
	int index = (vlan_id >> BFI_VLAN_WORD_SHIFT);
	int bit = (1 << (vlan_id & BFI_VLAN_WORD_MASK));
	int group_id = (vlan_id >> BFI_VLAN_BLOCK_SHIFT);

	rxf->vlan_filter_table[index] |= bit;
	if (rxf->vlan_filter_status == BNA_STATUS_T_ENABLED) {
		rxf->vlan_pending_bitmask |= (1 << group_id);
		bfa_fsm_send_event(rxf, RXF_E_CONFIG);
	}
}

void
bna_rx_vlan_del(struct bna_rx *rx, int vlan_id)
{
	struct bna_rxf *rxf = &rx->rxf;
	int index = (vlan_id >> BFI_VLAN_WORD_SHIFT);
	int bit = (1 << (vlan_id & BFI_VLAN_WORD_MASK));
	int group_id = (vlan_id >> BFI_VLAN_BLOCK_SHIFT);

	rxf->vlan_filter_table[index] &= ~bit;
	if (rxf->vlan_filter_status == BNA_STATUS_T_ENABLED) {
		rxf->vlan_pending_bitmask |= (1 << group_id);
		bfa_fsm_send_event(rxf, RXF_E_CONFIG);
	}
}

static int
bna_rxf_ucast_cfg_apply(struct bna_rxf *rxf)
{
	struct bna_mac *mac = NULL;
	struct list_head *qe;

	/* Delete MAC addresses previousely added */
	if (!list_empty(&rxf->ucast_pending_del_q)) {
		bfa_q_deq(&rxf->ucast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		bna_bfi_ucast_req(rxf, mac, BFI_ENET_H2I_MAC_UCAST_DEL_REQ);
		bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod, mac);
		return 1;
	}

	/* Set default unicast MAC */
	if (rxf->ucast_pending_set) {
		rxf->ucast_pending_set = 0;
		memcpy(rxf->ucast_active_mac.addr,
			rxf->ucast_pending_mac->addr, ETH_ALEN);
		rxf->ucast_active_set = 1;
		bna_bfi_ucast_req(rxf, &rxf->ucast_active_mac,
			BFI_ENET_H2I_MAC_UCAST_SET_REQ);
		return 1;
	}

	/* Add additional MAC entries */
	if (!list_empty(&rxf->ucast_pending_add_q)) {
		bfa_q_deq(&rxf->ucast_pending_add_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		list_add_tail(&mac->qe, &rxf->ucast_active_q);
		bna_bfi_ucast_req(rxf, mac, BFI_ENET_H2I_MAC_UCAST_ADD_REQ);
		return 1;
	}

	return 0;
}

static int
bna_rxf_ucast_cfg_reset(struct bna_rxf *rxf, enum bna_cleanup_type cleanup)
{
	struct list_head *qe;
	struct bna_mac *mac;

	/* Throw away delete pending ucast entries */
	while (!list_empty(&rxf->ucast_pending_del_q)) {
		bfa_q_deq(&rxf->ucast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		if (cleanup == BNA_SOFT_CLEANUP)
			bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod, mac);
		else {
			bna_bfi_ucast_req(rxf, mac,
				BFI_ENET_H2I_MAC_UCAST_DEL_REQ);
			bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod, mac);
			return 1;
		}
	}

	/* Move active ucast entries to pending_add_q */
	while (!list_empty(&rxf->ucast_active_q)) {
		bfa_q_deq(&rxf->ucast_active_q, &qe);
		bfa_q_qe_init(qe);
		list_add_tail(qe, &rxf->ucast_pending_add_q);
		if (cleanup == BNA_HARD_CLEANUP) {
			mac = (struct bna_mac *)qe;
			bna_bfi_ucast_req(rxf, mac,
				BFI_ENET_H2I_MAC_UCAST_DEL_REQ);
			return 1;
		}
	}

	if (rxf->ucast_active_set) {
		rxf->ucast_pending_set = 1;
		rxf->ucast_active_set = 0;
		if (cleanup == BNA_HARD_CLEANUP) {
			bna_bfi_ucast_req(rxf, &rxf->ucast_active_mac,
				BFI_ENET_H2I_MAC_UCAST_CLR_REQ);
			return 1;
		}
	}

	return 0;
}

static int
bna_rxf_promisc_cfg_apply(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;

	/* Enable/disable promiscuous mode */
	if (is_promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* move promisc configuration from pending -> active */
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active |= BNA_RXMODE_PROMISC;
		bna_bfi_rx_promisc_req(rxf, BNA_STATUS_T_ENABLED);
		return 1;
	} else if (is_promisc_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* move promisc configuration from pending -> active */
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_PROMISC;
		bna->promisc_rid = BFI_INVALID_RID;
		bna_bfi_rx_promisc_req(rxf, BNA_STATUS_T_DISABLED);
		return 1;
	}

	return 0;
}

static int
bna_rxf_promisc_cfg_reset(struct bna_rxf *rxf, enum bna_cleanup_type cleanup)
{
	struct bna *bna = rxf->rx->bna;

	/* Clear pending promisc mode disable */
	if (is_promisc_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_PROMISC;
		bna->promisc_rid = BFI_INVALID_RID;
		if (cleanup == BNA_HARD_CLEANUP) {
			bna_bfi_rx_promisc_req(rxf, BNA_STATUS_T_DISABLED);
			return 1;
		}
	}

	/* Move promisc mode config from active -> pending */
	if (rxf->rxmode_active & BNA_RXMODE_PROMISC) {
		promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_PROMISC;
		if (cleanup == BNA_HARD_CLEANUP) {
			bna_bfi_rx_promisc_req(rxf, BNA_STATUS_T_DISABLED);
			return 1;
		}
	}

	return 0;
}

static int
bna_rxf_allmulti_cfg_apply(struct bna_rxf *rxf)
{
	/* Enable/disable allmulti mode */
	if (is_allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* move allmulti configuration from pending -> active */
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active |= BNA_RXMODE_ALLMULTI;
		bna_bfi_mcast_filter_req(rxf, BNA_STATUS_T_DISABLED);
		return 1;
	} else if (is_allmulti_disable(rxf->rxmode_pending,
					rxf->rxmode_pending_bitmask)) {
		/* move allmulti configuration from pending -> active */
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_ALLMULTI;
		bna_bfi_mcast_filter_req(rxf, BNA_STATUS_T_ENABLED);
		return 1;
	}

	return 0;
}

static int
bna_rxf_allmulti_cfg_reset(struct bna_rxf *rxf, enum bna_cleanup_type cleanup)
{
	/* Clear pending allmulti mode disable */
	if (is_allmulti_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_ALLMULTI;
		if (cleanup == BNA_HARD_CLEANUP) {
			bna_bfi_mcast_filter_req(rxf, BNA_STATUS_T_ENABLED);
			return 1;
		}
	}

	/* Move allmulti mode config from active -> pending */
	if (rxf->rxmode_active & BNA_RXMODE_ALLMULTI) {
		allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_ALLMULTI;
		if (cleanup == BNA_HARD_CLEANUP) {
			bna_bfi_mcast_filter_req(rxf, BNA_STATUS_T_ENABLED);
			return 1;
		}
	}

	return 0;
}

static int
bna_rxf_promisc_enable(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;
	int ret = 0;

	if (is_promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask) ||
		(rxf->rxmode_active & BNA_RXMODE_PROMISC)) {
		/* Do nothing if pending enable or already enabled */
	} else if (is_promisc_disable(rxf->rxmode_pending,
					rxf->rxmode_pending_bitmask)) {
		/* Turn off pending disable command */
		promisc_inactive(rxf->rxmode_pending,
			rxf->rxmode_pending_bitmask);
	} else {
		/* Schedule enable */
		promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		bna->promisc_rid = rxf->rx->rid;
		ret = 1;
	}

	return ret;
}

static int
bna_rxf_promisc_disable(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;
	int ret = 0;

	if (is_promisc_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask) ||
		(!(rxf->rxmode_active & BNA_RXMODE_PROMISC))) {
		/* Do nothing if pending disable or already disabled */
	} else if (is_promisc_enable(rxf->rxmode_pending,
					rxf->rxmode_pending_bitmask)) {
		/* Turn off pending enable command */
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		bna->promisc_rid = BFI_INVALID_RID;
	} else if (rxf->rxmode_active & BNA_RXMODE_PROMISC) {
		/* Schedule disable */
		promisc_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		ret = 1;
	}

	return ret;
}

static int
bna_rxf_allmulti_enable(struct bna_rxf *rxf)
{
	int ret = 0;

	if (is_allmulti_enable(rxf->rxmode_pending,
			rxf->rxmode_pending_bitmask) ||
			(rxf->rxmode_active & BNA_RXMODE_ALLMULTI)) {
		/* Do nothing if pending enable or already enabled */
	} else if (is_allmulti_disable(rxf->rxmode_pending,
					rxf->rxmode_pending_bitmask)) {
		/* Turn off pending disable command */
		allmulti_inactive(rxf->rxmode_pending,
			rxf->rxmode_pending_bitmask);
	} else {
		/* Schedule enable */
		allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		ret = 1;
	}

	return ret;
}

static int
bna_rxf_allmulti_disable(struct bna_rxf *rxf)
{
	int ret = 0;

	if (is_allmulti_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask) ||
		(!(rxf->rxmode_active & BNA_RXMODE_ALLMULTI))) {
		/* Do nothing if pending disable or already disabled */
	} else if (is_allmulti_enable(rxf->rxmode_pending,
					rxf->rxmode_pending_bitmask)) {
		/* Turn off pending enable command */
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
	} else if (rxf->rxmode_active & BNA_RXMODE_ALLMULTI) {
		/* Schedule disable */
		allmulti_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		ret = 1;
	}

	return ret;
}

static int
bna_rxf_vlan_strip_cfg_apply(struct bna_rxf *rxf)
{
	if (rxf->vlan_strip_pending) {
			rxf->vlan_strip_pending = false;
			bna_bfi_vlan_strip_enable(rxf);
			return 1;
	}

	return 0;
}

/* RX */

#define	BNA_GET_RXQS(qcfg)	(((qcfg)->rxp_type == BNA_RXP_SINGLE) ?	\
	(qcfg)->num_paths : ((qcfg)->num_paths * 2))

#define	SIZE_TO_PAGES(size)	(((size) >> PAGE_SHIFT) + ((((size) &\
	(PAGE_SIZE - 1)) + (PAGE_SIZE - 1)) >> PAGE_SHIFT))

#define	call_rx_stop_cbfn(rx)						\
do {								    \
	if ((rx)->stop_cbfn) {						\
		void (*cbfn)(void *, struct bna_rx *);	  \
		void *cbarg;					    \
		cbfn = (rx)->stop_cbfn;				 \
		cbarg = (rx)->stop_cbarg;			       \
		(rx)->stop_cbfn = NULL;					\
		(rx)->stop_cbarg = NULL;				\
		cbfn(cbarg, rx);					\
	}							       \
} while (0)

#define call_rx_stall_cbfn(rx)						\
do {									\
	if ((rx)->rx_stall_cbfn)					\
		(rx)->rx_stall_cbfn((rx)->bna->bnad, (rx));		\
} while (0)

#define bfi_enet_datapath_q_init(bfi_q, bna_qpt)			\
do {									\
	struct bna_dma_addr cur_q_addr =				\
		*((struct bna_dma_addr *)((bna_qpt)->kv_qpt_ptr));	\
	(bfi_q)->pg_tbl.a32.addr_lo = (bna_qpt)->hw_qpt_ptr.lsb;	\
	(bfi_q)->pg_tbl.a32.addr_hi = (bna_qpt)->hw_qpt_ptr.msb;	\
	(bfi_q)->first_entry.a32.addr_lo = cur_q_addr.lsb;		\
	(bfi_q)->first_entry.a32.addr_hi = cur_q_addr.msb;		\
	(bfi_q)->pages = htons((u16)(bna_qpt)->page_count);	\
	(bfi_q)->page_sz = htons((u16)(bna_qpt)->page_size);\
} while (0)

static void bna_bfi_rx_enet_start(struct bna_rx *rx);
static void bna_rx_enet_stop(struct bna_rx *rx);
static void bna_rx_mod_cb_rx_stopped(void *arg, struct bna_rx *rx);

bfa_fsm_state_decl(bna_rx, stopped,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, start_wait,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, rxf_start_wait,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, started,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, rxf_stop_wait,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, stop_wait,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, cleanup_wait,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, failed,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, quiesce_wait,
	struct bna_rx, enum bna_rx_event);

static void bna_rx_sm_stopped_entry(struct bna_rx *rx)
{
	call_rx_stop_cbfn(rx);
}

static void bna_rx_sm_stopped(struct bna_rx *rx,
				enum bna_rx_event event)
{
	switch (event) {
	case RX_E_START:
		bfa_fsm_set_state(rx, bna_rx_sm_start_wait);
		break;

	case RX_E_STOP:
		call_rx_stop_cbfn(rx);
		break;

	case RX_E_FAIL:
		/* no-op */
		break;

	default:
		bfa_sm_fault(event);
		break;
	}
}

static void bna_rx_sm_start_wait_entry(struct bna_rx *rx)
{
	bna_bfi_rx_enet_start(rx);
}

void
bna_rx_sm_stop_wait_entry(struct bna_rx *rx)
{
}

static void
bna_rx_sm_stop_wait(struct bna_rx *rx, enum bna_rx_event event)
{
	switch (event) {
	case RX_E_FAIL:
	case RX_E_STOPPED:
		bfa_fsm_set_state(rx, bna_rx_sm_cleanup_wait);
		rx->rx_cleanup_cbfn(rx->bna->bnad, rx);
		break;

	case RX_E_STARTED:
		bna_rx_enet_stop(rx);
		break;

	default:
		bfa_sm_fault(event);
		break;
	}
}

static void bna_rx_sm_start_wait(struct bna_rx *rx,
				enum bna_rx_event event)
{
	switch (event) {
	case RX_E_STOP:
		bfa_fsm_set_state(rx, bna_rx_sm_stop_wait);
		break;

	case RX_E_FAIL:
		bfa_fsm_set_state(rx, bna_rx_sm_stopped);
		break;

	case RX_E_STARTED:
		bfa_fsm_set_state(rx, bna_rx_sm_rxf_start_wait);
		break;

	default:
		bfa_sm_fault(event);
		break;
	}
}

static void bna_rx_sm_rxf_start_wait_entry(struct bna_rx *rx)
{
	rx->rx_post_cbfn(rx->bna->bnad, rx);
	bna_rxf_start(&rx->rxf);
}

void
bna_rx_sm_rxf_stop_wait_entry(struct bna_rx *rx)
{
}

static void
bna_rx_sm_rxf_stop_wait(struct bna_rx *rx, enum bna_rx_event event)
{
	switch (event) {
	case RX_E_FAIL:
		bfa_fsm_set_state(rx, bna_rx_sm_cleanup_wait);
		bna_rxf_fail(&rx->rxf);
		call_rx_stall_cbfn(rx);
		rx->rx_cleanup_cbfn(rx->bna->bnad, rx);
		break;

	case RX_E_RXF_STARTED:
		bna_rxf_stop(&rx->rxf);
		break;

	case RX_E_RXF_STOPPED:
		bfa_fsm_set_state(rx, bna_rx_sm_stop_wait);
		call_rx_stall_cbfn(rx);
		bna_rx_enet_stop(rx);
		break;

	default:
		bfa_sm_fault(event);
		break;
	}

}

void
bna_rx_sm_started_entry(struct bna_rx *rx)
{
	struct bna_rxp *rxp;
	struct list_head *qe_rxp;
	int is_regular = (rx->type == BNA_RX_T_REGULAR);

	/* Start IB */
	list_for_each(qe_rxp, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe_rxp;
		bna_ib_start(rx->bna, &rxp->cq.ib, is_regular);
	}

	bna_ethport_cb_rx_started(&rx->bna->ethport);
}

static void
bna_rx_sm_started(struct bna_rx *rx, enum bna_rx_event event)
{
	switch (event) {
	case RX_E_STOP:
		bfa_fsm_set_state(rx, bna_rx_sm_rxf_stop_wait);
		bna_ethport_cb_rx_stopped(&rx->bna->ethport);
		bna_rxf_stop(&rx->rxf);
		break;

	case RX_E_FAIL:
		bfa_fsm_set_state(rx, bna_rx_sm_failed);
		bna_ethport_cb_rx_stopped(&rx->bna->ethport);
		bna_rxf_fail(&rx->rxf);
		call_rx_stall_cbfn(rx);
		rx->rx_cleanup_cbfn(rx->bna->bnad, rx);
		break;

	default:
		bfa_sm_fault(event);
		break;
	}
}

static void bna_rx_sm_rxf_start_wait(struct bna_rx *rx,
				enum bna_rx_event event)
{
	switch (event) {
	case RX_E_STOP:
		bfa_fsm_set_state(rx, bna_rx_sm_rxf_stop_wait);
		break;

	case RX_E_FAIL:
		bfa_fsm_set_state(rx, bna_rx_sm_failed);
		bna_rxf_fail(&rx->rxf);
		call_rx_stall_cbfn(rx);
		rx->rx_cleanup_cbfn(rx->bna->bnad, rx);
		break;

	case RX_E_RXF_STARTED:
		bfa_fsm_set_state(rx, bna_rx_sm_started);
		break;

	default:
		bfa_sm_fault(event);
		break;
	}
}

void
bna_rx_sm_cleanup_wait_entry(struct bna_rx *rx)
{
}

void
bna_rx_sm_cleanup_wait(struct bna_rx *rx, enum bna_rx_event event)
{
	switch (event) {
	case RX_E_FAIL:
	case RX_E_RXF_STOPPED:
		/* No-op */
		break;

	case RX_E_CLEANUP_DONE:
		bfa_fsm_set_state(rx, bna_rx_sm_stopped);
		break;

	default:
		bfa_sm_fault(event);
		break;
	}
}

static void
bna_rx_sm_failed_entry(struct bna_rx *rx)
{
}

static void
bna_rx_sm_failed(struct bna_rx *rx, enum bna_rx_event event)
{
	switch (event) {
	case RX_E_START:
		bfa_fsm_set_state(rx, bna_rx_sm_quiesce_wait);
		break;

	case RX_E_STOP:
		bfa_fsm_set_state(rx, bna_rx_sm_cleanup_wait);
		break;

	case RX_E_FAIL:
	case RX_E_RXF_STARTED:
	case RX_E_RXF_STOPPED:
		/* No-op */
		break;

	case RX_E_CLEANUP_DONE:
		bfa_fsm_set_state(rx, bna_rx_sm_stopped);
		break;

	default:
		bfa_sm_fault(event);
		break;
}	}

static void
bna_rx_sm_quiesce_wait_entry(struct bna_rx *rx)
{
}

static void
bna_rx_sm_quiesce_wait(struct bna_rx *rx, enum bna_rx_event event)
{
	switch (event) {
	case RX_E_STOP:
		bfa_fsm_set_state(rx, bna_rx_sm_cleanup_wait);
		break;

	case RX_E_FAIL:
		bfa_fsm_set_state(rx, bna_rx_sm_failed);
		break;

	case RX_E_CLEANUP_DONE:
		bfa_fsm_set_state(rx, bna_rx_sm_start_wait);
		break;

	default:
		bfa_sm_fault(event);
		break;
	}
}

static void
bna_bfi_rx_enet_start(struct bna_rx *rx)
{
	struct bfi_enet_rx_cfg_req *cfg_req = &rx->bfi_enet_cmd.cfg_req;
	struct bna_rxp *rxp = NULL;
	struct bna_rxq *q0 = NULL, *q1 = NULL;
	struct list_head *rxp_qe;
	int i;

	bfi_msgq_mhdr_set(cfg_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_RX_CFG_SET_REQ, 0, rx->rid);
	cfg_req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_rx_cfg_req)));

	cfg_req->num_queue_sets = rx->num_paths;
	for (i = 0, rxp_qe = bfa_q_first(&rx->rxp_q);
		i < rx->num_paths;
		i++, rxp_qe = bfa_q_next(rxp_qe)) {
		rxp = (struct bna_rxp *)rxp_qe;

		GET_RXQS(rxp, q0, q1);
		switch (rxp->type) {
		case BNA_RXP_SLR:
		case BNA_RXP_HDS:
			/* Small RxQ */
			bfi_enet_datapath_q_init(&cfg_req->q_cfg[i].qs.q,
						&q1->qpt);
			cfg_req->q_cfg[i].qs.rx_buffer_size =
				htons((u16)q1->buffer_size);
			/* Fall through */

		case BNA_RXP_SINGLE:
			/* Large/Single RxQ */
			bfi_enet_datapath_q_init(&cfg_req->q_cfg[i].ql.q,
						&q0->qpt);
			q0->buffer_size =
				bna_enet_mtu_get(&rx->bna->enet);
			cfg_req->q_cfg[i].ql.rx_buffer_size =
				htons((u16)q0->buffer_size);
			break;

		default:
			BUG_ON(1);
		}

		bfi_enet_datapath_q_init(&cfg_req->q_cfg[i].cq.q,
					&rxp->cq.qpt);

		cfg_req->q_cfg[i].ib.index_addr.a32.addr_lo =
			rxp->cq.ib.ib_seg_host_addr.lsb;
		cfg_req->q_cfg[i].ib.index_addr.a32.addr_hi =
			rxp->cq.ib.ib_seg_host_addr.msb;
		cfg_req->q_cfg[i].ib.intr.msix_index =
			htons((u16)rxp->cq.ib.intr_vector);
	}

	cfg_req->ib_cfg.int_pkt_dma = BNA_STATUS_T_DISABLED;
	cfg_req->ib_cfg.int_enabled = BNA_STATUS_T_ENABLED;
	cfg_req->ib_cfg.int_pkt_enabled = BNA_STATUS_T_DISABLED;
	cfg_req->ib_cfg.continuous_coalescing = BNA_STATUS_T_DISABLED;
	cfg_req->ib_cfg.msix = (rxp->cq.ib.intr_type == BNA_INTR_T_MSIX)
				? BNA_STATUS_T_ENABLED :
				BNA_STATUS_T_DISABLED;
	cfg_req->ib_cfg.coalescing_timeout =
			htonl((u32)rxp->cq.ib.coalescing_timeo);
	cfg_req->ib_cfg.inter_pkt_timeout =
			htonl((u32)rxp->cq.ib.interpkt_timeo);
	cfg_req->ib_cfg.inter_pkt_count = (u8)rxp->cq.ib.interpkt_count;

	switch (rxp->type) {
	case BNA_RXP_SLR:
		cfg_req->rx_cfg.rxq_type = BFI_ENET_RXQ_LARGE_SMALL;
		break;

	case BNA_RXP_HDS:
		cfg_req->rx_cfg.rxq_type = BFI_ENET_RXQ_HDS;
		cfg_req->rx_cfg.hds.type = rx->hds_cfg.hdr_type;
		cfg_req->rx_cfg.hds.force_offset = rx->hds_cfg.forced_offset;
		cfg_req->rx_cfg.hds.max_header_size = rx->hds_cfg.forced_offset;
		break;

	case BNA_RXP_SINGLE:
		cfg_req->rx_cfg.rxq_type = BFI_ENET_RXQ_SINGLE;
		break;

	default:
		BUG_ON(1);
	}
	cfg_req->rx_cfg.strip_vlan = rx->rxf.vlan_strip_status;

	bfa_msgq_cmd_set(&rx->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_rx_cfg_req), &cfg_req->mh);
	bfa_msgq_cmd_post(&rx->bna->msgq, &rx->msgq_cmd);
}

static void
bna_bfi_rx_enet_stop(struct bna_rx *rx)
{
	struct bfi_enet_req *req = &rx->bfi_enet_cmd.req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_RX_CFG_CLR_REQ, 0, rx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_req)));
	bfa_msgq_cmd_set(&rx->msgq_cmd, NULL, NULL, sizeof(struct bfi_enet_req),
		&req->mh);
	bfa_msgq_cmd_post(&rx->bna->msgq, &rx->msgq_cmd);
}

static void
bna_rx_enet_stop(struct bna_rx *rx)
{
	struct bna_rxp *rxp;
	struct list_head		 *qe_rxp;

	/* Stop IB */
	list_for_each(qe_rxp, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe_rxp;
		bna_ib_stop(rx->bna, &rxp->cq.ib);
	}

	bna_bfi_rx_enet_stop(rx);
}

static int
bna_rx_res_check(struct bna_rx_mod *rx_mod, struct bna_rx_config *rx_cfg)
{
	if ((rx_mod->rx_free_count == 0) ||
		(rx_mod->rxp_free_count == 0) ||
		(rx_mod->rxq_free_count == 0))
		return 0;

	if (rx_cfg->rxp_type == BNA_RXP_SINGLE) {
		if ((rx_mod->rxp_free_count < rx_cfg->num_paths) ||
			(rx_mod->rxq_free_count < rx_cfg->num_paths))
				return 0;
	} else {
		if ((rx_mod->rxp_free_count < rx_cfg->num_paths) ||
			(rx_mod->rxq_free_count < (2 * rx_cfg->num_paths)))
			return 0;
	}

	return 1;
}

static struct bna_rxq *
bna_rxq_get(struct bna_rx_mod *rx_mod)
{
	struct bna_rxq *rxq = NULL;
	struct list_head	*qe = NULL;

	bfa_q_deq(&rx_mod->rxq_free_q, &qe);
	rx_mod->rxq_free_count--;
	rxq = (struct bna_rxq *)qe;
	bfa_q_qe_init(&rxq->qe);

	return rxq;
}

static void
bna_rxq_put(struct bna_rx_mod *rx_mod, struct bna_rxq *rxq)
{
	bfa_q_qe_init(&rxq->qe);
	list_add_tail(&rxq->qe, &rx_mod->rxq_free_q);
	rx_mod->rxq_free_count++;
}

static struct bna_rxp *
bna_rxp_get(struct bna_rx_mod *rx_mod)
{
	struct list_head	*qe = NULL;
	struct bna_rxp *rxp = NULL;

	bfa_q_deq(&rx_mod->rxp_free_q, &qe);
	rx_mod->rxp_free_count--;
	rxp = (struct bna_rxp *)qe;
	bfa_q_qe_init(&rxp->qe);

	return rxp;
}

static void
bna_rxp_put(struct bna_rx_mod *rx_mod, struct bna_rxp *rxp)
{
	bfa_q_qe_init(&rxp->qe);
	list_add_tail(&rxp->qe, &rx_mod->rxp_free_q);
	rx_mod->rxp_free_count++;
}

static struct bna_rx *
bna_rx_get(struct bna_rx_mod *rx_mod, enum bna_rx_type type)
{
	struct list_head	*qe = NULL;
	struct bna_rx *rx = NULL;

	if (type == BNA_RX_T_REGULAR) {
		bfa_q_deq(&rx_mod->rx_free_q, &qe);
	} else
		bfa_q_deq_tail(&rx_mod->rx_free_q, &qe);

	rx_mod->rx_free_count--;
	rx = (struct bna_rx *)qe;
	bfa_q_qe_init(&rx->qe);
	list_add_tail(&rx->qe, &rx_mod->rx_active_q);
	rx->type = type;

	return rx;
}

static void
bna_rx_put(struct bna_rx_mod *rx_mod, struct bna_rx *rx)
{
	struct list_head *prev_qe = NULL;
	struct list_head *qe;

	bfa_q_qe_init(&rx->qe);

	list_for_each(qe, &rx_mod->rx_free_q) {
		if (((struct bna_rx *)qe)->rid < rx->rid)
			prev_qe = qe;
		else
			break;
	}

	if (prev_qe == NULL) {
		/* This is the first entry */
		bfa_q_enq_head(&rx_mod->rx_free_q, &rx->qe);
	} else if (bfa_q_next(prev_qe) == &rx_mod->rx_free_q) {
		/* This is the last entry */
		list_add_tail(&rx->qe, &rx_mod->rx_free_q);
	} else {
		/* Somewhere in the middle */
		bfa_q_next(&rx->qe) = bfa_q_next(prev_qe);
		bfa_q_prev(&rx->qe) = prev_qe;
		bfa_q_next(prev_qe) = &rx->qe;
		bfa_q_prev(bfa_q_next(&rx->qe)) = &rx->qe;
	}

	rx_mod->rx_free_count++;
}

static void
bna_rxp_add_rxqs(struct bna_rxp *rxp, struct bna_rxq *q0,
		struct bna_rxq *q1)
{
	switch (rxp->type) {
	case BNA_RXP_SINGLE:
		rxp->rxq.single.only = q0;
		rxp->rxq.single.reserved = NULL;
		break;
	case BNA_RXP_SLR:
		rxp->rxq.slr.large = q0;
		rxp->rxq.slr.small = q1;
		break;
	case BNA_RXP_HDS:
		rxp->rxq.hds.data = q0;
		rxp->rxq.hds.hdr = q1;
		break;
	default:
		break;
	}
}

static void
bna_rxq_qpt_setup(struct bna_rxq *rxq,
		struct bna_rxp *rxp,
		u32 page_count,
		u32 page_size,
		struct bna_mem_descr *qpt_mem,
		struct bna_mem_descr *swqpt_mem,
		struct bna_mem_descr *page_mem)
{
	int	i;

	rxq->qpt.hw_qpt_ptr.lsb = qpt_mem->dma.lsb;
	rxq->qpt.hw_qpt_ptr.msb = qpt_mem->dma.msb;
	rxq->qpt.kv_qpt_ptr = qpt_mem->kva;
	rxq->qpt.page_count = page_count;
	rxq->qpt.page_size = page_size;

	rxq->rcb->sw_qpt = (void **) swqpt_mem->kva;

	for (i = 0; i < rxq->qpt.page_count; i++) {
		rxq->rcb->sw_qpt[i] = page_mem[i].kva;
		((struct bna_dma_addr *)rxq->qpt.kv_qpt_ptr)[i].lsb =
			page_mem[i].dma.lsb;
		((struct bna_dma_addr *)rxq->qpt.kv_qpt_ptr)[i].msb =
			page_mem[i].dma.msb;
	}
}

static void
bna_rxp_cqpt_setup(struct bna_rxp *rxp,
		u32 page_count,
		u32 page_size,
		struct bna_mem_descr *qpt_mem,
		struct bna_mem_descr *swqpt_mem,
		struct bna_mem_descr *page_mem)
{
	int	i;

	rxp->cq.qpt.hw_qpt_ptr.lsb = qpt_mem->dma.lsb;
	rxp->cq.qpt.hw_qpt_ptr.msb = qpt_mem->dma.msb;
	rxp->cq.qpt.kv_qpt_ptr = qpt_mem->kva;
	rxp->cq.qpt.page_count = page_count;
	rxp->cq.qpt.page_size = page_size;

	rxp->cq.ccb->sw_qpt = (void **) swqpt_mem->kva;

	for (i = 0; i < rxp->cq.qpt.page_count; i++) {
		rxp->cq.ccb->sw_qpt[i] = page_mem[i].kva;

		((struct bna_dma_addr *)rxp->cq.qpt.kv_qpt_ptr)[i].lsb =
			page_mem[i].dma.lsb;
		((struct bna_dma_addr *)rxp->cq.qpt.kv_qpt_ptr)[i].msb =
			page_mem[i].dma.msb;
	}
}

static void
bna_rx_mod_cb_rx_stopped(void *arg, struct bna_rx *rx)
{
	struct bna_rx_mod *rx_mod = (struct bna_rx_mod *)arg;

	bfa_wc_down(&rx_mod->rx_stop_wc);
}

static void
bna_rx_mod_cb_rx_stopped_all(void *arg)
{
	struct bna_rx_mod *rx_mod = (struct bna_rx_mod *)arg;

	if (rx_mod->stop_cbfn)
		rx_mod->stop_cbfn(&rx_mod->bna->enet);
	rx_mod->stop_cbfn = NULL;
}

static void
bna_rx_start(struct bna_rx *rx)
{
	rx->rx_flags |= BNA_RX_F_ENET_STARTED;
	if (rx->rx_flags & BNA_RX_F_ENABLED)
		bfa_fsm_send_event(rx, RX_E_START);
}

static void
bna_rx_stop(struct bna_rx *rx)
{
	rx->rx_flags &= ~BNA_RX_F_ENET_STARTED;
	if (rx->fsm == (bfa_fsm_t) bna_rx_sm_stopped)
		bna_rx_mod_cb_rx_stopped(&rx->bna->rx_mod, rx);
	else {
		rx->stop_cbfn = bna_rx_mod_cb_rx_stopped;
		rx->stop_cbarg = &rx->bna->rx_mod;
		bfa_fsm_send_event(rx, RX_E_STOP);
	}
}

static void
bna_rx_fail(struct bna_rx *rx)
{
	/* Indicate Enet is not enabled, and failed */
	rx->rx_flags &= ~BNA_RX_F_ENET_STARTED;
	bfa_fsm_send_event(rx, RX_E_FAIL);
}

void
bna_rx_mod_start(struct bna_rx_mod *rx_mod, enum bna_rx_type type)
{
	struct bna_rx *rx;
	struct list_head *qe;

	rx_mod->flags |= BNA_RX_MOD_F_ENET_STARTED;
	if (type == BNA_RX_T_LOOPBACK)
		rx_mod->flags |= BNA_RX_MOD_F_ENET_LOOPBACK;

	list_for_each(qe, &rx_mod->rx_active_q) {
		rx = (struct bna_rx *)qe;
		if (rx->type == type)
			bna_rx_start(rx);
	}
}

void
bna_rx_mod_stop(struct bna_rx_mod *rx_mod, enum bna_rx_type type)
{
	struct bna_rx *rx;
	struct list_head *qe;

	rx_mod->flags &= ~BNA_RX_MOD_F_ENET_STARTED;
	rx_mod->flags &= ~BNA_RX_MOD_F_ENET_LOOPBACK;

	rx_mod->stop_cbfn = bna_enet_cb_rx_stopped;

	bfa_wc_init(&rx_mod->rx_stop_wc, bna_rx_mod_cb_rx_stopped_all, rx_mod);

	list_for_each(qe, &rx_mod->rx_active_q) {
		rx = (struct bna_rx *)qe;
		if (rx->type == type) {
			bfa_wc_up(&rx_mod->rx_stop_wc);
			bna_rx_stop(rx);
		}
	}

	bfa_wc_wait(&rx_mod->rx_stop_wc);
}

void
bna_rx_mod_fail(struct bna_rx_mod *rx_mod)
{
	struct bna_rx *rx;
	struct list_head *qe;

	rx_mod->flags &= ~BNA_RX_MOD_F_ENET_STARTED;
	rx_mod->flags &= ~BNA_RX_MOD_F_ENET_LOOPBACK;

	list_for_each(qe, &rx_mod->rx_active_q) {
		rx = (struct bna_rx *)qe;
		bna_rx_fail(rx);
	}
}

void bna_rx_mod_init(struct bna_rx_mod *rx_mod, struct bna *bna,
			struct bna_res_info *res_info)
{
	int	index;
	struct bna_rx *rx_ptr;
	struct bna_rxp *rxp_ptr;
	struct bna_rxq *rxq_ptr;

	rx_mod->bna = bna;
	rx_mod->flags = 0;

	rx_mod->rx = (struct bna_rx *)
		res_info[BNA_MOD_RES_MEM_T_RX_ARRAY].res_u.mem_info.mdl[0].kva;
	rx_mod->rxp = (struct bna_rxp *)
		res_info[BNA_MOD_RES_MEM_T_RXP_ARRAY].res_u.mem_info.mdl[0].kva;
	rx_mod->rxq = (struct bna_rxq *)
		res_info[BNA_MOD_RES_MEM_T_RXQ_ARRAY].res_u.mem_info.mdl[0].kva;

	/* Initialize the queues */
	INIT_LIST_HEAD(&rx_mod->rx_free_q);
	rx_mod->rx_free_count = 0;
	INIT_LIST_HEAD(&rx_mod->rxq_free_q);
	rx_mod->rxq_free_count = 0;
	INIT_LIST_HEAD(&rx_mod->rxp_free_q);
	rx_mod->rxp_free_count = 0;
	INIT_LIST_HEAD(&rx_mod->rx_active_q);

	/* Build RX queues */
	for (index = 0; index < bna->ioceth.attr.num_rxp; index++) {
		rx_ptr = &rx_mod->rx[index];

		bfa_q_qe_init(&rx_ptr->qe);
		INIT_LIST_HEAD(&rx_ptr->rxp_q);
		rx_ptr->bna = NULL;
		rx_ptr->rid = index;
		rx_ptr->stop_cbfn = NULL;
		rx_ptr->stop_cbarg = NULL;

		list_add_tail(&rx_ptr->qe, &rx_mod->rx_free_q);
		rx_mod->rx_free_count++;
	}

	/* build RX-path queue */
	for (index = 0; index < bna->ioceth.attr.num_rxp; index++) {
		rxp_ptr = &rx_mod->rxp[index];
		bfa_q_qe_init(&rxp_ptr->qe);
		list_add_tail(&rxp_ptr->qe, &rx_mod->rxp_free_q);
		rx_mod->rxp_free_count++;
	}

	/* build RXQ queue */
	for (index = 0; index < (bna->ioceth.attr.num_rxp * 2); index++) {
		rxq_ptr = &rx_mod->rxq[index];
		bfa_q_qe_init(&rxq_ptr->qe);
		list_add_tail(&rxq_ptr->qe, &rx_mod->rxq_free_q);
		rx_mod->rxq_free_count++;
	}
}

void
bna_rx_mod_uninit(struct bna_rx_mod *rx_mod)
{
	struct list_head		*qe;
	int i;

	i = 0;
	list_for_each(qe, &rx_mod->rx_free_q)
		i++;

	i = 0;
	list_for_each(qe, &rx_mod->rxp_free_q)
		i++;

	i = 0;
	list_for_each(qe, &rx_mod->rxq_free_q)
		i++;

	rx_mod->bna = NULL;
}

void
bna_bfi_rx_enet_start_rsp(struct bna_rx *rx, struct bfi_msgq_mhdr *msghdr)
{
	struct bfi_enet_rx_cfg_rsp *cfg_rsp = &rx->bfi_enet_cmd.cfg_rsp;
	struct bna_rxp *rxp = NULL;
	struct bna_rxq *q0 = NULL, *q1 = NULL;
	struct list_head *rxp_qe;
	int i;

	bfa_msgq_rsp_copy(&rx->bna->msgq, (u8 *)cfg_rsp,
		sizeof(struct bfi_enet_rx_cfg_rsp));

	rx->hw_id = cfg_rsp->hw_id;

	for (i = 0, rxp_qe = bfa_q_first(&rx->rxp_q);
		i < rx->num_paths;
		i++, rxp_qe = bfa_q_next(rxp_qe)) {
		rxp = (struct bna_rxp *)rxp_qe;
		GET_RXQS(rxp, q0, q1);

		/* Setup doorbells */
		rxp->cq.ccb->i_dbell->doorbell_addr =
			rx->bna->pcidev.pci_bar_kva
			+ ntohl(cfg_rsp->q_handles[i].i_dbell);
		rxp->hw_id = cfg_rsp->q_handles[i].hw_cqid;
		q0->rcb->q_dbell =
			rx->bna->pcidev.pci_bar_kva
			+ ntohl(cfg_rsp->q_handles[i].ql_dbell);
		q0->hw_id = cfg_rsp->q_handles[i].hw_lqid;
		if (q1) {
			q1->rcb->q_dbell =
			rx->bna->pcidev.pci_bar_kva
			+ ntohl(cfg_rsp->q_handles[i].qs_dbell);
			q1->hw_id = cfg_rsp->q_handles[i].hw_sqid;
		}

		/* Initialize producer/consumer indexes */
		(*rxp->cq.ccb->hw_producer_index) = 0;
		rxp->cq.ccb->producer_index = 0;
		q0->rcb->producer_index = q0->rcb->consumer_index = 0;
		if (q1)
			q1->rcb->producer_index = q1->rcb->consumer_index = 0;
	}

	bfa_fsm_send_event(rx, RX_E_STARTED);
}

void
bna_bfi_rx_enet_stop_rsp(struct bna_rx *rx, struct bfi_msgq_mhdr *msghdr)
{
	bfa_fsm_send_event(rx, RX_E_STOPPED);
}

void
bna_rx_res_req(struct bna_rx_config *q_cfg, struct bna_res_info *res_info)
{
	u32 cq_size, hq_size, dq_size;
	u32 cpage_count, hpage_count, dpage_count;
	struct bna_mem_info *mem_info;
	u32 cq_depth;
	u32 hq_depth;
	u32 dq_depth;

	dq_depth = q_cfg->q_depth;
	hq_depth = ((q_cfg->rxp_type == BNA_RXP_SINGLE) ? 0 : q_cfg->q_depth);
	cq_depth = dq_depth + hq_depth;

	BNA_TO_POWER_OF_2_HIGH(cq_depth);
	cq_size = cq_depth * BFI_CQ_WI_SIZE;
	cq_size = ALIGN(cq_size, PAGE_SIZE);
	cpage_count = SIZE_TO_PAGES(cq_size);

	BNA_TO_POWER_OF_2_HIGH(dq_depth);
	dq_size = dq_depth * BFI_RXQ_WI_SIZE;
	dq_size = ALIGN(dq_size, PAGE_SIZE);
	dpage_count = SIZE_TO_PAGES(dq_size);

	if (BNA_RXP_SINGLE != q_cfg->rxp_type) {
		BNA_TO_POWER_OF_2_HIGH(hq_depth);
		hq_size = hq_depth * BFI_RXQ_WI_SIZE;
		hq_size = ALIGN(hq_size, PAGE_SIZE);
		hpage_count = SIZE_TO_PAGES(hq_size);
	} else
		hpage_count = 0;

	res_info[BNA_RX_RES_MEM_T_CCB].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_CCB].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = sizeof(struct bna_ccb);
	mem_info->num = q_cfg->num_paths;

	res_info[BNA_RX_RES_MEM_T_RCB].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_RCB].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = sizeof(struct bna_rcb);
	mem_info->num = BNA_GET_RXQS(q_cfg);

	res_info[BNA_RX_RES_MEM_T_CQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_CQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = cpage_count * sizeof(struct bna_dma_addr);
	mem_info->num = q_cfg->num_paths;

	res_info[BNA_RX_RES_MEM_T_CSWQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_CSWQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = cpage_count * sizeof(void *);
	mem_info->num = q_cfg->num_paths;

	res_info[BNA_RX_RES_MEM_T_CQPT_PAGE].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_CQPT_PAGE].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = PAGE_SIZE;
	mem_info->num = cpage_count * q_cfg->num_paths;

	res_info[BNA_RX_RES_MEM_T_DQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_DQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = dpage_count * sizeof(struct bna_dma_addr);
	mem_info->num = q_cfg->num_paths;

	res_info[BNA_RX_RES_MEM_T_DSWQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_DSWQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = dpage_count * sizeof(void *);
	mem_info->num = q_cfg->num_paths;

	res_info[BNA_RX_RES_MEM_T_DPAGE].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_DPAGE].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = PAGE_SIZE;
	mem_info->num = dpage_count * q_cfg->num_paths;

	res_info[BNA_RX_RES_MEM_T_HQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_HQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = hpage_count * sizeof(struct bna_dma_addr);
	mem_info->num = (hpage_count ? q_cfg->num_paths : 0);

	res_info[BNA_RX_RES_MEM_T_HSWQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_HSWQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = hpage_count * sizeof(void *);
	mem_info->num = (hpage_count ? q_cfg->num_paths : 0);

	res_info[BNA_RX_RES_MEM_T_HPAGE].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_HPAGE].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = (hpage_count ? PAGE_SIZE : 0);
	mem_info->num = (hpage_count ? (hpage_count * q_cfg->num_paths) : 0);

	res_info[BNA_RX_RES_MEM_T_IBIDX].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_IBIDX].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = BFI_IBIDX_SIZE;
	mem_info->num = q_cfg->num_paths;

	res_info[BNA_RX_RES_MEM_T_RIT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_RIT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = BFI_ENET_RSS_RIT_MAX;
	mem_info->num = 1;

	res_info[BNA_RX_RES_T_INTR].res_type = BNA_RES_T_INTR;
	res_info[BNA_RX_RES_T_INTR].res_u.intr_info.intr_type = BNA_INTR_T_MSIX;
	res_info[BNA_RX_RES_T_INTR].res_u.intr_info.num = q_cfg->num_paths;
}

struct bna_rx *
bna_rx_create(struct bna *bna, struct bnad *bnad,
		struct bna_rx_config *rx_cfg,
		const struct bna_rx_event_cbfn *rx_cbfn,
		struct bna_res_info *res_info,
		void *priv)
{
	struct bna_rx_mod *rx_mod = &bna->rx_mod;
	struct bna_rx *rx;
	struct bna_rxp *rxp;
	struct bna_rxq *q0;
	struct bna_rxq *q1;
	struct bna_intr_info *intr_info;
	u32 page_count;
	struct bna_mem_descr *ccb_mem;
	struct bna_mem_descr *rcb_mem;
	struct bna_mem_descr *unmapq_mem;
	struct bna_mem_descr *cqpt_mem;
	struct bna_mem_descr *cswqpt_mem;
	struct bna_mem_descr *cpage_mem;
	struct bna_mem_descr *hqpt_mem;
	struct bna_mem_descr *dqpt_mem;
	struct bna_mem_descr *hsqpt_mem;
	struct bna_mem_descr *dsqpt_mem;
	struct bna_mem_descr *hpage_mem;
	struct bna_mem_descr *dpage_mem;
	int i, cpage_idx = 0, dpage_idx = 0, hpage_idx = 0;
	int dpage_count, hpage_count, rcb_idx;

	if (!bna_rx_res_check(rx_mod, rx_cfg))
		return NULL;

	intr_info = &res_info[BNA_RX_RES_T_INTR].res_u.intr_info;
	ccb_mem = &res_info[BNA_RX_RES_MEM_T_CCB].res_u.mem_info.mdl[0];
	rcb_mem = &res_info[BNA_RX_RES_MEM_T_RCB].res_u.mem_info.mdl[0];
	unmapq_mem = &res_info[BNA_RX_RES_MEM_T_UNMAPQ].res_u.mem_info.mdl[0];
	cqpt_mem = &res_info[BNA_RX_RES_MEM_T_CQPT].res_u.mem_info.mdl[0];
	cswqpt_mem = &res_info[BNA_RX_RES_MEM_T_CSWQPT].res_u.mem_info.mdl[0];
	cpage_mem = &res_info[BNA_RX_RES_MEM_T_CQPT_PAGE].res_u.mem_info.mdl[0];
	hqpt_mem = &res_info[BNA_RX_RES_MEM_T_HQPT].res_u.mem_info.mdl[0];
	dqpt_mem = &res_info[BNA_RX_RES_MEM_T_DQPT].res_u.mem_info.mdl[0];
	hsqpt_mem = &res_info[BNA_RX_RES_MEM_T_HSWQPT].res_u.mem_info.mdl[0];
	dsqpt_mem = &res_info[BNA_RX_RES_MEM_T_DSWQPT].res_u.mem_info.mdl[0];
	hpage_mem = &res_info[BNA_RX_RES_MEM_T_HPAGE].res_u.mem_info.mdl[0];
	dpage_mem = &res_info[BNA_RX_RES_MEM_T_DPAGE].res_u.mem_info.mdl[0];

	page_count = res_info[BNA_RX_RES_MEM_T_CQPT_PAGE].res_u.mem_info.num /
			rx_cfg->num_paths;

	dpage_count = res_info[BNA_RX_RES_MEM_T_DPAGE].res_u.mem_info.num /
			rx_cfg->num_paths;

	hpage_count = res_info[BNA_RX_RES_MEM_T_HPAGE].res_u.mem_info.num /
			rx_cfg->num_paths;

	rx = bna_rx_get(rx_mod, rx_cfg->rx_type);
	rx->bna = bna;
	rx->rx_flags = 0;
	INIT_LIST_HEAD(&rx->rxp_q);
	rx->stop_cbfn = NULL;
	rx->stop_cbarg = NULL;
	rx->priv = priv;

	rx->rcb_setup_cbfn = rx_cbfn->rcb_setup_cbfn;
	rx->rcb_destroy_cbfn = rx_cbfn->rcb_destroy_cbfn;
	rx->ccb_setup_cbfn = rx_cbfn->ccb_setup_cbfn;
	rx->ccb_destroy_cbfn = rx_cbfn->ccb_destroy_cbfn;
	rx->rx_stall_cbfn = rx_cbfn->rx_stall_cbfn;
	/* Following callbacks are mandatory */
	rx->rx_cleanup_cbfn = rx_cbfn->rx_cleanup_cbfn;
	rx->rx_post_cbfn = rx_cbfn->rx_post_cbfn;

	if (rx->bna->rx_mod.flags & BNA_RX_MOD_F_ENET_STARTED) {
		switch (rx->type) {
		case BNA_RX_T_REGULAR:
			if (!(rx->bna->rx_mod.flags &
				BNA_RX_MOD_F_ENET_LOOPBACK))
				rx->rx_flags |= BNA_RX_F_ENET_STARTED;
			break;
		case BNA_RX_T_LOOPBACK:
			if (rx->bna->rx_mod.flags & BNA_RX_MOD_F_ENET_LOOPBACK)
				rx->rx_flags |= BNA_RX_F_ENET_STARTED;
			break;
		}
	}

	rx->num_paths = rx_cfg->num_paths;
	for (i = 0, rcb_idx = 0; i < rx->num_paths; i++) {
		rxp = bna_rxp_get(rx_mod);
		list_add_tail(&rxp->qe, &rx->rxp_q);
		rxp->type = rx_cfg->rxp_type;
		rxp->rx = rx;
		rxp->cq.rx = rx;

		q0 = bna_rxq_get(rx_mod);
		if (BNA_RXP_SINGLE == rx_cfg->rxp_type)
			q1 = NULL;
		else
			q1 = bna_rxq_get(rx_mod);

		if (1 == intr_info->num)
			rxp->vector = intr_info->idl[0].vector;
		else
			rxp->vector = intr_info->idl[i].vector;

		/* Setup IB */

		rxp->cq.ib.ib_seg_host_addr.lsb =
		res_info[BNA_RX_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].dma.lsb;
		rxp->cq.ib.ib_seg_host_addr.msb =
		res_info[BNA_RX_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].dma.msb;
		rxp->cq.ib.ib_seg_host_addr_kva =
		res_info[BNA_RX_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].kva;
		rxp->cq.ib.intr_type = intr_info->intr_type;
		if (intr_info->intr_type == BNA_INTR_T_MSIX)
			rxp->cq.ib.intr_vector = rxp->vector;
		else
			rxp->cq.ib.intr_vector = (1 << rxp->vector);
		rxp->cq.ib.coalescing_timeo = rx_cfg->coalescing_timeo;
		rxp->cq.ib.interpkt_count = BFI_RX_INTERPKT_COUNT;
		rxp->cq.ib.interpkt_timeo = BFI_RX_INTERPKT_TIMEO;

		bna_rxp_add_rxqs(rxp, q0, q1);

		/* Setup large Q */

		q0->rx = rx;
		q0->rxp = rxp;

		q0->rcb = (struct bna_rcb *) rcb_mem[rcb_idx].kva;
		q0->rcb->unmap_q = (void *)unmapq_mem[rcb_idx].kva;
		rcb_idx++;
		q0->rcb->q_depth = rx_cfg->q_depth;
		q0->rcb->rxq = q0;
		q0->rcb->bnad = bna->bnad;
		q0->rcb->id = 0;
		q0->rx_packets = q0->rx_bytes = 0;
		q0->rx_packets_with_error = q0->rxbuf_alloc_failed = 0;

		bna_rxq_qpt_setup(q0, rxp, dpage_count, PAGE_SIZE,
			&dqpt_mem[i], &dsqpt_mem[i], &dpage_mem[dpage_idx]);
		q0->rcb->page_idx = dpage_idx;
		q0->rcb->page_count = dpage_count;
		dpage_idx += dpage_count;

		if (rx->rcb_setup_cbfn)
			rx->rcb_setup_cbfn(bnad, q0->rcb);

		/* Setup small Q */

		if (q1) {
			q1->rx = rx;
			q1->rxp = rxp;

			q1->rcb = (struct bna_rcb *) rcb_mem[rcb_idx].kva;
			q1->rcb->unmap_q = (void *)unmapq_mem[rcb_idx].kva;
			rcb_idx++;
			q1->rcb->q_depth = rx_cfg->q_depth;
			q1->rcb->rxq = q1;
			q1->rcb->bnad = bna->bnad;
			q1->rcb->id = 1;
			q1->buffer_size = (rx_cfg->rxp_type == BNA_RXP_HDS) ?
					rx_cfg->hds_config.forced_offset
					: rx_cfg->small_buff_size;
			q1->rx_packets = q1->rx_bytes = 0;
			q1->rx_packets_with_error = q1->rxbuf_alloc_failed = 0;

			bna_rxq_qpt_setup(q1, rxp, hpage_count, PAGE_SIZE,
				&hqpt_mem[i], &hsqpt_mem[i],
				&hpage_mem[hpage_idx]);
			q1->rcb->page_idx = hpage_idx;
			q1->rcb->page_count = hpage_count;
			hpage_idx += hpage_count;

			if (rx->rcb_setup_cbfn)
				rx->rcb_setup_cbfn(bnad, q1->rcb);
		}

		/* Setup CQ */

		rxp->cq.ccb = (struct bna_ccb *) ccb_mem[i].kva;
		rxp->cq.ccb->q_depth =	rx_cfg->q_depth +
					((rx_cfg->rxp_type == BNA_RXP_SINGLE) ?
					0 : rx_cfg->q_depth);
		rxp->cq.ccb->cq = &rxp->cq;
		rxp->cq.ccb->rcb[0] = q0->rcb;
		q0->rcb->ccb = rxp->cq.ccb;
		if (q1) {
			rxp->cq.ccb->rcb[1] = q1->rcb;
			q1->rcb->ccb = rxp->cq.ccb;
		}
		rxp->cq.ccb->hw_producer_index =
			(u32 *)rxp->cq.ib.ib_seg_host_addr_kva;
		rxp->cq.ccb->i_dbell = &rxp->cq.ib.door_bell;
		rxp->cq.ccb->intr_type = rxp->cq.ib.intr_type;
		rxp->cq.ccb->intr_vector = rxp->cq.ib.intr_vector;
		rxp->cq.ccb->rx_coalescing_timeo =
			rxp->cq.ib.coalescing_timeo;
		rxp->cq.ccb->pkt_rate.small_pkt_cnt = 0;
		rxp->cq.ccb->pkt_rate.large_pkt_cnt = 0;
		rxp->cq.ccb->bnad = bna->bnad;
		rxp->cq.ccb->id = i;

		bna_rxp_cqpt_setup(rxp, page_count, PAGE_SIZE,
			&cqpt_mem[i], &cswqpt_mem[i], &cpage_mem[cpage_idx]);
		rxp->cq.ccb->page_idx = cpage_idx;
		rxp->cq.ccb->page_count = page_count;
		cpage_idx += page_count;

		if (rx->ccb_setup_cbfn)
			rx->ccb_setup_cbfn(bnad, rxp->cq.ccb);
	}

	rx->hds_cfg = rx_cfg->hds_config;

	bna_rxf_init(&rx->rxf, rx, rx_cfg, res_info);

	bfa_fsm_set_state(rx, bna_rx_sm_stopped);

	rx_mod->rid_mask |= (1 << rx->rid);

	return rx;
}

void
bna_rx_destroy(struct bna_rx *rx)
{
	struct bna_rx_mod *rx_mod = &rx->bna->rx_mod;
	struct bna_rxq *q0 = NULL;
	struct bna_rxq *q1 = NULL;
	struct bna_rxp *rxp;
	struct list_head *qe;

	bna_rxf_uninit(&rx->rxf);

	while (!list_empty(&rx->rxp_q)) {
		bfa_q_deq(&rx->rxp_q, &rxp);
		GET_RXQS(rxp, q0, q1);
		if (rx->rcb_destroy_cbfn)
			rx->rcb_destroy_cbfn(rx->bna->bnad, q0->rcb);
		q0->rcb = NULL;
		q0->rxp = NULL;
		q0->rx = NULL;
		bna_rxq_put(rx_mod, q0);

		if (q1) {
			if (rx->rcb_destroy_cbfn)
				rx->rcb_destroy_cbfn(rx->bna->bnad, q1->rcb);
			q1->rcb = NULL;
			q1->rxp = NULL;
			q1->rx = NULL;
			bna_rxq_put(rx_mod, q1);
		}
		rxp->rxq.slr.large = NULL;
		rxp->rxq.slr.small = NULL;

		if (rx->ccb_destroy_cbfn)
			rx->ccb_destroy_cbfn(rx->bna->bnad, rxp->cq.ccb);
		rxp->cq.ccb = NULL;
		rxp->rx = NULL;
		bna_rxp_put(rx_mod, rxp);
	}

	list_for_each(qe, &rx_mod->rx_active_q) {
		if (qe == &rx->qe) {
			list_del(&rx->qe);
			bfa_q_qe_init(&rx->qe);
			break;
		}
	}

	rx_mod->rid_mask &= ~(1 << rx->rid);

	rx->bna = NULL;
	rx->priv = NULL;
	bna_rx_put(rx_mod, rx);
}

void
bna_rx_enable(struct bna_rx *rx)
{
	if (rx->fsm != (bfa_sm_t)bna_rx_sm_stopped)
		return;

	rx->rx_flags |= BNA_RX_F_ENABLED;
	if (rx->rx_flags & BNA_RX_F_ENET_STARTED)
		bfa_fsm_send_event(rx, RX_E_START);
}

void
bna_rx_disable(struct bna_rx *rx, enum bna_cleanup_type type,
		void (*cbfn)(void *, struct bna_rx *))
{
	if (type == BNA_SOFT_CLEANUP) {
		/* h/w should not be accessed. Treat we're stopped */
		(*cbfn)(rx->bna->bnad, rx);
	} else {
		rx->stop_cbfn = cbfn;
		rx->stop_cbarg = rx->bna->bnad;

		rx->rx_flags &= ~BNA_RX_F_ENABLED;

		bfa_fsm_send_event(rx, RX_E_STOP);
	}
}

void
bna_rx_cleanup_complete(struct bna_rx *rx)
{
	bfa_fsm_send_event(rx, RX_E_CLEANUP_DONE);
}

enum bna_cb_status
bna_rx_mode_set(struct bna_rx *rx, enum bna_rxmode new_mode,
		enum bna_rxmode bitmask,
		void (*cbfn)(struct bnad *, struct bna_rx *))
{
	struct bna_rxf *rxf = &rx->rxf;
	int need_hw_config = 0;

	/* Error checks */

	if (is_promisc_enable(new_mode, bitmask)) {
		/* If promisc mode is already enabled elsewhere in the system */
		if ((rx->bna->promisc_rid != BFI_INVALID_RID) &&
			(rx->bna->promisc_rid != rxf->rx->rid))
			goto err_return;

		/* If default mode is already enabled in the system */
		if (rx->bna->default_mode_rid != BFI_INVALID_RID)
			goto err_return;

		/* Trying to enable promiscuous and default mode together */
		if (is_default_enable(new_mode, bitmask))
			goto err_return;
	}

	if (is_default_enable(new_mode, bitmask)) {
		/* If default mode is already enabled elsewhere in the system */
		if ((rx->bna->default_mode_rid != BFI_INVALID_RID) &&
			(rx->bna->default_mode_rid != rxf->rx->rid)) {
				goto err_return;
		}

		/* If promiscuous mode is already enabled in the system */
		if (rx->bna->promisc_rid != BFI_INVALID_RID)
			goto err_return;
	}

	/* Process the commands */

	if (is_promisc_enable(new_mode, bitmask)) {
		if (bna_rxf_promisc_enable(rxf))
			need_hw_config = 1;
	} else if (is_promisc_disable(new_mode, bitmask)) {
		if (bna_rxf_promisc_disable(rxf))
			need_hw_config = 1;
	}

	if (is_allmulti_enable(new_mode, bitmask)) {
		if (bna_rxf_allmulti_enable(rxf))
			need_hw_config = 1;
	} else if (is_allmulti_disable(new_mode, bitmask)) {
		if (bna_rxf_allmulti_disable(rxf))
			need_hw_config = 1;
	}

	/* Trigger h/w if needed */

	if (need_hw_config) {
		rxf->cam_fltr_cbfn = cbfn;
		rxf->cam_fltr_cbarg = rx->bna->bnad;
		bfa_fsm_send_event(rxf, RXF_E_CONFIG);
	} else if (cbfn)
		(*cbfn)(rx->bna->bnad, rx);

	return BNA_CB_SUCCESS;

err_return:
	return BNA_CB_FAIL;
}

void
bna_rx_vlanfilter_enable(struct bna_rx *rx)
{
	struct bna_rxf *rxf = &rx->rxf;

	if (rxf->vlan_filter_status == BNA_STATUS_T_DISABLED) {
		rxf->vlan_filter_status = BNA_STATUS_T_ENABLED;
		rxf->vlan_pending_bitmask = (u8)BFI_VLAN_BMASK_ALL;
		bfa_fsm_send_event(rxf, RXF_E_CONFIG);
	}
}

void
bna_rx_coalescing_timeo_set(struct bna_rx *rx, int coalescing_timeo)
{
	struct bna_rxp *rxp;
	struct list_head *qe;

	list_for_each(qe, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe;
		rxp->cq.ccb->rx_coalescing_timeo = coalescing_timeo;
		bna_ib_coalescing_timeo_set(&rxp->cq.ib, coalescing_timeo);
	}
}

void
bna_rx_dim_reconfig(struct bna *bna, const u32 vector[][BNA_BIAS_T_MAX])
{
	int i, j;

	for (i = 0; i < BNA_LOAD_T_MAX; i++)
		for (j = 0; j < BNA_BIAS_T_MAX; j++)
			bna->rx_mod.dim_vector[i][j] = vector[i][j];
}

void
bna_rx_dim_update(struct bna_ccb *ccb)
{
	struct bna *bna = ccb->cq->rx->bna;
	u32 load, bias;
	u32 pkt_rt, small_rt, large_rt;
	u8 coalescing_timeo;

	if ((ccb->pkt_rate.small_pkt_cnt == 0) &&
		(ccb->pkt_rate.large_pkt_cnt == 0))
		return;

	/* Arrive at preconfigured coalescing timeo value based on pkt rate */

	small_rt = ccb->pkt_rate.small_pkt_cnt;
	large_rt = ccb->pkt_rate.large_pkt_cnt;

	pkt_rt = small_rt + large_rt;

	if (pkt_rt < BNA_PKT_RATE_10K)
		load = BNA_LOAD_T_LOW_4;
	else if (pkt_rt < BNA_PKT_RATE_20K)
		load = BNA_LOAD_T_LOW_3;
	else if (pkt_rt < BNA_PKT_RATE_30K)
		load = BNA_LOAD_T_LOW_2;
	else if (pkt_rt < BNA_PKT_RATE_40K)
		load = BNA_LOAD_T_LOW_1;
	else if (pkt_rt < BNA_PKT_RATE_50K)
		load = BNA_LOAD_T_HIGH_1;
	else if (pkt_rt < BNA_PKT_RATE_60K)
		load = BNA_LOAD_T_HIGH_2;
	else if (pkt_rt < BNA_PKT_RATE_80K)
		load = BNA_LOAD_T_HIGH_3;
	else
		load = BNA_LOAD_T_HIGH_4;

	if (small_rt > (large_rt << 1))
		bias = 0;
	else
		bias = 1;

	ccb->pkt_rate.small_pkt_cnt = 0;
	ccb->pkt_rate.large_pkt_cnt = 0;

	coalescing_timeo = bna->rx_mod.dim_vector[load][bias];
	ccb->rx_coalescing_timeo = coalescing_timeo;

	/* Set it to IB */
	bna_ib_coalescing_timeo_set(&ccb->cq->ib, coalescing_timeo);
}

const u32 bna_napi_dim_vector[BNA_LOAD_T_MAX][BNA_BIAS_T_MAX] = {
	{12, 12},
	{6, 10},
	{5, 10},
	{4, 8},
	{3, 6},
	{3, 6},
	{2, 4},
	{1, 2},
};

/* TX */

#define call_tx_stop_cbfn(tx)						\
do {									\
	if ((tx)->stop_cbfn) {						\
		void (*cbfn)(void *, struct bna_tx *);		\
		void *cbarg;						\
		cbfn = (tx)->stop_cbfn;					\
		cbarg = (tx)->stop_cbarg;				\
		(tx)->stop_cbfn = NULL;					\
		(tx)->stop_cbarg = NULL;				\
		cbfn(cbarg, (tx));					\
	}								\
} while (0)

#define call_tx_prio_change_cbfn(tx)					\
do {									\
	if ((tx)->prio_change_cbfn) {					\
		void (*cbfn)(struct bnad *, struct bna_tx *);	\
		cbfn = (tx)->prio_change_cbfn;				\
		(tx)->prio_change_cbfn = NULL;				\
		cbfn((tx)->bna->bnad, (tx));				\
	}								\
} while (0)

static void bna_tx_mod_cb_tx_stopped(void *tx_mod, struct bna_tx *tx);
static void bna_bfi_tx_enet_start(struct bna_tx *tx);
static void bna_tx_enet_stop(struct bna_tx *tx);

enum bna_tx_event {
	TX_E_START			= 1,
	TX_E_STOP			= 2,
	TX_E_FAIL			= 3,
	TX_E_STARTED			= 4,
	TX_E_STOPPED			= 5,
	TX_E_PRIO_CHANGE		= 6,
	TX_E_CLEANUP_DONE		= 7,
	TX_E_BW_UPDATE			= 8,
};

bfa_fsm_state_decl(bna_tx, stopped, struct bna_tx, enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, start_wait, struct bna_tx, enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, started, struct bna_tx, enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, stop_wait, struct bna_tx, enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, cleanup_wait, struct bna_tx,
			enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, prio_stop_wait, struct bna_tx,
			enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, prio_cleanup_wait, struct bna_tx,
			enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, failed, struct bna_tx, enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, quiesce_wait, struct bna_tx,
			enum bna_tx_event);

static void
bna_tx_sm_stopped_entry(struct bna_tx *tx)
{
	call_tx_stop_cbfn(tx);
}

static void
bna_tx_sm_stopped(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_START:
		bfa_fsm_set_state(tx, bna_tx_sm_start_wait);
		break;

	case TX_E_STOP:
		call_tx_stop_cbfn(tx);
		break;

	case TX_E_FAIL:
		/* No-op */
		break;

	case TX_E_PRIO_CHANGE:
		call_tx_prio_change_cbfn(tx);
		break;

	case TX_E_BW_UPDATE:
		/* No-op */
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_tx_sm_start_wait_entry(struct bna_tx *tx)
{
	bna_bfi_tx_enet_start(tx);
}

static void
bna_tx_sm_start_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_STOP:
		tx->flags &= ~(BNA_TX_F_PRIO_CHANGED | BNA_TX_F_BW_UPDATED);
		bfa_fsm_set_state(tx, bna_tx_sm_stop_wait);
		break;

	case TX_E_FAIL:
		tx->flags &= ~(BNA_TX_F_PRIO_CHANGED | BNA_TX_F_BW_UPDATED);
		bfa_fsm_set_state(tx, bna_tx_sm_stopped);
		break;

	case TX_E_STARTED:
		if (tx->flags & (BNA_TX_F_PRIO_CHANGED | BNA_TX_F_BW_UPDATED)) {
			tx->flags &= ~(BNA_TX_F_PRIO_CHANGED |
				BNA_TX_F_BW_UPDATED);
			bfa_fsm_set_state(tx, bna_tx_sm_prio_stop_wait);
		} else
			bfa_fsm_set_state(tx, bna_tx_sm_started);
		break;

	case TX_E_PRIO_CHANGE:
		tx->flags |=  BNA_TX_F_PRIO_CHANGED;
		break;

	case TX_E_BW_UPDATE:
		tx->flags |= BNA_TX_F_BW_UPDATED;
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_tx_sm_started_entry(struct bna_tx *tx)
{
	struct bna_txq *txq;
	struct list_head		 *qe;
	int is_regular = (tx->type == BNA_TX_T_REGULAR);

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		txq->tcb->priority = txq->priority;
		/* Start IB */
		bna_ib_start(tx->bna, &txq->ib, is_regular);
	}
	tx->tx_resume_cbfn(tx->bna->bnad, tx);
}

static void
bna_tx_sm_started(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_STOP:
		bfa_fsm_set_state(tx, bna_tx_sm_stop_wait);
		tx->tx_stall_cbfn(tx->bna->bnad, tx);
		bna_tx_enet_stop(tx);
		break;

	case TX_E_FAIL:
		bfa_fsm_set_state(tx, bna_tx_sm_failed);
		tx->tx_stall_cbfn(tx->bna->bnad, tx);
		tx->tx_cleanup_cbfn(tx->bna->bnad, tx);
		break;

	case TX_E_PRIO_CHANGE:
	case TX_E_BW_UPDATE:
		bfa_fsm_set_state(tx, bna_tx_sm_prio_stop_wait);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_tx_sm_stop_wait_entry(struct bna_tx *tx)
{
}

static void
bna_tx_sm_stop_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_FAIL:
	case TX_E_STOPPED:
		bfa_fsm_set_state(tx, bna_tx_sm_cleanup_wait);
		tx->tx_cleanup_cbfn(tx->bna->bnad, tx);
		break;

	case TX_E_STARTED:
		/**
		 * We are here due to start_wait -> stop_wait transition on
		 * TX_E_STOP event
		 */
		bna_tx_enet_stop(tx);
		break;

	case TX_E_PRIO_CHANGE:
	case TX_E_BW_UPDATE:
		/* No-op */
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_tx_sm_cleanup_wait_entry(struct bna_tx *tx)
{
}

static void
bna_tx_sm_cleanup_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_FAIL:
	case TX_E_PRIO_CHANGE:
	case TX_E_BW_UPDATE:
		/* No-op */
		break;

	case TX_E_CLEANUP_DONE:
		bfa_fsm_set_state(tx, bna_tx_sm_stopped);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_tx_sm_prio_stop_wait_entry(struct bna_tx *tx)
{
	tx->tx_stall_cbfn(tx->bna->bnad, tx);
	bna_tx_enet_stop(tx);
}

static void
bna_tx_sm_prio_stop_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_STOP:
		bfa_fsm_set_state(tx, bna_tx_sm_stop_wait);
		break;

	case TX_E_FAIL:
		bfa_fsm_set_state(tx, bna_tx_sm_failed);
		call_tx_prio_change_cbfn(tx);
		tx->tx_cleanup_cbfn(tx->bna->bnad, tx);
		break;

	case TX_E_STOPPED:
		bfa_fsm_set_state(tx, bna_tx_sm_prio_cleanup_wait);
		break;

	case TX_E_PRIO_CHANGE:
	case TX_E_BW_UPDATE:
		/* No-op */
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_tx_sm_prio_cleanup_wait_entry(struct bna_tx *tx)
{
	call_tx_prio_change_cbfn(tx);
	tx->tx_cleanup_cbfn(tx->bna->bnad, tx);
}

static void
bna_tx_sm_prio_cleanup_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_STOP:
		bfa_fsm_set_state(tx, bna_tx_sm_cleanup_wait);
		break;

	case TX_E_FAIL:
		bfa_fsm_set_state(tx, bna_tx_sm_failed);
		break;

	case TX_E_PRIO_CHANGE:
	case TX_E_BW_UPDATE:
		/* No-op */
		break;

	case TX_E_CLEANUP_DONE:
		bfa_fsm_set_state(tx, bna_tx_sm_start_wait);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_tx_sm_failed_entry(struct bna_tx *tx)
{
}

static void
bna_tx_sm_failed(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_START:
		bfa_fsm_set_state(tx, bna_tx_sm_quiesce_wait);
		break;

	case TX_E_STOP:
		bfa_fsm_set_state(tx, bna_tx_sm_cleanup_wait);
		break;

	case TX_E_FAIL:
		/* No-op */
		break;

	case TX_E_CLEANUP_DONE:
		bfa_fsm_set_state(tx, bna_tx_sm_stopped);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_tx_sm_quiesce_wait_entry(struct bna_tx *tx)
{
}

static void
bna_tx_sm_quiesce_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_STOP:
		bfa_fsm_set_state(tx, bna_tx_sm_cleanup_wait);
		break;

	case TX_E_FAIL:
		bfa_fsm_set_state(tx, bna_tx_sm_failed);
		break;

	case TX_E_CLEANUP_DONE:
		bfa_fsm_set_state(tx, bna_tx_sm_start_wait);
		break;

	case TX_E_BW_UPDATE:
		/* No-op */
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_bfi_tx_enet_start(struct bna_tx *tx)
{
	struct bfi_enet_tx_cfg_req *cfg_req = &tx->bfi_enet_cmd.cfg_req;
	struct bna_txq *txq = NULL;
	struct list_head *qe;
	int i;

	bfi_msgq_mhdr_set(cfg_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_TX_CFG_SET_REQ, 0, tx->rid);
	cfg_req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_tx_cfg_req)));

	cfg_req->num_queues = tx->num_txq;
	for (i = 0, qe = bfa_q_first(&tx->txq_q);
		i < tx->num_txq;
		i++, qe = bfa_q_next(qe)) {
		txq = (struct bna_txq *)qe;

		bfi_enet_datapath_q_init(&cfg_req->q_cfg[i].q.q, &txq->qpt);
		cfg_req->q_cfg[i].q.priority = txq->priority;

		cfg_req->q_cfg[i].ib.index_addr.a32.addr_lo =
			txq->ib.ib_seg_host_addr.lsb;
		cfg_req->q_cfg[i].ib.index_addr.a32.addr_hi =
			txq->ib.ib_seg_host_addr.msb;
		cfg_req->q_cfg[i].ib.intr.msix_index =
			htons((u16)txq->ib.intr_vector);
	}

	cfg_req->ib_cfg.int_pkt_dma = BNA_STATUS_T_ENABLED;
	cfg_req->ib_cfg.int_enabled = BNA_STATUS_T_ENABLED;
	cfg_req->ib_cfg.int_pkt_enabled = BNA_STATUS_T_DISABLED;
	cfg_req->ib_cfg.continuous_coalescing = BNA_STATUS_T_ENABLED;
	cfg_req->ib_cfg.msix = (txq->ib.intr_type == BNA_INTR_T_MSIX)
				? BNA_STATUS_T_ENABLED : BNA_STATUS_T_DISABLED;
	cfg_req->ib_cfg.coalescing_timeout =
			htonl((u32)txq->ib.coalescing_timeo);
	cfg_req->ib_cfg.inter_pkt_timeout =
			htonl((u32)txq->ib.interpkt_timeo);
	cfg_req->ib_cfg.inter_pkt_count = (u8)txq->ib.interpkt_count;

	cfg_req->tx_cfg.vlan_mode = BFI_ENET_TX_VLAN_WI;
	cfg_req->tx_cfg.vlan_id = htons((u16)tx->txf_vlan_id);
	cfg_req->tx_cfg.admit_tagged_frame = BNA_STATUS_T_DISABLED;
	cfg_req->tx_cfg.apply_vlan_filter = BNA_STATUS_T_DISABLED;

	bfa_msgq_cmd_set(&tx->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_tx_cfg_req), &cfg_req->mh);
	bfa_msgq_cmd_post(&tx->bna->msgq, &tx->msgq_cmd);
}

static void
bna_bfi_tx_enet_stop(struct bna_tx *tx)
{
	struct bfi_enet_req *req = &tx->bfi_enet_cmd.req;

	bfi_msgq_mhdr_set(req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_TX_CFG_CLR_REQ, 0, tx->rid);
	req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_req)));
	bfa_msgq_cmd_set(&tx->msgq_cmd, NULL, NULL, sizeof(struct bfi_enet_req),
		&req->mh);
	bfa_msgq_cmd_post(&tx->bna->msgq, &tx->msgq_cmd);
}

static void
bna_tx_enet_stop(struct bna_tx *tx)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	/* Stop IB */
	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		bna_ib_stop(tx->bna, &txq->ib);
	}

	bna_bfi_tx_enet_stop(tx);
}

static void
bna_txq_qpt_setup(struct bna_txq *txq, int page_count, int page_size,
		struct bna_mem_descr *qpt_mem,
		struct bna_mem_descr *swqpt_mem,
		struct bna_mem_descr *page_mem)
{
	int i;

	txq->qpt.hw_qpt_ptr.lsb = qpt_mem->dma.lsb;
	txq->qpt.hw_qpt_ptr.msb = qpt_mem->dma.msb;
	txq->qpt.kv_qpt_ptr = qpt_mem->kva;
	txq->qpt.page_count = page_count;
	txq->qpt.page_size = page_size;

	txq->tcb->sw_qpt = (void **) swqpt_mem->kva;

	for (i = 0; i < page_count; i++) {
		txq->tcb->sw_qpt[i] = page_mem[i].kva;

		((struct bna_dma_addr *)txq->qpt.kv_qpt_ptr)[i].lsb =
			page_mem[i].dma.lsb;
		((struct bna_dma_addr *)txq->qpt.kv_qpt_ptr)[i].msb =
			page_mem[i].dma.msb;
	}
}

static struct bna_tx *
bna_tx_get(struct bna_tx_mod *tx_mod, enum bna_tx_type type)
{
	struct list_head	*qe = NULL;
	struct bna_tx *tx = NULL;

	if (list_empty(&tx_mod->tx_free_q))
		return NULL;
	if (type == BNA_TX_T_REGULAR) {
		bfa_q_deq(&tx_mod->tx_free_q, &qe);
	} else {
		bfa_q_deq_tail(&tx_mod->tx_free_q, &qe);
	}
	tx = (struct bna_tx *)qe;
	bfa_q_qe_init(&tx->qe);
	tx->type = type;

	return tx;
}

static void
bna_tx_free(struct bna_tx *tx)
{
	struct bna_tx_mod *tx_mod = &tx->bna->tx_mod;
	struct bna_txq *txq;
	struct list_head *prev_qe;
	struct list_head *qe;

	while (!list_empty(&tx->txq_q)) {
		bfa_q_deq(&tx->txq_q, &txq);
		bfa_q_qe_init(&txq->qe);
		txq->tcb = NULL;
		txq->tx = NULL;
		list_add_tail(&txq->qe, &tx_mod->txq_free_q);
	}

	list_for_each(qe, &tx_mod->tx_active_q) {
		if (qe == &tx->qe) {
			list_del(&tx->qe);
			bfa_q_qe_init(&tx->qe);
			break;
		}
	}

	tx->bna = NULL;
	tx->priv = NULL;

	prev_qe = NULL;
	list_for_each(qe, &tx_mod->tx_free_q) {
		if (((struct bna_tx *)qe)->rid < tx->rid)
			prev_qe = qe;
		else {
			break;
		}
	}

	if (prev_qe == NULL) {
		/* This is the first entry */
		bfa_q_enq_head(&tx_mod->tx_free_q, &tx->qe);
	} else if (bfa_q_next(prev_qe) == &tx_mod->tx_free_q) {
		/* This is the last entry */
		list_add_tail(&tx->qe, &tx_mod->tx_free_q);
	} else {
		/* Somewhere in the middle */
		bfa_q_next(&tx->qe) = bfa_q_next(prev_qe);
		bfa_q_prev(&tx->qe) = prev_qe;
		bfa_q_next(prev_qe) = &tx->qe;
		bfa_q_prev(bfa_q_next(&tx->qe)) = &tx->qe;
	}
}

static void
bna_tx_start(struct bna_tx *tx)
{
	tx->flags |= BNA_TX_F_ENET_STARTED;
	if (tx->flags & BNA_TX_F_ENABLED)
		bfa_fsm_send_event(tx, TX_E_START);
}

static void
bna_tx_stop(struct bna_tx *tx)
{
	tx->stop_cbfn = bna_tx_mod_cb_tx_stopped;
	tx->stop_cbarg = &tx->bna->tx_mod;

	tx->flags &= ~BNA_TX_F_ENET_STARTED;
	bfa_fsm_send_event(tx, TX_E_STOP);
}

static void
bna_tx_fail(struct bna_tx *tx)
{
	tx->flags &= ~BNA_TX_F_ENET_STARTED;
	bfa_fsm_send_event(tx, TX_E_FAIL);
}

void
bna_bfi_tx_enet_start_rsp(struct bna_tx *tx, struct bfi_msgq_mhdr *msghdr)
{
	struct bfi_enet_tx_cfg_rsp *cfg_rsp = &tx->bfi_enet_cmd.cfg_rsp;
	struct bna_txq *txq = NULL;
	struct list_head *qe;
	int i;

	bfa_msgq_rsp_copy(&tx->bna->msgq, (u8 *)cfg_rsp,
		sizeof(struct bfi_enet_tx_cfg_rsp));

	tx->hw_id = cfg_rsp->hw_id;

	for (i = 0, qe = bfa_q_first(&tx->txq_q);
		i < tx->num_txq; i++, qe = bfa_q_next(qe)) {
		txq = (struct bna_txq *)qe;

		/* Setup doorbells */
		txq->tcb->i_dbell->doorbell_addr =
			tx->bna->pcidev.pci_bar_kva
			+ ntohl(cfg_rsp->q_handles[i].i_dbell);
		txq->tcb->q_dbell =
			tx->bna->pcidev.pci_bar_kva
			+ ntohl(cfg_rsp->q_handles[i].q_dbell);
		txq->hw_id = cfg_rsp->q_handles[i].hw_qid;

		/* Initialize producer/consumer indexes */
		(*txq->tcb->hw_consumer_index) = 0;
		txq->tcb->producer_index = txq->tcb->consumer_index = 0;
	}

	bfa_fsm_send_event(tx, TX_E_STARTED);
}

void
bna_bfi_tx_enet_stop_rsp(struct bna_tx *tx, struct bfi_msgq_mhdr *msghdr)
{
	bfa_fsm_send_event(tx, TX_E_STOPPED);
}

void
bna_bfi_bw_update_aen(struct bna_tx_mod *tx_mod)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		bfa_fsm_send_event(tx, TX_E_BW_UPDATE);
	}
}

void
bna_tx_res_req(int num_txq, int txq_depth, struct bna_res_info *res_info)
{
	u32 q_size;
	u32 page_count;
	struct bna_mem_info *mem_info;

	res_info[BNA_TX_RES_MEM_T_TCB].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_TCB].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = sizeof(struct bna_tcb);
	mem_info->num = num_txq;

	q_size = txq_depth * BFI_TXQ_WI_SIZE;
	q_size = ALIGN(q_size, PAGE_SIZE);
	page_count = q_size >> PAGE_SHIFT;

	res_info[BNA_TX_RES_MEM_T_QPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_QPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = page_count * sizeof(struct bna_dma_addr);
	mem_info->num = num_txq;

	res_info[BNA_TX_RES_MEM_T_SWQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_SWQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = page_count * sizeof(void *);
	mem_info->num = num_txq;

	res_info[BNA_TX_RES_MEM_T_PAGE].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_PAGE].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = PAGE_SIZE;
	mem_info->num = num_txq * page_count;

	res_info[BNA_TX_RES_MEM_T_IBIDX].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_IBIDX].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = BFI_IBIDX_SIZE;
	mem_info->num = num_txq;

	res_info[BNA_TX_RES_INTR_T_TXCMPL].res_type = BNA_RES_T_INTR;
	res_info[BNA_TX_RES_INTR_T_TXCMPL].res_u.intr_info.intr_type =
			BNA_INTR_T_MSIX;
	res_info[BNA_TX_RES_INTR_T_TXCMPL].res_u.intr_info.num = num_txq;
}

struct bna_tx *
bna_tx_create(struct bna *bna, struct bnad *bnad,
		struct bna_tx_config *tx_cfg,
		const struct bna_tx_event_cbfn *tx_cbfn,
		struct bna_res_info *res_info, void *priv)
{
	struct bna_intr_info *intr_info;
	struct bna_tx_mod *tx_mod = &bna->tx_mod;
	struct bna_tx *tx;
	struct bna_txq *txq;
	struct list_head *qe;
	int page_count;
	int page_size;
	int page_idx;
	int i;

	intr_info = &res_info[BNA_TX_RES_INTR_T_TXCMPL].res_u.intr_info;
	page_count = (res_info[BNA_TX_RES_MEM_T_PAGE].res_u.mem_info.num) /
			tx_cfg->num_txq;
	page_size = res_info[BNA_TX_RES_MEM_T_PAGE].res_u.mem_info.len;

	/**
	 * Get resources
	 */

	if ((intr_info->num != 1) && (intr_info->num != tx_cfg->num_txq))
		return NULL;

	/* Tx */

	tx = bna_tx_get(tx_mod, tx_cfg->tx_type);
	if (!tx)
		return NULL;
	tx->bna = bna;
	tx->priv = priv;

	/* TxQs */

	INIT_LIST_HEAD(&tx->txq_q);
	for (i = 0; i < tx_cfg->num_txq; i++) {
		if (list_empty(&tx_mod->txq_free_q))
			goto err_return;

		bfa_q_deq(&tx_mod->txq_free_q, &txq);
		bfa_q_qe_init(&txq->qe);
		list_add_tail(&txq->qe, &tx->txq_q);
		txq->tx = tx;
	}

	/*
	 * Initialize
	 */

	/* Tx */

	tx->tcb_setup_cbfn = tx_cbfn->tcb_setup_cbfn;
	tx->tcb_destroy_cbfn = tx_cbfn->tcb_destroy_cbfn;
	/* Following callbacks are mandatory */
	tx->tx_stall_cbfn = tx_cbfn->tx_stall_cbfn;
	tx->tx_resume_cbfn = tx_cbfn->tx_resume_cbfn;
	tx->tx_cleanup_cbfn = tx_cbfn->tx_cleanup_cbfn;

	list_add_tail(&tx->qe, &tx_mod->tx_active_q);

	tx->num_txq = tx_cfg->num_txq;

	tx->flags = 0;
	if (tx->bna->tx_mod.flags & BNA_TX_MOD_F_ENET_STARTED) {
		switch (tx->type) {
		case BNA_TX_T_REGULAR:
			if (!(tx->bna->tx_mod.flags &
				BNA_TX_MOD_F_ENET_LOOPBACK))
				tx->flags |= BNA_TX_F_ENET_STARTED;
			break;
		case BNA_TX_T_LOOPBACK:
			if (tx->bna->tx_mod.flags & BNA_TX_MOD_F_ENET_LOOPBACK)
				tx->flags |= BNA_TX_F_ENET_STARTED;
			break;
		}
	}

	/* TxQ */

	i = 0;
	page_idx = 0;
	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		txq->tcb = (struct bna_tcb *)
		res_info[BNA_TX_RES_MEM_T_TCB].res_u.mem_info.mdl[i].kva;
		txq->tx_packets = 0;
		txq->tx_bytes = 0;

		/* IB */
		txq->ib.ib_seg_host_addr.lsb =
		res_info[BNA_TX_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].dma.lsb;
		txq->ib.ib_seg_host_addr.msb =
		res_info[BNA_TX_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].dma.msb;
		txq->ib.ib_seg_host_addr_kva =
		res_info[BNA_TX_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].kva;
		txq->ib.intr_type = intr_info->intr_type;
		txq->ib.intr_vector = (intr_info->num == 1) ?
					intr_info->idl[0].vector :
					intr_info->idl[i].vector;
		if (intr_info->intr_type == BNA_INTR_T_INTX)
			txq->ib.intr_vector = (1 <<  txq->ib.intr_vector);
		txq->ib.coalescing_timeo = tx_cfg->coalescing_timeo;
		txq->ib.interpkt_timeo = 0; /* Not used */
		txq->ib.interpkt_count = BFI_TX_INTERPKT_COUNT;

		/* TCB */

		txq->tcb->q_depth = tx_cfg->txq_depth;
		txq->tcb->unmap_q = (void *)
		res_info[BNA_TX_RES_MEM_T_UNMAPQ].res_u.mem_info.mdl[i].kva;
		txq->tcb->hw_consumer_index =
			(u32 *)txq->ib.ib_seg_host_addr_kva;
		txq->tcb->i_dbell = &txq->ib.door_bell;
		txq->tcb->intr_type = txq->ib.intr_type;
		txq->tcb->intr_vector = txq->ib.intr_vector;
		txq->tcb->txq = txq;
		txq->tcb->bnad = bnad;
		txq->tcb->id = i;

		/* QPT, SWQPT, Pages */
		bna_txq_qpt_setup(txq, page_count, page_size,
			&res_info[BNA_TX_RES_MEM_T_QPT].res_u.mem_info.mdl[i],
			&res_info[BNA_TX_RES_MEM_T_SWQPT].res_u.mem_info.mdl[i],
			&res_info[BNA_TX_RES_MEM_T_PAGE].
				  res_u.mem_info.mdl[page_idx]);
		txq->tcb->page_idx = page_idx;
		txq->tcb->page_count = page_count;
		page_idx += page_count;

		/* Callback to bnad for setting up TCB */
		if (tx->tcb_setup_cbfn)
			(tx->tcb_setup_cbfn)(bna->bnad, txq->tcb);

		if (tx_cfg->num_txq == BFI_TX_MAX_PRIO)
			txq->priority = txq->tcb->id;
		else
			txq->priority = tx_mod->default_prio;

		i++;
	}

	tx->txf_vlan_id = 0;

	bfa_fsm_set_state(tx, bna_tx_sm_stopped);

	tx_mod->rid_mask |= (1 << tx->rid);

	return tx;

err_return:
	bna_tx_free(tx);
	return NULL;
}

void
bna_tx_destroy(struct bna_tx *tx)
{
	struct bna_txq *txq;
	struct list_head *qe;

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		if (tx->tcb_destroy_cbfn)
			(tx->tcb_destroy_cbfn)(tx->bna->bnad, txq->tcb);
	}

	tx->bna->tx_mod.rid_mask &= ~(1 << tx->rid);
	bna_tx_free(tx);
}

void
bna_tx_enable(struct bna_tx *tx)
{
	if (tx->fsm != (bfa_sm_t)bna_tx_sm_stopped)
		return;

	tx->flags |= BNA_TX_F_ENABLED;

	if (tx->flags & BNA_TX_F_ENET_STARTED)
		bfa_fsm_send_event(tx, TX_E_START);
}

void
bna_tx_disable(struct bna_tx *tx, enum bna_cleanup_type type,
		void (*cbfn)(void *, struct bna_tx *))
{
	if (type == BNA_SOFT_CLEANUP) {
		(*cbfn)(tx->bna->bnad, tx);
		return;
	}

	tx->stop_cbfn = cbfn;
	tx->stop_cbarg = tx->bna->bnad;

	tx->flags &= ~BNA_TX_F_ENABLED;

	bfa_fsm_send_event(tx, TX_E_STOP);
}

void
bna_tx_cleanup_complete(struct bna_tx *tx)
{
	bfa_fsm_send_event(tx, TX_E_CLEANUP_DONE);
}

static void
bna_tx_mod_cb_tx_stopped(void *arg, struct bna_tx *tx)
{
	struct bna_tx_mod *tx_mod = (struct bna_tx_mod *)arg;

	bfa_wc_down(&tx_mod->tx_stop_wc);
}

static void
bna_tx_mod_cb_tx_stopped_all(void *arg)
{
	struct bna_tx_mod *tx_mod = (struct bna_tx_mod *)arg;

	if (tx_mod->stop_cbfn)
		tx_mod->stop_cbfn(&tx_mod->bna->enet);
	tx_mod->stop_cbfn = NULL;
}

void
bna_tx_mod_init(struct bna_tx_mod *tx_mod, struct bna *bna,
		struct bna_res_info *res_info)
{
	int i;

	tx_mod->bna = bna;
	tx_mod->flags = 0;

	tx_mod->tx = (struct bna_tx *)
		res_info[BNA_MOD_RES_MEM_T_TX_ARRAY].res_u.mem_info.mdl[0].kva;
	tx_mod->txq = (struct bna_txq *)
		res_info[BNA_MOD_RES_MEM_T_TXQ_ARRAY].res_u.mem_info.mdl[0].kva;

	INIT_LIST_HEAD(&tx_mod->tx_free_q);
	INIT_LIST_HEAD(&tx_mod->tx_active_q);

	INIT_LIST_HEAD(&tx_mod->txq_free_q);

	for (i = 0; i < bna->ioceth.attr.num_txq; i++) {
		tx_mod->tx[i].rid = i;
		bfa_q_qe_init(&tx_mod->tx[i].qe);
		list_add_tail(&tx_mod->tx[i].qe, &tx_mod->tx_free_q);
		bfa_q_qe_init(&tx_mod->txq[i].qe);
		list_add_tail(&tx_mod->txq[i].qe, &tx_mod->txq_free_q);
	}

	tx_mod->prio_map = BFI_TX_PRIO_MAP_ALL;
	tx_mod->default_prio = 0;
	tx_mod->iscsi_over_cee = BNA_STATUS_T_DISABLED;
	tx_mod->iscsi_prio = -1;
}

void
bna_tx_mod_uninit(struct bna_tx_mod *tx_mod)
{
	struct list_head		*qe;
	int i;

	i = 0;
	list_for_each(qe, &tx_mod->tx_free_q)
		i++;

	i = 0;
	list_for_each(qe, &tx_mod->txq_free_q)
		i++;

	tx_mod->bna = NULL;
}

void
bna_tx_mod_start(struct bna_tx_mod *tx_mod, enum bna_tx_type type)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	tx_mod->flags |= BNA_TX_MOD_F_ENET_STARTED;
	if (type == BNA_TX_T_LOOPBACK)
		tx_mod->flags |= BNA_TX_MOD_F_ENET_LOOPBACK;

	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		if (tx->type == type)
			bna_tx_start(tx);
	}
}

void
bna_tx_mod_stop(struct bna_tx_mod *tx_mod, enum bna_tx_type type)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	tx_mod->flags &= ~BNA_TX_MOD_F_ENET_STARTED;
	tx_mod->flags &= ~BNA_TX_MOD_F_ENET_LOOPBACK;

	tx_mod->stop_cbfn = bna_enet_cb_tx_stopped;

	bfa_wc_init(&tx_mod->tx_stop_wc, bna_tx_mod_cb_tx_stopped_all, tx_mod);

	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		if (tx->type == type) {
			bfa_wc_up(&tx_mod->tx_stop_wc);
			bna_tx_stop(tx);
		}
	}

	bfa_wc_wait(&tx_mod->tx_stop_wc);
}

void
bna_tx_mod_fail(struct bna_tx_mod *tx_mod)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	tx_mod->flags &= ~BNA_TX_MOD_F_ENET_STARTED;
	tx_mod->flags &= ~BNA_TX_MOD_F_ENET_LOOPBACK;

	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		bna_tx_fail(tx);
	}
}

void
bna_tx_coalescing_timeo_set(struct bna_tx *tx, int coalescing_timeo)
{
	struct bna_txq *txq;
	struct list_head *qe;

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		bna_ib_coalescing_timeo_set(&txq->ib, coalescing_timeo);
	}
}
