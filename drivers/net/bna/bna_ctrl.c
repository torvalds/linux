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
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */
#include "bna.h"
#include "bfa_sm.h"
#include "bfa_wc.h"

static void bna_device_cb_port_stopped(void *arg, enum bna_cb_status status);

static void
bna_port_cb_link_up(struct bna_port *port, struct bfi_ll_aen *aen,
			int status)
{
	int i;
	u8 prio_map;

	port->llport.link_status = BNA_LINK_UP;
	if (aen->cee_linkup)
		port->llport.link_status = BNA_CEE_UP;

	/* Compute the priority */
	prio_map = aen->prio_map;
	if (prio_map) {
		for (i = 0; i < 8; i++) {
			if ((prio_map >> i) & 0x1)
				break;
		}
		port->priority = i;
	} else
		port->priority = 0;

	/* Dispatch events */
	bna_tx_mod_cee_link_status(&port->bna->tx_mod, aen->cee_linkup);
	bna_tx_mod_prio_changed(&port->bna->tx_mod, port->priority);
	port->link_cbfn(port->bna->bnad, port->llport.link_status);
}

static void
bna_port_cb_link_down(struct bna_port *port, int status)
{
	port->llport.link_status = BNA_LINK_DOWN;

	/* Dispatch events */
	bna_tx_mod_cee_link_status(&port->bna->tx_mod, BNA_LINK_DOWN);
	port->link_cbfn(port->bna->bnad, BNA_LINK_DOWN);
}

static inline int
llport_can_be_up(struct bna_llport *llport)
{
	int ready = 0;
	if (llport->type == BNA_PORT_T_REGULAR)
		ready = ((llport->flags & BNA_LLPORT_F_ADMIN_UP) &&
			 (llport->flags & BNA_LLPORT_F_RX_STARTED) &&
			 (llport->flags & BNA_LLPORT_F_PORT_ENABLED));
	else
		ready = ((llport->flags & BNA_LLPORT_F_ADMIN_UP) &&
			 (llport->flags & BNA_LLPORT_F_RX_STARTED) &&
			 !(llport->flags & BNA_LLPORT_F_PORT_ENABLED));
	return ready;
}

#define llport_is_up llport_can_be_up

enum bna_llport_event {
	LLPORT_E_START			= 1,
	LLPORT_E_STOP			= 2,
	LLPORT_E_FAIL			= 3,
	LLPORT_E_UP			= 4,
	LLPORT_E_DOWN			= 5,
	LLPORT_E_FWRESP_UP_OK		= 6,
	LLPORT_E_FWRESP_UP_FAIL		= 7,
	LLPORT_E_FWRESP_DOWN		= 8
};

static void
bna_llport_cb_port_enabled(struct bna_llport *llport)
{
	llport->flags |= BNA_LLPORT_F_PORT_ENABLED;

	if (llport_can_be_up(llport))
		bfa_fsm_send_event(llport, LLPORT_E_UP);
}

static void
bna_llport_cb_port_disabled(struct bna_llport *llport)
{
	int llport_up = llport_is_up(llport);

	llport->flags &= ~BNA_LLPORT_F_PORT_ENABLED;

	if (llport_up)
		bfa_fsm_send_event(llport, LLPORT_E_DOWN);
}

/**
 * MBOX
 */
static int
bna_is_aen(u8 msg_id)
{
	switch (msg_id) {
	case BFI_LL_I2H_LINK_DOWN_AEN:
	case BFI_LL_I2H_LINK_UP_AEN:
	case BFI_LL_I2H_PORT_ENABLE_AEN:
	case BFI_LL_I2H_PORT_DISABLE_AEN:
		return 1;

	default:
		return 0;
	}
}

static void
bna_mbox_aen_callback(struct bna *bna, struct bfi_mbmsg *msg)
{
	struct bfi_ll_aen *aen = (struct bfi_ll_aen *)(msg);

	switch (aen->mh.msg_id) {
	case BFI_LL_I2H_LINK_UP_AEN:
		bna_port_cb_link_up(&bna->port, aen, aen->reason);
		break;
	case BFI_LL_I2H_LINK_DOWN_AEN:
		bna_port_cb_link_down(&bna->port, aen->reason);
		break;
	case BFI_LL_I2H_PORT_ENABLE_AEN:
		bna_llport_cb_port_enabled(&bna->port.llport);
		break;
	case BFI_LL_I2H_PORT_DISABLE_AEN:
		bna_llport_cb_port_disabled(&bna->port.llport);
		break;
	default:
		break;
	}
}

static void
bna_ll_isr(void *llarg, struct bfi_mbmsg *msg)
{
	struct bna *bna = (struct bna *)(llarg);
	struct bfi_ll_rsp *mb_rsp = (struct bfi_ll_rsp *)(msg);
	struct bfi_mhdr *cmd_h, *rsp_h;
	struct bna_mbox_qe *mb_qe = NULL;
	int to_post = 0;
	u8 aen = 0;
	char message[BNA_MESSAGE_SIZE];

	aen = bna_is_aen(mb_rsp->mh.msg_id);

	if (!aen) {
		mb_qe = bfa_q_first(&bna->mbox_mod.posted_q);
		cmd_h = (struct bfi_mhdr *)(&mb_qe->cmd.msg[0]);
		rsp_h = (struct bfi_mhdr *)(&mb_rsp->mh);

		if ((BFA_I2HM(cmd_h->msg_id) == rsp_h->msg_id) &&
		    (cmd_h->mtag.i2htok == rsp_h->mtag.i2htok)) {
			/* Remove the request from posted_q, update state  */
			list_del(&mb_qe->qe);
			bna->mbox_mod.msg_pending--;
			if (list_empty(&bna->mbox_mod.posted_q))
				bna->mbox_mod.state = BNA_MBOX_FREE;
			else
				to_post = 1;

			/* Dispatch the cbfn */
			if (mb_qe->cbfn)
				mb_qe->cbfn(mb_qe->cbarg, mb_rsp->error);

			/* Post the next entry, if needed */
			if (to_post) {
				mb_qe = bfa_q_first(&bna->mbox_mod.posted_q);
				bfa_nw_ioc_mbox_queue(&bna->device.ioc,
							&mb_qe->cmd);
			}
		} else {
			snprintf(message, BNA_MESSAGE_SIZE,
				       "No matching rsp for [%d:%d:%d]\n",
				       mb_rsp->mh.msg_class, mb_rsp->mh.msg_id,
				       mb_rsp->mh.mtag.i2htok);
		pr_info("%s", message);
		}

	} else
		bna_mbox_aen_callback(bna, msg);
}

static void
bna_err_handler(struct bna *bna, u32 intr_status)
{
	u32 init_halt;

	if (intr_status & __HALT_STATUS_BITS) {
		init_halt = readl(bna->device.ioc.ioc_regs.ll_halt);
		init_halt &= ~__FW_INIT_HALT_P;
		writel(init_halt, bna->device.ioc.ioc_regs.ll_halt);
	}

	bfa_nw_ioc_error_isr(&bna->device.ioc);
}

void
bna_mbox_handler(struct bna *bna, u32 intr_status)
{
	if (BNA_IS_ERR_INTR(intr_status)) {
		bna_err_handler(bna, intr_status);
		return;
	}
	if (BNA_IS_MBOX_INTR(intr_status))
		bfa_nw_ioc_mbox_isr(&bna->device.ioc);
}

void
bna_mbox_send(struct bna *bna, struct bna_mbox_qe *mbox_qe)
{
	struct bfi_mhdr *mh;

	mh = (struct bfi_mhdr *)(&mbox_qe->cmd.msg[0]);

	mh->mtag.i2htok = htons(bna->mbox_mod.msg_ctr);
	bna->mbox_mod.msg_ctr++;
	bna->mbox_mod.msg_pending++;
	if (bna->mbox_mod.state == BNA_MBOX_FREE) {
		list_add_tail(&mbox_qe->qe, &bna->mbox_mod.posted_q);
		bfa_nw_ioc_mbox_queue(&bna->device.ioc, &mbox_qe->cmd);
		bna->mbox_mod.state = BNA_MBOX_POSTED;
	} else {
		list_add_tail(&mbox_qe->qe, &bna->mbox_mod.posted_q);
	}
}

static void
bna_mbox_flush_q(struct bna *bna, struct list_head *q)
{
	struct bna_mbox_qe *mb_qe = NULL;
	struct bfi_mhdr *cmd_h;
	struct list_head			*mb_q;
	void 			(*cbfn)(void *arg, int status);
	void 			*cbarg;

	mb_q = &bna->mbox_mod.posted_q;

	while (!list_empty(mb_q)) {
		bfa_q_deq(mb_q, &mb_qe);
		cbfn = mb_qe->cbfn;
		cbarg = mb_qe->cbarg;
		bfa_q_qe_init(mb_qe);
		bna->mbox_mod.msg_pending--;

		cmd_h = (struct bfi_mhdr *)(&mb_qe->cmd.msg[0]);
		if (cbfn)
			cbfn(cbarg, BNA_CB_NOT_EXEC);
	}

	bna->mbox_mod.state = BNA_MBOX_FREE;
}

static void
bna_mbox_mod_start(struct bna_mbox_mod *mbox_mod)
{
}

static void
bna_mbox_mod_stop(struct bna_mbox_mod *mbox_mod)
{
	bna_mbox_flush_q(mbox_mod->bna, &mbox_mod->posted_q);
}

static void
bna_mbox_mod_init(struct bna_mbox_mod *mbox_mod, struct bna *bna)
{
	bfa_nw_ioc_mbox_regisr(&bna->device.ioc, BFI_MC_LL, bna_ll_isr, bna);
	mbox_mod->state = BNA_MBOX_FREE;
	mbox_mod->msg_ctr = mbox_mod->msg_pending = 0;
	INIT_LIST_HEAD(&mbox_mod->posted_q);
	mbox_mod->bna = bna;
}

static void
bna_mbox_mod_uninit(struct bna_mbox_mod *mbox_mod)
{
	mbox_mod->bna = NULL;
}

/**
 * LLPORT
 */
#define call_llport_stop_cbfn(llport, status)\
do {\
	if ((llport)->stop_cbfn)\
		(llport)->stop_cbfn(&(llport)->bna->port, status);\
	(llport)->stop_cbfn = NULL;\
} while (0)

static void bna_fw_llport_up(struct bna_llport *llport);
static void bna_fw_cb_llport_up(void *arg, int status);
static void bna_fw_llport_down(struct bna_llport *llport);
static void bna_fw_cb_llport_down(void *arg, int status);
static void bna_llport_start(struct bna_llport *llport);
static void bna_llport_stop(struct bna_llport *llport);
static void bna_llport_fail(struct bna_llport *llport);

enum bna_llport_state {
	BNA_LLPORT_STOPPED		= 1,
	BNA_LLPORT_DOWN			= 2,
	BNA_LLPORT_UP_RESP_WAIT		= 3,
	BNA_LLPORT_DOWN_RESP_WAIT	= 4,
	BNA_LLPORT_UP			= 5,
	BNA_LLPORT_LAST_RESP_WAIT 	= 6
};

bfa_fsm_state_decl(bna_llport, stopped, struct bna_llport,
			enum bna_llport_event);
bfa_fsm_state_decl(bna_llport, down, struct bna_llport,
			enum bna_llport_event);
bfa_fsm_state_decl(bna_llport, up_resp_wait, struct bna_llport,
			enum bna_llport_event);
bfa_fsm_state_decl(bna_llport, down_resp_wait, struct bna_llport,
			enum bna_llport_event);
bfa_fsm_state_decl(bna_llport, up, struct bna_llport,
			enum bna_llport_event);
bfa_fsm_state_decl(bna_llport, last_resp_wait, struct bna_llport,
			enum bna_llport_event);

static struct bfa_sm_table llport_sm_table[] = {
	{BFA_SM(bna_llport_sm_stopped), BNA_LLPORT_STOPPED},
	{BFA_SM(bna_llport_sm_down), BNA_LLPORT_DOWN},
	{BFA_SM(bna_llport_sm_up_resp_wait), BNA_LLPORT_UP_RESP_WAIT},
	{BFA_SM(bna_llport_sm_down_resp_wait), BNA_LLPORT_DOWN_RESP_WAIT},
	{BFA_SM(bna_llport_sm_up), BNA_LLPORT_UP},
	{BFA_SM(bna_llport_sm_last_resp_wait), BNA_LLPORT_LAST_RESP_WAIT}
};

