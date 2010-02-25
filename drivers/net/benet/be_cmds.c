/*
 * Copyright (C) 2005 - 2009 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 * ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 */

#include "be.h"
#include "be_cmds.h"

static void be_mcc_notify(struct be_adapter *adapter)
{
	struct be_queue_info *mccq = &adapter->mcc_obj.q;
	u32 val = 0;

	val |= mccq->id & DB_MCCQ_RING_ID_MASK;
	val |= 1 << DB_MCCQ_NUM_POSTED_SHIFT;
	iowrite32(val, adapter->db + DB_MCCQ_OFFSET);
}

/* To check if valid bit is set, check the entire word as we don't know
 * the endianness of the data (old entry is host endian while a new entry is
 * little endian) */
static inline bool be_mcc_compl_is_new(struct be_mcc_compl *compl)
{
	if (compl->flags != 0) {
		compl->flags = le32_to_cpu(compl->flags);
		BUG_ON((compl->flags & CQE_FLAGS_VALID_MASK) == 0);
		return true;
	} else {
		return false;
	}
}

/* Need to reset the entire word that houses the valid bit */
static inline void be_mcc_compl_use(struct be_mcc_compl *compl)
{
	compl->flags = 0;
}

static int be_mcc_compl_process(struct be_adapter *adapter,
	struct be_mcc_compl *compl)
{
	u16 compl_status, extd_status;

	/* Just swap the status to host endian; mcc tag is opaquely copied
	 * from mcc_wrb */
	be_dws_le_to_cpu(compl, 4);

	compl_status = (compl->status >> CQE_STATUS_COMPL_SHIFT) &
				CQE_STATUS_COMPL_MASK;
	if (compl_status == MCC_STATUS_SUCCESS) {
		if (compl->tag0 == OPCODE_ETH_GET_STATISTICS) {
			struct be_cmd_resp_get_stats *resp =
						adapter->stats.cmd.va;
			be_dws_le_to_cpu(&resp->hw_stats,
						sizeof(resp->hw_stats));
			netdev_stats_update(adapter);
		}
	} else if (compl_status != MCC_STATUS_NOT_SUPPORTED) {
		extd_status = (compl->status >> CQE_STATUS_EXTD_SHIFT) &
				CQE_STATUS_EXTD_MASK;
		dev_warn(&adapter->pdev->dev,
		"Error in cmd completion - opcode %d, compl %d, extd %d\n",
			compl->tag0, compl_status, extd_status);
	}
	return compl_status;
}

/* Link state evt is a string of bytes; no need for endian swapping */
static void be_async_link_state_process(struct be_adapter *adapter,
		struct be_async_event_link_state *evt)
{
	be_link_status_update(adapter,
		evt->port_link_status == ASYNC_EVENT_LINK_UP);
}

static inline bool is_link_state_evt(u32 trailer)
{
	return (((trailer >> ASYNC_TRAILER_EVENT_CODE_SHIFT) &
		ASYNC_TRAILER_EVENT_CODE_MASK) ==
				ASYNC_EVENT_CODE_LINK_STATE);
}

static struct be_mcc_compl *be_mcc_compl_get(struct be_adapter *adapter)
{
	struct be_queue_info *mcc_cq = &adapter->mcc_obj.cq;
	struct be_mcc_compl *compl = queue_tail_node(mcc_cq);

	if (be_mcc_compl_is_new(compl)) {
		queue_tail_inc(mcc_cq);
		return compl;
	}
	return NULL;
}

int be_process_mcc(struct be_adapter *adapter)
{
	struct be_mcc_compl *compl;
	int num = 0, status = 0;

	spin_lock_bh(&adapter->mcc_cq_lock);
	while ((compl = be_mcc_compl_get(adapter))) {
		if (compl->flags & CQE_FLAGS_ASYNC_MASK) {
			/* Interpret flags as an async trailer */
			BUG_ON(!is_link_state_evt(compl->flags));

			/* Interpret compl as a async link evt */
			be_async_link_state_process(adapter,
				(struct be_async_event_link_state *) compl);
		} else if (compl->flags & CQE_FLAGS_COMPLETED_MASK) {
				status = be_mcc_compl_process(adapter, compl);
				atomic_dec(&adapter->mcc_obj.q.used);
		}
		be_mcc_compl_use(compl);
		num++;
	}

	if (num)
		be_cq_notify(adapter, adapter->mcc_obj.cq.id, true, num);

	spin_unlock_bh(&adapter->mcc_cq_lock);
	return status;
}

