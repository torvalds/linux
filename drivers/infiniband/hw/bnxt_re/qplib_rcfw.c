/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: RDMA Controller HW interface
 */
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/prefetch.h>
#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_rcfw.h"
static void bnxt_qplib_service_creq(unsigned long data);

/* Hardware communication channel */
int bnxt_qplib_rcfw_wait_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	u16 cbit;
	int rc;

	cookie &= RCFW_MAX_COOKIE_VALUE;
	cbit = cookie % RCFW_MAX_OUTSTANDING_CMD;
	if (!test_bit(cbit, rcfw->cmdq_bitmap))
		dev_warn(&rcfw->pdev->dev,
			 "QPLIB: CMD bit %d for cookie 0x%x is not set?",
			 cbit, cookie);

	rc = wait_event_timeout(rcfw->waitq,
				!test_bit(cbit, rcfw->cmdq_bitmap),
				msecs_to_jiffies(RCFW_CMD_WAIT_TIME_MS));
	if (!rc) {
		dev_warn(&rcfw->pdev->dev,
			 "QPLIB: Bono Error: timeout %d msec, msg {0x%x}\n",
			 RCFW_CMD_WAIT_TIME_MS, cookie);
	}

	return rc;
};

int bnxt_qplib_rcfw_block_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	u32 count = -1;
	u16 cbit;

	cookie &= RCFW_MAX_COOKIE_VALUE;
	cbit = cookie % RCFW_MAX_OUTSTANDING_CMD;
	if (!test_bit(cbit, rcfw->cmdq_bitmap))
		goto done;
	do {
		bnxt_qplib_service_creq((unsigned long)rcfw);
	} while (test_bit(cbit, rcfw->cmdq_bitmap) && --count);
done:
	return count;
};