static void
bna_llport_sm_stopped_entry(struct bna_llport *llport)
{
	llport->bna->port.link_cbfn((llport)->bna->bnad, BNA_LINK_DOWN);
	call_llport_stop_cbfn(llport, BNA_CB_SUCCESS);
}

static void
bna_llport_sm_stopped(struct bna_llport *llport,
			enum bna_llport_event event)
{
	switch (event) {
	case LLPORT_E_START:
		bfa_fsm_set_state(llport, bna_llport_sm_down);
		break;

	case LLPORT_E_STOP:
		call_llport_stop_cbfn(llport, BNA_CB_SUCCESS);
		break;

	case LLPORT_E_FAIL:
		break;

	case LLPORT_E_DOWN:
		/* This event is received due to Rx objects failing */
		/* No-op */
		break;

	case LLPORT_E_FWRESP_UP_OK:
	case LLPORT_E_FWRESP_DOWN:
		/**
		 * These events are received due to flushing of mbox when
		 * device fails
		 */
		/* No-op */
		break;

	default:
		bfa_sm_fault(llport->bna, event);
	}
}

static void
bna_llport_sm_down_entry(struct bna_llport *llport)
{
	bnad_cb_port_link_status((llport)->bna->bnad, BNA_LINK_DOWN);
}

static void
bna_llport_sm_down(struct bna_llport *llport,
			enum bna_llport_event event)
{
	switch (event) {
	case LLPORT_E_STOP:
		bfa_fsm_set_state(llport, bna_llport_sm_stopped);
		break;

	case LLPORT_E_FAIL:
		bfa_fsm_set_state(llport, bna_llport_sm_stopped);
		break;

	case LLPORT_E_UP:
		bfa_fsm_set_state(llport, bna_llport_sm_up_resp_wait);
		bna_fw_llport_up(llport);
		break;

	default:
		bfa_sm_fault(llport->bna, event);
	}
}

static void
bna_llport_sm_up_resp_wait_entry(struct bna_llport *llport)
{
	BUG_ON(!llport_can_be_up(llport));
	/**
	 * NOTE: Do not call bna_fw_llport_up() here. That will over step
	 * mbox due to down_resp_wait -> up_resp_wait transition on event
	 * LLPORT_E_UP
	 */
}

static void
bna_llport_sm_up_resp_wait(struct bna_llport *llport,
			enum bna_llport_event event)
{
	switch (event) {
	case LLPORT_E_STOP:
		bfa_fsm_set_state(llport, bna_llport_sm_last_resp_wait);
		break;

	case LLPORT_E_FAIL:
		bfa_fsm_set_state(llport, bna_llport_sm_stopped);
		break;

	case LLPORT_E_DOWN:
		bfa_fsm_set_state(llport, bna_llport_sm_down_resp_wait);
		break;

	case LLPORT_E_FWRESP_UP_OK:
		bfa_fsm_set_state(llport, bna_llport_sm_up);
		break;

	case LLPORT_E_FWRESP_UP_FAIL:
		bfa_fsm_set_state(llport, bna_llport_sm_down);
		break;

	case LLPORT_E_FWRESP_DOWN:
		/* down_resp_wait -> up_resp_wait transition on LLPORT_E_UP */
		bna_fw_llport_up(llport);
		break;

	default:
		bfa_sm_fault(llport->bna, event);
	}
}

static void
bna_llport_sm_down_resp_wait_entry(struct bna_llport *llport)
{
	/**
	 * NOTE: Do not call bna_fw_llport_down() here. That will over step
	 * mbox due to up_resp_wait -> down_resp_wait transition on event
	 * LLPORT_E_DOWN
	 */
}

static void
bna_llport_sm_down_resp_wait(struct bna_llport *llport,
			enum bna_llport_event event)
{
	switch (event) {
	case LLPORT_E_STOP:
		bfa_fsm_set_state(llport, bna_llport_sm_last_resp_wait);
		break;

	case LLPORT_E_FAIL:
		bfa_fsm_set_state(llport, bna_llport_sm_stopped);
		break;

	case LLPORT_E_UP:
		bfa_fsm_set_state(llport, bna_llport_sm_up_resp_wait);
		break;

	case LLPORT_E_FWRESP_UP_OK:
		/* up_resp_wait->down_resp_wait transition on LLPORT_E_DOWN */
		bna_fw_llport_down(llport);
		break;

	case LLPORT_E_FWRESP_UP_FAIL:
	case LLPORT_E_FWRESP_DOWN:
		bfa_fsm_set_state(llport, bna_llport_sm_down);
		break;

	default:
		bfa_sm_fault(llport->bna, event);
	}
}

static void
bna_llport_sm_up_entry(struct bna_llport *llport)
{
}

static void
bna_llport_sm_up(struct bna_llport *llport,
			enum bna_llport_event event)
{
	switch (event) {
	case LLPORT_E_STOP:
		bfa_fsm_set_state(llport, bna_llport_sm_last_resp_wait);
		bna_fw_llport_down(llport);
		break;

	case LLPORT_E_FAIL:
		bfa_fsm_set_state(llport, bna_llport_sm_stopped);
		break;

	case LLPORT_E_DOWN:
		bfa_fsm_set_state(llport, bna_llport_sm_down_resp_wait);
		bna_fw_llport_down(llport);
		break;

	default:
		bfa_sm_fault(llport->bna, event);
	}
}

static void
bna_llport_sm_last_resp_wait_entry(struct bna_llport *llport)
{
}

static void
bna_llport_sm_last_resp_wait(struct bna_llport *llport,
			enum bna_llport_event event)
{
	switch (event) {
	case LLPORT_E_FAIL:
		bfa_fsm_set_state(llport, bna_llport_sm_stopped);
		break;

	case LLPORT_E_DOWN:
		/**
		 * This event is received due to Rx objects stopping in
		 * parallel to llport
		 */
		/* No-op */
		break;

	case LLPORT_E_FWRESP_UP_OK:
		/* up_resp_wait->last_resp_wait transition on LLPORT_T_STOP */
		bna_fw_llport_down(llport);
		break;

	case LLPORT_E_FWRESP_UP_FAIL:
	case LLPORT_E_FWRESP_DOWN:
		bfa_fsm_set_state(llport, bna_llport_sm_stopped);
		break;

	default:
		bfa_sm_fault(llport->bna, event);
	}
}

static void
bna_fw_llport_admin_up(struct bna_llport *llport)
{
	struct bfi_ll_port_admin_req ll_req;

	memset(&ll_req, 0, sizeof(ll_req));
	ll_req.mh.msg_class = BFI_MC_LL;
	ll_req.mh.msg_id = BFI_LL_H2I_PORT_ADMIN_REQ;
	ll_req.mh.mtag.h2i.lpu_id = 0;

	ll_req.up = BNA_STATUS_T_ENABLED;

	bna_mbox_qe_fill(&llport->mbox_qe, &ll_req, sizeof(ll_req),
			bna_fw_cb_llport_up, llport);

	bna_mbox_send(llport->bna, &llport->mbox_qe);
}

static void
bna_fw_llport_up(struct bna_llport *llport)
{
	if (llport->type == BNA_PORT_T_REGULAR)
		bna_fw_llport_admin_up(llport);
}

static void
bna_fw_cb_llport_up(void *arg, int status)
{
	struct bna_llport *llport = (struct bna_llport *)arg;

	bfa_q_qe_init(&llport->mbox_qe.qe);
	if (status == BFI_LL_CMD_FAIL) {
		if (llport->type == BNA_PORT_T_REGULAR)
			llport->flags &= ~BNA_LLPORT_F_PORT_ENABLED;
		else
			llport->flags &= ~BNA_LLPORT_F_ADMIN_UP;
		bfa_fsm_send_event(llport, LLPORT_E_FWRESP_UP_FAIL);
	} else
		bfa_fsm_send_event(llport, LLPORT_E_FWRESP_UP_OK);
}

static void
bna_fw_llport_admin_down(struct bna_llport *llport)
{
	struct bfi_ll_port_admin_req ll_req;

	memset(&ll_req, 0, sizeof(ll_req));
	ll_req.mh.msg_class = BFI_MC_LL;
	ll_req.mh.msg_id = BFI_LL_H2I_PORT_ADMIN_REQ;
	ll_req.mh.mtag.h2i.lpu_id = 0;

	ll_req.up = BNA_STATUS_T_DISABLED;

	bna_mbox_qe_fill(&llport->mbox_qe, &ll_req, sizeof(ll_req),
			bna_fw_cb_llport_down, llport);

	bna_mbox_send(llport->bna, &llport->mbox_qe);
}

static void
bna_fw_llport_down(struct bna_llport *llport)
{
	if (llport->type == BNA_PORT_T_REGULAR)
		bna_fw_llport_admin_down(llport);
}

static void
bna_fw_cb_llport_down(void *arg, int status)
{
	struct bna_llport *llport = (struct bna_llport *)arg;

	bfa_q_qe_init(&llport->mbox_qe.qe);
	bfa_fsm_send_event(llport, LLPORT_E_FWRESP_DOWN);
}

static void
bna_port_cb_llport_stopped(struct bna_port *port,
				enum bna_cb_status status)
{
	bfa_wc_down(&port->chld_stop_wc);
}

static void
bna_llport_init(struct bna_llport *llport, struct bna *bna)
{
	llport->flags |= BNA_LLPORT_F_ADMIN_UP;
	llport->flags |= BNA_LLPORT_F_PORT_ENABLED;
	llport->type = BNA_PORT_T_REGULAR;
	llport->bna = bna;

	llport->link_status = BNA_LINK_DOWN;

	llport->rx_started_count = 0;

	llport->stop_cbfn = NULL;

	bfa_q_qe_init(&llport->mbox_qe.qe);

	bfa_fsm_set_state(llport, bna_llport_sm_stopped);
}

static void
bna_llport_uninit(struct bna_llport *llport)
{
	llport->flags &= ~BNA_LLPORT_F_ADMIN_UP;
	llport->flags &= ~BNA_LLPORT_F_PORT_ENABLED;

	llport->bna = NULL;
}

static void
bna_llport_start(struct bna_llport *llport)
{
	bfa_fsm_send_event(llport, LLPORT_E_START);
}

static void
bna_llport_stop(struct bna_llport *llport)
{
	llport->stop_cbfn = bna_port_cb_llport_stopped;

	bfa_fsm_send_event(llport, LLPORT_E_STOP);
}

static void
bna_llport_fail(struct bna_llport *llport)
{
	/* Reset the physical port status to enabled */
	llport->flags |= BNA_LLPORT_F_PORT_ENABLED;
	bfa_fsm_send_event(llport, LLPORT_E_FAIL);
}

static int
bna_llport_state_get(struct bna_llport *llport)
{
	return bfa_sm_to_state(llport_sm_table, llport->fsm);
}

void
bna_llport_rx_started(struct bna_llport *llport)
{
	llport->rx_started_count++;

	if (llport->rx_started_count == 1) {

		llport->flags |= BNA_LLPORT_F_RX_STARTED;

		if (llport_can_be_up(llport))
			bfa_fsm_send_event(llport, LLPORT_E_UP);
	}
}

void
bna_llport_rx_stopped(struct bna_llport *llport)
{
	int llport_up = llport_is_up(llport);

	llport->rx_started_count--;

	if (llport->rx_started_count == 0) {

		llport->flags &= ~BNA_LLPORT_F_RX_STARTED;

		if (llport_up)
			bfa_fsm_send_event(llport, LLPORT_E_DOWN);
	}
}

/**
 * PORT
 */
#define bna_port_chld_start(port)\
do {\
	enum bna_tx_type tx_type = ((port)->type == BNA_PORT_T_REGULAR) ?\
					BNA_TX_T_REGULAR : BNA_TX_T_LOOPBACK;\
	enum bna_rx_type rx_type = ((port)->type == BNA_PORT_T_REGULAR) ?\
					BNA_RX_T_REGULAR : BNA_RX_T_LOOPBACK;\
	bna_llport_start(&(port)->llport);\
	bna_tx_mod_start(&(port)->bna->tx_mod, tx_type);\
	bna_rx_mod_start(&(port)->bna->rx_mod, rx_type);\
} while (0)

