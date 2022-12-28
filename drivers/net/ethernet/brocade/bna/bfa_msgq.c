// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */

/* MSGQ module source file. */

#include "bfi.h"
#include "bfa_msgq.h"
#include "bfa_ioc.h"

#define call_cmdq_ent_cbfn(_cmdq_ent, _status)				\
{									\
	bfa_msgq_cmdcbfn_t cbfn;					\
	void *cbarg;							\
	cbfn = (_cmdq_ent)->cbfn;					\
	cbarg = (_cmdq_ent)->cbarg;					\
	(_cmdq_ent)->cbfn = NULL;					\
	(_cmdq_ent)->cbarg = NULL;					\
	if (cbfn) {							\
		cbfn(cbarg, (_status));					\
	}								\
}

static void bfa_msgq_cmdq_dbell(struct bfa_msgq_cmdq *cmdq);
static void bfa_msgq_cmdq_copy_rsp(struct bfa_msgq_cmdq *cmdq);

enum cmdq_event {
	CMDQ_E_START			= 1,
	CMDQ_E_STOP			= 2,
	CMDQ_E_FAIL			= 3,
	CMDQ_E_POST			= 4,
	CMDQ_E_INIT_RESP		= 5,
	CMDQ_E_DB_READY			= 6,
};

bfa_fsm_state_decl(cmdq, stopped, struct bfa_msgq_cmdq, enum cmdq_event);
bfa_fsm_state_decl(cmdq, init_wait, struct bfa_msgq_cmdq, enum cmdq_event);
bfa_fsm_state_decl(cmdq, ready, struct bfa_msgq_cmdq, enum cmdq_event);
bfa_fsm_state_decl(cmdq, dbell_wait, struct bfa_msgq_cmdq,
			enum cmdq_event);

static void
cmdq_sm_stopped_entry(struct bfa_msgq_cmdq *cmdq)
{
	struct bfa_msgq_cmd_entry *cmdq_ent;

	cmdq->producer_index = 0;
	cmdq->consumer_index = 0;
	cmdq->flags = 0;
	cmdq->token = 0;
	cmdq->offset = 0;
	cmdq->bytes_to_copy = 0;
	while (!list_empty(&cmdq->pending_q)) {
		cmdq_ent = list_first_entry(&cmdq->pending_q,
					    struct bfa_msgq_cmd_entry, qe);
		list_del(&cmdq_ent->qe);
		call_cmdq_ent_cbfn(cmdq_ent, BFA_STATUS_FAILED);
	}
}