/* Wait till no more pending mcc requests are present */
static int be_mcc_wait_compl(struct be_adapter *adapter)
{
#define mcc_timeout		120000 /* 12s timeout */
	int i, status;
	for (i = 0; i < mcc_timeout; i++) {
		status = be_process_mcc(adapter);
		if (status)
			return status;

		if (atomic_read(&adapter->mcc_obj.q.used) == 0)
			break;
		udelay(100);
	}
	if (i == mcc_timeout) {
		dev_err(&adapter->pdev->dev, "mccq poll timed out\n");
		return -1;
	}
	return 0;
}

/* Notify MCC requests and wait for completion */
static int be_mcc_notify_wait(struct be_adapter *adapter)
{
	be_mcc_notify(adapter);
	return be_mcc_wait_compl(adapter);
}

static int be_mbox_db_ready_wait(struct be_adapter *adapter, void __iomem *db)
{
	int cnt = 0, wait = 5;
	u32 ready;

	do {
		ready = ioread32(db) & MPU_MAILBOX_DB_RDY_MASK;
		if (ready)
			break;

		if (cnt > 4000000) {
			dev_err(&adapter->pdev->dev, "mbox poll timed out\n");
			return -1;
		}

		if (cnt > 50)
			wait = 200;
		cnt += wait;
		udelay(wait);
	} while (true);

	return 0;
}

/*
 * Insert the mailbox address into the doorbell in two steps
 * Polls on the mbox doorbell till a command completion (or a timeout) occurs
 */
static int be_mbox_notify_wait(struct be_adapter *adapter)
{
	int status;
	u32 val = 0;
	void __iomem *db = adapter->db + MPU_MAILBOX_DB_OFFSET;
	struct be_dma_mem *mbox_mem = &adapter->mbox_mem;
	struct be_mcc_mailbox *mbox = mbox_mem->va;
	struct be_mcc_compl *compl = &mbox->compl;

	val |= MPU_MAILBOX_DB_HI_MASK;
	/* at bits 2 - 31 place mbox dma addr msb bits 34 - 63 */
	val |= (upper_32_bits(mbox_mem->dma) >> 2) << 2;
	iowrite32(val, db);

	/* wait for ready to be set */
	status = be_mbox_db_ready_wait(adapter, db);
	if (status != 0)
		return status;

	val = 0;
	/* at bits 2 - 31 place mbox dma addr lsb bits 4 - 33 */
	val |= (u32)(mbox_mem->dma >> 4) << 2;
	iowrite32(val, db);

	status = be_mbox_db_ready_wait(adapter, db);
	if (status != 0)
		return status;

	/* A cq entry has been made now */
	if (be_mcc_compl_is_new(compl)) {
		status = be_mcc_compl_process(adapter, &mbox->compl);
		be_mcc_compl_use(compl);
		if (status)
			return status;
	} else {
		dev_err(&adapter->pdev->dev, "invalid mailbox completion\n");
		return -1;
	}
	return 0;
}

static int be_POST_stage_get(struct be_adapter *adapter, u16 *stage)
{
	u32 sem = ioread32(adapter->csr + MPU_EP_SEMAPHORE_OFFSET);

	*stage = sem & EP_SEMAPHORE_POST_STAGE_MASK;
	if ((sem >> EP_SEMAPHORE_POST_ERR_SHIFT) & EP_SEMAPHORE_POST_ERR_MASK)
		return -1;
	else
		return 0;
}

int be_cmd_POST(struct be_adapter *adapter)
{
	u16 stage;
	int status, timeout = 0;

	do {
		status = be_POST_stage_get(adapter, &stage);
		if (status) {
			dev_err(&adapter->pdev->dev, "POST error; stage=0x%x\n",
				stage);
			return -1;
		} else if (stage != POST_STAGE_ARMFW_RDY) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(2 * HZ);
			timeout += 2;
		} else {
			return 0;
		}
	} while (timeout < 20);

	dev_err(&adapter->pdev->dev, "POST timeout; stage=0x%x\n", stage);
	return -1;
}

static inline void *embedded_payload(struct be_mcc_wrb *wrb)
{
	return wrb->payload.embedded_payload;
}

static inline struct be_sge *nonembedded_sgl(struct be_mcc_wrb *wrb)
{
	return &wrb->payload.sgl[0];
}

/* Don't touch the hdr after it's prepared */
static void be_wrb_hdr_prepare(struct be_mcc_wrb *wrb, int payload_len,
				bool embedded, u8 sge_cnt, u32 opcode)
{
	if (embedded)
		wrb->embedded |= MCC_WRB_EMBEDDED_MASK;
	else
		wrb->embedded |= (sge_cnt & MCC_WRB_SGE_CNT_MASK) <<
				MCC_WRB_SGE_CNT_SHIFT;
	wrb->payload_length = payload_len;
	wrb->tag0 = opcode;
	be_dws_cpu_to_le(wrb, 8);
}