#define bna_port_chld_stop(port)\
do {\
	enum bna_tx_type tx_type = ((port)->type == BNA_PORT_T_REGULAR) ?\
					BNA_TX_T_REGULAR : BNA_TX_T_LOOPBACK;\
	enum bna_rx_type rx_type = ((port)->type == BNA_PORT_T_REGULAR) ?\
					BNA_RX_T_REGULAR : BNA_RX_T_LOOPBACK;\
	bfa_wc_up(&(port)->chld_stop_wc);\
	bfa_wc_up(&(port)->chld_stop_wc);\
	bfa_wc_up(&(port)->chld_stop_wc);\
	bna_llport_stop(&(port)->llport);\
	bna_tx_mod_stop(&(port)->bna->tx_mod, tx_type);\
	bna_rx_mod_stop(&(port)->bna->rx_mod, rx_type);\
} while (0)

#define bna_port_chld_fail(port)\
do {\
	bna_llport_fail(&(port)->llport);\
	bna_tx_mod_fail(&(port)->bna->tx_mod);\
	bna_rx_mod_fail(&(port)->bna->rx_mod);\
} while (0)

#define bna_port_rx_start(port)\
do {\
	enum bna_rx_type rx_type = ((port)->type == BNA_PORT_T_REGULAR) ?\
					BNA_RX_T_REGULAR : BNA_RX_T_LOOPBACK;\
	bna_rx_mod_start(&(port)->bna->rx_mod, rx_type);\
} while (0)

#define bna_port_rx_stop(port)\
do {\
	enum bna_rx_type rx_type = ((port)->type == BNA_PORT_T_REGULAR) ?\
					BNA_RX_T_REGULAR : BNA_RX_T_LOOPBACK;\
	bfa_wc_up(&(port)->chld_stop_wc);\
	bna_rx_mod_stop(&(port)->bna->rx_mod, rx_type);\
} while (0)

#define call_port_stop_cbfn(port, status)\
do {\
	if ((port)->stop_cbfn)\
		(port)->stop_cbfn((port)->stop_cbarg, status);\
	(port)->stop_cbfn = NULL;\
	(port)->stop_cbarg = NULL;\
} while (0)

#define call_port_pause_cbfn(port, status)\
do {\
	if ((port)->pause_cbfn)\
		(port)->pause_cbfn((port)->bna->bnad, status);\
	(port)->pause_cbfn = NULL;\
} while (0)

#define call_port_mtu_cbfn(port, status)\
do {\
	if ((port)->mtu_cbfn)\
		(port)->mtu_cbfn((port)->bna->bnad, status);\
	(port)->mtu_cbfn = NULL;\
} while (0)

static void bna_fw_pause_set(struct bna_port *port);
static void bna_fw_cb_pause_set(void *arg, int status);
static void bna_fw_mtu_set(struct bna_port *port);
static void bna_fw_cb_mtu_set(void *arg, int status);

enum bna_port_event {
	PORT_E_START			= 1,
	PORT_E_STOP			= 2,
	PORT_E_FAIL			= 3,
	PORT_E_PAUSE_CFG		= 4,
	PORT_E_MTU_CFG			= 5,
	PORT_E_CHLD_STOPPED		= 6,
	PORT_E_FWRESP_PAUSE		= 7,
	PORT_E_FWRESP_MTU		= 8
};

enum bna_port_state {
	BNA_PORT_STOPPED		= 1,
	BNA_PORT_MTU_INIT_WAIT		= 2,
	BNA_PORT_PAUSE_INIT_WAIT	= 3,
	BNA_PORT_LAST_RESP_WAIT		= 4,
	BNA_PORT_STARTED		= 5,
	BNA_PORT_PAUSE_CFG_WAIT		= 6,
	BNA_PORT_RX_STOP_WAIT		= 7,
	BNA_PORT_MTU_CFG_WAIT 		= 8,
	BNA_PORT_CHLD_STOP_WAIT		= 9
};

bfa_fsm_state_decl(bna_port, stopped, struct bna_port,
			enum bna_port_event);
bfa_fsm_state_decl(bna_port, mtu_init_wait, struct bna_port,
			enum bna_port_event);
bfa_fsm_state_decl(bna_port, pause_init_wait, struct bna_port,
			enum bna_port_event);
bfa_fsm_state_decl(bna_port, last_resp_wait, struct bna_port,
			enum bna_port_event);
bfa_fsm_state_decl(bna_port, started, struct bna_port,
			enum bna_port_event);
bfa_fsm_state_decl(bna_port, pause_cfg_wait, struct bna_port,
			enum bna_port_event);
bfa_fsm_state_decl(bna_port, rx_stop_wait, struct bna_port,
			enum bna_port_event);
bfa_fsm_state_decl(bna_port, mtu_cfg_wait, struct bna_port,
			enum bna_port_event);
bfa_fsm_state_decl(bna_port, chld_stop_wait, struct bna_port,
			enum bna_port_event);

static struct bfa_sm_table port_sm_table[] = {
	{BFA_SM(bna_port_sm_stopped), BNA_PORT_STOPPED},
	{BFA_SM(bna_port_sm_mtu_init_wait), BNA_PORT_MTU_INIT_WAIT},
	{BFA_SM(bna_port_sm_pause_init_wait), BNA_PORT_PAUSE_INIT_WAIT},
	{BFA_SM(bna_port_sm_last_resp_wait), BNA_PORT_LAST_RESP_WAIT},
	{BFA_SM(bna_port_sm_started), BNA_PORT_STARTED},
	{BFA_SM(bna_port_sm_pause_cfg_wait), BNA_PORT_PAUSE_CFG_WAIT},
	{BFA_SM(bna_port_sm_rx_stop_wait), BNA_PORT_RX_STOP_WAIT},
	{BFA_SM(bna_port_sm_mtu_cfg_wait), BNA_PORT_MTU_CFG_WAIT},
	{BFA_SM(bna_port_sm_chld_stop_wait), BNA_PORT_CHLD_STOP_WAIT}
};

static void
bna_port_sm_stopped_entry(struct bna_port *port)
{
	call_port_pause_cbfn(port, BNA_CB_SUCCESS);
	call_port_mtu_cbfn(port, BNA_CB_SUCCESS);
	call_port_stop_cbfn(port, BNA_CB_SUCCESS);
}