void *bnxt_qplib_rcfw_send_message(struct bnxt_qplib_rcfw *rcfw,
				   struct cmdq_base *req, void **crsbe,
				   u8 is_block)
{
	struct bnxt_qplib_crsq *crsq = &rcfw->crsq;
	struct bnxt_qplib_cmdqe *cmdqe, **cmdq_ptr;
	struct bnxt_qplib_hwq *cmdq = &rcfw->cmdq;
	struct bnxt_qplib_hwq *crsb = &rcfw->crsb;
	struct bnxt_qplib_crsqe *crsqe = NULL;
	struct bnxt_qplib_crsbe **crsb_ptr;
	u32 sw_prod, cmdq_prod;
	u8 retry_cnt = 0xFF;
	dma_addr_t dma_addr;
	unsigned long flags;
	u32 size, opcode;
	u16 cookie, cbit;
	int pg, idx;
	u8 *preq;

retry:
	opcode = req->opcode;
	if (!test_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->flags) &&
	    (opcode != CMDQ_BASE_OPCODE_QUERY_FUNC &&
	     opcode != CMDQ_BASE_OPCODE_INITIALIZE_FW)) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: RCFW not initialized, reject opcode 0x%x",
			opcode);
		return NULL;
	}

	if (test_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->flags) &&
	    opcode == CMDQ_BASE_OPCODE_INITIALIZE_FW) {
		dev_err(&rcfw->pdev->dev, "QPLIB: RCFW already initialized!");
		return NULL;
	}

	/* Cmdq are in 16-byte units, each request can consume 1 or more
	 * cmdqe
	 */
	spin_lock_irqsave(&cmdq->lock, flags);
	if (req->cmd_size > cmdq->max_elements -
	    ((HWQ_CMP(cmdq->prod, cmdq) - HWQ_CMP(cmdq->cons, cmdq)) &
	     (cmdq->max_elements - 1))) {
		dev_err(&rcfw->pdev->dev, "QPLIB: RCFW: CMDQ is full!");
		spin_unlock_irqrestore(&cmdq->lock, flags);

		if (!retry_cnt--)
			return NULL;
		goto retry;
	}

	retry_cnt = 0xFF;

	cookie = atomic_inc_return(&rcfw->seq_num) & RCFW_MAX_COOKIE_VALUE;
	cbit = cookie % RCFW_MAX_OUTSTANDING_CMD;
	if (is_block)
		cookie |= RCFW_CMD_IS_BLOCKING;
	req->cookie = cpu_to_le16(cookie);
	if (test_and_set_bit(cbit, rcfw->cmdq_bitmap)) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: RCFW MAX outstanding cmd reached!");
		atomic_dec(&rcfw->seq_num);
		spin_unlock_irqrestore(&cmdq->lock, flags);

		if (!retry_cnt--)
			return NULL;
		goto retry;
	}
	/* Reserve a resp buffer slot if requested */
	if (req->resp_size && crsbe) {
		spin_lock(&crsb->lock);
		sw_prod = HWQ_CMP(crsb->prod, crsb);
		crsb_ptr = (struct bnxt_qplib_crsbe **)crsb->pbl_ptr;
		*crsbe = (void *)&crsb_ptr[get_crsb_pg(sw_prod)]
					  [get_crsb_idx(sw_prod)];
		bnxt_qplib_crsb_dma_next(crsb->pbl_dma_ptr, sw_prod, &dma_addr);
		req->resp_addr = cpu_to_le64(dma_addr);
		crsb->prod++;
		spin_unlock(&crsb->lock);

		req->resp_size = (sizeof(struct bnxt_qplib_crsbe) +
				  BNXT_QPLIB_CMDQE_UNITS - 1) /
				 BNXT_QPLIB_CMDQE_UNITS;
	}
	cmdq_ptr = (struct bnxt_qplib_cmdqe **)cmdq->pbl_ptr;
	preq = (u8 *)req;
	size = req->cmd_size * BNXT_QPLIB_CMDQE_UNITS;
	do {
		pg = 0;
		idx = 0;

		/* Locate the next cmdq slot */
		sw_prod = HWQ_CMP(cmdq->prod, cmdq);
		cmdqe = &cmdq_ptr[get_cmdq_pg(sw_prod)][get_cmdq_idx(sw_prod)];
		if (!cmdqe) {
			dev_err(&rcfw->pdev->dev,
				"QPLIB: RCFW request failed with no cmdqe!");
			goto done;
		}
		/* Copy a segment of the req cmd to the cmdq */
		memset(cmdqe, 0, sizeof(*cmdqe));
		memcpy(cmdqe, preq, min_t(u32, size, sizeof(*cmdqe)));
		preq += min_t(u32, size, sizeof(*cmdqe));
		size -= min_t(u32, size, sizeof(*cmdqe));
		cmdq->prod++;
	} while (size > 0);

	cmdq_prod = cmdq->prod;
	if (rcfw->flags & FIRMWARE_FIRST_FLAG) {
		/* The very first doorbell write is required to set this flag
		 * which prompts the FW to reset its internal pointers
		 */
		cmdq_prod |= FIRMWARE_FIRST_FLAG;
		rcfw->flags &= ~FIRMWARE_FIRST_FLAG;
	}
	sw_prod = HWQ_CMP(crsq->prod, crsq);
	crsqe = &crsq->crsq[sw_prod];
	memset(crsqe, 0, sizeof(*crsqe));
	crsq->prod++;
	crsqe->req_size = req->cmd_size;

	/* ring CMDQ DB */
	writel(cmdq_prod, rcfw->cmdq_bar_reg_iomem +
	       rcfw->cmdq_bar_reg_prod_off);
	writel(RCFW_CMDQ_TRIG_VAL, rcfw->cmdq_bar_reg_iomem +
	       rcfw->cmdq_bar_reg_trig_off);
done:
	spin_unlock_irqrestore(&cmdq->lock, flags);
	/* Return the CREQ response pointer */
	return crsqe ? &crsqe->qp_event : NULL;
}

