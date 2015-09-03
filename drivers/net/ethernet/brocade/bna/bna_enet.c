/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
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
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */
#include "bna.h"

static inline int
ethport_can_be_up(struct bna_ethport *ethport)
{
	int ready = 0;
	if (ethport->bna->enet.type == BNA_ENET_T_REGULAR)
		ready = ((ethport->flags & BNA_ETHPORT_F_ADMIN_UP) &&
			 (ethport->flags & BNA_ETHPORT_F_RX_STARTED) &&
			 (ethport->flags & BNA_ETHPORT_F_PORT_ENABLED));
	else
		ready = ((ethport->flags & BNA_ETHPORT_F_ADMIN_UP) &&
			 (ethport->flags & BNA_ETHPORT_F_RX_STARTED) &&
			 !(ethport->flags & BNA_ETHPORT_F_PORT_ENABLED));
	return ready;
}

#define ethport_is_up ethport_can_be_up

enum bna_ethport_event {
	ETHPORT_E_START			= 1,
	ETHPORT_E_STOP			= 2,
	ETHPORT_E_FAIL			= 3,
	ETHPORT_E_UP			= 4,
	ETHPORT_E_DOWN			= 5,
	ETHPORT_E_FWRESP_UP_OK		= 6,
	ETHPORT_E_FWRESP_DOWN		= 7,
	ETHPORT_E_FWRESP_UP_FAIL	= 8,
};

enum bna_enet_event {
	ENET_E_START			= 1,
	ENET_E_STOP			= 2,
	ENET_E_FAIL			= 3,
	ENET_E_PAUSE_CFG		= 4,
	ENET_E_MTU_CFG			= 5,
	ENET_E_FWRESP_PAUSE		= 6,
	ENET_E_CHLD_STOPPED		= 7,
};

enum bna_ioceth_event {
	IOCETH_E_ENABLE			= 1,
	IOCETH_E_DISABLE		= 2,
	IOCETH_E_IOC_RESET		= 3,
	IOCETH_E_IOC_FAILED		= 4,
	IOCETH_E_IOC_READY		= 5,
	IOCETH_E_ENET_ATTR_RESP		= 6,
	IOCETH_E_ENET_STOPPED		= 7,
	IOCETH_E_IOC_DISABLED		= 8,
};