static void
bna_port_sm_stopped(struct bna_port *port, enum bna_port_event event)
{
	switch (event) {
	case PORT_E_START:
		bfa_fsm_set_state(port, bna_port_sm_mtu_init_wait);
		break;

	case PORT_E_STOP:
		call_port_stop_cbfn(port, BNA_CB_SUCCESS);
		break;

	case PORT_E_FAIL:
		/* No-op */
		break;

	case PORT_E_PAUSE_CFG:
		call_port_pause_cbfn(port, BNA_CB_SUCCESS);
		break;

	case PORT_E_MTU_CFG:
		call_port_mtu_cbfn(port, BNA_CB_SUCCESS);
		break;

	case PORT_E_CHLD_STOPPED:
		/**
		 * This event is received due to LLPort, Tx and Rx objects
		 * failing
		 */
		/* No-op */
		break;

	case PORT_E_FWRESP_PAUSE:
	case PORT_E_FWRESP_MTU:
		/**
		 * These events are received due to flushing of mbox when
		 * device fails
		 */
		/* No-op */
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_port_sm_mtu_init_wait_entry(struct bna_port *port)
{
	bna_fw_mtu_set(port);
}

static void
bna_port_sm_mtu_init_wait(struct bna_port *port, enum bna_port_event event)
{
	switch (event) {
	case PORT_E_STOP:
		bfa_fsm_set_state(port, bna_port_sm_last_resp_wait);
		break;

	case PORT_E_FAIL:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		break;

	case PORT_E_PAUSE_CFG:
		/* No-op */
		break;

	case PORT_E_MTU_CFG:
		port->flags |= BNA_PORT_F_MTU_CHANGED;
		break;

	case PORT_E_FWRESP_MTU:
		if (port->flags & BNA_PORT_F_MTU_CHANGED) {
			port->flags &= ~BNA_PORT_F_MTU_CHANGED;
			bna_fw_mtu_set(port);
		} else {
			bfa_fsm_set_state(port, bna_port_sm_pause_init_wait);
		}
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_port_sm_pause_init_wait_entry(struct bna_port *port)
{
	bna_fw_pause_set(port);
}

static void
bna_port_sm_pause_init_wait(struct bna_port *port,
				enum bna_port_event event)
{
	switch (event) {
	case PORT_E_STOP:
		bfa_fsm_set_state(port, bna_port_sm_last_resp_wait);
		break;

	case PORT_E_FAIL:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		break;

	case PORT_E_PAUSE_CFG:
		port->flags |= BNA_PORT_F_PAUSE_CHANGED;
		break;

	case PORT_E_MTU_CFG:
		port->flags |= BNA_PORT_F_MTU_CHANGED;
		break;

	case PORT_E_FWRESP_PAUSE:
		if (port->flags & BNA_PORT_F_PAUSE_CHANGED) {
			port->flags &= ~BNA_PORT_F_PAUSE_CHANGED;
			bna_fw_pause_set(port);
		} else if (port->flags & BNA_PORT_F_MTU_CHANGED) {
			port->flags &= ~BNA_PORT_F_MTU_CHANGED;
			bfa_fsm_set_state(port, bna_port_sm_mtu_init_wait);
		} else {
			bfa_fsm_set_state(port, bna_port_sm_started);
			bna_port_chld_start(port);
		}
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_port_sm_last_resp_wait_entry(struct bna_port *port)
{
}

static void
bna_port_sm_last_resp_wait(struct bna_port *port,
				enum bna_port_event event)
{
	switch (event) {
	case PORT_E_FAIL:
	case PORT_E_FWRESP_PAUSE:
	case PORT_E_FWRESP_MTU:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_port_sm_started_entry(struct bna_port *port)
{
	/**
	 * NOTE: Do not call bna_port_chld_start() here, since it will be
	 * inadvertently called during pause_cfg_wait->started transition
	 * as well
	 */
	call_port_pause_cbfn(port, BNA_CB_SUCCESS);
	call_port_mtu_cbfn(port, BNA_CB_SUCCESS);
}

static void
bna_port_sm_started(struct bna_port *port,
			enum bna_port_event event)
{
	switch (event) {
	case PORT_E_STOP:
		bfa_fsm_set_state(port, bna_port_sm_chld_stop_wait);
		break;

	case PORT_E_FAIL:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		bna_port_chld_fail(port);
		break;

	case PORT_E_PAUSE_CFG:
		bfa_fsm_set_state(port, bna_port_sm_pause_cfg_wait);
		break;

	case PORT_E_MTU_CFG:
		bfa_fsm_set_state(port, bna_port_sm_rx_stop_wait);
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_port_sm_pause_cfg_wait_entry(struct bna_port *port)
{
	bna_fw_pause_set(port);
}

static void
bna_port_sm_pause_cfg_wait(struct bna_port *port,
				enum bna_port_event event)
{
	switch (event) {
	case PORT_E_FAIL:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		bna_port_chld_fail(port);
		break;

	case PORT_E_FWRESP_PAUSE:
		bfa_fsm_set_state(port, bna_port_sm_started);
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_port_sm_rx_stop_wait_entry(struct bna_port *port)
{
	bna_port_rx_stop(port);
}

static void
bna_port_sm_rx_stop_wait(struct bna_port *port,
				enum bna_port_event event)
{
	switch (event) {
	case PORT_E_FAIL:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		bna_port_chld_fail(port);
		break;

	case PORT_E_CHLD_STOPPED:
		bfa_fsm_set_state(port, bna_port_sm_mtu_cfg_wait);
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_port_sm_mtu_cfg_wait_entry(struct bna_port *port)
{
	bna_fw_mtu_set(port);
}

static void
bna_port_sm_mtu_cfg_wait(struct bna_port *port, enum bna_port_event event)
{
	switch (event) {
	case PORT_E_FAIL:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		bna_port_chld_fail(port);
		break;

	case PORT_E_FWRESP_MTU:
		bfa_fsm_set_state(port, bna_port_sm_started);
		bna_port_rx_start(port);
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_port_sm_chld_stop_wait_entry(struct bna_port *port)
{
	bna_port_chld_stop(port);
}

static void
bna_port_sm_chld_stop_wait(struct bna_port *port,
				enum bna_port_event event)
{
	switch (event) {
	case PORT_E_FAIL:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		bna_port_chld_fail(port);
		break;

	case PORT_E_CHLD_STOPPED:
		bfa_fsm_set_state(port, bna_port_sm_stopped);
		break;

	default:
		bfa_sm_fault(port->bna, event);
	}
}

static void
bna_fw_pause_set(struct bna_port *port)
{
	struct bfi_ll_set_pause_req ll_req;

	memset(&ll_req, 0, sizeof(ll_req));
	ll_req.mh.msg_class = BFI_MC_LL;
	ll_req.mh.msg_id = BFI_LL_H2I_SET_PAUSE_REQ;
	ll_req.mh.mtag.h2i.lpu_id = 0;

	ll_req.tx_pause = port->pause_config.tx_pause;
	ll_req.rx_pause = port->pause_config.rx_pause;

	bna_mbox_qe_fill(&port->mbox_qe, &ll_req, sizeof(ll_req),
			bna_fw_cb_pause_set, port);

	bna_mbox_send(port->bna, &port->mbox_qe);
}

static void
bna_fw_cb_pause_set(void *arg, int status)
{
	struct bna_port *port = (struct bna_port *)arg;

	bfa_q_qe_init(&port->mbox_qe.qe);
	bfa_fsm_send_event(port, PORT_E_FWRESP_PAUSE);
}

void
bna_fw_mtu_set(struct bna_port *port)
{
	struct bfi_ll_mtu_info_req ll_req;

	bfi_h2i_set(ll_req.mh, BFI_MC_LL, BFI_LL_H2I_MTU_INFO_REQ, 0);
	ll_req.mtu = htons((u16)port->mtu);

	bna_mbox_qe_fill(&port->mbox_qe, &ll_req, sizeof(ll_req),
				bna_fw_cb_mtu_set, port);
	bna_mbox_send(port->bna, &port->mbox_qe);
}

void
bna_fw_cb_mtu_set(void *arg, int status)
{
	struct bna_port *port = (struct bna_port *)arg;

	bfa_q_qe_init(&port->mbox_qe.qe);
	bfa_fsm_send_event(port, PORT_E_FWRESP_MTU);
}

static void
bna_port_cb_chld_stopped(void *arg)
{
	struct bna_port *port = (struct bna_port *)arg;

	bfa_fsm_send_event(port, PORT_E_CHLD_STOPPED);
}

static void
bna_port_init(struct bna_port *port, struct bna *bna)
{
	port->bna = bna;
	port->flags = 0;
	port->mtu = 0;
	port->type = BNA_PORT_T_REGULAR;

	port->link_cbfn = bnad_cb_port_link_status;

	port->chld_stop_wc.wc_resume = bna_port_cb_chld_stopped;
	port->chld_stop_wc.wc_cbarg = port;
	port->chld_stop_wc.wc_count = 0;

	port->stop_cbfn = NULL;
	port->stop_cbarg = NULL;

	port->pause_cbfn = NULL;

	port->mtu_cbfn = NULL;

	bfa_q_qe_init(&port->mbox_qe.qe);

	bfa_fsm_set_state(port, bna_port_sm_stopped);

	bna_llport_init(&port->llport, bna);
}

static void
bna_port_uninit(struct bna_port *port)
{
	bna_llport_uninit(&port->llport);

	port->flags = 0;

	port->bna = NULL;
}

static int
bna_port_state_get(struct bna_port *port)
{
	return bfa_sm_to_state(port_sm_table, port->fsm);
}

static void
bna_port_start(struct bna_port *port)
{
	port->flags |= BNA_PORT_F_DEVICE_READY;
	if (port->flags & BNA_PORT_F_ENABLED)
		bfa_fsm_send_event(port, PORT_E_START);
}

static void
bna_port_stop(struct bna_port *port)
{
	port->stop_cbfn = bna_device_cb_port_stopped;
	port->stop_cbarg = &port->bna->device;

	port->flags &= ~BNA_PORT_F_DEVICE_READY;
	bfa_fsm_send_event(port, PORT_E_STOP);
}

static void
bna_port_fail(struct bna_port *port)
{
	port->flags &= ~BNA_PORT_F_DEVICE_READY;
	bfa_fsm_send_event(port, PORT_E_FAIL);
}

void
bna_port_cb_tx_stopped(struct bna_port *port, enum bna_cb_status status)
{
	bfa_wc_down(&port->chld_stop_wc);
}

void
bna_port_cb_rx_stopped(struct bna_port *port, enum bna_cb_status status)
{
	bfa_wc_down(&port->chld_stop_wc);
}

int
bna_port_mtu_get(struct bna_port *port)
{
	return port->mtu;
}

void
bna_port_enable(struct bna_port *port)
{
	if (port->fsm != (bfa_sm_t)bna_port_sm_stopped)
		return;

	port->flags |= BNA_PORT_F_ENABLED;

	if (port->flags & BNA_PORT_F_DEVICE_READY)
		bfa_fsm_send_event(port, PORT_E_START);
}

void
bna_port_disable(struct bna_port *port, enum bna_cleanup_type type,
		 void (*cbfn)(void *, enum bna_cb_status))
{
	if (type == BNA_SOFT_CLEANUP) {
		(*cbfn)(port->bna->bnad, BNA_CB_SUCCESS);
		return;
	}

	port->stop_cbfn = cbfn;
	port->stop_cbarg = port->bna->bnad;

	port->flags &= ~BNA_PORT_F_ENABLED;

	bfa_fsm_send_event(port, PORT_E_STOP);
}

void
bna_port_pause_config(struct bna_port *port,
		      struct bna_pause_config *pause_config,
		      void (*cbfn)(struct bnad *, enum bna_cb_status))
{
	port->pause_config = *pause_config;

	port->pause_cbfn = cbfn;

	bfa_fsm_send_event(port, PORT_E_PAUSE_CFG);
}

void
bna_port_mtu_set(struct bna_port *port, int mtu,
		 void (*cbfn)(struct bnad *, enum bna_cb_status))
{
	port->mtu = mtu;

	port->mtu_cbfn = cbfn;

	bfa_fsm_send_event(port, PORT_E_MTU_CFG);
}

void
bna_port_mac_get(struct bna_port *port, mac_t *mac)
{
	*mac = bfa_nw_ioc_get_mac(&port->bna->device.ioc);
}

/**
 * DEVICE
 */
#define enable_mbox_intr(_device)\
do {\
	u32 intr_status;\
	bna_intr_status_get((_device)->bna, intr_status);\
	bnad_cb_device_enable_mbox_intr((_device)->bna->bnad);\
	bna_mbox_intr_enable((_device)->bna);\
} while (0)

#define disable_mbox_intr(_device)\
do {\
	bna_mbox_intr_disable((_device)->bna);\
	bnad_cb_device_disable_mbox_intr((_device)->bna->bnad);\
} while (0)

static const struct bna_chip_regs_offset reg_offset[] =
{{HOST_PAGE_NUM_FN0, HOSTFN0_INT_STATUS,
	HOSTFN0_INT_MASK, HOST_MSIX_ERR_INDEX_FN0},
{HOST_PAGE_NUM_FN1, HOSTFN1_INT_STATUS,
	HOSTFN1_INT_MASK, HOST_MSIX_ERR_INDEX_FN1},
{HOST_PAGE_NUM_FN2, HOSTFN2_INT_STATUS,
	HOSTFN2_INT_MASK, HOST_MSIX_ERR_INDEX_FN2},
{HOST_PAGE_NUM_FN3, HOSTFN3_INT_STATUS,
	HOSTFN3_INT_MASK, HOST_MSIX_ERR_INDEX_FN3},
};

enum bna_device_event {
	DEVICE_E_ENABLE			= 1,
	DEVICE_E_DISABLE		= 2,
	DEVICE_E_IOC_READY		= 3,
	DEVICE_E_IOC_FAILED		= 4,
	DEVICE_E_IOC_DISABLED		= 5,
	DEVICE_E_IOC_RESET		= 6,
	DEVICE_E_PORT_STOPPED		= 7,
};

enum bna_device_state {
	BNA_DEVICE_STOPPED		= 1,
	BNA_DEVICE_IOC_READY_WAIT 	= 2,
	BNA_DEVICE_READY		= 3,
	BNA_DEVICE_PORT_STOP_WAIT 	= 4,
	BNA_DEVICE_IOC_DISABLE_WAIT 	= 5,
	BNA_DEVICE_FAILED		= 6
};

bfa_fsm_state_decl(bna_device, stopped, struct bna_device,
			enum bna_device_event);
bfa_fsm_state_decl(bna_device, ioc_ready_wait, struct bna_device,
			enum bna_device_event);
bfa_fsm_state_decl(bna_device, ready, struct bna_device,
			enum bna_device_event);
bfa_fsm_state_decl(bna_device, port_stop_wait, struct bna_device,
			enum bna_device_event);
bfa_fsm_state_decl(bna_device, ioc_disable_wait, struct bna_device,
			enum bna_device_event);
bfa_fsm_state_decl(bna_device, failed, struct bna_device,
			enum bna_device_event);

static struct bfa_sm_table device_sm_table[] = {
	{BFA_SM(bna_device_sm_stopped), BNA_DEVICE_STOPPED},
	{BFA_SM(bna_device_sm_ioc_ready_wait), BNA_DEVICE_IOC_READY_WAIT},
	{BFA_SM(bna_device_sm_ready), BNA_DEVICE_READY},
	{BFA_SM(bna_device_sm_port_stop_wait), BNA_DEVICE_PORT_STOP_WAIT},
	{BFA_SM(bna_device_sm_ioc_disable_wait), BNA_DEVICE_IOC_DISABLE_WAIT},
	{BFA_SM(bna_device_sm_failed), BNA_DEVICE_FAILED},
};

static void
bna_device_sm_stopped_entry(struct bna_device *device)
{
	if (device->stop_cbfn)
		device->stop_cbfn(device->stop_cbarg, BNA_CB_SUCCESS);

	device->stop_cbfn = NULL;
	device->stop_cbarg = NULL;
}

static void
bna_device_sm_stopped(struct bna_device *device,
			enum bna_device_event event)
{
	switch (event) {
	case DEVICE_E_ENABLE:
		if (device->intr_type == BNA_INTR_T_MSIX)
			bna_mbox_msix_idx_set(device);
		bfa_nw_ioc_enable(&device->ioc);
		bfa_fsm_set_state(device, bna_device_sm_ioc_ready_wait);
		break;

	case DEVICE_E_DISABLE:
		bfa_fsm_set_state(device, bna_device_sm_stopped);
		break;

	case DEVICE_E_IOC_RESET:
		enable_mbox_intr(device);
		break;

	case DEVICE_E_IOC_FAILED:
		bfa_fsm_set_state(device, bna_device_sm_failed);
		break;

	default:
		bfa_sm_fault(device->bna, event);
	}
}

static void
bna_device_sm_ioc_ready_wait_entry(struct bna_device *device)
{
	/**
	 * Do not call bfa_ioc_enable() here. It must be called in the
	 * previous state due to failed -> ioc_ready_wait transition.
	 */
}

static void
bna_device_sm_ioc_ready_wait(struct bna_device *device,
				enum bna_device_event event)
{
	switch (event) {
	case DEVICE_E_DISABLE:
		if (device->ready_cbfn)
			device->ready_cbfn(device->ready_cbarg,
						BNA_CB_INTERRUPT);
		device->ready_cbfn = NULL;
		device->ready_cbarg = NULL;
		bfa_fsm_set_state(device, bna_device_sm_ioc_disable_wait);
		break;

	case DEVICE_E_IOC_READY:
		bfa_fsm_set_state(device, bna_device_sm_ready);
		break;

	case DEVICE_E_IOC_FAILED:
		bfa_fsm_set_state(device, bna_device_sm_failed);
		break;

	case DEVICE_E_IOC_RESET:
		enable_mbox_intr(device);
		break;

	default:
		bfa_sm_fault(device->bna, event);
	}
}

static void
bna_device_sm_ready_entry(struct bna_device *device)
{
	bna_mbox_mod_start(&device->bna->mbox_mod);
	bna_port_start(&device->bna->port);

	if (device->ready_cbfn)
		device->ready_cbfn(device->ready_cbarg,
					BNA_CB_SUCCESS);
	device->ready_cbfn = NULL;
	device->ready_cbarg = NULL;
}

static void
bna_device_sm_ready(struct bna_device *device, enum bna_device_event event)
{
	switch (event) {
	case DEVICE_E_DISABLE:
		bfa_fsm_set_state(device, bna_device_sm_port_stop_wait);
		break;

	case DEVICE_E_IOC_FAILED:
		bfa_fsm_set_state(device, bna_device_sm_failed);
		break;

	default:
		bfa_sm_fault(device->bna, event);
	}
}

static void
bna_device_sm_port_stop_wait_entry(struct bna_device *device)
{
	bna_port_stop(&device->bna->port);
}

static void
bna_device_sm_port_stop_wait(struct bna_device *device,
				enum bna_device_event event)
{
	switch (event) {
	case DEVICE_E_PORT_STOPPED:
		bna_mbox_mod_stop(&device->bna->mbox_mod);
		bfa_fsm_set_state(device, bna_device_sm_ioc_disable_wait);
		break;

	case DEVICE_E_IOC_FAILED:
		disable_mbox_intr(device);
		bna_port_fail(&device->bna->port);
		break;

	default:
		bfa_sm_fault(device->bna, event);
	}
}

static void
bna_device_sm_ioc_disable_wait_entry(struct bna_device *device)
{
	bfa_nw_ioc_disable(&device->ioc);
}

static void
bna_device_sm_ioc_disable_wait(struct bna_device *device,
				enum bna_device_event event)
{
	switch (event) {
	case DEVICE_E_IOC_DISABLED:
		disable_mbox_intr(device);
		bfa_fsm_set_state(device, bna_device_sm_stopped);
		break;

	default:
		bfa_sm_fault(device->bna, event);
	}
}

static void
bna_device_sm_failed_entry(struct bna_device *device)
{
	disable_mbox_intr(device);
	bna_port_fail(&device->bna->port);
	bna_mbox_mod_stop(&device->bna->mbox_mod);

	if (device->ready_cbfn)
		device->ready_cbfn(device->ready_cbarg,
					BNA_CB_FAIL);
	device->ready_cbfn = NULL;
	device->ready_cbarg = NULL;
}

static void
bna_device_sm_failed(struct bna_device *device,
			enum bna_device_event event)
{
	switch (event) {
	case DEVICE_E_DISABLE:
		bfa_fsm_set_state(device, bna_device_sm_ioc_disable_wait);
		break;

	case DEVICE_E_IOC_RESET:
		enable_mbox_intr(device);
		bfa_fsm_set_state(device, bna_device_sm_ioc_ready_wait);
		break;

	default:
		bfa_sm_fault(device->bna, event);
	}
}

/* IOC callback functions */

static void
bna_device_cb_iocll_ready(void *dev, enum bfa_status error)
{
	struct bna_device *device = (struct bna_device *)dev;

	if (error)
		bfa_fsm_send_event(device, DEVICE_E_IOC_FAILED);
	else
		bfa_fsm_send_event(device, DEVICE_E_IOC_READY);
}

static void
bna_device_cb_iocll_disabled(void *dev)
{
	struct bna_device *device = (struct bna_device *)dev;

	bfa_fsm_send_event(device, DEVICE_E_IOC_DISABLED);
}

static void
bna_device_cb_iocll_failed(void *dev)
{
	struct bna_device *device = (struct bna_device *)dev;

	bfa_fsm_send_event(device, DEVICE_E_IOC_FAILED);
}

static void
bna_device_cb_iocll_reset(void *dev)
{
	struct bna_device *device = (struct bna_device *)dev;

	bfa_fsm_send_event(device, DEVICE_E_IOC_RESET);
}

static struct bfa_ioc_cbfn bfa_iocll_cbfn = {
	bna_device_cb_iocll_ready,
	bna_device_cb_iocll_disabled,
	bna_device_cb_iocll_failed,
	bna_device_cb_iocll_reset
};

/* device */
static void
bna_adv_device_init(struct bna_device *device, struct bna *bna,
		struct bna_res_info *res_info)
{
	u8 *kva;
	u64 dma;

	device->bna = bna;

	kva = res_info[BNA_RES_MEM_T_FWTRC].res_u.mem_info.mdl[0].kva;

	/**
	 * Attach common modules (Diag, SFP, CEE, Port) and claim respective
	 * DMA memory.
	 */
	BNA_GET_DMA_ADDR(
		&res_info[BNA_RES_MEM_T_COM].res_u.mem_info.mdl[0].dma, dma);
	kva = res_info[BNA_RES_MEM_T_COM].res_u.mem_info.mdl[0].kva;

	bfa_nw_cee_attach(&bna->cee, &device->ioc, bna);
	bfa_nw_cee_mem_claim(&bna->cee, kva, dma);
	kva += bfa_nw_cee_meminfo();
	dma += bfa_nw_cee_meminfo();

}

static void
bna_device_init(struct bna_device *device, struct bna *bna,
		struct bna_res_info *res_info)
{
	u64 dma;

	device->bna = bna;

	/**
	 * Attach IOC and claim:
	 *	1. DMA memory for IOC attributes
	 *	2. Kernel memory for FW trace
	 */
	bfa_nw_ioc_attach(&device->ioc, device, &bfa_iocll_cbfn);
	bfa_nw_ioc_pci_init(&device->ioc, &bna->pcidev, BFI_MC_LL);

	BNA_GET_DMA_ADDR(
		&res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.mdl[0].dma, dma);
	bfa_nw_ioc_mem_claim(&device->ioc,
		res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.mdl[0].kva,
			  dma);

	bna_adv_device_init(device, bna, res_info);
	/*
	 * Initialize mbox_mod only after IOC, so that mbox handler
	 * registration goes through
	 */
	device->intr_type =
		res_info[BNA_RES_INTR_T_MBOX].res_u.intr_info.intr_type;
	device->vector =
		res_info[BNA_RES_INTR_T_MBOX].res_u.intr_info.idl[0].vector;
	bna_mbox_mod_init(&bna->mbox_mod, bna);

	device->ready_cbfn = device->stop_cbfn = NULL;
	device->ready_cbarg = device->stop_cbarg = NULL;

	bfa_fsm_set_state(device, bna_device_sm_stopped);
}

static void
bna_device_uninit(struct bna_device *device)
{
	bna_mbox_mod_uninit(&device->bna->mbox_mod);

	bfa_nw_ioc_detach(&device->ioc);

	device->bna = NULL;
}

static void
bna_device_cb_port_stopped(void *arg, enum bna_cb_status status)
{
	struct bna_device *device = (struct bna_device *)arg;

	bfa_fsm_send_event(device, DEVICE_E_PORT_STOPPED);
}

static int
bna_device_status_get(struct bna_device *device)
{
	return device->fsm == (bfa_fsm_t)bna_device_sm_ready;
}

void
bna_device_enable(struct bna_device *device)
{
	if (device->fsm != (bfa_fsm_t)bna_device_sm_stopped) {
		bnad_cb_device_enabled(device->bna->bnad, BNA_CB_BUSY);
		return;
	}

	device->ready_cbfn = bnad_cb_device_enabled;
	device->ready_cbarg = device->bna->bnad;

	bfa_fsm_send_event(device, DEVICE_E_ENABLE);
}

void
bna_device_disable(struct bna_device *device, enum bna_cleanup_type type)
{
	if (type == BNA_SOFT_CLEANUP) {
		bnad_cb_device_disabled(device->bna->bnad, BNA_CB_SUCCESS);
		return;
	}

	device->stop_cbfn = bnad_cb_device_disabled;
	device->stop_cbarg = device->bna->bnad;

	bfa_fsm_send_event(device, DEVICE_E_DISABLE);
}

static int
bna_device_state_get(struct bna_device *device)
{
	return bfa_sm_to_state(device_sm_table, device->fsm);
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

/* utils */

static void
bna_adv_res_req(struct bna_res_info *res_info)
{
	/* DMA memory for COMMON_MODULE */
	res_info[BNA_RES_MEM_T_COM].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_COM].res_u.mem_info.mem_type = BNA_MEM_T_DMA;
	res_info[BNA_RES_MEM_T_COM].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_COM].res_u.mem_info.len = ALIGN(
				bfa_nw_cee_meminfo(), PAGE_SIZE);

	/* Virtual memory for retreiving fw_trc */
	res_info[BNA_RES_MEM_T_FWTRC].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_FWTRC].res_u.mem_info.mem_type = BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_FWTRC].res_u.mem_info.num = 0;
	res_info[BNA_RES_MEM_T_FWTRC].res_u.mem_info.len = 0;

	/* DMA memory for retreiving stats */
	res_info[BNA_RES_MEM_T_STATS].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.mem_type = BNA_MEM_T_DMA;
	res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.len =
				ALIGN(BFI_HW_STATS_SIZE, PAGE_SIZE);

	/* Virtual memory for soft stats */
	res_info[BNA_RES_MEM_T_SWSTATS].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_SWSTATS].res_u.mem_info.mem_type = BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_SWSTATS].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_SWSTATS].res_u.mem_info.len =
				sizeof(struct bna_sw_stats);
}

static void
bna_sw_stats_get(struct bna *bna, struct bna_sw_stats *sw_stats)
{
	struct bna_tx *tx;
	struct bna_txq *txq;
	struct bna_rx *rx;
	struct bna_rxp *rxp;
	struct list_head *qe;
	struct list_head *txq_qe;
	struct list_head *rxp_qe;
	struct list_head *mac_qe;
	int i;

	sw_stats->device_state = bna_device_state_get(&bna->device);
	sw_stats->port_state = bna_port_state_get(&bna->port);
	sw_stats->port_flags = bna->port.flags;
	sw_stats->llport_state = bna_llport_state_get(&bna->port.llport);
	sw_stats->priority = bna->port.priority;

	i = 0;
	list_for_each(qe, &bna->tx_mod.tx_active_q) {
		tx = (struct bna_tx *)qe;
		sw_stats->tx_stats[i].tx_state = bna_tx_state_get(tx);
		sw_stats->tx_stats[i].tx_flags = tx->flags;

		sw_stats->tx_stats[i].num_txqs = 0;
		sw_stats->tx_stats[i].txq_bmap[0] = 0;
		sw_stats->tx_stats[i].txq_bmap[1] = 0;
		list_for_each(txq_qe, &tx->txq_q) {
			txq = (struct bna_txq *)txq_qe;
			if (txq->txq_id < 32)
				sw_stats->tx_stats[i].txq_bmap[0] |=
						((u32)1 << txq->txq_id);
			else
				sw_stats->tx_stats[i].txq_bmap[1] |=
						((u32)
						 1 << (txq->txq_id - 32));
			sw_stats->tx_stats[i].num_txqs++;
		}

		sw_stats->tx_stats[i].txf_id = tx->txf.txf_id;

		i++;
	}
	sw_stats->num_active_tx = i;

	i = 0;
	list_for_each(qe, &bna->rx_mod.rx_active_q) {
		rx = (struct bna_rx *)qe;
		sw_stats->rx_stats[i].rx_state = bna_rx_state_get(rx);
		sw_stats->rx_stats[i].rx_flags = rx->rx_flags;

		sw_stats->rx_stats[i].num_rxps = 0;
		sw_stats->rx_stats[i].num_rxqs = 0;
		sw_stats->rx_stats[i].rxq_bmap[0] = 0;
		sw_stats->rx_stats[i].rxq_bmap[1] = 0;
		sw_stats->rx_stats[i].cq_bmap[0] = 0;
		sw_stats->rx_stats[i].cq_bmap[1] = 0;
		list_for_each(rxp_qe, &rx->rxp_q) {
			rxp = (struct bna_rxp *)rxp_qe;

			sw_stats->rx_stats[i].num_rxqs += 1;

			if (rxp->type == BNA_RXP_SINGLE) {
				if (rxp->rxq.single.only->rxq_id < 32) {
					sw_stats->rx_stats[i].rxq_bmap[0] |=
					((u32)1 <<
					rxp->rxq.single.only->rxq_id);
				} else {
					sw_stats->rx_stats[i].rxq_bmap[1] |=
					((u32)1 <<
					(rxp->rxq.single.only->rxq_id - 32));
				}
			} else {
				if (rxp->rxq.slr.large->rxq_id < 32) {
					sw_stats->rx_stats[i].rxq_bmap[0] |=
					((u32)1 <<
					rxp->rxq.slr.large->rxq_id);
				} else {
					sw_stats->rx_stats[i].rxq_bmap[1] |=
					((u32)1 <<
					(rxp->rxq.slr.large->rxq_id - 32));
				}

				if (rxp->rxq.slr.small->rxq_id < 32) {
					sw_stats->rx_stats[i].rxq_bmap[0] |=
					((u32)1 <<
					rxp->rxq.slr.small->rxq_id);
				} else {
					sw_stats->rx_stats[i].rxq_bmap[1] |=
				((u32)1 <<
				 (rxp->rxq.slr.small->rxq_id - 32));
				}
				sw_stats->rx_stats[i].num_rxqs += 1;
			}

			if (rxp->cq.cq_id < 32)
				sw_stats->rx_stats[i].cq_bmap[0] |=
					(1 << rxp->cq.cq_id);
			else
				sw_stats->rx_stats[i].cq_bmap[1] |=
					(1 << (rxp->cq.cq_id - 32));

			sw_stats->rx_stats[i].num_rxps++;
		}

		sw_stats->rx_stats[i].rxf_id = rx->rxf.rxf_id;
		sw_stats->rx_stats[i].rxf_state = bna_rxf_state_get(&rx->rxf);
		sw_stats->rx_stats[i].rxf_oper_state = rx->rxf.rxf_oper_state;

		sw_stats->rx_stats[i].num_active_ucast = 0;
		if (rx->rxf.ucast_active_mac)
			sw_stats->rx_stats[i].num_active_ucast++;
		list_for_each(mac_qe, &rx->rxf.ucast_active_q)
			sw_stats->rx_stats[i].num_active_ucast++;

		sw_stats->rx_stats[i].num_active_mcast = 0;
		list_for_each(mac_qe, &rx->rxf.mcast_active_q)
			sw_stats->rx_stats[i].num_active_mcast++;

		sw_stats->rx_stats[i].rxmode_active = rx->rxf.rxmode_active;
		sw_stats->rx_stats[i].vlan_filter_status =
						rx->rxf.vlan_filter_status;
		memcpy(sw_stats->rx_stats[i].vlan_filter_table,
				rx->rxf.vlan_filter_table,
				sizeof(u32) * ((BFI_MAX_VLAN + 1) / 32));

		sw_stats->rx_stats[i].rss_status = rx->rxf.rss_status;
		sw_stats->rx_stats[i].hds_status = rx->rxf.hds_status;

		i++;
	}
	sw_stats->num_active_rx = i;
}

static void
bna_fw_cb_stats_get(void *arg, int status)
{
	struct bna *bna = (struct bna *)arg;
	u64 *p_stats;
	int i, count;
	int rxf_count, txf_count;
	u64 rxf_bmap, txf_bmap;

	bfa_q_qe_init(&bna->mbox_qe.qe);

	if (status == 0) {
		p_stats = (u64 *)bna->stats.hw_stats;
		count = sizeof(struct bfi_ll_stats) / sizeof(u64);
		for (i = 0; i < count; i++)
			p_stats[i] = cpu_to_be64(p_stats[i]);

		rxf_count = 0;
		rxf_bmap = (u64)bna->stats.rxf_bmap[0] |
			((u64)bna->stats.rxf_bmap[1] << 32);
		for (i = 0; i < BFI_LL_RXF_ID_MAX; i++)
			if (rxf_bmap & ((u64)1 << i))
				rxf_count++;

		txf_count = 0;
		txf_bmap = (u64)bna->stats.txf_bmap[0] |
			((u64)bna->stats.txf_bmap[1] << 32);
		for (i = 0; i < BFI_LL_TXF_ID_MAX; i++)
			if (txf_bmap & ((u64)1 << i))
				txf_count++;

		p_stats = (u64 *)&bna->stats.hw_stats->rxf_stats[0] +
				((rxf_count * sizeof(struct bfi_ll_stats_rxf) +
				txf_count * sizeof(struct bfi_ll_stats_txf))/
				sizeof(u64));

		/* Populate the TXF stats from the firmware DMAed copy */
		for (i = (BFI_LL_TXF_ID_MAX - 1); i >= 0; i--)
			if (txf_bmap & ((u64)1 << i)) {
				p_stats -= sizeof(struct bfi_ll_stats_txf)/
						sizeof(u64);
				memcpy(&bna->stats.hw_stats->txf_stats[i],
					p_stats,
					sizeof(struct bfi_ll_stats_txf));
			}

		/* Populate the RXF stats from the firmware DMAed copy */
		for (i = (BFI_LL_RXF_ID_MAX - 1); i >= 0; i--)
			if (rxf_bmap & ((u64)1 << i)) {
				p_stats -= sizeof(struct bfi_ll_stats_rxf)/
						sizeof(u64);
				memcpy(&bna->stats.hw_stats->rxf_stats[i],
					p_stats,
					sizeof(struct bfi_ll_stats_rxf));
			}

		bna_sw_stats_get(bna, bna->stats.sw_stats);
		bnad_cb_stats_get(bna->bnad, BNA_CB_SUCCESS, &bna->stats);
	} else
		bnad_cb_stats_get(bna->bnad, BNA_CB_FAIL, &bna->stats);
}

static void
bna_fw_stats_get(struct bna *bna)
{
	struct bfi_ll_stats_req ll_req;

	bfi_h2i_set(ll_req.mh, BFI_MC_LL, BFI_LL_H2I_STATS_GET_REQ, 0);
	ll_req.stats_mask = htons(BFI_LL_STATS_ALL);

	ll_req.rxf_id_mask[0] = htonl(bna->rx_mod.rxf_bmap[0]);
	ll_req.rxf_id_mask[1] =	htonl(bna->rx_mod.rxf_bmap[1]);
	ll_req.txf_id_mask[0] =	htonl(bna->tx_mod.txf_bmap[0]);
	ll_req.txf_id_mask[1] =	htonl(bna->tx_mod.txf_bmap[1]);

	ll_req.host_buffer.a32.addr_hi = bna->hw_stats_dma.msb;
	ll_req.host_buffer.a32.addr_lo = bna->hw_stats_dma.lsb;

	bna_mbox_qe_fill(&bna->mbox_qe, &ll_req, sizeof(ll_req),
				bna_fw_cb_stats_get, bna);
	bna_mbox_send(bna, &bna->mbox_qe);

	bna->stats.rxf_bmap[0] = bna->rx_mod.rxf_bmap[0];
	bna->stats.rxf_bmap[1] = bna->rx_mod.rxf_bmap[1];
	bna->stats.txf_bmap[0] = bna->tx_mod.txf_bmap[0];
	bna->stats.txf_bmap[1] = bna->tx_mod.txf_bmap[1];
}

void
bna_stats_get(struct bna *bna)
{
	if (bna_device_status_get(&bna->device))
		bna_fw_stats_get(bna);
	else
		bnad_cb_stats_get(bna->bnad, BNA_CB_FAIL, &bna->stats);
}

/* IB */
static void
bna_ib_coalescing_timeo_set(struct bna_ib *ib, u8 coalescing_timeo)
{
	ib->ib_config.coalescing_timeo = coalescing_timeo;

	if (ib->start_count)
		ib->door_bell.doorbell_ack = BNA_DOORBELL_IB_INT_ACK(
				(u32)ib->ib_config.coalescing_timeo, 0);
}

/* RxF */
void
bna_rxf_adv_init(struct bna_rxf *rxf,
		struct bna_rx *rx,
		struct bna_rx_config *q_config)
{
	switch (q_config->rxp_type) {
	case BNA_RXP_SINGLE:
		/* No-op */
		break;
	case BNA_RXP_SLR:
		rxf->ctrl_flags |= BNA_RXF_CF_SM_LG_RXQ;
		break;
	case BNA_RXP_HDS:
		rxf->hds_cfg.hdr_type = q_config->hds_config.hdr_type;
		rxf->hds_cfg.header_size =
				q_config->hds_config.header_size;
		rxf->forced_offset = 0;
		break;
	default:
		break;
	}

	if (q_config->rss_status == BNA_STATUS_T_ENABLED) {
		rxf->ctrl_flags |= BNA_RXF_CF_RSS_ENABLE;
		rxf->rss_cfg.hash_type = q_config->rss_config.hash_type;
		rxf->rss_cfg.hash_mask = q_config->rss_config.hash_mask;
		memcpy(&rxf->rss_cfg.toeplitz_hash_key[0],
			&q_config->rss_config.toeplitz_hash_key[0],
			sizeof(rxf->rss_cfg.toeplitz_hash_key));
	}
}

static void
rxf_fltr_mbox_cmd(struct bna_rxf *rxf, u8 cmd, enum bna_status status)
{
	struct bfi_ll_rxf_req req;

	bfi_h2i_set(req.mh, BFI_MC_LL, cmd, 0);

	req.rxf_id = rxf->rxf_id;
	req.enable = status;

	bna_mbox_qe_fill(&rxf->mbox_qe, &req, sizeof(req),
			rxf_cb_cam_fltr_mbox_cmd, rxf);

	bna_mbox_send(rxf->rx->bna, &rxf->mbox_qe);
}

int
rxf_process_packet_filter_ucast(struct bna_rxf *rxf)
{
	struct bna_mac *mac = NULL;
	struct list_head *qe;

	/* Add additional MAC entries */
	if (!list_empty(&rxf->ucast_pending_add_q)) {
		bfa_q_deq(&rxf->ucast_pending_add_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_UCAST_ADD_REQ, mac);
		list_add_tail(&mac->qe, &rxf->ucast_active_q);
		return 1;
	}

	/* Delete MAC addresses previousely added */
	if (!list_empty(&rxf->ucast_pending_del_q)) {
		bfa_q_deq(&rxf->ucast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_UCAST_DEL_REQ, mac);
		bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod, mac);
		return 1;
	}

	return 0;
}

int
rxf_process_packet_filter_promisc(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;

	/* Enable/disable promiscuous mode */
	if (is_promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* move promisc configuration from pending -> active */
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active |= BNA_RXMODE_PROMISC;

		/* Disable VLAN filter to allow all VLANs */
		__rxf_vlan_filter_set(rxf, BNA_STATUS_T_DISABLED);
		rxf_fltr_mbox_cmd(rxf, BFI_LL_H2I_RXF_PROMISCUOUS_SET_REQ,
				BNA_STATUS_T_ENABLED);
		return 1;
	} else if (is_promisc_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* move promisc configuration from pending -> active */
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_PROMISC;
		bna->rxf_promisc_id = BFI_MAX_RXF;

		/* Revert VLAN filter */
		__rxf_vlan_filter_set(rxf, rxf->vlan_filter_status);
		rxf_fltr_mbox_cmd(rxf, BFI_LL_H2I_RXF_PROMISCUOUS_SET_REQ,
				BNA_STATUS_T_DISABLED);
		return 1;
	}

	return 0;
}

int
rxf_process_packet_filter_allmulti(struct bna_rxf *rxf)
{
	/* Enable/disable allmulti mode */
	if (is_allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* move allmulti configuration from pending -> active */
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active |= BNA_RXMODE_ALLMULTI;

		rxf_fltr_mbox_cmd(rxf, BFI_LL_H2I_MAC_MCAST_FILTER_REQ,
				BNA_STATUS_T_ENABLED);
		return 1;
	} else if (is_allmulti_disable(rxf->rxmode_pending,
					rxf->rxmode_pending_bitmask)) {
		/* move allmulti configuration from pending -> active */
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_ALLMULTI;

		rxf_fltr_mbox_cmd(rxf, BFI_LL_H2I_MAC_MCAST_FILTER_REQ,
				BNA_STATUS_T_DISABLED);
		return 1;
	}

	return 0;
}

int
rxf_clear_packet_filter_ucast(struct bna_rxf *rxf)
{
	struct bna_mac *mac = NULL;
	struct list_head *qe;

	/* 1. delete pending ucast entries */
	if (!list_empty(&rxf->ucast_pending_del_q)) {
		bfa_q_deq(&rxf->ucast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_UCAST_DEL_REQ, mac);
		bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod, mac);
		return 1;
	}

	/* 2. clear active ucast entries; move them to pending_add_q */
	if (!list_empty(&rxf->ucast_active_q)) {
		bfa_q_deq(&rxf->ucast_active_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_UCAST_DEL_REQ, mac);
		list_add_tail(&mac->qe, &rxf->ucast_pending_add_q);
		return 1;
	}

	return 0;
}

int
rxf_clear_packet_filter_promisc(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;

	/* 6. Execute pending promisc mode disable command */
	if (is_promisc_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* move promisc configuration from pending -> active */
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_PROMISC;
		bna->rxf_promisc_id = BFI_MAX_RXF;

		/* Revert VLAN filter */
		__rxf_vlan_filter_set(rxf, rxf->vlan_filter_status);
		rxf_fltr_mbox_cmd(rxf, BFI_LL_H2I_RXF_PROMISCUOUS_SET_REQ,
				BNA_STATUS_T_DISABLED);
		return 1;
	}

	/* 7. Clear active promisc mode; move it to pending enable */
	if (rxf->rxmode_active & BNA_RXMODE_PROMISC) {
		/* move promisc configuration from active -> pending */
		promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_PROMISC;

		/* Revert VLAN filter */
		__rxf_vlan_filter_set(rxf, rxf->vlan_filter_status);
		rxf_fltr_mbox_cmd(rxf, BFI_LL_H2I_RXF_PROMISCUOUS_SET_REQ,
				BNA_STATUS_T_DISABLED);
		return 1;
	}

	return 0;
}

int
rxf_clear_packet_filter_allmulti(struct bna_rxf *rxf)
{
	/* 10. Execute pending allmulti mode disable command */
	if (is_allmulti_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* move allmulti configuration from pending -> active */
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_ALLMULTI;
		rxf_fltr_mbox_cmd(rxf, BFI_LL_H2I_MAC_MCAST_FILTER_REQ,
				BNA_STATUS_T_DISABLED);
		return 1;
	}

	/* 11. Clear active allmulti mode; move it to pending enable */
	if (rxf->rxmode_active & BNA_RXMODE_ALLMULTI) {
		/* move allmulti configuration from active -> pending */
		allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_ALLMULTI;
		rxf_fltr_mbox_cmd(rxf, BFI_LL_H2I_MAC_MCAST_FILTER_REQ,
				BNA_STATUS_T_DISABLED);
		return 1;
	}

	return 0;
}

void
rxf_reset_packet_filter_ucast(struct bna_rxf *rxf)
{
	struct list_head *qe;
	struct bna_mac *mac;

	/* 1. Move active ucast entries to pending_add_q */
	while (!list_empty(&rxf->ucast_active_q)) {
		bfa_q_deq(&rxf->ucast_active_q, &qe);
		bfa_q_qe_init(qe);
		list_add_tail(qe, &rxf->ucast_pending_add_q);
	}

	/* 2. Throw away delete pending ucast entries */
	while (!list_empty(&rxf->ucast_pending_del_q)) {
		bfa_q_deq(&rxf->ucast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod, mac);
	}
}

void
rxf_reset_packet_filter_promisc(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;

	/* 6. Clear pending promisc mode disable */
	if (is_promisc_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_PROMISC;
		bna->rxf_promisc_id = BFI_MAX_RXF;
	}

	/* 7. Move promisc mode config from active -> pending */
	if (rxf->rxmode_active & BNA_RXMODE_PROMISC) {
		promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_PROMISC;
	}

}

void
rxf_reset_packet_filter_allmulti(struct bna_rxf *rxf)
{
	/* 10. Clear pending allmulti mode disable */
	if (is_allmulti_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_ALLMULTI;
	}

	/* 11. Move allmulti mode config from active -> pending */
	if (rxf->rxmode_active & BNA_RXMODE_ALLMULTI) {
		allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		rxf->rxmode_active &= ~BNA_RXMODE_ALLMULTI;
	}
}

/**
 * Should only be called by bna_rxf_mode_set.
 * Helps deciding if h/w configuration is needed or not.
 *  Returns:
 *	0 = no h/w change
 *	1 = need h/w change
 */
static int
rxf_promisc_enable(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;
	int ret = 0;

	/* There can not be any pending disable command */

	/* Do nothing if pending enable or already enabled */
	if (is_promisc_enable(rxf->rxmode_pending,
			rxf->rxmode_pending_bitmask) ||
			(rxf->rxmode_active & BNA_RXMODE_PROMISC)) {
		/* Schedule enable */
	} else {
		/* Promisc mode should not be active in the system */
		promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		bna->rxf_promisc_id = rxf->rxf_id;
		ret = 1;
	}

	return ret;
}

/**
 * Should only be called by bna_rxf_mode_set.
 * Helps deciding if h/w configuration is needed or not.
 *  Returns:
 *	0 = no h/w change
 *	1 = need h/w change
 */
static int
rxf_promisc_disable(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;
	int ret = 0;

	/* There can not be any pending disable */

	/* Turn off pending enable command , if any */
	if (is_promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* Promisc mode should not be active */
		/* system promisc state should be pending */
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		/* Remove the promisc state from the system */
		bna->rxf_promisc_id = BFI_MAX_RXF;

		/* Schedule disable */
	} else if (rxf->rxmode_active & BNA_RXMODE_PROMISC) {
		/* Promisc mode should be active in the system */
		promisc_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		ret = 1;

	/* Do nothing if already disabled */
	} else {
	}

	return ret;
}

/**
 * Should only be called by bna_rxf_mode_set.
 * Helps deciding if h/w configuration is needed or not.
 *  Returns:
 *	0 = no h/w change
 *	1 = need h/w change
 */
static int
rxf_allmulti_enable(struct bna_rxf *rxf)
{
	int ret = 0;

	/* There can not be any pending disable command */

	/* Do nothing if pending enable or already enabled */
	if (is_allmulti_enable(rxf->rxmode_pending,
			rxf->rxmode_pending_bitmask) ||
			(rxf->rxmode_active & BNA_RXMODE_ALLMULTI)) {
		/* Schedule enable */
	} else {
		allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		ret = 1;
	}

	return ret;
}

/**
 * Should only be called by bna_rxf_mode_set.
 * Helps deciding if h/w configuration is needed or not.
 *  Returns:
 *	0 = no h/w change
 *	1 = need h/w change
 */
static int
rxf_allmulti_disable(struct bna_rxf *rxf)
{
	int ret = 0;

	/* There can not be any pending disable */

	/* Turn off pending enable command , if any */
	if (is_allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* Allmulti mode should not be active */
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);

	/* Schedule disable */
	} else if (rxf->rxmode_active & BNA_RXMODE_ALLMULTI) {
		allmulti_disable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		ret = 1;
	}

	return ret;
}

/* RxF <- bnad */
enum bna_cb_status
bna_rx_mode_set(struct bna_rx *rx, enum bna_rxmode new_mode,
		enum bna_rxmode bitmask,
		void (*cbfn)(struct bnad *, struct bna_rx *,
			     enum bna_cb_status))
{
	struct bna_rxf *rxf = &rx->rxf;
	int need_hw_config = 0;

	/* Process the commands */

	if (is_promisc_enable(new_mode, bitmask)) {
		/* If promisc mode is already enabled elsewhere in the system */
		if ((rx->bna->rxf_promisc_id != BFI_MAX_RXF) &&
			(rx->bna->rxf_promisc_id != rxf->rxf_id))
			goto err_return;
		if (rxf_promisc_enable(rxf))
			need_hw_config = 1;
	} else if (is_promisc_disable(new_mode, bitmask)) {
		if (rxf_promisc_disable(rxf))
			need_hw_config = 1;
	}

	if (is_allmulti_enable(new_mode, bitmask)) {
		if (rxf_allmulti_enable(rxf))
			need_hw_config = 1;
	} else if (is_allmulti_disable(new_mode, bitmask)) {
		if (rxf_allmulti_disable(rxf))
			need_hw_config = 1;
	}

	/* Trigger h/w if needed */

	if (need_hw_config) {
		rxf->cam_fltr_cbfn = cbfn;
		rxf->cam_fltr_cbarg = rx->bna->bnad;
		bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_MOD);
	} else if (cbfn)
		(*cbfn)(rx->bna->bnad, rx, BNA_CB_SUCCESS);

	return BNA_CB_SUCCESS;

err_return:
	return BNA_CB_FAIL;
}

void
/* RxF <- bnad */
bna_rx_vlanfilter_enable(struct bna_rx *rx)
{
	struct bna_rxf *rxf = &rx->rxf;

	if (rxf->vlan_filter_status == BNA_STATUS_T_DISABLED) {
		rxf->rxf_flags |= BNA_RXF_FL_VLAN_CONFIG_PENDING;
		rxf->vlan_filter_status = BNA_STATUS_T_ENABLED;
		bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_MOD);
	}
}

/* Rx */

/* Rx <- bnad */
void
bna_rx_coalescing_timeo_set(struct bna_rx *rx, int coalescing_timeo)
{
	struct bna_rxp *rxp;
	struct list_head *qe;

	list_for_each(qe, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe;
		rxp->cq.ccb->rx_coalescing_timeo = coalescing_timeo;
		bna_ib_coalescing_timeo_set(rxp->cq.ib, coalescing_timeo);
	}
}

/* Rx <- bnad */
void
bna_rx_dim_reconfig(struct bna *bna, const u32 vector[][BNA_BIAS_T_MAX])
{
	int i, j;

	for (i = 0; i < BNA_LOAD_T_MAX; i++)
		for (j = 0; j < BNA_BIAS_T_MAX; j++)
			bna->rx_mod.dim_vector[i][j] = vector[i][j];
}

/* Rx <- bnad */
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
	bna_ib_coalescing_timeo_set(ccb->cq->ib, coalescing_timeo);
}

/* Tx */
/* TX <- bnad */
void
bna_tx_coalescing_timeo_set(struct bna_tx *tx, int coalescing_timeo)
{
	struct bna_txq *txq;
	struct list_head *qe;

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		bna_ib_coalescing_timeo_set(txq->ib, coalescing_timeo);
	}
}

/*
 * Private data
 */

struct bna_ritseg_pool_cfg {
	u32	pool_size;
	u32	pool_entry_size;
};
init_ritseg_pool(ritseg_pool_cfg);

/*
 * Private functions
 */
static void
bna_ucam_mod_init(struct bna_ucam_mod *ucam_mod, struct bna *bna,
		  struct bna_res_info *res_info)
{
	int i;

	ucam_mod->ucmac = (struct bna_mac *)
		res_info[BNA_RES_MEM_T_UCMAC_ARRAY].res_u.mem_info.mdl[0].kva;

	INIT_LIST_HEAD(&ucam_mod->free_q);
	for (i = 0; i < BFI_MAX_UCMAC; i++) {
		bfa_q_qe_init(&ucam_mod->ucmac[i].qe);
		list_add_tail(&ucam_mod->ucmac[i].qe, &ucam_mod->free_q);
	}

	ucam_mod->bna = bna;
}

static void
bna_ucam_mod_uninit(struct bna_ucam_mod *ucam_mod)
{
	struct list_head *qe;
	int i = 0;

	list_for_each(qe, &ucam_mod->free_q)
		i++;

	ucam_mod->bna = NULL;
}

static void
bna_mcam_mod_init(struct bna_mcam_mod *mcam_mod, struct bna *bna,
		  struct bna_res_info *res_info)
{
	int i;

	mcam_mod->mcmac = (struct bna_mac *)
		res_info[BNA_RES_MEM_T_MCMAC_ARRAY].res_u.mem_info.mdl[0].kva;

	INIT_LIST_HEAD(&mcam_mod->free_q);
	for (i = 0; i < BFI_MAX_MCMAC; i++) {
		bfa_q_qe_init(&mcam_mod->mcmac[i].qe);
		list_add_tail(&mcam_mod->mcmac[i].qe, &mcam_mod->free_q);
	}

	mcam_mod->bna = bna;
}

static void
bna_mcam_mod_uninit(struct bna_mcam_mod *mcam_mod)
{
	struct list_head *qe;
	int i = 0;

	list_for_each(qe, &mcam_mod->free_q)
		i++;

	mcam_mod->bna = NULL;
}

static void
bna_rit_mod_init(struct bna_rit_mod *rit_mod,
		struct bna_res_info *res_info)
{
	int i;
	int j;
	int count;
	int offset;

	rit_mod->rit = (struct bna_rit_entry *)
		res_info[BNA_RES_MEM_T_RIT_ENTRY].res_u.mem_info.mdl[0].kva;
	rit_mod->rit_segment = (struct bna_rit_segment *)
		res_info[BNA_RES_MEM_T_RIT_SEGMENT].res_u.mem_info.mdl[0].kva;

	count = 0;
	offset = 0;
	for (i = 0; i < BFI_RIT_SEG_TOTAL_POOLS; i++) {
		INIT_LIST_HEAD(&rit_mod->rit_seg_pool[i]);
		for (j = 0; j < ritseg_pool_cfg[i].pool_size; j++) {
			bfa_q_qe_init(&rit_mod->rit_segment[count].qe);
			rit_mod->rit_segment[count].max_rit_size =
					ritseg_pool_cfg[i].pool_entry_size;
			rit_mod->rit_segment[count].rit_offset = offset;
			rit_mod->rit_segment[count].rit =
					&rit_mod->rit[offset];
			list_add_tail(&rit_mod->rit_segment[count].qe,
				&rit_mod->rit_seg_pool[i]);
			count++;
			offset += ritseg_pool_cfg[i].pool_entry_size;
		}
	}
}

static void
bna_rit_mod_uninit(struct bna_rit_mod *rit_mod)
{
	struct bna_rit_segment *rit_segment;
	struct list_head *qe;
	int i;
	int j;

	for (i = 0; i < BFI_RIT_SEG_TOTAL_POOLS; i++) {
		j = 0;
		list_for_each(qe, &rit_mod->rit_seg_pool[i]) {
			rit_segment = (struct bna_rit_segment *)qe;
			j++;
		}
	}
}

/*
 * Public functions
 */

/* Called during probe(), before calling bna_init() */
void
bna_res_req(struct bna_res_info *res_info)
{
	bna_adv_res_req(res_info);

	/* DMA memory for retrieving IOC attributes */
	res_info[BNA_RES_MEM_T_ATTR].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.mem_type = BNA_MEM_T_DMA;
	res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_ATTR].res_u.mem_info.len =
				ALIGN(bfa_nw_ioc_meminfo(), PAGE_SIZE);

	/* DMA memory for index segment of an IB */
	res_info[BNA_RES_MEM_T_IBIDX].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_IBIDX].res_u.mem_info.mem_type = BNA_MEM_T_DMA;
	res_info[BNA_RES_MEM_T_IBIDX].res_u.mem_info.len =
				BFI_IBIDX_SIZE * BFI_IBIDX_MAX_SEGSIZE;
	res_info[BNA_RES_MEM_T_IBIDX].res_u.mem_info.num = BFI_MAX_IB;

	/* Virtual memory for IB objects - stored by IB module */
	res_info[BNA_RES_MEM_T_IB_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_IB_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_IB_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_IB_ARRAY].res_u.mem_info.len =
				BFI_MAX_IB * sizeof(struct bna_ib);

	/* Virtual memory for intr objects - stored by IB module */
	res_info[BNA_RES_MEM_T_INTR_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_INTR_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_INTR_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_INTR_ARRAY].res_u.mem_info.len =
				BFI_MAX_IB * sizeof(struct bna_intr);

	/* Virtual memory for idx_seg objects - stored by IB module */
	res_info[BNA_RES_MEM_T_IDXSEG_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_IDXSEG_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_IDXSEG_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_IDXSEG_ARRAY].res_u.mem_info.len =
			BFI_IBIDX_TOTAL_SEGS * sizeof(struct bna_ibidx_seg);

	/* Virtual memory for Tx objects - stored by Tx module */
	res_info[BNA_RES_MEM_T_TX_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_TX_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_TX_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_TX_ARRAY].res_u.mem_info.len =
			BFI_MAX_TXQ * sizeof(struct bna_tx);

	/* Virtual memory for TxQ - stored by Tx module */
	res_info[BNA_RES_MEM_T_TXQ_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_TXQ_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_TXQ_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_TXQ_ARRAY].res_u.mem_info.len =
			BFI_MAX_TXQ * sizeof(struct bna_txq);

	/* Virtual memory for Rx objects - stored by Rx module */
	res_info[BNA_RES_MEM_T_RX_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_RX_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_RX_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_RX_ARRAY].res_u.mem_info.len =
			BFI_MAX_RXQ * sizeof(struct bna_rx);

	/* Virtual memory for RxPath - stored by Rx module */
	res_info[BNA_RES_MEM_T_RXP_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_RXP_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_RXP_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_RXP_ARRAY].res_u.mem_info.len =
			BFI_MAX_RXQ * sizeof(struct bna_rxp);

	/* Virtual memory for RxQ - stored by Rx module */
	res_info[BNA_RES_MEM_T_RXQ_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_RXQ_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_RXQ_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_RXQ_ARRAY].res_u.mem_info.len =
			BFI_MAX_RXQ * sizeof(struct bna_rxq);

	/* Virtual memory for Unicast MAC address - stored by ucam module */
	res_info[BNA_RES_MEM_T_UCMAC_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_UCMAC_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_UCMAC_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_UCMAC_ARRAY].res_u.mem_info.len =
			BFI_MAX_UCMAC * sizeof(struct bna_mac);

	/* Virtual memory for Multicast MAC address - stored by mcam module */
	res_info[BNA_RES_MEM_T_MCMAC_ARRAY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_MCMAC_ARRAY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_MCMAC_ARRAY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_MCMAC_ARRAY].res_u.mem_info.len =
			BFI_MAX_MCMAC * sizeof(struct bna_mac);

	/* Virtual memory for RIT entries */
	res_info[BNA_RES_MEM_T_RIT_ENTRY].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_RIT_ENTRY].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_RIT_ENTRY].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_RIT_ENTRY].res_u.mem_info.len =
			BFI_MAX_RIT_SIZE * sizeof(struct bna_rit_entry);

	/* Virtual memory for RIT segment table */
	res_info[BNA_RES_MEM_T_RIT_SEGMENT].res_type = BNA_RES_T_MEM;
	res_info[BNA_RES_MEM_T_RIT_SEGMENT].res_u.mem_info.mem_type =
								BNA_MEM_T_KVA;
	res_info[BNA_RES_MEM_T_RIT_SEGMENT].res_u.mem_info.num = 1;
	res_info[BNA_RES_MEM_T_RIT_SEGMENT].res_u.mem_info.len =
			BFI_RIT_TOTAL_SEGS * sizeof(struct bna_rit_segment);

	/* Interrupt resource for mailbox interrupt */
	res_info[BNA_RES_INTR_T_MBOX].res_type = BNA_RES_T_INTR;
	res_info[BNA_RES_INTR_T_MBOX].res_u.intr_info.intr_type =
							BNA_INTR_T_MSIX;
	res_info[BNA_RES_INTR_T_MBOX].res_u.intr_info.num = 1;
}