/* Completions */
static int bnxt_qplib_process_func_event(struct bnxt_qplib_rcfw *rcfw,
					 struct creq_func_event *func_event)
{
	switch (func_event->event) {
	case CREQ_FUNC_EVENT_EVENT_TX_WQE_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_TX_DATA_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_RX_WQE_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_RX_DATA_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CQ_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_TQM_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CFCQ_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CFCS_ERROR:
		/* SRQ ctx error, call srq_handler??
		 * But there's no SRQ handle!
		 */
		break;
	case CREQ_FUNC_EVENT_EVENT_CFCC_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CFCM_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_TIM_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_VF_COMM_REQUEST:
		break;
	case CREQ_FUNC_EVENT_EVENT_RESOURCE_EXHAUSTED:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int bnxt_qplib_process_qp_event(struct bnxt_qplib_rcfw *rcfw,
				       struct creq_qp_event *qp_event)
{
	struct bnxt_qplib_crsq *crsq = &rcfw->crsq;
	struct bnxt_qplib_hwq *cmdq = &rcfw->cmdq;
	struct bnxt_qplib_crsqe *crsqe;
	u16 cbit, cookie, blocked = 0;
	unsigned long flags;
	u32 sw_cons;

	switch (qp_event->event) {
	case CREQ_QP_EVENT_EVENT_QP_ERROR_NOTIFICATION:
		dev_dbg(&rcfw->pdev->dev,
			"QPLIB: Received QP error notification");
		break;
	default:
		/* Command Response */
		spin_lock_irqsave(&cmdq->lock, flags);
		sw_cons = HWQ_CMP(crsq->cons, crsq);
		crsqe = &crsq->crsq[sw_cons];
		crsq->cons++;
		memcpy(&crsqe->qp_event, qp_event, sizeof(crsqe->qp_event));

		cookie = le16_to_cpu(crsqe->qp_event.cookie);
		blocked = cookie & RCFW_CMD_IS_BLOCKING;
		cookie &= RCFW_MAX_COOKIE_VALUE;
		cbit = cookie % RCFW_MAX_OUTSTANDING_CMD;
		if (!test_and_clear_bit(cbit, rcfw->cmdq_bitmap))
			dev_warn(&rcfw->pdev->dev,
				 "QPLIB: CMD bit %d was not requested", cbit);

		cmdq->cons += crsqe->req_size;
		spin_unlock_irqrestore(&cmdq->lock, flags);
		if (!blocked)
			wake_up(&rcfw->waitq);
		break;
	}
	return 0;
}

/* SP - CREQ Completion handlers */
static void bnxt_qplib_service_creq(unsigned long data)
{
	struct bnxt_qplib_rcfw *rcfw = (struct bnxt_qplib_rcfw *)data;
	struct bnxt_qplib_hwq *creq = &rcfw->creq;
	struct creq_base *creqe, **creq_ptr;
	u32 sw_cons, raw_cons;
	unsigned long flags;
	u32 type;

	/* Service the CREQ until empty */
	spin_lock_irqsave(&creq->lock, flags);
	raw_cons = creq->cons;
	while (1) {
		sw_cons = HWQ_CMP(raw_cons, creq);
		creq_ptr = (struct creq_base **)creq->pbl_ptr;
		creqe = &creq_ptr[get_creq_pg(sw_cons)][get_creq_idx(sw_cons)];
		if (!CREQ_CMP_VALID(creqe, raw_cons, creq->max_elements))
			break;

		type = creqe->type & CREQ_BASE_TYPE_MASK;
		switch (type) {
		case CREQ_BASE_TYPE_QP_EVENT:
			if (!bnxt_qplib_process_qp_event
			    (rcfw, (struct creq_qp_event *)creqe))
				rcfw->creq_qp_event_processed++;
			else {
				dev_warn(&rcfw->pdev->dev, "QPLIB: crsqe with");
				dev_warn(&rcfw->pdev->dev,
					 "QPLIB: type = 0x%x not handled",
					 type);
			}
			break;
		case CREQ_BASE_TYPE_FUNC_EVENT:
			if (!bnxt_qplib_process_func_event
			    (rcfw, (struct creq_func_event *)creqe))
				rcfw->creq_func_event_processed++;
			else
				dev_warn
				(&rcfw->pdev->dev, "QPLIB:aeqe:%#x Not handled",
				 type);
			break;
		default:
			dev_warn(&rcfw->pdev->dev, "QPLIB: creqe with ");
			dev_warn(&rcfw->pdev->dev,
				 "QPLIB: op_event = 0x%x not handled", type);
			break;
		}
		raw_cons++;
	}
	if (creq->cons != raw_cons) {
		creq->cons = raw_cons;
		CREQ_DB_REARM(rcfw->creq_bar_reg_iomem, raw_cons,
			      creq->max_elements);
	}
	spin_unlock_irqrestore(&creq->lock, flags);
}

static irqreturn_t bnxt_qplib_creq_irq(int irq, void *dev_instance)
{
	struct bnxt_qplib_rcfw *rcfw = dev_instance;
	struct bnxt_qplib_hwq *creq = &rcfw->creq;
	struct creq_base **creq_ptr;
	u32 sw_cons;

	/* Prefetch the CREQ element */
	sw_cons = HWQ_CMP(creq->cons, creq);
	creq_ptr = (struct creq_base **)rcfw->creq.pbl_ptr;
	prefetch(&creq_ptr[get_creq_pg(sw_cons)][get_creq_idx(sw_cons)]);

	tasklet_schedule(&rcfw->worker);

	return IRQ_HANDLED;
}

/* RCFW */
int bnxt_qplib_deinit_rcfw(struct bnxt_qplib_rcfw *rcfw)
{
	struct creq_deinitialize_fw_resp *resp;
	struct cmdq_deinitialize_fw req;
	u16 cmd_flags = 0;

	RCFW_CMD_PREP(req, DEINITIALIZE_FW, cmd_flags);
	resp = (struct creq_deinitialize_fw_resp *)
			bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
						     NULL, 0);
	if (!resp)
		return -EINVAL;

	if (!bnxt_qplib_rcfw_wait_for_resp(rcfw, le16_to_cpu(req.cookie)))
		return -ETIMEDOUT;

	if (resp->status ||
	    le16_to_cpu(resp->cookie) != le16_to_cpu(req.cookie))
		return -EFAULT;

	clear_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->flags);
	return 0;
}