#define bna_stats_copy(_name, _type)					\
do {									\
	count = sizeof(struct bfi_enet_stats_ ## _type) / sizeof(u64);	\
	stats_src = (u64 *)&bna->stats.hw_stats_kva->_name ## _stats;	\
	stats_dst = (u64 *)&bna->stats.hw_stats._name ## _stats;	\
	for (i = 0; i < count; i++)					\
		stats_dst[i] = be64_to_cpu(stats_src[i]);		\
} while (0)								\

/*
 * FW response handlers
 */

static void
bna_bfi_ethport_enable_aen(struct bna_ethport *ethport,
				struct bfi_msgq_mhdr *msghdr)
{
	ethport->flags |= BNA_ETHPORT_F_PORT_ENABLED;

	if (ethport_can_be_up(ethport))
		bfa_fsm_send_event(ethport, ETHPORT_E_UP);
}

static void
bna_bfi_ethport_disable_aen(struct bna_ethport *ethport,
				struct bfi_msgq_mhdr *msghdr)
{
	int ethport_up = ethport_is_up(ethport);

	ethport->flags &= ~BNA_ETHPORT_F_PORT_ENABLED;

	if (ethport_up)
		bfa_fsm_send_event(ethport, ETHPORT_E_DOWN);
}

static void
bna_bfi_ethport_admin_rsp(struct bna_ethport *ethport,
				struct bfi_msgq_mhdr *msghdr)
{
	struct bfi_enet_enable_req *admin_req =
		&ethport->bfi_enet_cmd.admin_req;
	struct bfi_enet_rsp *rsp =
		container_of(msghdr, struct bfi_enet_rsp, mh);

	switch (admin_req->enable) {
	case BNA_STATUS_T_ENABLED:
		if (rsp->error == BFI_ENET_CMD_OK)
			bfa_fsm_send_event(ethport, ETHPORT_E_FWRESP_UP_OK);
		else {
			ethport->flags &= ~BNA_ETHPORT_F_PORT_ENABLED;
			bfa_fsm_send_event(ethport, ETHPORT_E_FWRESP_UP_FAIL);
		}
		break;

	case BNA_STATUS_T_DISABLED:
		bfa_fsm_send_event(ethport, ETHPORT_E_FWRESP_DOWN);
		ethport->link_status = BNA_LINK_DOWN;
		ethport->link_cbfn(ethport->bna->bnad, BNA_LINK_DOWN);
		break;
	}
}

static void
bna_bfi_ethport_lpbk_rsp(struct bna_ethport *ethport,
				struct bfi_msgq_mhdr *msghdr)
{
	struct bfi_enet_diag_lb_req *diag_lb_req =
		&ethport->bfi_enet_cmd.lpbk_req;
	struct bfi_enet_rsp *rsp =
		container_of(msghdr, struct bfi_enet_rsp, mh);

	switch (diag_lb_req->enable) {
	case BNA_STATUS_T_ENABLED:
		if (rsp->error == BFI_ENET_CMD_OK)
			bfa_fsm_send_event(ethport, ETHPORT_E_FWRESP_UP_OK);
		else {
			ethport->flags &= ~BNA_ETHPORT_F_ADMIN_UP;
			bfa_fsm_send_event(ethport, ETHPORT_E_FWRESP_UP_FAIL);
		}
		break;

	case BNA_STATUS_T_DISABLED:
		bfa_fsm_send_event(ethport, ETHPORT_E_FWRESP_DOWN);
		break;
	}
}

static void
bna_bfi_pause_set_rsp(struct bna_enet *enet, struct bfi_msgq_mhdr *msghdr)
{
	bfa_fsm_send_event(enet, ENET_E_FWRESP_PAUSE);
}

static void
bna_bfi_attr_get_rsp(struct bna_ioceth *ioceth,
			struct bfi_msgq_mhdr *msghdr)
{
	struct bfi_enet_attr_rsp *rsp =
		container_of(msghdr, struct bfi_enet_attr_rsp, mh);

	/**
	 * Store only if not set earlier, since BNAD can override the HW
	 * attributes
	 */
	if (!ioceth->attr.fw_query_complete) {
		ioceth->attr.num_txq = ntohl(rsp->max_cfg);
		ioceth->attr.num_rxp = ntohl(rsp->max_cfg);
		ioceth->attr.num_ucmac = ntohl(rsp->max_ucmac);
		ioceth->attr.num_mcmac = BFI_ENET_MAX_MCAM;
		ioceth->attr.max_rit_size = ntohl(rsp->rit_size);
		ioceth->attr.fw_query_complete = true;
	}

	bfa_fsm_send_event(ioceth, IOCETH_E_ENET_ATTR_RESP);
}

static void
bna_bfi_stats_get_rsp(struct bna *bna, struct bfi_msgq_mhdr *msghdr)
{
	struct bfi_enet_stats_req *stats_req = &bna->stats_mod.stats_get;
	u64 *stats_src;
	u64 *stats_dst;
	u32 tx_enet_mask = ntohl(stats_req->tx_enet_mask);
	u32 rx_enet_mask = ntohl(stats_req->rx_enet_mask);
	int count;
	int i;

	bna_stats_copy(mac, mac);
	bna_stats_copy(bpc, bpc);
	bna_stats_copy(rad, rad);
	bna_stats_copy(rlb, rad);
	bna_stats_copy(fc_rx, fc_rx);
	bna_stats_copy(fc_tx, fc_tx);

	stats_src = (u64 *)&(bna->stats.hw_stats_kva->rxf_stats[0]);

	/* Copy Rxf stats to SW area, scatter them while copying */
	for (i = 0; i < BFI_ENET_CFG_MAX; i++) {
		stats_dst = (u64 *)&(bna->stats.hw_stats.rxf_stats[i]);
		memset(stats_dst, 0, sizeof(struct bfi_enet_stats_rxf));
		if (rx_enet_mask & BIT(i)) {
			int k;
			count = sizeof(struct bfi_enet_stats_rxf) /
				sizeof(u64);
			for (k = 0; k < count; k++) {
				stats_dst[k] = be64_to_cpu(*stats_src);
				stats_src++;
			}
		}
	}

	/* Copy Txf stats to SW area, scatter them while copying */
	for (i = 0; i < BFI_ENET_CFG_MAX; i++) {
		stats_dst = (u64 *)&(bna->stats.hw_stats.txf_stats[i]);
		memset(stats_dst, 0, sizeof(struct bfi_enet_stats_txf));
		if (tx_enet_mask & BIT(i)) {
			int k;
			count = sizeof(struct bfi_enet_stats_txf) /
				sizeof(u64);
			for (k = 0; k < count; k++) {
				stats_dst[k] = be64_to_cpu(*stats_src);
				stats_src++;
			}
		}
	}

	bna->stats_mod.stats_get_busy = false;
	bnad_cb_stats_get(bna->bnad, BNA_CB_SUCCESS, &bna->stats);
}

static void
bna_bfi_ethport_linkup_aen(struct bna_ethport *ethport,
			struct bfi_msgq_mhdr *msghdr)
{
	ethport->link_status = BNA_LINK_UP;

	/* Dispatch events */
	ethport->link_cbfn(ethport->bna->bnad, ethport->link_status);
}

static void
bna_bfi_ethport_linkdown_aen(struct bna_ethport *ethport,
				struct bfi_msgq_mhdr *msghdr)
{
	ethport->link_status = BNA_LINK_DOWN;

	/* Dispatch events */
	ethport->link_cbfn(ethport->bna->bnad, BNA_LINK_DOWN);
}

static void
bna_err_handler(struct bna *bna, u32 intr_status)
{
	if (BNA_IS_HALT_INTR(bna, intr_status))
		bna_halt_clear(bna);

	bfa_nw_ioc_error_isr(&bna->ioceth.ioc);
}

void
bna_mbox_handler(struct bna *bna, u32 intr_status)
{
	if (BNA_IS_ERR_INTR(bna, intr_status)) {
		bna_err_handler(bna, intr_status);
		return;
	}
	if (BNA_IS_MBOX_INTR(bna, intr_status))
		bfa_nw_ioc_mbox_isr(&bna->ioceth.ioc);
}

static void
bna_msgq_rsp_handler(void *arg, struct bfi_msgq_mhdr *msghdr)
{
	struct bna *bna = (struct bna *)arg;
	struct bna_tx *tx;
	struct bna_rx *rx;

	switch (msghdr->msg_id) {
	case BFI_ENET_I2H_RX_CFG_SET_RSP:
		bna_rx_from_rid(bna, msghdr->enet_id, rx);
		if (rx)
			bna_bfi_rx_enet_start_rsp(rx, msghdr);
		break;

	case BFI_ENET_I2H_RX_CFG_CLR_RSP:
		bna_rx_from_rid(bna, msghdr->enet_id, rx);
		if (rx)
			bna_bfi_rx_enet_stop_rsp(rx, msghdr);
		break;

	case BFI_ENET_I2H_RIT_CFG_RSP:
	case BFI_ENET_I2H_RSS_CFG_RSP:
	case BFI_ENET_I2H_RSS_ENABLE_RSP:
	case BFI_ENET_I2H_RX_PROMISCUOUS_RSP:
	case BFI_ENET_I2H_RX_DEFAULT_RSP:
	case BFI_ENET_I2H_MAC_UCAST_CLR_RSP:
	case BFI_ENET_I2H_MAC_UCAST_ADD_RSP:
	case BFI_ENET_I2H_MAC_UCAST_DEL_RSP:
	case BFI_ENET_I2H_MAC_MCAST_DEL_RSP:
	case BFI_ENET_I2H_MAC_MCAST_FILTER_RSP:
	case BFI_ENET_I2H_RX_VLAN_SET_RSP:
	case BFI_ENET_I2H_RX_VLAN_STRIP_ENABLE_RSP:
		bna_rx_from_rid(bna, msghdr->enet_id, rx);
		if (rx)
			bna_bfi_rxf_cfg_rsp(&rx->rxf, msghdr);
		break;

	case BFI_ENET_I2H_MAC_UCAST_SET_RSP:
		bna_rx_from_rid(bna, msghdr->enet_id, rx);
		if (rx)
			bna_bfi_rxf_ucast_set_rsp(&rx->rxf, msghdr);
		break;

	case BFI_ENET_I2H_MAC_MCAST_ADD_RSP:
		bna_rx_from_rid(bna, msghdr->enet_id, rx);
		if (rx)
			bna_bfi_rxf_mcast_add_rsp(&rx->rxf, msghdr);
		break;

	case BFI_ENET_I2H_TX_CFG_SET_RSP:
		bna_tx_from_rid(bna, msghdr->enet_id, tx);
		if (tx)
			bna_bfi_tx_enet_start_rsp(tx, msghdr);
		break;

	case BFI_ENET_I2H_TX_CFG_CLR_RSP:
		bna_tx_from_rid(bna, msghdr->enet_id, tx);
		if (tx)
			bna_bfi_tx_enet_stop_rsp(tx, msghdr);
		break;

	case BFI_ENET_I2H_PORT_ADMIN_RSP:
		bna_bfi_ethport_admin_rsp(&bna->ethport, msghdr);
		break;

	case BFI_ENET_I2H_DIAG_LOOPBACK_RSP:
		bna_bfi_ethport_lpbk_rsp(&bna->ethport, msghdr);
		break;

	case BFI_ENET_I2H_SET_PAUSE_RSP:
		bna_bfi_pause_set_rsp(&bna->enet, msghdr);
		break;

	case BFI_ENET_I2H_GET_ATTR_RSP:
		bna_bfi_attr_get_rsp(&bna->ioceth, msghdr);
		break;

	case BFI_ENET_I2H_STATS_GET_RSP:
		bna_bfi_stats_get_rsp(bna, msghdr);
		break;

	case BFI_ENET_I2H_STATS_CLR_RSP:
		/* No-op */
		break;

	case BFI_ENET_I2H_LINK_UP_AEN:
		bna_bfi_ethport_linkup_aen(&bna->ethport, msghdr);
		break;

	case BFI_ENET_I2H_LINK_DOWN_AEN:
		bna_bfi_ethport_linkdown_aen(&bna->ethport, msghdr);
		break;

	case BFI_ENET_I2H_PORT_ENABLE_AEN:
		bna_bfi_ethport_enable_aen(&bna->ethport, msghdr);
		break;

	case BFI_ENET_I2H_PORT_DISABLE_AEN:
		bna_bfi_ethport_disable_aen(&bna->ethport, msghdr);
		break;

	case BFI_ENET_I2H_BW_UPDATE_AEN:
		bna_bfi_bw_update_aen(&bna->tx_mod);
		break;

	default:
		break;
	}
}

/* ETHPORT */

#define call_ethport_stop_cbfn(_ethport)				\
do {									\
	if ((_ethport)->stop_cbfn) {					\
		void (*cbfn)(struct bna_enet *);			\
		cbfn = (_ethport)->stop_cbfn;				\
		(_ethport)->stop_cbfn = NULL;				\
		cbfn(&(_ethport)->bna->enet);				\
	}								\
} while (0)

#define call_ethport_adminup_cbfn(ethport, status)			\
do {									\
	if ((ethport)->adminup_cbfn) {					\
		void (*cbfn)(struct bnad *, enum bna_cb_status);	\
		cbfn = (ethport)->adminup_cbfn;				\
		(ethport)->adminup_cbfn = NULL;				\
		cbfn((ethport)->bna->bnad, status);			\
	}								\
} while (0)

static void
bna_bfi_ethport_admin_up(struct bna_ethport *ethport)
{
	struct bfi_enet_enable_req *admin_up_req =
		&ethport->bfi_enet_cmd.admin_req;

	bfi_msgq_mhdr_set(admin_up_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_PORT_ADMIN_UP_REQ, 0, 0);
	admin_up_req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_enable_req)));
	admin_up_req->enable = BNA_STATUS_T_ENABLED;

	bfa_msgq_cmd_set(&ethport->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_enable_req), &admin_up_req->mh);
	bfa_msgq_cmd_post(&ethport->bna->msgq, &ethport->msgq_cmd);
}

static void
bna_bfi_ethport_admin_down(struct bna_ethport *ethport)
{
	struct bfi_enet_enable_req *admin_down_req =
		&ethport->bfi_enet_cmd.admin_req;

	bfi_msgq_mhdr_set(admin_down_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_PORT_ADMIN_UP_REQ, 0, 0);
	admin_down_req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_enable_req)));
	admin_down_req->enable = BNA_STATUS_T_DISABLED;

	bfa_msgq_cmd_set(&ethport->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_enable_req), &admin_down_req->mh);
	bfa_msgq_cmd_post(&ethport->bna->msgq, &ethport->msgq_cmd);
}

static void
bna_bfi_ethport_lpbk_up(struct bna_ethport *ethport)
{
	struct bfi_enet_diag_lb_req *lpbk_up_req =
		&ethport->bfi_enet_cmd.lpbk_req;

	bfi_msgq_mhdr_set(lpbk_up_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_DIAG_LOOPBACK_REQ, 0, 0);
	lpbk_up_req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_diag_lb_req)));
	lpbk_up_req->mode = (ethport->bna->enet.type ==
				BNA_ENET_T_LOOPBACK_INTERNAL) ?
				BFI_ENET_DIAG_LB_OPMODE_EXT :
				BFI_ENET_DIAG_LB_OPMODE_CBL;
	lpbk_up_req->enable = BNA_STATUS_T_ENABLED;

	bfa_msgq_cmd_set(&ethport->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_diag_lb_req), &lpbk_up_req->mh);
	bfa_msgq_cmd_post(&ethport->bna->msgq, &ethport->msgq_cmd);
}