/* Called during probe() */
void
bna_init(struct bna *bna, struct bnad *bnad, struct bfa_pcidev *pcidev,
		struct bna_res_info *res_info)
{
	bna->bnad = bnad;
	bna->pcidev = *pcidev;

	bna->stats.hw_stats = (struct bfi_ll_stats *)
		res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.mdl[0].kva;
	bna->hw_stats_dma.msb =
		res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.mdl[0].dma.msb;
	bna->hw_stats_dma.lsb =
		res_info[BNA_RES_MEM_T_STATS].res_u.mem_info.mdl[0].dma.lsb;
	bna->stats.sw_stats = (struct bna_sw_stats *)
		res_info[BNA_RES_MEM_T_SWSTATS].res_u.mem_info.mdl[0].kva;

	bna->regs.page_addr = bna->pcidev.pci_bar_kva +
				reg_offset[bna->pcidev.pci_func].page_addr;
	bna->regs.fn_int_status = bna->pcidev.pci_bar_kva +
				reg_offset[bna->pcidev.pci_func].fn_int_status;
	bna->regs.fn_int_mask = bna->pcidev.pci_bar_kva +
				reg_offset[bna->pcidev.pci_func].fn_int_mask;

	if (bna->pcidev.pci_func < 3)
		bna->port_num = 0;
	else
		bna->port_num = 1;