static int __get_pbl_pg_idx(struct bnxt_qplib_pbl *pbl)
{
	return (pbl->pg_size == ROCE_PG_SIZE_4K ?
				      CMDQ_INITIALIZE_FW_QPC_PG_SIZE_PG_4K :
		pbl->pg_size == ROCE_PG_SIZE_8K ?
				      CMDQ_INITIALIZE_FW_QPC_PG_SIZE_PG_8K :
		pbl->pg_size == ROCE_PG_SIZE_64K ?
				      CMDQ_INITIALIZE_FW_QPC_PG_SIZE_PG_64K :
		pbl->pg_size == ROCE_PG_SIZE_2M ?
				      CMDQ_INITIALIZE_FW_QPC_PG_SIZE_PG_2M :
		pbl->pg_size == ROCE_PG_SIZE_8M ?
				      CMDQ_INITIALIZE_FW_QPC_PG_SIZE_PG_8M :
		pbl->pg_size == ROCE_PG_SIZE_1G ?
				      CMDQ_INITIALIZE_FW_QPC_PG_SIZE_PG_1G :
				      CMDQ_INITIALIZE_FW_QPC_PG_SIZE_PG_4K);
}

int bnxt_qplib_init_rcfw(struct bnxt_qplib_rcfw *rcfw,
			 struct bnxt_qplib_ctx *ctx, int is_virtfn)
{
	struct creq_initialize_fw_resp *resp;
	struct cmdq_initialize_fw req;
	u16 cmd_flags = 0, level;

	RCFW_CMD_PREP(req, INITIALIZE_FW, cmd_flags);

	/*
	 * VFs need not setup the HW context area, PF
	 * shall setup this area for VF. Skipping the
	 * HW programming
	 */
	if (is_virtfn)
		goto skip_ctx_setup;