/* Don't touch the hdr after it's prepared */
static void be_cmd_hdr_prepare(struct be_cmd_req_hdr *req_hdr,
				u8 subsystem, u8 opcode, int cmd_len)
{
	req_hdr->opcode = opcode;
	req_hdr->subsystem = subsystem;
	req_hdr->request_length = cpu_to_le32(cmd_len - sizeof(*req_hdr));
	req_hdr->version = 0;
}

static void be_cmd_page_addrs_prepare(struct phys_addr *pages, u32 max_pages,
			struct be_dma_mem *mem)
{
	int i, buf_pages = min(PAGES_4K_SPANNED(mem->va, mem->size), max_pages);
	u64 dma = (u64)mem->dma;

	for (i = 0; i < buf_pages; i++) {
		pages[i].lo = cpu_to_le32(dma & 0xFFFFFFFF);
		pages[i].hi = cpu_to_le32(upper_32_bits(dma));
		dma += PAGE_SIZE_4K;
	}
}

/* Converts interrupt delay in microseconds to multiplier value */
static u32 eq_delay_to_mult(u32 usec_delay)
{
#define MAX_INTR_RATE			651042
	const u32 round = 10;
	u32 multiplier;

	if (usec_delay == 0)
		multiplier = 0;
	else {
		u32 interrupt_rate = 1000000 / usec_delay;
		/* Max delay, corresponding to the lowest interrupt rate */
		if (interrupt_rate == 0)
			multiplier = 1023;
		else {
			multiplier = (MAX_INTR_RATE - interrupt_rate) * round;
			multiplier /= interrupt_rate;
			/* Round the multiplier to the closest value.*/
			multiplier = (multiplier + round/2) / round;
			multiplier = min(multiplier, (u32)1023);
		}
	}
	return multiplier;
}

static inline struct be_mcc_wrb *wrb_from_mbox(struct be_adapter *adapter)
{
	struct be_dma_mem *mbox_mem = &adapter->mbox_mem;
	struct be_mcc_wrb *wrb
		= &((struct be_mcc_mailbox *)(mbox_mem->va))->wrb;
	memset(wrb, 0, sizeof(*wrb));
	return wrb;
}

static struct be_mcc_wrb *wrb_from_mccq(struct be_adapter *adapter)
{
	struct be_queue_info *mccq = &adapter->mcc_obj.q;
	struct be_mcc_wrb *wrb;

	if (atomic_read(&mccq->used) >= mccq->len) {
		dev_err(&adapter->pdev->dev, "Out of MCCQ wrbs\n");
		return NULL;
	}

	wrb = queue_head_node(mccq);
	queue_head_inc(mccq);
	atomic_inc(&mccq->used);
	memset(wrb, 0, sizeof(*wrb));
	return wrb;
}

/* Tell fw we're about to start firing cmds by writing a
 * special pattern across the wrb hdr; uses mbox
 */
int be_cmd_fw_init(struct be_adapter *adapter)
{
	u8 *wrb;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = (u8 *)wrb_from_mbox(adapter);
	*wrb++ = 0xFF;
	*wrb++ = 0x12;
	*wrb++ = 0x34;
	*wrb++ = 0xFF;
	*wrb++ = 0xFF;
	*wrb++ = 0x56;
	*wrb++ = 0x78;
	*wrb = 0xFF;

	status = be_mbox_notify_wait(adapter);

	spin_unlock(&adapter->mbox_lock);
	return status;
}

/* Tell fw we're done with firing cmds by writing a
 * special pattern across the wrb hdr; uses mbox
 */
int be_cmd_fw_clean(struct be_adapter *adapter)
{
	u8 *wrb;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = (u8 *)wrb_from_mbox(adapter);
	*wrb++ = 0xFF;
	*wrb++ = 0xAA;
	*wrb++ = 0xBB;
	*wrb++ = 0xFF;
	*wrb++ = 0xFF;
	*wrb++ = 0xCC;
	*wrb++ = 0xDD;
	*wrb = 0xFF;

	status = be_mbox_notify_wait(adapter);

	spin_unlock(&adapter->mbox_lock);
	return status;
}
int be_cmd_eq_create(struct be_adapter *adapter,
		struct be_queue_info *eq, int eq_delay)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_eq_create *req;
	struct be_dma_mem *q_mem = &eq->dma_mem;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0, OPCODE_COMMON_EQ_CREATE);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_EQ_CREATE, sizeof(*req));

	req->num_pages =  cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));

	AMAP_SET_BITS(struct amap_eq_context, func, req->context,
			be_pci_func(adapter));
	AMAP_SET_BITS(struct amap_eq_context, valid, req->context, 1);
	/* 4byte eqe*/
	AMAP_SET_BITS(struct amap_eq_context, size, req->context, 0);
	AMAP_SET_BITS(struct amap_eq_context, count, req->context,
			__ilog2_u32(eq->len/256));
	AMAP_SET_BITS(struct amap_eq_context, delaymult, req->context,
			eq_delay_to_mult(eq_delay));
	be_dws_cpu_to_le(req->context, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_eq_create *resp = embedded_payload(wrb);
		eq->id = le16_to_cpu(resp->eq_id);
		eq->created = true;
	}

	spin_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses mbox */