	/* Also initializes diag, cee, sfp, phy_port and mbox_mod */
	bna_device_init(&bna->device, bna, res_info);

	bna_port_init(&bna->port, bna);

	bna_tx_mod_init(&bna->tx_mod, bna, res_info);

	bna_rx_mod_init(&bna->rx_mod, bna, res_info);

	bna_ib_mod_init(&bna->ib_mod, bna, res_info);

	bna_rit_mod_init(&bna->rit_mod, res_info);

	bna_ucam_mod_init(&bna->ucam_mod, bna, res_info);

	bna_mcam_mod_init(&bna->mcam_mod, bna, res_info);

	bna->rxf_promisc_id = BFI_MAX_RXF;

	/* Mbox q element for posting stat request to f/w */
	bfa_q_qe_init(&bna->mbox_qe.qe);
}

void
bna_uninit(struct bna *bna)
{
	bna_mcam_mod_uninit(&bna->mcam_mod);

	bna_ucam_mod_uninit(&bna->ucam_mod);

	bna_rit_mod_uninit(&bna->rit_mod);

	bna_ib_mod_uninit(&bna->ib_mod);

	bna_rx_mod_uninit(&bna->rx_mod);

	bna_tx_mod_uninit(&bna->tx_mod);

	bna_port_uninit(&bna->port);

	bna_device_uninit(&bna->device);

	bna->bnad = NULL;
}