static void
bna_bfi_ethport_lpbk_down(struct bna_ethport *ethport)
{
	struct bfi_enet_diag_lb_req *lpbk_down_req =
		&ethport->bfi_enet_cmd.lpbk_req;

	bfi_msgq_mhdr_set(lpbk_down_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_DIAG_LOOPBACK_REQ, 0, 0);
	lpbk_down_req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_diag_lb_req)));
	lpbk_down_req->enable = BNA_STATUS_T_DISABLED;

	bfa_msgq_cmd_set(&ethport->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_diag_lb_req), &lpbk_down_req->mh);
	bfa_msgq_cmd_post(&ethport->bna->msgq, &ethport->msgq_cmd);
}

static void
bna_bfi_ethport_up(struct bna_ethport *ethport)
{
	if (ethport->bna->enet.type == BNA_ENET_T_REGULAR)
		bna_bfi_ethport_admin_up(ethport);
	else
		bna_bfi_ethport_lpbk_up(ethport);
}

static void
bna_bfi_ethport_down(struct bna_ethport *ethport)
{
	if (ethport->bna->enet.type == BNA_ENET_T_REGULAR)
		bna_bfi_ethport_admin_down(ethport);
	else
		bna_bfi_ethport_lpbk_down(ethport);
}

bfa_fsm_state_decl(bna_ethport, stopped, struct bna_ethport,
			enum bna_ethport_event);
bfa_fsm_state_decl(bna_ethport, down, struct bna_ethport,
			enum bna_ethport_event);
bfa_fsm_state_decl(bna_ethport, up_resp_wait, struct bna_ethport,
			enum bna_ethport_event);
bfa_fsm_state_decl(bna_ethport, down_resp_wait, struct bna_ethport,
			enum bna_ethport_event);
bfa_fsm_state_decl(bna_ethport, up, struct bna_ethport,
			enum bna_ethport_event);
bfa_fsm_state_decl(bna_ethport, last_resp_wait, struct bna_ethport,
			enum bna_ethport_event);

static void
bna_ethport_sm_stopped_entry(struct bna_ethport *ethport)
{
	call_ethport_stop_cbfn(ethport);
}