static void
cmdq_sm_stopped(struct bfa_msgq_cmdq *cmdq, enum cmdq_event event)
{
	switch (event) {
	case CMDQ_E_START:
		bfa_fsm_set_state(cmdq, cmdq_sm_init_wait);
		break;

	case CMDQ_E_STOP:
	case CMDQ_E_FAIL:
		/* No-op */
		break;

	case CMDQ_E_POST:
		cmdq->flags |= BFA_MSGQ_CMDQ_F_DB_UPDATE;
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
cmdq_sm_init_wait_entry(struct bfa_msgq_cmdq *cmdq)
{
	bfa_wc_down(&cmdq->msgq->init_wc);
}

static void
cmdq_sm_init_wait(struct bfa_msgq_cmdq *cmdq, enum cmdq_event event)
{
	switch (event) {
	case CMDQ_E_STOP:
	case CMDQ_E_FAIL:
		bfa_fsm_set_state(cmdq, cmdq_sm_stopped);
		break;

	case CMDQ_E_POST:
		cmdq->flags |= BFA_MSGQ_CMDQ_F_DB_UPDATE;
		break;

	case CMDQ_E_INIT_RESP:
		if (cmdq->flags & BFA_MSGQ_CMDQ_F_DB_UPDATE) {
			cmdq->flags &= ~BFA_MSGQ_CMDQ_F_DB_UPDATE;
			bfa_fsm_set_state(cmdq, cmdq_sm_dbell_wait);
		} else
			bfa_fsm_set_state(cmdq, cmdq_sm_ready);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
cmdq_sm_ready_entry(struct bfa_msgq_cmdq *cmdq)
{
}

static void
cmdq_sm_ready(struct bfa_msgq_cmdq *cmdq, enum cmdq_event event)
{
	switch (event) {
	case CMDQ_E_STOP:
	case CMDQ_E_FAIL:
		bfa_fsm_set_state(cmdq, cmdq_sm_stopped);
		break;

	case CMDQ_E_POST:
		bfa_fsm_set_state(cmdq, cmdq_sm_dbell_wait);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
cmdq_sm_dbell_wait_entry(struct bfa_msgq_cmdq *cmdq)
{
	bfa_msgq_cmdq_dbell(cmdq);
}

static void
cmdq_sm_dbell_wait(struct bfa_msgq_cmdq *cmdq, enum cmdq_event event)
{
	switch (event) {
	case CMDQ_E_STOP:
	case CMDQ_E_FAIL:
		bfa_fsm_set_state(cmdq, cmdq_sm_stopped);
		break;

	case CMDQ_E_POST:
		cmdq->flags |= BFA_MSGQ_CMDQ_F_DB_UPDATE;
		break;

	case CMDQ_E_DB_READY:
		if (cmdq->flags & BFA_MSGQ_CMDQ_F_DB_UPDATE) {
			cmdq->flags &= ~BFA_MSGQ_CMDQ_F_DB_UPDATE;
			bfa_fsm_set_state(cmdq, cmdq_sm_dbell_wait);
		} else
			bfa_fsm_set_state(cmdq, cmdq_sm_ready);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_msgq_cmdq_dbell_ready(void *arg)
{
	struct bfa_msgq_cmdq *cmdq = (struct bfa_msgq_cmdq *)arg;
	bfa_fsm_send_event(cmdq, CMDQ_E_DB_READY);
}

static void
bfa_msgq_cmdq_dbell(struct bfa_msgq_cmdq *cmdq)
{
	struct bfi_msgq_h2i_db *dbell =
		(struct bfi_msgq_h2i_db *)(&cmdq->dbell_mb.msg[0]);

	memset(dbell, 0, sizeof(struct bfi_msgq_h2i_db));
	bfi_h2i_set(dbell->mh, BFI_MC_MSGQ, BFI_MSGQ_H2I_DOORBELL_PI, 0);
	dbell->mh.mtag.i2htok = 0;
	dbell->idx.cmdq_pi = htons(cmdq->producer_index);

	if (!bfa_nw_ioc_mbox_queue(cmdq->msgq->ioc, &cmdq->dbell_mb,
				bfa_msgq_cmdq_dbell_ready, cmdq)) {
		bfa_msgq_cmdq_dbell_ready(cmdq);
	}
}

static void
__cmd_copy(struct bfa_msgq_cmdq *cmdq, struct bfa_msgq_cmd_entry *cmd)
{
	size_t len = cmd->msg_size;
	size_t to_copy;
	u8 *src, *dst;

	src = (u8 *)cmd->msg_hdr;
	dst = (u8 *)cmdq->addr.kva;
	dst += (cmdq->producer_index * BFI_MSGQ_CMD_ENTRY_SIZE);

	while (len) {
		to_copy = (len < BFI_MSGQ_CMD_ENTRY_SIZE) ?
				len : BFI_MSGQ_CMD_ENTRY_SIZE;
		memcpy(dst, src, to_copy);
		len -= to_copy;
		src += BFI_MSGQ_CMD_ENTRY_SIZE;
		BFA_MSGQ_INDX_ADD(cmdq->producer_index, 1, cmdq->depth);
		dst = (u8 *)cmdq->addr.kva;
		dst += (cmdq->producer_index * BFI_MSGQ_CMD_ENTRY_SIZE);
	}

}

static void
bfa_msgq_cmdq_ci_update(struct bfa_msgq_cmdq *cmdq, struct bfi_mbmsg *mb)
{
	struct bfi_msgq_i2h_db *dbell = (struct bfi_msgq_i2h_db *)mb;
	struct bfa_msgq_cmd_entry *cmd;
	int posted = 0;

	cmdq->consumer_index = ntohs(dbell->idx.cmdq_ci);

	/* Walk through pending list to see if the command can be posted */
	while (!list_empty(&cmdq->pending_q)) {
		cmd = list_first_entry(&cmdq->pending_q,
				       struct bfa_msgq_cmd_entry, qe);
		if (ntohs(cmd->msg_hdr->num_entries) <=
			BFA_MSGQ_FREE_CNT(cmdq)) {
			list_del(&cmd->qe);
			__cmd_copy(cmdq, cmd);
			posted = 1;
			call_cmdq_ent_cbfn(cmd, BFA_STATUS_OK);
		} else {
			break;
		}
	}

	if (posted)
		bfa_fsm_send_event(cmdq, CMDQ_E_POST);
}

static void
bfa_msgq_cmdq_copy_next(void *arg)
{
	struct bfa_msgq_cmdq *cmdq = (struct bfa_msgq_cmdq *)arg;

	if (cmdq->bytes_to_copy)
		bfa_msgq_cmdq_copy_rsp(cmdq);
}

static void
bfa_msgq_cmdq_copy_req(struct bfa_msgq_cmdq *cmdq, struct bfi_mbmsg *mb)
{
	struct bfi_msgq_i2h_cmdq_copy_req *req =
		(struct bfi_msgq_i2h_cmdq_copy_req *)mb;

	cmdq->token = 0;
	cmdq->offset = ntohs(req->offset);
	cmdq->bytes_to_copy = ntohs(req->len);
	bfa_msgq_cmdq_copy_rsp(cmdq);
}

static void
bfa_msgq_cmdq_copy_rsp(struct bfa_msgq_cmdq *cmdq)
{
	struct bfi_msgq_h2i_cmdq_copy_rsp *rsp =
		(struct bfi_msgq_h2i_cmdq_copy_rsp *)&cmdq->copy_mb.msg[0];
	int copied;
	u8 *addr = (u8 *)cmdq->addr.kva;

	memset(rsp, 0, sizeof(struct bfi_msgq_h2i_cmdq_copy_rsp));
	bfi_h2i_set(rsp->mh, BFI_MC_MSGQ, BFI_MSGQ_H2I_CMDQ_COPY_RSP, 0);
	rsp->mh.mtag.i2htok = htons(cmdq->token);
	copied = (cmdq->bytes_to_copy >= BFI_CMD_COPY_SZ) ? BFI_CMD_COPY_SZ :
		cmdq->bytes_to_copy;
	addr += cmdq->offset;
	memcpy(rsp->data, addr, copied);

	cmdq->token++;
	cmdq->offset += copied;
	cmdq->bytes_to_copy -= copied;

	if (!bfa_nw_ioc_mbox_queue(cmdq->msgq->ioc, &cmdq->copy_mb,
				bfa_msgq_cmdq_copy_next, cmdq)) {
		bfa_msgq_cmdq_copy_next(cmdq);
	}
}

static void
bfa_msgq_cmdq_attach(struct bfa_msgq_cmdq *cmdq, struct bfa_msgq *msgq)
{
	cmdq->depth = BFA_MSGQ_CMDQ_NUM_ENTRY;
	INIT_LIST_HEAD(&cmdq->pending_q);
	cmdq->msgq = msgq;
	bfa_fsm_set_state(cmdq, cmdq_sm_stopped);
}

static void bfa_msgq_rspq_dbell(struct bfa_msgq_rspq *rspq);

enum rspq_event {
	RSPQ_E_START			= 1,
	RSPQ_E_STOP			= 2,
	RSPQ_E_FAIL			= 3,
	RSPQ_E_RESP			= 4,
	RSPQ_E_INIT_RESP		= 5,
	RSPQ_E_DB_READY			= 6,
};

bfa_fsm_state_decl(rspq, stopped, struct bfa_msgq_rspq, enum rspq_event);
bfa_fsm_state_decl(rspq, init_wait, struct bfa_msgq_rspq,
			enum rspq_event);
bfa_fsm_state_decl(rspq, ready, struct bfa_msgq_rspq, enum rspq_event);
bfa_fsm_state_decl(rspq, dbell_wait, struct bfa_msgq_rspq,
			enum rspq_event);

static void
rspq_sm_stopped_entry(struct bfa_msgq_rspq *rspq)
{
	rspq->producer_index = 0;
	rspq->consumer_index = 0;
	rspq->flags = 0;
}

static void
rspq_sm_stopped(struct bfa_msgq_rspq *rspq, enum rspq_event event)
{
	switch (event) {
	case RSPQ_E_START:
		bfa_fsm_set_state(rspq, rspq_sm_init_wait);
		break;

	case RSPQ_E_STOP:
	case RSPQ_E_FAIL:
		/* No-op */
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
rspq_sm_init_wait_entry(struct bfa_msgq_rspq *rspq)
{
	bfa_wc_down(&rspq->msgq->init_wc);
}

static void
rspq_sm_init_wait(struct bfa_msgq_rspq *rspq, enum rspq_event event)
{
	switch (event) {
	case RSPQ_E_FAIL:
	case RSPQ_E_STOP:
		bfa_fsm_set_state(rspq, rspq_sm_stopped);
		break;

	case RSPQ_E_INIT_RESP:
		bfa_fsm_set_state(rspq, rspq_sm_ready);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
rspq_sm_ready_entry(struct bfa_msgq_rspq *rspq)
{
}

static void
rspq_sm_ready(struct bfa_msgq_rspq *rspq, enum rspq_event event)
{
	switch (event) {
	case RSPQ_E_STOP:
	case RSPQ_E_FAIL:
		bfa_fsm_set_state(rspq, rspq_sm_stopped);
		break;

	case RSPQ_E_RESP:
		bfa_fsm_set_state(rspq, rspq_sm_dbell_wait);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
rspq_sm_dbell_wait_entry(struct bfa_msgq_rspq *rspq)
{
	if (!bfa_nw_ioc_is_disabled(rspq->msgq->ioc))
		bfa_msgq_rspq_dbell(rspq);
}

static void
rspq_sm_dbell_wait(struct bfa_msgq_rspq *rspq, enum rspq_event event)
{
	switch (event) {
	case RSPQ_E_STOP:
	case RSPQ_E_FAIL:
		bfa_fsm_set_state(rspq, rspq_sm_stopped);
		break;

	case RSPQ_E_RESP:
		rspq->flags |= BFA_MSGQ_RSPQ_F_DB_UPDATE;
		break;

	case RSPQ_E_DB_READY:
		if (rspq->flags & BFA_MSGQ_RSPQ_F_DB_UPDATE) {
			rspq->flags &= ~BFA_MSGQ_RSPQ_F_DB_UPDATE;
			bfa_fsm_set_state(rspq, rspq_sm_dbell_wait);
		} else
			bfa_fsm_set_state(rspq, rspq_sm_ready);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_msgq_rspq_dbell_ready(void *arg)
{
	struct bfa_msgq_rspq *rspq = (struct bfa_msgq_rspq *)arg;
	bfa_fsm_send_event(rspq, RSPQ_E_DB_READY);
}

static void
bfa_msgq_rspq_dbell(struct bfa_msgq_rspq *rspq)
{
	struct bfi_msgq_h2i_db *dbell =
		(struct bfi_msgq_h2i_db *)(&rspq->dbell_mb.msg[0]);

	memset(dbell, 0, sizeof(struct bfi_msgq_h2i_db));
	bfi_h2i_set(dbell->mh, BFI_MC_MSGQ, BFI_MSGQ_H2I_DOORBELL_CI, 0);
	dbell->mh.mtag.i2htok = 0;
	dbell->idx.rspq_ci = htons(rspq->consumer_index);

	if (!bfa_nw_ioc_mbox_queue(rspq->msgq->ioc, &rspq->dbell_mb,
				bfa_msgq_rspq_dbell_ready, rspq)) {
		bfa_msgq_rspq_dbell_ready(rspq);
	}
}

static void
bfa_msgq_rspq_pi_update(struct bfa_msgq_rspq *rspq, struct bfi_mbmsg *mb)
{
	struct bfi_msgq_i2h_db *dbell = (struct bfi_msgq_i2h_db *)mb;
	struct bfi_msgq_mhdr *msghdr;
	int num_entries;
	int mc;
	u8 *rspq_qe;

	rspq->producer_index = ntohs(dbell->idx.rspq_pi);

	while (rspq->consumer_index != rspq->producer_index) {
		rspq_qe = (u8 *)rspq->addr.kva;
		rspq_qe += (rspq->consumer_index * BFI_MSGQ_RSP_ENTRY_SIZE);
		msghdr = (struct bfi_msgq_mhdr *)rspq_qe;

		mc = msghdr->msg_class;
		num_entries = ntohs(msghdr->num_entries);

		if ((mc >= BFI_MC_MAX) || (rspq->rsphdlr[mc].cbfn == NULL))
			break;

		(rspq->rsphdlr[mc].cbfn)(rspq->rsphdlr[mc].cbarg, msghdr);

		BFA_MSGQ_INDX_ADD(rspq->consumer_index, num_entries,
				rspq->depth);
	}

	bfa_fsm_send_event(rspq, RSPQ_E_RESP);
}

static void
bfa_msgq_rspq_attach(struct bfa_msgq_rspq *rspq, struct bfa_msgq *msgq)
{
	rspq->depth = BFA_MSGQ_RSPQ_NUM_ENTRY;
	rspq->msgq = msgq;
	bfa_fsm_set_state(rspq, rspq_sm_stopped);
}

static void
bfa_msgq_init_rsp(struct bfa_msgq *msgq,
		 struct bfi_mbmsg *mb)
{
	bfa_fsm_send_event(&msgq->cmdq, CMDQ_E_INIT_RESP);
	bfa_fsm_send_event(&msgq->rspq, RSPQ_E_INIT_RESP);
}

static void
bfa_msgq_init(void *arg)
{
	struct bfa_msgq *msgq = (struct bfa_msgq *)arg;
	struct bfi_msgq_cfg_req *msgq_cfg =
		(struct bfi_msgq_cfg_req *)&msgq->init_mb.msg[0];

	memset(msgq_cfg, 0, sizeof(struct bfi_msgq_cfg_req));
	bfi_h2i_set(msgq_cfg->mh, BFI_MC_MSGQ, BFI_MSGQ_H2I_INIT_REQ, 0);
	msgq_cfg->mh.mtag.i2htok = 0;

	bfa_dma_be_addr_set(msgq_cfg->cmdq.addr, msgq->cmdq.addr.pa);
	msgq_cfg->cmdq.q_depth = htons(msgq->cmdq.depth);
	bfa_dma_be_addr_set(msgq_cfg->rspq.addr, msgq->rspq.addr.pa);
	msgq_cfg->rspq.q_depth = htons(msgq->rspq.depth);

	bfa_nw_ioc_mbox_queue(msgq->ioc, &msgq->init_mb, NULL, NULL);
}

static void
bfa_msgq_isr(void *cbarg, struct bfi_mbmsg *msg)
{
	struct bfa_msgq *msgq = (struct bfa_msgq *)cbarg;

	switch (msg->mh.msg_id) {
	case BFI_MSGQ_I2H_INIT_RSP:
		bfa_msgq_init_rsp(msgq, msg);
		break;

	case BFI_MSGQ_I2H_DOORBELL_PI:
		bfa_msgq_rspq_pi_update(&msgq->rspq, msg);
		break;

	case BFI_MSGQ_I2H_DOORBELL_CI:
		bfa_msgq_cmdq_ci_update(&msgq->cmdq, msg);
		break;

	case BFI_MSGQ_I2H_CMDQ_COPY_REQ:
		bfa_msgq_cmdq_copy_req(&msgq->cmdq, msg);
		break;

	default:
		BUG_ON(1);
	}
}

static void
bfa_msgq_notify(void *cbarg, enum bfa_ioc_event event)
{
	struct bfa_msgq *msgq = (struct bfa_msgq *)cbarg;

	switch (event) {
	case BFA_IOC_E_ENABLED:
		bfa_wc_init(&msgq->init_wc, bfa_msgq_init, msgq);
		bfa_wc_up(&msgq->init_wc);
		bfa_fsm_send_event(&msgq->cmdq, CMDQ_E_START);
		bfa_wc_up(&msgq->init_wc);
		bfa_fsm_send_event(&msgq->rspq, RSPQ_E_START);
		bfa_wc_wait(&msgq->init_wc);
		break;

	case BFA_IOC_E_DISABLED:
		bfa_fsm_send_event(&msgq->cmdq, CMDQ_E_STOP);
		bfa_fsm_send_event(&msgq->rspq, RSPQ_E_STOP);
		break;

	case BFA_IOC_E_FAILED:
		bfa_fsm_send_event(&msgq->cmdq, CMDQ_E_FAIL);
		bfa_fsm_send_event(&msgq->rspq, RSPQ_E_FAIL);
		break;

	default:
		break;
	}
}

u32
bfa_msgq_meminfo(void)
{
	return roundup(BFA_MSGQ_CMDQ_SIZE, BFA_DMA_ALIGN_SZ) +
		roundup(BFA_MSGQ_RSPQ_SIZE, BFA_DMA_ALIGN_SZ);
}

void
bfa_msgq_memclaim(struct bfa_msgq *msgq, u8 *kva, u64 pa)
{
	msgq->cmdq.addr.kva = kva;
	msgq->cmdq.addr.pa  = pa;

	kva += roundup(BFA_MSGQ_CMDQ_SIZE, BFA_DMA_ALIGN_SZ);
	pa += roundup(BFA_MSGQ_CMDQ_SIZE, BFA_DMA_ALIGN_SZ);

	msgq->rspq.addr.kva = kva;
	msgq->rspq.addr.pa = pa;
}

void
bfa_msgq_attach(struct bfa_msgq *msgq, struct bfa_ioc *ioc)
{
	msgq->ioc    = ioc;

	bfa_msgq_cmdq_attach(&msgq->cmdq, msgq);
	bfa_msgq_rspq_attach(&msgq->rspq, msgq);

	bfa_nw_ioc_mbox_regisr(msgq->ioc, BFI_MC_MSGQ, bfa_msgq_isr, msgq);
	bfa_ioc_notify_init(&msgq->ioc_notify, bfa_msgq_notify, msgq);
	bfa_nw_ioc_notify_register(msgq->ioc, &msgq->ioc_notify);
}

void
bfa_msgq_regisr(struct bfa_msgq *msgq, enum bfi_mclass mc,
		bfa_msgq_mcfunc_t cbfn, void *cbarg)
{
	msgq->rspq.rsphdlr[mc].cbfn	= cbfn;
	msgq->rspq.rsphdlr[mc].cbarg	= cbarg;
}

void
bfa_msgq_cmd_post(struct bfa_msgq *msgq,  struct bfa_msgq_cmd_entry *cmd)
{
	if (ntohs(cmd->msg_hdr->num_entries) <=
		BFA_MSGQ_FREE_CNT(&msgq->cmdq)) {
		__cmd_copy(&msgq->cmdq, cmd);
		call_cmdq_ent_cbfn(cmd, BFA_STATUS_OK);
		bfa_fsm_send_event(&msgq->cmdq, CMDQ_E_POST);
	} else {
		list_add_tail(&cmd->qe, &msgq->cmdq.pending_q);
	}
}

void
bfa_msgq_rsp_copy(struct bfa_msgq *msgq, u8 *buf, size_t buf_len)
{
	struct bfa_msgq_rspq *rspq = &msgq->rspq;
	size_t len = buf_len;
	size_t to_copy;
	int ci;
	u8 *src, *dst;

	ci = rspq->consumer_index;
	src = (u8 *)rspq->addr.kva;
	src += (ci * BFI_MSGQ_RSP_ENTRY_SIZE);
	dst = buf;

	while (len) {
		to_copy = (len < BFI_MSGQ_RSP_ENTRY_SIZE) ?
				len : BFI_MSGQ_RSP_ENTRY_SIZE;
		memcpy(dst, src, to_copy);
		len -= to_copy;
		dst += BFI_MSGQ_RSP_ENTRY_SIZE;
		BFA_MSGQ_INDX_ADD(ci, 1, rspq->depth);
		src = (u8 *)rspq->addr.kva;
		src += (ci * BFI_MSGQ_RSP_ENTRY_SIZE);
	}
}