struct bna_mac *
bna_ucam_mod_mac_get(struct bna_ucam_mod *ucam_mod)
{
	struct list_head *qe;

	if (list_empty(&ucam_mod->free_q))
		return NULL;

	bfa_q_deq(&ucam_mod->free_q, &qe);

	return (struct bna_mac *)qe;
}

void
bna_ucam_mod_mac_put(struct bna_ucam_mod *ucam_mod, struct bna_mac *mac)
{
	list_add_tail(&mac->qe, &ucam_mod->free_q);
}

struct bna_mac *
bna_mcam_mod_mac_get(struct bna_mcam_mod *mcam_mod)
{
	struct list_head *qe;

	if (list_empty(&mcam_mod->free_q))
		return NULL;

	bfa_q_deq(&mcam_mod->free_q, &qe);

	return (struct bna_mac *)qe;
}

void
bna_mcam_mod_mac_put(struct bna_mcam_mod *mcam_mod, struct bna_mac *mac)
{
	list_add_tail(&mac->qe, &mcam_mod->free_q);
}

/**
 * Note: This should be called in the same locking context as the call to
 * bna_rit_mod_seg_get()
 */
int
bna_rit_mod_can_satisfy(struct bna_rit_mod *rit_mod, int seg_size)
{
	int i;

	/* Select the pool for seg_size */
	for (i = 0; i < BFI_RIT_SEG_TOTAL_POOLS; i++) {
		if (seg_size <= ritseg_pool_cfg[i].pool_entry_size)
			break;
	}

	if (i == BFI_RIT_SEG_TOTAL_POOLS)
		return 0;

	if (list_empty(&rit_mod->rit_seg_pool[i]))
		return 0;

	return 1;
}