int be_cmd_mac_addr_query(struct be_adapter *adapter, u8 *mac_addr,
			u8 type, bool permanent, u32 if_handle)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_mac_query *req;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_NTWK_MAC_QUERY);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_MAC_QUERY, sizeof(*req));

	req->type = type;
	if (permanent) {
		req->permanent = 1;
	} else {
		req->if_id = cpu_to_le16((u16) if_handle);
		req->permanent = 0;
	}

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_mac_query *resp = embedded_payload(wrb);
		memcpy(mac_addr, resp->mac.addr, ETH_ALEN);
	}

	spin_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses synchronous MCCQ */
int be_cmd_pmac_add(struct be_adapter *adapter, u8 *mac_addr,
		u32 if_id, u32 *pmac_id)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_pmac_add *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_NTWK_PMAC_ADD);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_PMAC_ADD, sizeof(*req));

	req->if_id = cpu_to_le32(if_id);
	memcpy(req->mac_address, mac_addr, ETH_ALEN);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_pmac_add *resp = embedded_payload(wrb);
		*pmac_id = le32_to_cpu(resp->pmac_id);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous MCCQ */
int be_cmd_pmac_del(struct be_adapter *adapter, u32 if_id, u32 pmac_id)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_pmac_del *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_NTWK_PMAC_DEL);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_PMAC_DEL, sizeof(*req));

	req->if_id = cpu_to_le32(if_id);
	req->pmac_id = cpu_to_le32(pmac_id);

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses Mbox */
int be_cmd_cq_create(struct be_adapter *adapter,
		struct be_queue_info *cq, struct be_queue_info *eq,
		bool sol_evts, bool no_delay, int coalesce_wm)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_cq_create *req;
	struct be_dma_mem *q_mem = &cq->dma_mem;
	void *ctxt;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_CQ_CREATE);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_CQ_CREATE, sizeof(*req));

	req->num_pages =  cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));

	AMAP_SET_BITS(struct amap_cq_context, coalescwm, ctxt, coalesce_wm);
	AMAP_SET_BITS(struct amap_cq_context, nodelay, ctxt, no_delay);
	AMAP_SET_BITS(struct amap_cq_context, count, ctxt,
			__ilog2_u32(cq->len/256));
	AMAP_SET_BITS(struct amap_cq_context, valid, ctxt, 1);
	AMAP_SET_BITS(struct amap_cq_context, solevent, ctxt, sol_evts);
	AMAP_SET_BITS(struct amap_cq_context, eventable, ctxt, 1);
	AMAP_SET_BITS(struct amap_cq_context, eqid, ctxt, eq->id);
	AMAP_SET_BITS(struct amap_cq_context, armed, ctxt, 1);
	AMAP_SET_BITS(struct amap_cq_context, func, ctxt, be_pci_func(adapter));
	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_cq_create *resp = embedded_payload(wrb);
		cq->id = le16_to_cpu(resp->cq_id);
		cq->created = true;
	}

	spin_unlock(&adapter->mbox_lock);

	return status;
}

static u32 be_encoded_q_len(int q_len)
{
	u32 len_encoded = fls(q_len); /* log2(len) + 1 */
	if (len_encoded == 16)
		len_encoded = 0;
	return len_encoded;
}

int be_cmd_mccq_create(struct be_adapter *adapter,
			struct be_queue_info *mccq,
			struct be_queue_info *cq)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_mcc_create *req;
	struct be_dma_mem *q_mem = &mccq->dma_mem;
	void *ctxt;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_MCC_CREATE);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			OPCODE_COMMON_MCC_CREATE, sizeof(*req));

	req->num_pages = PAGES_4K_SPANNED(q_mem->va, q_mem->size);

	AMAP_SET_BITS(struct amap_mcc_context, fid, ctxt, be_pci_func(adapter));
	AMAP_SET_BITS(struct amap_mcc_context, valid, ctxt, 1);
	AMAP_SET_BITS(struct amap_mcc_context, ring_size, ctxt,
		be_encoded_q_len(mccq->len));
	AMAP_SET_BITS(struct amap_mcc_context, cq_id, ctxt, cq->id);

	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_mcc_create *resp = embedded_payload(wrb);
		mccq->id = le16_to_cpu(resp->id);
		mccq->created = true;
	}
	spin_unlock(&adapter->mbox_lock);

	return status;
}