static void
bna_ethport_sm_stopped(struct bna_ethport *ethport,
			enum bna_ethport_event event)
{
	switch (event) {
	case ETHPORT_E_START:
		bfa_fsm_set_state(ethport, bna_ethport_sm_down);
		break;

	case ETHPORT_E_STOP:
		call_ethport_stop_cbfn(ethport);
		break;

	case ETHPORT_E_FAIL:
		/* No-op */
		break;

	case ETHPORT_E_DOWN:
		/* This event is received due to Rx objects failing */
		/* No-op */
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ethport_sm_down_entry(struct bna_ethport *ethport)
{
}

static void
bna_ethport_sm_down(struct bna_ethport *ethport,
			enum bna_ethport_event event)
{
	switch (event) {
	case ETHPORT_E_STOP:
		bfa_fsm_set_state(ethport, bna_ethport_sm_stopped);
		break;

	case ETHPORT_E_FAIL:
		bfa_fsm_set_state(ethport, bna_ethport_sm_stopped);
		break;

	case ETHPORT_E_UP:
		bfa_fsm_set_state(ethport, bna_ethport_sm_up_resp_wait);
		bna_bfi_ethport_up(ethport);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ethport_sm_up_resp_wait_entry(struct bna_ethport *ethport)
{
}

static void
bna_ethport_sm_up_resp_wait(struct bna_ethport *ethport,
			enum bna_ethport_event event)
{
	switch (event) {
	case ETHPORT_E_STOP:
		bfa_fsm_set_state(ethport, bna_ethport_sm_last_resp_wait);
		break;

	case ETHPORT_E_FAIL:
		call_ethport_adminup_cbfn(ethport, BNA_CB_FAIL);
		bfa_fsm_set_state(ethport, bna_ethport_sm_stopped);
		break;

	case ETHPORT_E_DOWN:
		call_ethport_adminup_cbfn(ethport, BNA_CB_INTERRUPT);
		bfa_fsm_set_state(ethport, bna_ethport_sm_down_resp_wait);
		break;

	case ETHPORT_E_FWRESP_UP_OK:
		call_ethport_adminup_cbfn(ethport, BNA_CB_SUCCESS);
		bfa_fsm_set_state(ethport, bna_ethport_sm_up);
		break;

	case ETHPORT_E_FWRESP_UP_FAIL:
		call_ethport_adminup_cbfn(ethport, BNA_CB_FAIL);
		bfa_fsm_set_state(ethport, bna_ethport_sm_down);
		break;

	case ETHPORT_E_FWRESP_DOWN:
		/* down_resp_wait -> up_resp_wait transition on ETHPORT_E_UP */
		bna_bfi_ethport_up(ethport);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ethport_sm_down_resp_wait_entry(struct bna_ethport *ethport)
{
	/**
	 * NOTE: Do not call bna_bfi_ethport_down() here. That will over step
	 * mbox due to up_resp_wait -> down_resp_wait transition on event
	 * ETHPORT_E_DOWN
	 */
}

static void
bna_ethport_sm_down_resp_wait(struct bna_ethport *ethport,
			enum bna_ethport_event event)
{
	switch (event) {
	case ETHPORT_E_STOP:
		bfa_fsm_set_state(ethport, bna_ethport_sm_last_resp_wait);
		break;

	case ETHPORT_E_FAIL:
		bfa_fsm_set_state(ethport, bna_ethport_sm_stopped);
		break;

	case ETHPORT_E_UP:
		bfa_fsm_set_state(ethport, bna_ethport_sm_up_resp_wait);
		break;

	case ETHPORT_E_FWRESP_UP_OK:
		/* up_resp_wait->down_resp_wait transition on ETHPORT_E_DOWN */
		bna_bfi_ethport_down(ethport);
		break;

	case ETHPORT_E_FWRESP_UP_FAIL:
	case ETHPORT_E_FWRESP_DOWN:
		bfa_fsm_set_state(ethport, bna_ethport_sm_down);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ethport_sm_up_entry(struct bna_ethport *ethport)
{
}

static void
bna_ethport_sm_up(struct bna_ethport *ethport,
			enum bna_ethport_event event)
{
	switch (event) {
	case ETHPORT_E_STOP:
		bfa_fsm_set_state(ethport, bna_ethport_sm_last_resp_wait);
		bna_bfi_ethport_down(ethport);
		break;

	case ETHPORT_E_FAIL:
		bfa_fsm_set_state(ethport, bna_ethport_sm_stopped);
		break;

	case ETHPORT_E_DOWN:
		bfa_fsm_set_state(ethport, bna_ethport_sm_down_resp_wait);
		bna_bfi_ethport_down(ethport);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ethport_sm_last_resp_wait_entry(struct bna_ethport *ethport)
{
}

static void
bna_ethport_sm_last_resp_wait(struct bna_ethport *ethport,
			enum bna_ethport_event event)
{
	switch (event) {
	case ETHPORT_E_FAIL:
		bfa_fsm_set_state(ethport, bna_ethport_sm_stopped);
		break;

	case ETHPORT_E_DOWN:
		/**
		 * This event is received due to Rx objects stopping in
		 * parallel to ethport
		 */
		/* No-op */
		break;

	case ETHPORT_E_FWRESP_UP_OK:
		/* up_resp_wait->last_resp_wait transition on ETHPORT_T_STOP */
		bna_bfi_ethport_down(ethport);
		break;

	case ETHPORT_E_FWRESP_UP_FAIL:
	case ETHPORT_E_FWRESP_DOWN:
		bfa_fsm_set_state(ethport, bna_ethport_sm_stopped);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ethport_init(struct bna_ethport *ethport, struct bna *bna)
{
	ethport->flags |= (BNA_ETHPORT_F_ADMIN_UP | BNA_ETHPORT_F_PORT_ENABLED);
	ethport->bna = bna;

	ethport->link_status = BNA_LINK_DOWN;
	ethport->link_cbfn = bnad_cb_ethport_link_status;

	ethport->rx_started_count = 0;

	ethport->stop_cbfn = NULL;
	ethport->adminup_cbfn = NULL;

	bfa_fsm_set_state(ethport, bna_ethport_sm_stopped);
}

static void
bna_ethport_uninit(struct bna_ethport *ethport)
{
	ethport->flags &= ~BNA_ETHPORT_F_ADMIN_UP;
	ethport->flags &= ~BNA_ETHPORT_F_PORT_ENABLED;

	ethport->bna = NULL;
}

static void
bna_ethport_start(struct bna_ethport *ethport)
{
	bfa_fsm_send_event(ethport, ETHPORT_E_START);
}

static void
bna_enet_cb_ethport_stopped(struct bna_enet *enet)
{
	bfa_wc_down(&enet->chld_stop_wc);
}

static void
bna_ethport_stop(struct bna_ethport *ethport)
{
	ethport->stop_cbfn = bna_enet_cb_ethport_stopped;
	bfa_fsm_send_event(ethport, ETHPORT_E_STOP);
}

static void
bna_ethport_fail(struct bna_ethport *ethport)
{
	/* Reset the physical port status to enabled */
	ethport->flags |= BNA_ETHPORT_F_PORT_ENABLED;

	if (ethport->link_status != BNA_LINK_DOWN) {
		ethport->link_status = BNA_LINK_DOWN;
		ethport->link_cbfn(ethport->bna->bnad, BNA_LINK_DOWN);
	}
	bfa_fsm_send_event(ethport, ETHPORT_E_FAIL);
}

/* Should be called only when ethport is disabled */
void
bna_ethport_cb_rx_started(struct bna_ethport *ethport)
{
	ethport->rx_started_count++;

	if (ethport->rx_started_count == 1) {
		ethport->flags |= BNA_ETHPORT_F_RX_STARTED;

		if (ethport_can_be_up(ethport))
			bfa_fsm_send_event(ethport, ETHPORT_E_UP);
	}
}

void
bna_ethport_cb_rx_stopped(struct bna_ethport *ethport)
{
	int ethport_up = ethport_is_up(ethport);

	ethport->rx_started_count--;

	if (ethport->rx_started_count == 0) {
		ethport->flags &= ~BNA_ETHPORT_F_RX_STARTED;

		if (ethport_up)
			bfa_fsm_send_event(ethport, ETHPORT_E_DOWN);
	}
}

/* ENET */

#define bna_enet_chld_start(enet)					\
do {									\
	enum bna_tx_type tx_type =					\
		((enet)->type == BNA_ENET_T_REGULAR) ?			\
		BNA_TX_T_REGULAR : BNA_TX_T_LOOPBACK;			\
	enum bna_rx_type rx_type =					\
		((enet)->type == BNA_ENET_T_REGULAR) ?			\
		BNA_RX_T_REGULAR : BNA_RX_T_LOOPBACK;			\
	bna_ethport_start(&(enet)->bna->ethport);			\
	bna_tx_mod_start(&(enet)->bna->tx_mod, tx_type);		\
	bna_rx_mod_start(&(enet)->bna->rx_mod, rx_type);		\
} while (0)

#define bna_enet_chld_stop(enet)					\
do {									\
	enum bna_tx_type tx_type =					\
		((enet)->type == BNA_ENET_T_REGULAR) ?			\
		BNA_TX_T_REGULAR : BNA_TX_T_LOOPBACK;			\
	enum bna_rx_type rx_type =					\
		((enet)->type == BNA_ENET_T_REGULAR) ?			\
		BNA_RX_T_REGULAR : BNA_RX_T_LOOPBACK;			\
	bfa_wc_init(&(enet)->chld_stop_wc, bna_enet_cb_chld_stopped, (enet));\
	bfa_wc_up(&(enet)->chld_stop_wc);				\
	bna_ethport_stop(&(enet)->bna->ethport);			\
	bfa_wc_up(&(enet)->chld_stop_wc);				\
	bna_tx_mod_stop(&(enet)->bna->tx_mod, tx_type);			\
	bfa_wc_up(&(enet)->chld_stop_wc);				\
	bna_rx_mod_stop(&(enet)->bna->rx_mod, rx_type);			\
	bfa_wc_wait(&(enet)->chld_stop_wc);				\
} while (0)

#define bna_enet_chld_fail(enet)					\
do {									\
	bna_ethport_fail(&(enet)->bna->ethport);			\
	bna_tx_mod_fail(&(enet)->bna->tx_mod);				\
	bna_rx_mod_fail(&(enet)->bna->rx_mod);				\
} while (0)

#define bna_enet_rx_start(enet)						\
do {									\
	enum bna_rx_type rx_type =					\
		((enet)->type == BNA_ENET_T_REGULAR) ?			\
		BNA_RX_T_REGULAR : BNA_RX_T_LOOPBACK;			\
	bna_rx_mod_start(&(enet)->bna->rx_mod, rx_type);		\
} while (0)

#define bna_enet_rx_stop(enet)						\
do {									\
	enum bna_rx_type rx_type =					\
		((enet)->type == BNA_ENET_T_REGULAR) ?			\
		BNA_RX_T_REGULAR : BNA_RX_T_LOOPBACK;			\
	bfa_wc_init(&(enet)->chld_stop_wc, bna_enet_cb_chld_stopped, (enet));\
	bfa_wc_up(&(enet)->chld_stop_wc);				\
	bna_rx_mod_stop(&(enet)->bna->rx_mod, rx_type);			\
	bfa_wc_wait(&(enet)->chld_stop_wc);				\
} while (0)

#define call_enet_stop_cbfn(enet)					\
do {									\
	if ((enet)->stop_cbfn) {					\
		void (*cbfn)(void *);					\
		void *cbarg;						\
		cbfn = (enet)->stop_cbfn;				\
		cbarg = (enet)->stop_cbarg;				\
		(enet)->stop_cbfn = NULL;				\
		(enet)->stop_cbarg = NULL;				\
		cbfn(cbarg);						\
	}								\
} while (0)

#define call_enet_mtu_cbfn(enet)					\
do {									\
	if ((enet)->mtu_cbfn) {						\
		void (*cbfn)(struct bnad *);				\
		cbfn = (enet)->mtu_cbfn;				\
		(enet)->mtu_cbfn = NULL;				\
		cbfn((enet)->bna->bnad);				\
	}								\
} while (0)

static void bna_enet_cb_chld_stopped(void *arg);
static void bna_bfi_pause_set(struct bna_enet *enet);

bfa_fsm_state_decl(bna_enet, stopped, struct bna_enet,
			enum bna_enet_event);
bfa_fsm_state_decl(bna_enet, pause_init_wait, struct bna_enet,
			enum bna_enet_event);
bfa_fsm_state_decl(bna_enet, last_resp_wait, struct bna_enet,
			enum bna_enet_event);
bfa_fsm_state_decl(bna_enet, started, struct bna_enet,
			enum bna_enet_event);
bfa_fsm_state_decl(bna_enet, cfg_wait, struct bna_enet,
			enum bna_enet_event);
bfa_fsm_state_decl(bna_enet, cfg_stop_wait, struct bna_enet,
			enum bna_enet_event);
bfa_fsm_state_decl(bna_enet, chld_stop_wait, struct bna_enet,
			enum bna_enet_event);

static void
bna_enet_sm_stopped_entry(struct bna_enet *enet)
{
	call_enet_mtu_cbfn(enet);
	call_enet_stop_cbfn(enet);
}

static void
bna_enet_sm_stopped(struct bna_enet *enet, enum bna_enet_event event)
{
	switch (event) {
	case ENET_E_START:
		bfa_fsm_set_state(enet, bna_enet_sm_pause_init_wait);
		break;

	case ENET_E_STOP:
		call_enet_stop_cbfn(enet);
		break;

	case ENET_E_FAIL:
		/* No-op */
		break;

	case ENET_E_PAUSE_CFG:
		break;

	case ENET_E_MTU_CFG:
		call_enet_mtu_cbfn(enet);
		break;

	case ENET_E_CHLD_STOPPED:
		/**
		 * This event is received due to Ethport, Tx and Rx objects
		 * failing
		 */
		/* No-op */
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_enet_sm_pause_init_wait_entry(struct bna_enet *enet)
{
	bna_bfi_pause_set(enet);
}

static void
bna_enet_sm_pause_init_wait(struct bna_enet *enet,
				enum bna_enet_event event)
{
	switch (event) {
	case ENET_E_STOP:
		enet->flags &= ~BNA_ENET_F_PAUSE_CHANGED;
		bfa_fsm_set_state(enet, bna_enet_sm_last_resp_wait);
		break;

	case ENET_E_FAIL:
		enet->flags &= ~BNA_ENET_F_PAUSE_CHANGED;
		bfa_fsm_set_state(enet, bna_enet_sm_stopped);
		break;

	case ENET_E_PAUSE_CFG:
		enet->flags |= BNA_ENET_F_PAUSE_CHANGED;
		break;

	case ENET_E_MTU_CFG:
		/* No-op */
		break;

	case ENET_E_FWRESP_PAUSE:
		if (enet->flags & BNA_ENET_F_PAUSE_CHANGED) {
			enet->flags &= ~BNA_ENET_F_PAUSE_CHANGED;
			bna_bfi_pause_set(enet);
		} else {
			bfa_fsm_set_state(enet, bna_enet_sm_started);
			bna_enet_chld_start(enet);
		}
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_enet_sm_last_resp_wait_entry(struct bna_enet *enet)
{
	enet->flags &= ~BNA_ENET_F_PAUSE_CHANGED;
}

static void
bna_enet_sm_last_resp_wait(struct bna_enet *enet,
				enum bna_enet_event event)
{
	switch (event) {
	case ENET_E_FAIL:
	case ENET_E_FWRESP_PAUSE:
		bfa_fsm_set_state(enet, bna_enet_sm_stopped);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_enet_sm_started_entry(struct bna_enet *enet)
{
	/**
	 * NOTE: Do not call bna_enet_chld_start() here, since it will be
	 * inadvertently called during cfg_wait->started transition as well
	 */
	call_enet_mtu_cbfn(enet);
}

static void
bna_enet_sm_started(struct bna_enet *enet,
			enum bna_enet_event event)
{
	switch (event) {
	case ENET_E_STOP:
		bfa_fsm_set_state(enet, bna_enet_sm_chld_stop_wait);
		break;

	case ENET_E_FAIL:
		bfa_fsm_set_state(enet, bna_enet_sm_stopped);
		bna_enet_chld_fail(enet);
		break;

	case ENET_E_PAUSE_CFG:
		bfa_fsm_set_state(enet, bna_enet_sm_cfg_wait);
		bna_bfi_pause_set(enet);
		break;

	case ENET_E_MTU_CFG:
		bfa_fsm_set_state(enet, bna_enet_sm_cfg_wait);
		bna_enet_rx_stop(enet);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_enet_sm_cfg_wait_entry(struct bna_enet *enet)
{
}

static void
bna_enet_sm_cfg_wait(struct bna_enet *enet,
			enum bna_enet_event event)
{
	switch (event) {
	case ENET_E_STOP:
		enet->flags &= ~BNA_ENET_F_PAUSE_CHANGED;
		enet->flags &= ~BNA_ENET_F_MTU_CHANGED;
		bfa_fsm_set_state(enet, bna_enet_sm_cfg_stop_wait);
		break;

	case ENET_E_FAIL:
		enet->flags &= ~BNA_ENET_F_PAUSE_CHANGED;
		enet->flags &= ~BNA_ENET_F_MTU_CHANGED;
		bfa_fsm_set_state(enet, bna_enet_sm_stopped);
		bna_enet_chld_fail(enet);
		break;

	case ENET_E_PAUSE_CFG:
		enet->flags |= BNA_ENET_F_PAUSE_CHANGED;
		break;

	case ENET_E_MTU_CFG:
		enet->flags |= BNA_ENET_F_MTU_CHANGED;
		break;

	case ENET_E_CHLD_STOPPED:
		bna_enet_rx_start(enet);
		/* Fall through */
	case ENET_E_FWRESP_PAUSE:
		if (enet->flags & BNA_ENET_F_PAUSE_CHANGED) {
			enet->flags &= ~BNA_ENET_F_PAUSE_CHANGED;
			bna_bfi_pause_set(enet);
		} else if (enet->flags & BNA_ENET_F_MTU_CHANGED) {
			enet->flags &= ~BNA_ENET_F_MTU_CHANGED;
			bna_enet_rx_stop(enet);
		} else {
			bfa_fsm_set_state(enet, bna_enet_sm_started);
		}
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_enet_sm_cfg_stop_wait_entry(struct bna_enet *enet)
{
	enet->flags &= ~BNA_ENET_F_PAUSE_CHANGED;
	enet->flags &= ~BNA_ENET_F_MTU_CHANGED;
}

static void
bna_enet_sm_cfg_stop_wait(struct bna_enet *enet,
				enum bna_enet_event event)
{
	switch (event) {
	case ENET_E_FAIL:
		bfa_fsm_set_state(enet, bna_enet_sm_stopped);
		bna_enet_chld_fail(enet);
		break;

	case ENET_E_FWRESP_PAUSE:
	case ENET_E_CHLD_STOPPED:
		bfa_fsm_set_state(enet, bna_enet_sm_chld_stop_wait);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_enet_sm_chld_stop_wait_entry(struct bna_enet *enet)
{
	bna_enet_chld_stop(enet);
}

static void
bna_enet_sm_chld_stop_wait(struct bna_enet *enet,
				enum bna_enet_event event)
{
	switch (event) {
	case ENET_E_FAIL:
		bfa_fsm_set_state(enet, bna_enet_sm_stopped);
		bna_enet_chld_fail(enet);
		break;

	case ENET_E_CHLD_STOPPED:
		bfa_fsm_set_state(enet, bna_enet_sm_stopped);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_bfi_pause_set(struct bna_enet *enet)
{
	struct bfi_enet_set_pause_req *pause_req = &enet->pause_req;

	bfi_msgq_mhdr_set(pause_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_SET_PAUSE_REQ, 0, 0);
	pause_req->mh.num_entries = htons(
	bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_set_pause_req)));
	pause_req->tx_pause = enet->pause_config.tx_pause;
	pause_req->rx_pause = enet->pause_config.rx_pause;

	bfa_msgq_cmd_set(&enet->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_set_pause_req), &pause_req->mh);
	bfa_msgq_cmd_post(&enet->bna->msgq, &enet->msgq_cmd);
}

static void
bna_enet_cb_chld_stopped(void *arg)
{
	struct bna_enet *enet = (struct bna_enet *)arg;

	bfa_fsm_send_event(enet, ENET_E_CHLD_STOPPED);
}

static void
bna_enet_init(struct bna_enet *enet, struct bna *bna)
{
	enet->bna = bna;
	enet->flags = 0;
	enet->mtu = 0;
	enet->type = BNA_ENET_T_REGULAR;

	enet->stop_cbfn = NULL;
	enet->stop_cbarg = NULL;

	enet->mtu_cbfn = NULL;

	bfa_fsm_set_state(enet, bna_enet_sm_stopped);
}

static void
bna_enet_uninit(struct bna_enet *enet)
{
	enet->flags = 0;

	enet->bna = NULL;
}

static void
bna_enet_start(struct bna_enet *enet)
{
	enet->flags |= BNA_ENET_F_IOCETH_READY;
	if (enet->flags & BNA_ENET_F_ENABLED)
		bfa_fsm_send_event(enet, ENET_E_START);
}

static void
bna_ioceth_cb_enet_stopped(void *arg)
{
	struct bna_ioceth *ioceth = (struct bna_ioceth *)arg;

	bfa_fsm_send_event(ioceth, IOCETH_E_ENET_STOPPED);
}

static void
bna_enet_stop(struct bna_enet *enet)
{
	enet->stop_cbfn = bna_ioceth_cb_enet_stopped;
	enet->stop_cbarg = &enet->bna->ioceth;

	enet->flags &= ~BNA_ENET_F_IOCETH_READY;
	bfa_fsm_send_event(enet, ENET_E_STOP);
}

static void
bna_enet_fail(struct bna_enet *enet)
{
	enet->flags &= ~BNA_ENET_F_IOCETH_READY;
	bfa_fsm_send_event(enet, ENET_E_FAIL);
}

void
bna_enet_cb_tx_stopped(struct bna_enet *enet)
{
	bfa_wc_down(&enet->chld_stop_wc);
}

void
bna_enet_cb_rx_stopped(struct bna_enet *enet)
{
	bfa_wc_down(&enet->chld_stop_wc);
}

int
bna_enet_mtu_get(struct bna_enet *enet)
{
	return enet->mtu;
}

void
bna_enet_enable(struct bna_enet *enet)
{
	if (enet->fsm != (bfa_sm_t)bna_enet_sm_stopped)
		return;

	enet->flags |= BNA_ENET_F_ENABLED;

	if (enet->flags & BNA_ENET_F_IOCETH_READY)
		bfa_fsm_send_event(enet, ENET_E_START);
}

void
bna_enet_disable(struct bna_enet *enet, enum bna_cleanup_type type,
		 void (*cbfn)(void *))
{
	if (type == BNA_SOFT_CLEANUP) {
		(*cbfn)(enet->bna->bnad);
		return;
	}

	enet->stop_cbfn = cbfn;
	enet->stop_cbarg = enet->bna->bnad;

	enet->flags &= ~BNA_ENET_F_ENABLED;

	bfa_fsm_send_event(enet, ENET_E_STOP);
}

void
bna_enet_pause_config(struct bna_enet *enet,
		      struct bna_pause_config *pause_config)
{
	enet->pause_config = *pause_config;

	bfa_fsm_send_event(enet, ENET_E_PAUSE_CFG);
}

void
bna_enet_mtu_set(struct bna_enet *enet, int mtu,
		 void (*cbfn)(struct bnad *))
{
	enet->mtu = mtu;

	enet->mtu_cbfn = cbfn;

	bfa_fsm_send_event(enet, ENET_E_MTU_CFG);
}

void
bna_enet_perm_mac_get(struct bna_enet *enet, u8 *mac)
{
	bfa_nw_ioc_get_mac(&enet->bna->ioceth.ioc, mac);
}

/* IOCETH */

#define enable_mbox_intr(_ioceth)					\
do {									\
	u32 intr_status;						\
	bna_intr_status_get((_ioceth)->bna, intr_status);		\
	bnad_cb_mbox_intr_enable((_ioceth)->bna->bnad);			\
	bna_mbox_intr_enable((_ioceth)->bna);				\
} while (0)

#define disable_mbox_intr(_ioceth)					\
do {									\
	bna_mbox_intr_disable((_ioceth)->bna);				\
	bnad_cb_mbox_intr_disable((_ioceth)->bna->bnad);		\
} while (0)

#define call_ioceth_stop_cbfn(_ioceth)					\
do {									\
	if ((_ioceth)->stop_cbfn) {					\
		void (*cbfn)(struct bnad *);				\
		struct bnad *cbarg;					\
		cbfn = (_ioceth)->stop_cbfn;				\
		cbarg = (_ioceth)->stop_cbarg;				\
		(_ioceth)->stop_cbfn = NULL;				\
		(_ioceth)->stop_cbarg = NULL;				\
		cbfn(cbarg);						\
	}								\
} while (0)

#define bna_stats_mod_uninit(_stats_mod)				\
do {									\
} while (0)

#define bna_stats_mod_start(_stats_mod)					\
do {									\
	(_stats_mod)->ioc_ready = true;					\
} while (0)

#define bna_stats_mod_stop(_stats_mod)					\
do {									\
	(_stats_mod)->ioc_ready = false;				\
} while (0)

#define bna_stats_mod_fail(_stats_mod)					\
do {									\
	(_stats_mod)->ioc_ready = false;				\
	(_stats_mod)->stats_get_busy = false;				\
	(_stats_mod)->stats_clr_busy = false;				\
} while (0)

static void bna_bfi_attr_get(struct bna_ioceth *ioceth);

bfa_fsm_state_decl(bna_ioceth, stopped, struct bna_ioceth,
			enum bna_ioceth_event);
bfa_fsm_state_decl(bna_ioceth, ioc_ready_wait, struct bna_ioceth,
			enum bna_ioceth_event);
bfa_fsm_state_decl(bna_ioceth, enet_attr_wait, struct bna_ioceth,
			enum bna_ioceth_event);
bfa_fsm_state_decl(bna_ioceth, ready, struct bna_ioceth,
			enum bna_ioceth_event);
bfa_fsm_state_decl(bna_ioceth, last_resp_wait, struct bna_ioceth,
			enum bna_ioceth_event);
bfa_fsm_state_decl(bna_ioceth, enet_stop_wait, struct bna_ioceth,
			enum bna_ioceth_event);
bfa_fsm_state_decl(bna_ioceth, ioc_disable_wait, struct bna_ioceth,
			enum bna_ioceth_event);
bfa_fsm_state_decl(bna_ioceth, failed, struct bna_ioceth,
			enum bna_ioceth_event);

static void
bna_ioceth_sm_stopped_entry(struct bna_ioceth *ioceth)
{
	call_ioceth_stop_cbfn(ioceth);
}

static void
bna_ioceth_sm_stopped(struct bna_ioceth *ioceth,
			enum bna_ioceth_event event)
{
	switch (event) {
	case IOCETH_E_ENABLE:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ioc_ready_wait);
		bfa_nw_ioc_enable(&ioceth->ioc);
		break;

	case IOCETH_E_DISABLE:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_stopped);
		break;

	case IOCETH_E_IOC_RESET:
		enable_mbox_intr(ioceth);
		break;

	case IOCETH_E_IOC_FAILED:
		disable_mbox_intr(ioceth);
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_failed);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ioceth_sm_ioc_ready_wait_entry(struct bna_ioceth *ioceth)
{
	/**
	 * Do not call bfa_nw_ioc_enable() here. It must be called in the
	 * previous state due to failed -> ioc_ready_wait transition.
	 */
}

static void
bna_ioceth_sm_ioc_ready_wait(struct bna_ioceth *ioceth,
				enum bna_ioceth_event event)
{
	switch (event) {
	case IOCETH_E_DISABLE:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ioc_disable_wait);
		bfa_nw_ioc_disable(&ioceth->ioc);
		break;

	case IOCETH_E_IOC_RESET:
		enable_mbox_intr(ioceth);
		break;

	case IOCETH_E_IOC_FAILED:
		disable_mbox_intr(ioceth);
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_failed);
		break;

	case IOCETH_E_IOC_READY:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_enet_attr_wait);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ioceth_sm_enet_attr_wait_entry(struct bna_ioceth *ioceth)
{
	bna_bfi_attr_get(ioceth);
}

static void
bna_ioceth_sm_enet_attr_wait(struct bna_ioceth *ioceth,
				enum bna_ioceth_event event)
{
	switch (event) {
	case IOCETH_E_DISABLE:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_last_resp_wait);
		break;

	case IOCETH_E_IOC_FAILED:
		disable_mbox_intr(ioceth);
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_failed);
		break;

	case IOCETH_E_ENET_ATTR_RESP:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ready);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ioceth_sm_ready_entry(struct bna_ioceth *ioceth)
{
	bna_enet_start(&ioceth->bna->enet);
	bna_stats_mod_start(&ioceth->bna->stats_mod);
	bnad_cb_ioceth_ready(ioceth->bna->bnad);
}

static void
bna_ioceth_sm_ready(struct bna_ioceth *ioceth, enum bna_ioceth_event event)
{
	switch (event) {
	case IOCETH_E_DISABLE:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_enet_stop_wait);
		break;

	case IOCETH_E_IOC_FAILED:
		disable_mbox_intr(ioceth);
		bna_enet_fail(&ioceth->bna->enet);
		bna_stats_mod_fail(&ioceth->bna->stats_mod);
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_failed);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ioceth_sm_last_resp_wait_entry(struct bna_ioceth *ioceth)
{
}

static void
bna_ioceth_sm_last_resp_wait(struct bna_ioceth *ioceth,
				enum bna_ioceth_event event)
{
	switch (event) {
	case IOCETH_E_IOC_FAILED:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ioc_disable_wait);
		disable_mbox_intr(ioceth);
		bfa_nw_ioc_disable(&ioceth->ioc);
		break;

	case IOCETH_E_ENET_ATTR_RESP:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ioc_disable_wait);
		bfa_nw_ioc_disable(&ioceth->ioc);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ioceth_sm_enet_stop_wait_entry(struct bna_ioceth *ioceth)
{
	bna_stats_mod_stop(&ioceth->bna->stats_mod);
	bna_enet_stop(&ioceth->bna->enet);
}

static void
bna_ioceth_sm_enet_stop_wait(struct bna_ioceth *ioceth,
				enum bna_ioceth_event event)
{
	switch (event) {
	case IOCETH_E_IOC_FAILED:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ioc_disable_wait);
		disable_mbox_intr(ioceth);
		bna_enet_fail(&ioceth->bna->enet);
		bna_stats_mod_fail(&ioceth->bna->stats_mod);
		bfa_nw_ioc_disable(&ioceth->ioc);
		break;

	case IOCETH_E_ENET_STOPPED:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ioc_disable_wait);
		bfa_nw_ioc_disable(&ioceth->ioc);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ioceth_sm_ioc_disable_wait_entry(struct bna_ioceth *ioceth)
{
}

static void
bna_ioceth_sm_ioc_disable_wait(struct bna_ioceth *ioceth,
				enum bna_ioceth_event event)
{
	switch (event) {
	case IOCETH_E_IOC_DISABLED:
		disable_mbox_intr(ioceth);
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_stopped);
		break;

	case IOCETH_E_ENET_STOPPED:
		/* This event is received due to enet failing */
		/* No-op */
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_ioceth_sm_failed_entry(struct bna_ioceth *ioceth)
{
	bnad_cb_ioceth_failed(ioceth->bna->bnad);
}

static void
bna_ioceth_sm_failed(struct bna_ioceth *ioceth,
			enum bna_ioceth_event event)
{
	switch (event) {
	case IOCETH_E_DISABLE:
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ioc_disable_wait);
		bfa_nw_ioc_disable(&ioceth->ioc);
		break;

	case IOCETH_E_IOC_RESET:
		enable_mbox_intr(ioceth);
		bfa_fsm_set_state(ioceth, bna_ioceth_sm_ioc_ready_wait);
		break;

	case IOCETH_E_IOC_FAILED:
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bna_bfi_attr_get(struct bna_ioceth *ioceth)
{
	struct bfi_enet_attr_req *attr_req = &ioceth->attr_req;

	bfi_msgq_mhdr_set(attr_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_GET_ATTR_REQ, 0, 0);
	attr_req->mh.num_entries = htons(
	bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_attr_req)));
	bfa_msgq_cmd_set(&ioceth->msgq_cmd, NULL, NULL,
		sizeof(struct bfi_enet_attr_req), &attr_req->mh);
	bfa_msgq_cmd_post(&ioceth->bna->msgq, &ioceth->msgq_cmd);
}

/* IOC callback functions */

static void
bna_cb_ioceth_enable(void *arg, enum bfa_status error)
{
	struct bna_ioceth *ioceth = (struct bna_ioceth *)arg;

	if (error)
		bfa_fsm_send_event(ioceth, IOCETH_E_IOC_FAILED);
	else
		bfa_fsm_send_event(ioceth, IOCETH_E_IOC_READY);
}

static void
bna_cb_ioceth_disable(void *arg)
{
	struct bna_ioceth *ioceth = (struct bna_ioceth *)arg;

	bfa_fsm_send_event(ioceth, IOCETH_E_IOC_DISABLED);
}

static void
bna_cb_ioceth_hbfail(void *arg)
{
	struct bna_ioceth *ioceth = (struct bna_ioceth *)arg;

	bfa_fsm_send_event(ioceth, IOCETH_E_IOC_FAILED);
}

static void
bna_cb_ioceth_reset(void *arg)
{
	struct bna_ioceth *ioceth = (struct bna_ioceth *)arg;

	bfa_fsm_send_event(ioceth, IOCETH_E_IOC_RESET);
}

static struct bfa_ioc_cbfn bna_ioceth_cbfn = {
	bna_cb_ioceth_enable,
	bna_cb_ioceth_disable,
	bna_cb_ioceth_hbfail,
	bna_cb_ioceth_reset
};

static void bna_attr_init(struct bna_ioceth *ioceth)
{
	ioceth->attr.num_txq = BFI_ENET_DEF_TXQ;
	ioceth->attr.num_rxp = BFI_ENET_DEF_RXP;
	ioceth->attr.num_ucmac = BFI_ENET_DEF_UCAM;
	ioceth->attr.num_mcmac = BFI_ENET_MAX_MCAM;
	ioceth->attr.max_rit_size = BFI_ENET_DEF_RITSZ;
	ioceth->attr.fw_query_complete = false;
}

static void
bna_ioceth_init(struct bna_ioceth *ioceth, struct bna *bna,
		struct bna_res_info *res_info)
{
	u64 dma;
	u8 *kva;

	ioceth->bna = bna;

	/**
	 * Attach IOC and claim:
	 *	1. DMA memory for IOC attributes
	 *	2. Kernel memory for FW trace
	 */
	bfa_nw_ioc_attach(&ioceth->ioc, ioceth, &bna_ioceth_cbfn);
	bfa_nw_ioc_pci_init(&ioceth->ioc, &bna->pcidev, BFI_PCIFN_CLASS_ETH);

	BNA_GET_DMA_ADDR(
		&res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.mdl[0].dma, dma);
	kva = res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.mdl[0].kva;
	bfa_nw_ioc_mem_claim(&ioceth->ioc, kva, dma);

	kva = res_info[BNA_RES_MEM_T_FWTRC].res_u.mem_info.mdl[0].kva;
	bfa_nw_ioc_debug_memclaim(&ioceth->ioc, kva);

	/**
	 * Attach common modules (Diag, SFP, CEE, Port) and claim respective
	 * DMA memory.
	 */
	BNA_GET_DMA_ADDR(
		&res_info[BNA_RES_MEM_T_COM].res_u.mem_info.mdl[0].dma, dma);
	kva = res_info[BNA_RES_MEM_T_COM].res_u.mem_info.mdl[0].kva;
	bfa_nw_cee_attach(&bna->cee, &ioceth->ioc, bna);
	bfa_nw_cee_mem_claim(&bna->cee, kva, dma);
	kva += bfa_nw_cee_meminfo();
	dma += bfa_nw_cee_meminfo();

	bfa_nw_flash_attach(&bna->flash, &ioceth->ioc, bna);
	bfa_nw_flash_memclaim(&bna->flash, kva, dma);
	kva += bfa_nw_flash_meminfo();
	dma += bfa_nw_flash_meminfo();

	bfa_msgq_attach(&bna->msgq, &ioceth->ioc);
	bfa_msgq_memclaim(&bna->msgq, kva, dma);
	bfa_msgq_regisr(&bna->msgq, BFI_MC_ENET, bna_msgq_rsp_handler, bna);
	kva += bfa_msgq_meminfo();
	dma += bfa_msgq_meminfo();

	ioceth->stop_cbfn = NULL;
	ioceth->stop_cbarg = NULL;

	bna_attr_init(ioceth);

	bfa_fsm_set_state(ioceth, bna_ioceth_sm_stopped);
}

static void
bna_ioceth_uninit(struct bna_ioceth *ioceth)
{
	bfa_nw_ioc_detach(&ioceth->ioc);

	ioceth->bna = NULL;
}

void
bna_ioceth_enable(struct bna_ioceth *ioceth)
{
	if (ioceth->fsm == (bfa_fsm_t)bna_ioceth_sm_ready) {
		bnad_cb_ioceth_ready(ioceth->bna->bnad);
		return;
	}

	if (ioceth->fsm == (bfa_fsm_t)bna_ioceth_sm_stopped)
		bfa_fsm_send_event(ioceth, IOCETH_E_ENABLE);
}

void
bna_ioceth_disable(struct bna_ioceth *ioceth, enum bna_cleanup_type type)
{
	if (type == BNA_SOFT_CLEANUP) {
		bnad_cb_ioceth_disabled(ioceth->bna->bnad);
		return;
	}

	ioceth->stop_cbfn = bnad_cb_ioceth_disabled;
	ioceth->stop_cbarg = ioceth->bna->bnad;

	bfa_fsm_send_event(ioceth, IOCETH_E_DISABLE);
}

static void
bna_ucam_mod_init(struct bna_ucam_mod *ucam_mod, struct bna *bna,
		  struct bna_res_info *res_info)
{
	int i;

	ucam_mod->ucmac = (struct bna_mac *)
	res_info[BNA_MOD_RES_MEM_T_UCMAC_ARRAY].res_u.mem_info.mdl[0].kva;

	INIT_LIST_HEAD(&ucam_mod->free_q);
	for (i = 0; i < bna->ioceth.attr.num_ucmac; i++)
		list_add_tail(&ucam_mod->ucmac[i].qe, &ucam_mod->free_q);

	/* A separate queue to allow synchronous setting of a list of MACs */
	INIT_LIST_HEAD(&ucam_mod->del_q);
	for (i = i; i < (bna->ioceth.attr.num_ucmac * 2); i++)
		list_add_tail(&ucam_mod->ucmac[i].qe, &ucam_mod->del_q);

	ucam_mod->bna = bna;
}

static void
bna_ucam_mod_uninit(struct bna_ucam_mod *ucam_mod)
{
	ucam_mod->bna = NULL;
}

static void
bna_mcam_mod_init(struct bna_mcam_mod *mcam_mod, struct bna *bna,
		  struct bna_res_info *res_info)
{
	int i;

	mcam_mod->mcmac = (struct bna_mac *)
	res_info[BNA_MOD_RES_MEM_T_MCMAC_ARRAY].res_u.mem_info.mdl[0].kva;

	INIT_LIST_HEAD(&mcam_mod->free_q);
	for (i = 0; i < bna->ioceth.attr.num_mcmac; i++)
		list_add_tail(&mcam_mod->mcmac[i].qe, &mcam_mod->free_q);

	mcam_mod->mchandle = (struct bna_mcam_handle *)
	res_info[BNA_MOD_RES_MEM_T_MCHANDLE_ARRAY].res_u.mem_info.mdl[0].kva;

	INIT_LIST_HEAD(&mcam_mod->free_handle_q);
	for (i = 0; i < bna->ioceth.attr.num_mcmac; i++)
		list_add_tail(&mcam_mod->mchandle[i].qe,
			      &mcam_mod->free_handle_q);

	/* A separate queue to allow synchronous setting of a list of MACs */
	INIT_LIST_HEAD(&mcam_mod->del_q);
	for (i = i; i < (bna->ioceth.attr.num_mcmac * 2); i++)
		list_add_tail(&mcam_mod->mcmac[i].qe, &mcam_mod->del_q);

	mcam_mod->bna = bna;
}

static void
bna_mcam_mod_uninit(struct bna_mcam_mod *mcam_mod)
{
	mcam_mod->bna = NULL;
}

static void
bna_bfi_stats_get(struct bna *bna)
{
	struct bfi_enet_stats_req *stats_req = &bna->stats_mod.stats_get;

	bna->stats_mod.stats_get_busy = true;

	bfi_msgq_mhdr_set(stats_req->mh, BFI_MC_ENET,
		BFI_ENET_H2I_STATS_GET_REQ, 0, 0);
	stats_req->mh.num_entries = htons(
		bfi_msgq_num_cmd_entries(sizeof(struct bfi_enet_stats_req)));
	stats_req->stats_mask = htons(BFI_ENET_STATS_ALL);
	stats_req->tx_enet_mask = htonl(bna->tx_mod.rid_mask);
	stats_req->rx_enet_mask = htonl(bna->rx_mod.rid_mask);
	stats_req->host_buffer.a32.addr_hi = bna->stats.hw_stats_dma.msb;
	stats_req->host_buffer.a32.addr_lo = bna->stats.hw_stats_dma.lsb;

	bfa_msgq_cmd_set(&bna->stats_mod.stats_get_cmd, NULL, NULL,
		sizeof(struct bfi_enet_stats_req), &stats_req->mh);
	bfa_msgq_cmd_post(&bna->msgq, &bna->stats_mod.stats_get_cmd);
}

void
bna_res_req(struct bna_res_info *res_info)
{
	/* DMA memory for COMMON_MODULE */
	res_info[BNA_RES_MEM_T_COM].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_COM].res_u.mem_info.mem_type = BNA_MEM_T_DMA;
	res_info[BNA_RES_MEM_T_COM].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_COM].res_u.mem_info.len = ALIGN(
				(bfa_nw_cee_meminfo() +
				 bfa_nw_flash_meminfo() +
				 bfa_msgq_meminfo()), PAGE_SIZE);

	/* DMA memory for retrieving IOC attributes */
	res_info[BNA_RES_MEM_T_ATTR].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.mem_type = BNA_MEM_T_DMA;
	res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.len =
				ALIGN(bfa_nw_ioc_meminfo(), PAGE_SIZE);

	/* Virtual memory for retreiving fw_trc */
	res_info[BNA_RES_MEM_T_FWTRC].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_FWTRC].res_u.mem_info.mem_type = BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_FWTRC].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_FWTRC].res_u.mem_info.len = BNA_DBG_FWTRC_LEN;

	/* DMA memory for retreiving stats */
	res_info[BNA_RES_MEM_T_STATS].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.mem_type = BNA_MEM_T_DMA;
	res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.len =
				ALIGN(sizeof(struct bfi_enet_stats),
					PAGE_SIZE);
}

void
bna_mod_res_req(struct bna *bna, struct bna_res_info *res_info)
{
	struct bna_attr *attr = &bna->ioceth.attr;

	/* Virtual memory for Tx objects - stored by Tx module */
	res_info[BNA_MOD_RES_MEM_T_TX_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_MOD_RES_MEM_T_TX_ARRAY].res_u.mem_info.mem_type =
		BNA_MEM_T_KVA;
	res_info[BNA_MOD_RES_MEM_T_TX_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_MOD_RES_MEM_T_TX_ARRAY].res_u.mem_info.len =
		attr->num_txq * sizeof(struct bna_tx);

	/* Virtual memory for TxQ - stored by Tx module */
	res_info[BNA_MOD_RES_MEM_T_TXQ_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_MOD_RES_MEM_T_TXQ_ARRAY].res_u.mem_info.mem_type =
		BNA_MEM_T_KVA;
	res_info[BNA_MOD_RES_MEM_T_TXQ_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_MOD_RES_MEM_T_TXQ_ARRAY].res_u.mem_info.len =
		attr->num_txq * sizeof(struct bna_txq);

	/* Virtual memory for Rx objects - stored by Rx module */
	res_info[BNA_MOD_RES_MEM_T_RX_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_MOD_RES_MEM_T_RX_ARRAY].res_u.mem_info.mem_type =
		BNA_MEM_T_KVA;
	res_info[BNA_MOD_RES_MEM_T_RX_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_MOD_RES_MEM_T_RX_ARRAY].res_u.mem_info.len =
		attr->num_rxp * sizeof(struct bna_rx);

	/* Virtual memory for RxPath - stored by Rx module */
	res_info[BNA_MOD_RES_MEM_T_RXP_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_MOD_RES_MEM_T_RXP_ARRAY].res_u.mem_info.mem_type =
		BNA_MEM_T_KVA;
	res_info[BNA_MOD_RES_MEM_T_RXP_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_MOD_RES_MEM_T_RXP_ARRAY].res_u.mem_info.len =
		attr->num_rxp * sizeof(struct bna_rxp);

	/* Virtual memory for RxQ - stored by Rx module */
	res_info[BNA_MOD_RES_MEM_T_RXQ_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_MOD_RES_MEM_T_RXQ_ARRAY].res_u.mem_info.mem_type =
		BNA_MEM_T_KVA;
	res_info[BNA_MOD_RES_MEM_T_RXQ_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_MOD_RES_MEM_T_RXQ_ARRAY].res_u.mem_info.len =
		(attr->num_rxp * 2) * sizeof(struct bna_rxq);

	/* Virtual memory for Unicast MAC address - stored by ucam module */
	res_info[BNA_MOD_RES_MEM_T_UCMAC_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_MOD_RES_MEM_T_UCMAC_ARRAY].res_u.mem_info.mem_type =
		BNA_MEM_T_KVA;
	res_info[BNA_MOD_RES_MEM_T_UCMAC_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_MOD_RES_MEM_T_UCMAC_ARRAY].res_u.mem_info.len =
		(attr->num_ucmac * 2) * sizeof(struct bna_mac);

	/* Virtual memory for Multicast MAC address - stored by mcam module */
	res_info[BNA_MOD_RES_MEM_T_MCMAC_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_MOD_RES_MEM_T_MCMAC_ARRAY].res_u.mem_info.mem_type =
		BNA_MEM_T_KVA;
	res_info[BNA_MOD_RES_MEM_T_MCMAC_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_MOD_RES_MEM_T_MCMAC_ARRAY].res_u.mem_info.len =
		(attr->num_mcmac * 2) * sizeof(struct bna_mac);

	/* Virtual memory for Multicast handle - stored by mcam module */
	res_info[BNA_MOD_RES_MEM_T_MCHANDLE_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_MOD_RES_MEM_T_MCHANDLE_ARRAY].res_u.mem_info.mem_type =
		BNA_MEM_T_KVA;
	res_info[BNA_MOD_RES_MEM_T_MCHANDLE_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_MOD_RES_MEM_T_MCHANDLE_ARRAY].res_u.mem_info.len =
		attr->num_mcmac * sizeof(struct bna_mcam_handle);
}

void
bna_init(struct bna *bna, struct bnad *bnad,
		struct bfa_pcidev *pcidev, struct bna_res_info *res_info)
{
	bna->bnad = bnad;
	bna->pcidev = *pcidev;

	bna->stats.hw_stats_kva = (struct bfi_enet_stats *)
		res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.mdl[0].kva;
	bna->stats.hw_stats_dma.msb =
		res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.mdl[0].dma.msb;
	bna->stats.hw_stats_dma.lsb =
		res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.mdl[0].dma.lsb;

	bna_reg_addr_init(bna, &bna->pcidev);

	/* Also initializes diag, cee, sfp, phy_port, msgq */
	bna_ioceth_init(&bna->ioceth, bna, res_info);

	bna_enet_init(&bna->enet, bna);
	bna_ethport_init(&bna->ethport, bna);
}

void
bna_mod_init(struct bna *bna, struct bna_res_info *res_info)
{
	bna_tx_mod_init(&bna->tx_mod, bna, res_info);

	bna_rx_mod_init(&bna->rx_mod, bna, res_info);

	bna_ucam_mod_init(&bna->ucam_mod, bna, res_info);

	bna_mcam_mod_init(&bna->mcam_mod, bna, res_info);

	bna->default_mode_rid = BFI_INVALID_RID;
	bna->promisc_rid = BFI_INVALID_RID;

	bna->mod_flags |= BNA_MOD_F_INIT_DONE;
}

void
bna_uninit(struct bna *bna)
{
	if (bna->mod_flags & BNA_MOD_F_INIT_DONE) {
		bna_mcam_mod_uninit(&bna->mcam_mod);
		bna_ucam_mod_uninit(&bna->ucam_mod);
		bna_rx_mod_uninit(&bna->rx_mod);
		bna_tx_mod_uninit(&bna->tx_mod);
		bna->mod_flags &= ~BNA_MOD_F_INIT_DONE;
	}

	bna_stats_mod_uninit(&bna->stats_mod);
	bna_ethport_uninit(&bna->ethport);
	bna_enet_uninit(&bna->enet);

	bna_ioceth_uninit(&bna->ioceth);

	bna->bnad = NULL;
}

int
bna_num_txq_set(struct bna *bna, int num_txq)
{
	if (bna->ioceth.attr.fw_query_complete &&
		(num_txq <= bna->ioceth.attr.num_txq)) {
		bna->ioceth.attr.num_txq = num_txq;
		return BNA_CB_SUCCESS;
	}

	return BNA_CB_FAIL;
}

int
bna_num_rxp_set(struct bna *bna, int num_rxp)
{
	if (bna->ioceth.attr.fw_query_complete &&
		(num_rxp <= bna->ioceth.attr.num_rxp)) {
		bna->ioceth.attr.num_rxp = num_rxp;
		return BNA_CB_SUCCESS;
	}

	return BNA_CB_FAIL;
}

struct bna_mac *
bna_cam_mod_mac_get(struct list_head *head)
{
	struct bna_mac *mac;

	mac = list_first_entry_or_null(head, struct bna_mac, qe);
	if (mac)
		list_del(&mac->qe);

	return mac;
}

struct bna_mcam_handle *
bna_mcam_mod_handle_get(struct bna_mcam_mod *mcam_mod)
{
	struct bna_mcam_handle *handle;

	handle = list_first_entry_or_null(&mcam_mod->free_handle_q,
					  struct bna_mcam_handle, qe);
	if (handle)
		list_del(&handle->qe);

	return handle;
}

void
bna_mcam_mod_handle_put(struct bna_mcam_mod *mcam_mod,
			struct bna_mcam_handle *handle)
{
	list_add_tail(&handle->qe, &mcam_mod->free_handle_q);
}

void
bna_hw_stats_get(struct bna *bna)
{
	if (!bna->stats_mod.ioc_ready) {
		bnad_cb_stats_get(bna->bnad, BNA_CB_FAIL, &bna->stats);
		return;
	}
	if (bna->stats_mod.stats_get_busy) {
		bnad_cb_stats_get(bna->bnad, BNA_CB_BUSY, &bna->stats);
		return;
	}

	bna_bfi_stats_get(bna);
}