struct bna_rit_segment *
bna_rit_mod_seg_get(struct bna_rit_mod *rit_mod, int seg_size)
{
	struct bna_rit_segment *seg;
	struct list_head *qe;
	int i;

	/* Select the pool for seg_size */
	for (i = 0; i < BFI_RIT_SEG_TOTAL_POOLS; i++) {
		if (seg_size <= ritseg_pool_cfg[i].pool_entry_size)
			break;
	}

	if (i == BFI_RIT_SEG_TOTAL_POOLS)
		return NULL;

	if (list_empty(&rit_mod->rit_seg_pool[i]))
		return NULL;

	bfa_q_deq(&rit_mod->rit_seg_pool[i], &qe);
	seg = (struct bna_rit_segment *)qe;
	bfa_q_qe_init(&seg->qe);
	seg->rit_size = seg_size;

	return seg;
}

void
bna_rit_mod_seg_put(struct bna_rit_mod *rit_mod,
			struct bna_rit_segment *seg)
{
	int i;

	/* Select the pool for seg->max_rit_size */
	for (i = 0; i < BFI_RIT_SEG_TOTAL_POOLS; i++) {
		if (seg->max_rit_size == ritseg_pool_cfg[i].pool_entry_size)
			break;
	}

	seg->rit_size = 0;
	list_add_tail(&seg->qe, &rit_mod->rit_seg_pool[i]);
}