int be_cmd_txq_create(struct be_adapter *adapter,
			struct be_queue_info *txq,
			struct be_queue_info *cq)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_eth_tx_create *req;
	struct be_dma_mem *q_mem = &txq->dma_mem;
	void *ctxt;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_ETH_TX_CREATE);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH, OPCODE_ETH_TX_CREATE,
		sizeof(*req));

	req->num_pages = PAGES_4K_SPANNED(q_mem->va, q_mem->size);
	req->ulp_num = BE_ULP1_NUM;
	req->type = BE_ETH_TX_RING_TYPE_STANDARD;

	AMAP_SET_BITS(struct amap_tx_context, tx_ring_size, ctxt,
		be_encoded_q_len(txq->len));
	AMAP_SET_BITS(struct amap_tx_context, pci_func_id, ctxt,
			be_pci_func(adapter));
	AMAP_SET_BITS(struct amap_tx_context, ctx_valid, ctxt, 1);
	AMAP_SET_BITS(struct amap_tx_context, cq_id_send, ctxt, cq->id);

	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_eth_tx_create *resp = embedded_payload(wrb);
		txq->id = le16_to_cpu(resp->cid);
		txq->created = true;
	}

	spin_unlock(&adapter->mbox_lock);

	return status;
}

/* Uses mbox */
int be_cmd_rxq_create(struct be_adapter *adapter,
		struct be_queue_info *rxq, u16 cq_id, u16 frag_size,
		u16 max_frame_size, u32 if_id, u32 rss)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_eth_rx_create *req;
	struct be_dma_mem *q_mem = &rxq->dma_mem;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_ETH_RX_CREATE);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH, OPCODE_ETH_RX_CREATE,
		sizeof(*req));

	req->cq_id = cpu_to_le16(cq_id);
	req->frag_size = fls(frag_size) - 1;
	req->num_pages = 2;
	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);
	req->interface_id = cpu_to_le32(if_id);
	req->max_frame_size = cpu_to_le16(max_frame_size);
	req->rss_queue = cpu_to_le32(rss);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_eth_rx_create *resp = embedded_payload(wrb);
		rxq->id = le16_to_cpu(resp->id);
		rxq->created = true;
	}

	spin_unlock(&adapter->mbox_lock);

	return status;
}

/* Generic destroyer function for all types of queues
 * Uses Mbox
 */
int be_cmd_q_destroy(struct be_adapter *adapter, struct be_queue_info *q,
		int queue_type)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_q_destroy *req;
	u8 subsys = 0, opcode = 0;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	switch (queue_type) {
	case QTYPE_EQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_EQ_DESTROY;
		break;
	case QTYPE_CQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_CQ_DESTROY;
		break;
	case QTYPE_TXQ:
		subsys = CMD_SUBSYSTEM_ETH;
		opcode = OPCODE_ETH_TX_DESTROY;
		break;
	case QTYPE_RXQ:
		subsys = CMD_SUBSYSTEM_ETH;
		opcode = OPCODE_ETH_RX_DESTROY;
		break;
	case QTYPE_MCCQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_MCC_DESTROY;
		break;
	default:
		BUG();
	}

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0, opcode);

	be_cmd_hdr_prepare(&req->hdr, subsys, opcode, sizeof(*req));
	req->id = cpu_to_le16(q->id);

	status = be_mbox_notify_wait(adapter);

	spin_unlock(&adapter->mbox_lock);

	return status;
}

/* Create an rx filtering policy configuration on an i/f
 * Uses mbox
 */
int be_cmd_if_create(struct be_adapter *adapter, u32 cap_flags, u32 en_flags,
		u8 *mac, bool pmac_invalid, u32 *if_handle, u32 *pmac_id)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_if_create *req;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_NTWK_INTERFACE_CREATE);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_INTERFACE_CREATE, sizeof(*req));

	req->capability_flags = cpu_to_le32(cap_flags);
	req->enable_flags = cpu_to_le32(en_flags);
	req->pmac_invalid = pmac_invalid;
	if (!pmac_invalid)
		memcpy(req->mac_addr, mac, ETH_ALEN);

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_if_create *resp = embedded_payload(wrb);
		*if_handle = le32_to_cpu(resp->interface_id);
		if (!pmac_invalid)
			*pmac_id = le32_to_cpu(resp->pmac_id);
	}

	spin_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses mbox */