	level = ctx->qpc_tbl.level;
	req.qpc_pg_size_qpc_lvl = (level << CMDQ_INITIALIZE_FW_QPC_LVL_SFT) |
				__get_pbl_pg_idx(&ctx->qpc_tbl.pbl[level]);
	level = ctx->mrw_tbl.level;
	req.mrw_pg_size_mrw_lvl = (level << CMDQ_INITIALIZE_FW_MRW_LVL_SFT) |
				__get_pbl_pg_idx(&ctx->mrw_tbl.pbl[level]);
	level = ctx->srqc_tbl.level;
	req.srq_pg_size_srq_lvl = (level << CMDQ_INITIALIZE_FW_SRQ_LVL_SFT) |
				__get_pbl_pg_idx(&ctx->srqc_tbl.pbl[level]);
	level = ctx->cq_tbl.level;
	req.cq_pg_size_cq_lvl = (level << CMDQ_INITIALIZE_FW_CQ_LVL_SFT) |
				__get_pbl_pg_idx(&ctx->cq_tbl.pbl[level]);
	level = ctx->srqc_tbl.level;
	req.srq_pg_size_srq_lvl = (level << CMDQ_INITIALIZE_FW_SRQ_LVL_SFT) |
				__get_pbl_pg_idx(&ctx->srqc_tbl.pbl[level]);
	level = ctx->cq_tbl.level;
	req.cq_pg_size_cq_lvl = (level << CMDQ_INITIALIZE_FW_CQ_LVL_SFT) |
				__get_pbl_pg_idx(&ctx->cq_tbl.pbl[level]);
	level = ctx->tim_tbl.level;
	req.tim_pg_size_tim_lvl = (level << CMDQ_INITIALIZE_FW_TIM_LVL_SFT) |
				  __get_pbl_pg_idx(&ctx->tim_tbl.pbl[level]);
	level = ctx->tqm_pde_level;
	req.tqm_pg_size_tqm_lvl = (level << CMDQ_INITIALIZE_FW_TQM_LVL_SFT) |
				  __get_pbl_pg_idx(&ctx->tqm_pde.pbl[level]);

	req.qpc_page_dir =
		cpu_to_le64(ctx->qpc_tbl.pbl[PBL_LVL_0].pg_map_arr[0]);
	req.mrw_page_dir =
		cpu_to_le64(ctx->mrw_tbl.pbl[PBL_LVL_0].pg_map_arr[0]);
	req.srq_page_dir =
		cpu_to_le64(ctx->srqc_tbl.pbl[PBL_LVL_0].pg_map_arr[0]);
	req.cq_page_dir =
		cpu_to_le64(ctx->cq_tbl.pbl[PBL_LVL_0].pg_map_arr[0]);
	req.tim_page_dir =
		cpu_to_le64(ctx->tim_tbl.pbl[PBL_LVL_0].pg_map_arr[0]);
	req.tqm_page_dir =
		cpu_to_le64(ctx->tqm_pde.pbl[PBL_LVL_0].pg_map_arr[0]);

	req.number_of_qp = cpu_to_le32(ctx->qpc_tbl.max_elements);
	req.number_of_mrw = cpu_to_le32(ctx->mrw_tbl.max_elements);
	req.number_of_srq = cpu_to_le32(ctx->srqc_tbl.max_elements);
	req.number_of_cq = cpu_to_le32(ctx->cq_tbl.max_elements);

	req.max_qp_per_vf = cpu_to_le32(ctx->vf_res.max_qp_per_vf);
	req.max_mrw_per_vf = cpu_to_le32(ctx->vf_res.max_mrw_per_vf);
	req.max_srq_per_vf = cpu_to_le32(ctx->vf_res.max_srq_per_vf);
	req.max_cq_per_vf = cpu_to_le32(ctx->vf_res.max_cq_per_vf);
	req.max_gid_per_vf = cpu_to_le32(ctx->vf_res.max_gid_per_vf);

skip_ctx_setup:
	req.stat_ctx_id = cpu_to_le32(ctx->stats.fw_id);
	resp = (struct creq_initialize_fw_resp *)
			bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
						     NULL, 0);
	if (!resp) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: RCFW: INITIALIZE_FW send failed");
		return -EINVAL;
	}
	if (!bnxt_qplib_rcfw_wait_for_resp(rcfw, le16_to_cpu(req.cookie))) {
		/* Cmd timed out */
		dev_err(&rcfw->pdev->dev,
			"QPLIB: RCFW: INITIALIZE_FW timed out");
		return -ETIMEDOUT;
	}
	if (resp->status ||
	    le16_to_cpu(resp->cookie) != le16_to_cpu(req.cookie)) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: RCFW: INITIALIZE_FW failed");
		return -EINVAL;
	}
	set_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->flags);
	return 0;
}