int be_cmd_if_destroy(struct be_adapter *adapter, u32 interface_id)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_if_destroy *req;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_NTWK_INTERFACE_DESTROY);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_INTERFACE_DESTROY, sizeof(*req));

	req->interface_id = cpu_to_le32(interface_id);

	status = be_mbox_notify_wait(adapter);

	spin_unlock(&adapter->mbox_lock);

	return status;
}

/* Get stats is a non embedded command: the request is not embedded inside
 * WRB but is a separate dma memory block
 * Uses asynchronous MCC
 */
int be_cmd_get_stats(struct be_adapter *adapter, struct be_dma_mem *nonemb_cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_stats *req;
	struct be_sge *sge;
	int status = 0;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = nonemb_cmd->va;
	sge = nonembedded_sgl(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), false, 1,
			OPCODE_ETH_GET_STATISTICS);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
		OPCODE_ETH_GET_STATISTICS, sizeof(*req));
	sge->pa_hi = cpu_to_le32(upper_32_bits(nonemb_cmd->dma));
	sge->pa_lo = cpu_to_le32(nonemb_cmd->dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(nonemb_cmd->size);

	be_mcc_notify(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses synchronous mcc */
int be_cmd_link_status_query(struct be_adapter *adapter,
			bool *link_up, u8 *mac_speed, u16 *link_speed)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_link_status *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	*link_up = false;

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_NTWK_LINK_STATUS_QUERY);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_LINK_STATUS_QUERY, sizeof(*req));

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_link_status *resp = embedded_payload(wrb);
		if (resp->mac_speed != PHY_LINK_SPEED_ZERO) {
			*link_up = true;
			*link_speed = le16_to_cpu(resp->link_speed);
			*mac_speed = resp->mac_speed;
		}
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses Mbox */
int be_cmd_get_fw_ver(struct be_adapter *adapter, char *fw_ver)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_fw_version *req;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_GET_FW_VERSION);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_GET_FW_VERSION, sizeof(*req));

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_fw_version *resp = embedded_payload(wrb);
		strncpy(fw_ver, resp->firmware_version_string, FW_VER_LEN);
	}

	spin_unlock(&adapter->mbox_lock);
	return status;
}

/* set the EQ delay interval of an EQ to specified value
 * Uses async mcc
 */
int be_cmd_modify_eqd(struct be_adapter *adapter, u32 eq_id, u32 eqd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_modify_eq_delay *req;
	int status = 0;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_MODIFY_EQ_DELAY);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_MODIFY_EQ_DELAY, sizeof(*req));

	req->num_eq = cpu_to_le32(1);
	req->delay[0].eq_id = cpu_to_le32(eq_id);
	req->delay[0].phase = 0;
	req->delay[0].delay_multiplier = cpu_to_le32(eqd);

	be_mcc_notify(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses sycnhronous mcc */
int be_cmd_vlan_config(struct be_adapter *adapter, u32 if_id, u16 *vtag_array,
			u32 num, bool untagged, bool promiscuous)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_vlan_config *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_NTWK_VLAN_CONFIG);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_VLAN_CONFIG, sizeof(*req));

	req->interface_id = if_id;
	req->promiscuous = promiscuous;
	req->untagged = untagged;
	req->num_vlan = num;
	if (!promiscuous) {
		memcpy(req->normal_vlan, vtag_array,
			req->num_vlan * sizeof(vtag_array[0]));
	}

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses MCC for this command as it may be called in BH context
 * Uses synchronous mcc
 */
int be_cmd_promiscuous_config(struct be_adapter *adapter, u8 port_num, bool en)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_promiscuous_config *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0, OPCODE_ETH_PROMISCUOUS);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
		OPCODE_ETH_PROMISCUOUS, sizeof(*req));

	if (port_num)
		req->port1_promiscuous = en;
	else
		req->port0_promiscuous = en;

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/*
 * Uses MCC for this command as it may be called in BH context
 * (mc == NULL) => multicast promiscous
 */
int be_cmd_multicast_set(struct be_adapter *adapter, u32 if_id,
		struct dev_mc_list *mc_list, u32 mc_count,
		struct be_dma_mem *mem)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_mcast_mac_config *req = mem->va;
	struct be_sge *sge;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	sge = nonembedded_sgl(wrb);
	memset(req, 0, sizeof(*req));

	be_wrb_hdr_prepare(wrb, sizeof(*req), false, 1,
			OPCODE_COMMON_NTWK_MULTICAST_SET);
	sge->pa_hi = cpu_to_le32(upper_32_bits(mem->dma));
	sge->pa_lo = cpu_to_le32(mem->dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(mem->size);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_NTWK_MULTICAST_SET, sizeof(*req));

	req->interface_id = if_id;
	if (mc_list) {
		int i;
		struct dev_mc_list *mc;

		req->num_mac = cpu_to_le16(mc_count);

		for (mc = mc_list, i = 0; mc; mc = mc->next, i++)
			memcpy(req->mac[i].byte, mc->dmi_addr, ETH_ALEN);
	} else {
		req->promiscuous = 1;
	}

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses synchrounous mcc */
int be_cmd_set_flow_control(struct be_adapter *adapter, u32 tx_fc, u32 rx_fc)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_flow_control *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_SET_FLOW_CONTROL);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_SET_FLOW_CONTROL, sizeof(*req));

	req->tx_flow_control = cpu_to_le16((u16)tx_fc);
	req->rx_flow_control = cpu_to_le16((u16)rx_fc);

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses sycn mcc */
int be_cmd_get_flow_control(struct be_adapter *adapter, u32 *tx_fc, u32 *rx_fc)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_flow_control *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_GET_FLOW_CONTROL);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_GET_FLOW_CONTROL, sizeof(*req));

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_flow_control *resp =
						embedded_payload(wrb);
		*tx_fc = le16_to_cpu(resp->tx_flow_control);
		*rx_fc = le16_to_cpu(resp->rx_flow_control);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses mbox */
int be_cmd_query_fw_cfg(struct be_adapter *adapter, u32 *port_num, u32 *cap)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_query_fw_cfg *req;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_QUERY_FIRMWARE_CONFIG);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_QUERY_FIRMWARE_CONFIG, sizeof(*req));

	status = be_mbox_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_query_fw_cfg *resp = embedded_payload(wrb);
		*port_num = le32_to_cpu(resp->phys_port);
		*cap = le32_to_cpu(resp->function_cap);
	}

	spin_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses mbox */
int be_cmd_reset_function(struct be_adapter *adapter)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_hdr *req;
	int status;

	spin_lock(&adapter->mbox_lock);

	wrb = wrb_from_mbox(adapter);
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_FUNCTION_RESET);

	be_cmd_hdr_prepare(req, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_FUNCTION_RESET, sizeof(*req));

	status = be_mbox_notify_wait(adapter);

	spin_unlock(&adapter->mbox_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_set_beacon_state(struct be_adapter *adapter, u8 port_num,
			u8 bcn, u8 sts, u8 state)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_enable_disable_beacon *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_ENABLE_DISABLE_BEACON);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_ENABLE_DISABLE_BEACON, sizeof(*req));

	req->port_num = port_num;
	req->beacon_state = state;
	req->beacon_duration = bcn;
	req->status_duration = sts;

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_get_beacon_state(struct be_adapter *adapter, u8 port_num, u32 *state)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_get_beacon_state *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
			OPCODE_COMMON_GET_BEACON_STATE);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_GET_BEACON_STATE, sizeof(*req));

	req->port_num = port_num;

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_get_beacon_state *resp =
						embedded_payload(wrb);
		*state = resp->beacon_state;
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

/* Uses sync mcc */
int be_cmd_read_port_type(struct be_adapter *adapter, u32 port,
				u8 *connector)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_port_type *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(struct be_cmd_resp_port_type), true, 0,
			OPCODE_COMMON_READ_TRANSRECV_DATA);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_READ_TRANSRECV_DATA, sizeof(*req));

	req->port = cpu_to_le32(port);
	req->page_num = cpu_to_le32(TR_PAGE_A0);
	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_port_type *resp = embedded_payload(wrb);
			*connector = resp->data.connector;
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_write_flashrom(struct be_adapter *adapter, struct be_dma_mem *cmd,
			u32 flash_type, u32 flash_opcode, u32 buf_size)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_write_flashrom *req = cmd->va;
	struct be_sge *sge;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = cmd->va;
	sge = nonembedded_sgl(wrb);

	be_wrb_hdr_prepare(wrb, cmd->size, false, 1,
			OPCODE_COMMON_WRITE_FLASHROM);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_WRITE_FLASHROM, cmd->size);
	sge->pa_hi = cpu_to_le32(upper_32_bits(cmd->dma));
	sge->pa_lo = cpu_to_le32(cmd->dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(cmd->size);

	req->params.op_type = cpu_to_le32(flash_type);
	req->params.op_code = cpu_to_le32(flash_opcode);
	req->params.data_buf_size = cpu_to_le32(buf_size);

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_get_flash_crc(struct be_adapter *adapter, u8 *flashed_crc)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_write_flashrom *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req)+4, true, 0,
			OPCODE_COMMON_READ_FLASHROM);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
		OPCODE_COMMON_READ_FLASHROM, sizeof(*req)+4);

	req->params.op_type = cpu_to_le32(FLASHROM_TYPE_REDBOOT);
	req->params.op_code = cpu_to_le32(FLASHROM_OPER_REPORT);
	req->params.offset = 0x3FFFC;
	req->params.data_buf_size = 0x4;

	status = be_mcc_notify_wait(adapter);
	if (!status)
		memcpy(flashed_crc, req->params.data_buf, 4);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