void bnxt_qplib_free_rcfw_channel(struct bnxt_qplib_rcfw *rcfw)
{
	bnxt_qplib_free_hwq(rcfw->pdev, &rcfw->crsb);
	kfree(rcfw->crsq.crsq);
	bnxt_qplib_free_hwq(rcfw->pdev, &rcfw->cmdq);
	bnxt_qplib_free_hwq(rcfw->pdev, &rcfw->creq);

	rcfw->pdev = NULL;
}

int bnxt_qplib_alloc_rcfw_channel(struct pci_dev *pdev,
				  struct bnxt_qplib_rcfw *rcfw)
{
	rcfw->pdev = pdev;
	rcfw->creq.max_elements = BNXT_QPLIB_CREQE_MAX_CNT;
	if (bnxt_qplib_alloc_init_hwq(rcfw->pdev, &rcfw->creq, NULL, 0,
				      &rcfw->creq.max_elements,
				      BNXT_QPLIB_CREQE_UNITS, 0, PAGE_SIZE,
				      HWQ_TYPE_L2_CMPL)) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: HW channel CREQ allocation failed");
		goto fail;
	}
	rcfw->cmdq.max_elements = BNXT_QPLIB_CMDQE_MAX_CNT;
	if (bnxt_qplib_alloc_init_hwq(rcfw->pdev, &rcfw->cmdq, NULL, 0,
				      &rcfw->cmdq.max_elements,
				      BNXT_QPLIB_CMDQE_UNITS, 0, PAGE_SIZE,
				      HWQ_TYPE_CTX)) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: HW channel CMDQ allocation failed");
		goto fail;
	}

	rcfw->crsq.max_elements = rcfw->cmdq.max_elements;
	rcfw->crsq.crsq = kcalloc(rcfw->crsq.max_elements,
				  sizeof(*rcfw->crsq.crsq), GFP_KERNEL);
	if (!rcfw->crsq.crsq)
		goto fail;

	rcfw->crsb.max_elements = BNXT_QPLIB_CRSBE_MAX_CNT;
	if (bnxt_qplib_alloc_init_hwq(rcfw->pdev, &rcfw->crsb, NULL, 0,
				      &rcfw->crsb.max_elements,
				      BNXT_QPLIB_CRSBE_UNITS, 0, PAGE_SIZE,
				      HWQ_TYPE_CTX)) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: HW channel CRSB allocation failed");
		goto fail;
	}
	return 0;

fail:
	bnxt_qplib_free_rcfw_channel(rcfw);
	return -ENOMEM;
}

void bnxt_qplib_disable_rcfw_channel(struct bnxt_qplib_rcfw *rcfw)
{
	unsigned long indx;

	/* Make sure the HW channel is stopped! */
	synchronize_irq(rcfw->vector);
	tasklet_disable(&rcfw->worker);
	tasklet_kill(&rcfw->worker);

	if (rcfw->requested) {
		free_irq(rcfw->vector, rcfw);
		rcfw->requested = false;
	}
	if (rcfw->cmdq_bar_reg_iomem)
		iounmap(rcfw->cmdq_bar_reg_iomem);
	rcfw->cmdq_bar_reg_iomem = NULL;

	if (rcfw->creq_bar_reg_iomem)
		iounmap(rcfw->creq_bar_reg_iomem);
	rcfw->creq_bar_reg_iomem = NULL;

	indx = find_first_bit(rcfw->cmdq_bitmap, rcfw->bmap_size);
	if (indx != rcfw->bmap_size)
		dev_err(&rcfw->pdev->dev,
			"QPLIB: disabling RCFW with pending cmd-bit %lx", indx);
	kfree(rcfw->cmdq_bitmap);
	rcfw->bmap_size = 0;

	rcfw->aeq_handler = NULL;
	rcfw->vector = 0;
}

int bnxt_qplib_enable_rcfw_channel(struct pci_dev *pdev,
				   struct bnxt_qplib_rcfw *rcfw,
				   int msix_vector,
				   int cp_bar_reg_off, int virt_fn,
				   int (*aeq_handler)(struct bnxt_qplib_rcfw *,
						      struct creq_func_event *))
{
	resource_size_t res_base;
	struct cmdq_init init;
	u16 bmap_size;
	int rc;

	/* General */
	atomic_set(&rcfw->seq_num, 0);
	rcfw->flags = FIRMWARE_FIRST_FLAG;
	bmap_size = BITS_TO_LONGS(RCFW_MAX_OUTSTANDING_CMD *
				  sizeof(unsigned long));
	rcfw->cmdq_bitmap = kzalloc(bmap_size, GFP_KERNEL);
	if (!rcfw->cmdq_bitmap)
		return -ENOMEM;
	rcfw->bmap_size = bmap_size;

	/* CMDQ */
	rcfw->cmdq_bar_reg = RCFW_COMM_PCI_BAR_REGION;
	res_base = pci_resource_start(pdev, rcfw->cmdq_bar_reg);
	if (!res_base)
		return -ENOMEM;

	rcfw->cmdq_bar_reg_iomem = ioremap_nocache(res_base +
					      RCFW_COMM_BASE_OFFSET,
					      RCFW_COMM_SIZE);
	if (!rcfw->cmdq_bar_reg_iomem) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: CMDQ BAR region %d mapping failed",
			rcfw->cmdq_bar_reg);
		return -ENOMEM;
	}

	rcfw->cmdq_bar_reg_prod_off = virt_fn ? RCFW_VF_COMM_PROD_OFFSET :
					RCFW_PF_COMM_PROD_OFFSET;

	rcfw->cmdq_bar_reg_trig_off = RCFW_COMM_TRIG_OFFSET;

	/* CRSQ */
	rcfw->crsq.prod = 0;
	rcfw->crsq.cons = 0;

	/* CREQ */
	rcfw->creq_bar_reg = RCFW_COMM_CONS_PCI_BAR_REGION;
	res_base = pci_resource_start(pdev, rcfw->creq_bar_reg);
	if (!res_base)
		dev_err(&rcfw->pdev->dev,
			"QPLIB: CREQ BAR region %d resc start is 0!",
			rcfw->creq_bar_reg);
	rcfw->creq_bar_reg_iomem = ioremap_nocache(res_base + cp_bar_reg_off,
						   4);
	if (!rcfw->creq_bar_reg_iomem) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: CREQ BAR region %d mapping failed",
			rcfw->creq_bar_reg);
		return -ENOMEM;
	}
	rcfw->creq_qp_event_processed = 0;
	rcfw->creq_func_event_processed = 0;

	rcfw->vector = msix_vector;
	if (aeq_handler)
		rcfw->aeq_handler = aeq_handler;

	tasklet_init(&rcfw->worker, bnxt_qplib_service_creq,
		     (unsigned long)rcfw);

	rcfw->requested = false;
	rc = request_irq(rcfw->vector, bnxt_qplib_creq_irq, 0,
			 "bnxt_qplib_creq", rcfw);
	if (rc) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: Failed to request IRQ for CREQ rc = 0x%x", rc);
		bnxt_qplib_disable_rcfw_channel(rcfw);
		return rc;
	}
	rcfw->requested = true;

	init_waitqueue_head(&rcfw->waitq);

	CREQ_DB_REARM(rcfw->creq_bar_reg_iomem, 0, rcfw->creq.max_elements);

	init.cmdq_pbl = cpu_to_le64(rcfw->cmdq.pbl[PBL_LVL_0].pg_map_arr[0]);
	init.cmdq_size_cmdq_lvl = cpu_to_le16(
		((BNXT_QPLIB_CMDQE_MAX_CNT << CMDQ_INIT_CMDQ_SIZE_SFT) &
		 CMDQ_INIT_CMDQ_SIZE_MASK) |
		((rcfw->cmdq.level << CMDQ_INIT_CMDQ_LVL_SFT) &
		 CMDQ_INIT_CMDQ_LVL_MASK));
	init.creq_ring_id = cpu_to_le16(rcfw->creq_ring_id);

	/* Write to the Bono mailbox register */
	__iowrite32_copy(rcfw->cmdq_bar_reg_iomem, &init, sizeof(init) / 4);
	return 0;
}