extern int be_cmd_enable_magic_wol(struct be_adapter *adapter, u8 *mac,
				struct be_dma_mem *nonemb_cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_acpi_wol_magic_config *req;
	struct be_sge *sge;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = nonemb_cmd->va;
	sge = nonembedded_sgl(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), false, 1,
			OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ETH,
		OPCODE_ETH_ACPI_WOL_MAGIC_CONFIG, sizeof(*req));
	memcpy(req->magic_mac, mac, ETH_ALEN);

	sge->pa_hi = cpu_to_le32(upper_32_bits(nonemb_cmd->dma));
	sge->pa_lo = cpu_to_le32(nonemb_cmd->dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(nonemb_cmd->size);

	status = be_mcc_notify_wait(adapter);

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_set_loopback(struct be_adapter *adapter, u8 port_num,
			u8 loopback_type, u8 enable)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_set_lmode *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
				OPCODE_LOWLEVEL_SET_LOOPBACK_MODE);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			OPCODE_LOWLEVEL_SET_LOOPBACK_MODE,
			sizeof(*req));

	req->src_port = port_num;
	req->dest_port = port_num;
	req->loopback_type = loopback_type;
	req->loopback_state = enable;

	status = be_mcc_notify_wait(adapter);
err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_loopback_test(struct be_adapter *adapter, u32 port_num,
		u32 loopback_type, u32 pkt_size, u32 num_pkts, u64 pattern)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_loopback_test *req;
	int status;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}

	req = embedded_payload(wrb);

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0,
				OPCODE_LOWLEVEL_LOOPBACK_TEST);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			OPCODE_LOWLEVEL_LOOPBACK_TEST, sizeof(*req));
	req->hdr.timeout = 4;

	req->pattern = cpu_to_le64(pattern);
	req->src_port = cpu_to_le32(port_num);
	req->dest_port = cpu_to_le32(port_num);
	req->pkt_size = cpu_to_le32(pkt_size);
	req->num_pkts = cpu_to_le32(num_pkts);
	req->loopback_type = cpu_to_le32(loopback_type);

	status = be_mcc_notify_wait(adapter);
	if (!status) {
		struct be_cmd_resp_loopback_test *resp = embedded_payload(wrb);
		status = le32_to_cpu(resp->status);
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}

int be_cmd_ddr_dma_test(struct be_adapter *adapter, u64 pattern,
				u32 byte_cnt, struct be_dma_mem *cmd)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_ddrdma_test *req;
	struct be_sge *sge;
	int status;
	int i, j = 0;

	spin_lock_bh(&adapter->mcc_lock);

	wrb = wrb_from_mccq(adapter);
	if (!wrb) {
		status = -EBUSY;
		goto err;
	}
	req = cmd->va;
	sge = nonembedded_sgl(wrb);
	be_wrb_hdr_prepare(wrb, cmd->size, false, 1,
				OPCODE_LOWLEVEL_HOST_DDR_DMA);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_LOWLEVEL,
			OPCODE_LOWLEVEL_HOST_DDR_DMA, cmd->size);

	sge->pa_hi = cpu_to_le32(upper_32_bits(cmd->dma));
	sge->pa_lo = cpu_to_le32(cmd->dma & 0xFFFFFFFF);
	sge->len = cpu_to_le32(cmd->size);

	req->pattern = cpu_to_le64(pattern);
	req->byte_count = cpu_to_le32(byte_cnt);
	for (i = 0; i < byte_cnt; i++) {
		req->snd_buff[i] = (u8)(pattern >> (j*8));
		j++;
		if (j > 7)
			j = 0;
	}

	status = be_mcc_notify_wait(adapter);

	if (!status) {
		struct be_cmd_resp_ddrdma_test *resp;
		resp = cmd->va;
		if ((memcmp(resp->rcv_buff, req->snd_buff, byte_cnt) != 0) ||
				resp->snd_err) {
			status = -1;
		}
	}

err:
	spin_unlock_bh(&adapter->mcc_lock);
	return status;
}
