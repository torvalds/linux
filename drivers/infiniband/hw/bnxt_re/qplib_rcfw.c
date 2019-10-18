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

#define dev_fmt(fmt) "QPLIB: " fmt

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/prefetch.h>
#include <linux/delay.h>

#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_rcfw.h"
#include "qplib_sp.h"
#include "qplib_fp.h"

static void bnxt_qplib_service_creq(unsigned long data);

/* Hardware communication channel */
static int __wait_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	u16 cbit;
	int rc;

	cbit = cookie % rcfw->cmdq_depth;
	rc = wait_event_timeout(rcfw->waitq,
				!test_bit(cbit, rcfw->cmdq_bitmap),
				msecs_to_jiffies(RCFW_CMD_WAIT_TIME_MS));
	return rc ? 0 : -ETIMEDOUT;
};

static int __block_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	u32 count = RCFW_BLOCKED_CMD_WAIT_COUNT;
	u16 cbit;

	cbit = cookie % rcfw->cmdq_depth;
	if (!test_bit(cbit, rcfw->cmdq_bitmap))
		goto done;
	do {
		mdelay(1); /* 1m sec */
		bnxt_qplib_service_creq((unsigned long)rcfw);
	} while (test_bit(cbit, rcfw->cmdq_bitmap) && --count);
done:
	return count ? 0 : -ETIMEDOUT;
};

static int __send_message(struct bnxt_qplib_rcfw *rcfw, struct cmdq_base *req,
			  struct creq_base *resp, void *sb, u8 is_block)
{
	struct bnxt_qplib_cmdqe *cmdqe, **cmdq_ptr;
	struct bnxt_qplib_hwq *cmdq = &rcfw->cmdq;
	u32 cmdq_depth = rcfw->cmdq_depth;
	struct bnxt_qplib_crsq *crsqe;
	u32 sw_prod, cmdq_prod;
	unsigned long flags;
	u32 size, opcode;
	u16 cookie, cbit;
	u8 *preq;

	opcode = req->opcode;
	if (!test_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->flags) &&
	    (opcode != CMDQ_BASE_OPCODE_QUERY_FUNC &&
	     opcode != CMDQ_BASE_OPCODE_INITIALIZE_FW &&
	     opcode != CMDQ_BASE_OPCODE_QUERY_VERSION)) {
		dev_err(&rcfw->pdev->dev,
			"RCFW not initialized, reject opcode 0x%x\n", opcode);
		return -EINVAL;
	}

	if (test_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->flags) &&
	    opcode == CMDQ_BASE_OPCODE_INITIALIZE_FW) {
		dev_err(&rcfw->pdev->dev, "RCFW already initialized!\n");
		return -EINVAL;
	}

	if (test_bit(FIRMWARE_TIMED_OUT, &rcfw->flags))
		return -ETIMEDOUT;

	/* Cmdq are in 16-byte units, each request can consume 1 or more
	 * cmdqe
	 */
	spin_lock_irqsave(&cmdq->lock, flags);
	if (req->cmd_size >= HWQ_FREE_SLOTS(cmdq)) {
		dev_err(&rcfw->pdev->dev, "RCFW: CMDQ is full!\n");
		spin_unlock_irqrestore(&cmdq->lock, flags);
		return -EAGAIN;
	}


	cookie = rcfw->seq_num & RCFW_MAX_COOKIE_VALUE;
	cbit = cookie % rcfw->cmdq_depth;
	if (is_block)
		cookie |= RCFW_CMD_IS_BLOCKING;

	set_bit(cbit, rcfw->cmdq_bitmap);
	req->cookie = cpu_to_le16(cookie);
	crsqe = &rcfw->crsqe_tbl[cbit];
	if (crsqe->resp) {
		spin_unlock_irqrestore(&cmdq->lock, flags);
		return -EBUSY;
	}

	size = req->cmd_size;
	/* change the cmd_size to the number of 16byte cmdq unit.
	 * req->cmd_size is modified here
	 */
	bnxt_qplib_set_cmd_slots(req);

	memset(resp, 0, sizeof(*resp));
	crsqe->resp = (struct creq_qp_event *)resp;
	crsqe->resp->cookie = req->cookie;
	crsqe->req_size = req->cmd_size;
	if (req->resp_size && sb) {
		struct bnxt_qplib_rcfw_sbuf *sbuf = sb;

		req->resp_addr = cpu_to_le64(sbuf->dma_addr);
		req->resp_size = (sbuf->size + BNXT_QPLIB_CMDQE_UNITS - 1) /
				  BNXT_QPLIB_CMDQE_UNITS;
	}

	cmdq_ptr = (struct bnxt_qplib_cmdqe **)cmdq->pbl_ptr;
	preq = (u8 *)req;
	do {
		/* Locate the next cmdq slot */
		sw_prod = HWQ_CMP(cmdq->prod, cmdq);
		cmdqe = &cmdq_ptr[get_cmdq_pg(sw_prod, cmdq_depth)]
				[get_cmdq_idx(sw_prod, cmdq_depth)];
		if (!cmdqe) {
			dev_err(&rcfw->pdev->dev,
				"RCFW request failed with no cmdqe!\n");
			goto done;
		}
		/* Copy a segment of the req cmd to the cmdq */
		memset(cmdqe, 0, sizeof(*cmdqe));
		memcpy(cmdqe, preq, min_t(u32, size, sizeof(*cmdqe)));
		preq += min_t(u32, size, sizeof(*cmdqe));
		size -= min_t(u32, size, sizeof(*cmdqe));
		cmdq->prod++;
		rcfw->seq_num++;
	} while (size > 0);

	rcfw->seq_num++;

	cmdq_prod = cmdq->prod;
	if (test_bit(FIRMWARE_FIRST_FLAG, &rcfw->flags)) {
		/* The very first doorbell write
		 * is required to set this flag
		 * which prompts the FW to reset
		 * its internal pointers
		 */
		cmdq_prod |= BIT(FIRMWARE_FIRST_FLAG);
		clear_bit(FIRMWARE_FIRST_FLAG, &rcfw->flags);
	}

	/* ring CMDQ DB */
	wmb();
	writel(cmdq_prod, rcfw->cmdq_bar_reg_iomem +
	       rcfw->cmdq_bar_reg_prod_off);
	writel(RCFW_CMDQ_TRIG_VAL, rcfw->cmdq_bar_reg_iomem +
	       rcfw->cmdq_bar_reg_trig_off);
done:
	spin_unlock_irqrestore(&cmdq->lock, flags);
	/* Return the CREQ response pointer */
	return 0;
}

int bnxt_qplib_rcfw_send_message(struct bnxt_qplib_rcfw *rcfw,
				 struct cmdq_base *req,
				 struct creq_base *resp,
				 void *sb, u8 is_block)
{
	struct creq_qp_event *evnt = (struct creq_qp_event *)resp;
	u16 cookie;
	u8 opcode, retry_cnt = 0xFF;
	int rc = 0;

	do {
		opcode = req->opcode;
		rc = __send_message(rcfw, req, resp, sb, is_block);
		cookie = le16_to_cpu(req->cookie) & RCFW_MAX_COOKIE_VALUE;
		if (!rc)
			break;

		if (!retry_cnt || (rc != -EAGAIN && rc != -EBUSY)) {
			/* send failed */
			dev_err(&rcfw->pdev->dev, "cmdq[%#x]=%#x send failed\n",
				cookie, opcode);
			return rc;
		}
		is_block ? mdelay(1) : usleep_range(500, 1000);

	} while (retry_cnt--);

	if (is_block)
		rc = __block_for_resp(rcfw, cookie);
	else
		rc = __wait_for_resp(rcfw, cookie);
	if (rc) {
		/* timed out */
		dev_err(&rcfw->pdev->dev, "cmdq[%#x]=%#x timedout (%d)msec\n",
			cookie, opcode, RCFW_CMD_WAIT_TIME_MS);
		set_bit(FIRMWARE_TIMED_OUT, &rcfw->flags);
		return rc;
	}

	if (evnt->status) {
		/* failed with status */
		dev_err(&rcfw->pdev->dev, "cmdq[%#x]=%#x status %#x\n",
			cookie, opcode, evnt->status);
		rc = -EFAULT;
	}

	return rc;
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
	struct bnxt_qplib_hwq *cmdq = &rcfw->cmdq;
	struct creq_qp_error_notification *err_event;
	struct bnxt_qplib_crsq *crsqe;
	unsigned long flags;
	struct bnxt_qplib_qp *qp;
	u16 cbit, blocked = 0;
	u16 cookie;
	__le16  mcookie;
	u32 qp_id;

	switch (qp_event->event) {
	case CREQ_QP_EVENT_EVENT_QP_ERROR_NOTIFICATION:
		err_event = (struct creq_qp_error_notification *)qp_event;
		qp_id = le32_to_cpu(err_event->xid);
		qp = rcfw->qp_tbl[qp_id].qp_handle;
		dev_dbg(&rcfw->pdev->dev,
			"Received QP error notification\n");
		dev_dbg(&rcfw->pdev->dev,
			"qpid 0x%x, req_err=0x%x, resp_err=0x%x\n",
			qp_id, err_event->req_err_state_reason,
			err_event->res_err_state_reason);
		if (!qp)
			break;
		bnxt_qplib_mark_qp_error(qp);
		rcfw->aeq_handler(rcfw, qp_event, qp);
		break;
	default:
		/*
		 * Command Response
		 * cmdq->lock needs to be acquired to synchronie
		 * the command send and completion reaping. This function
		 * is always called with creq->lock held. Using
		 * the nested variant of spin_lock.
		 *
		 */

		spin_lock_irqsave_nested(&cmdq->lock, flags,
					 SINGLE_DEPTH_NESTING);
		cookie = le16_to_cpu(qp_event->cookie);
		mcookie = qp_event->cookie;
		blocked = cookie & RCFW_CMD_IS_BLOCKING;
		cookie &= RCFW_MAX_COOKIE_VALUE;
		cbit = cookie % rcfw->cmdq_depth;
		crsqe = &rcfw->crsqe_tbl[cbit];
		if (crsqe->resp &&
		    crsqe->resp->cookie  == mcookie) {
			memcpy(crsqe->resp, qp_event, sizeof(*qp_event));
			crsqe->resp = NULL;
		} else {
			if (crsqe->resp && crsqe->resp->cookie)
				dev_err(&rcfw->pdev->dev,
					"CMD %s cookie sent=%#x, recd=%#x\n",
					crsqe->resp ? "mismatch" : "collision",
					crsqe->resp ? crsqe->resp->cookie : 0,
					mcookie);
		}
		if (!test_and_clear_bit(cbit, rcfw->cmdq_bitmap))
			dev_warn(&rcfw->pdev->dev,
				 "CMD bit %d was not requested\n", cbit);
		cmdq->cons += crsqe->req_size;
		crsqe->req_size = 0;

		if (!blocked)
			wake_up(&rcfw->waitq);
		spin_unlock_irqrestore(&cmdq->lock, flags);
	}
	return 0;
}

/* SP - CREQ Completion handlers */
static void bnxt_qplib_service_creq(unsigned long data)
{
	struct bnxt_qplib_rcfw *rcfw = (struct bnxt_qplib_rcfw *)data;
	bool gen_p5 = bnxt_qplib_is_chip_gen_p5(rcfw->res->cctx);
	struct bnxt_qplib_hwq *creq = &rcfw->creq;
	u32 type, budget = CREQ_ENTRY_POLL_BUDGET;
	struct creq_base *creqe, **creq_ptr;
	u32 sw_cons, raw_cons;
	unsigned long flags;

	/* Service the CREQ until budget is over */
	spin_lock_irqsave(&creq->lock, flags);
	raw_cons = creq->cons;
	while (budget > 0) {
		sw_cons = HWQ_CMP(raw_cons, creq);
		creq_ptr = (struct creq_base **)creq->pbl_ptr;
		creqe = &creq_ptr[get_creq_pg(sw_cons)][get_creq_idx(sw_cons)];
		if (!CREQ_CMP_VALID(creqe, raw_cons, creq->max_elements))
			break;
		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();

		type = creqe->type & CREQ_BASE_TYPE_MASK;
		switch (type) {
		case CREQ_BASE_TYPE_QP_EVENT:
			bnxt_qplib_process_qp_event
				(rcfw, (struct creq_qp_event *)creqe);
			rcfw->creq_qp_event_processed++;
			break;
		case CREQ_BASE_TYPE_FUNC_EVENT:
			if (!bnxt_qplib_process_func_event
			    (rcfw, (struct creq_func_event *)creqe))
				rcfw->creq_func_event_processed++;
			else
				dev_warn(&rcfw->pdev->dev,
					 "aeqe:%#x Not handled\n", type);
			break;
		default:
			if (type != ASYNC_EVENT_CMPL_TYPE_HWRM_ASYNC_EVENT)
				dev_warn(&rcfw->pdev->dev,
					 "creqe with event 0x%x not handled\n",
					 type);
			break;
		}
		raw_cons++;
		budget--;
	}

	if (creq->cons != raw_cons) {
		creq->cons = raw_cons;
		bnxt_qplib_ring_creq_db_rearm(rcfw->creq_bar_reg_iomem,
					      raw_cons, creq->max_elements,
					      rcfw->creq_ring_id, gen_p5);
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
	struct cmdq_deinitialize_fw req;
	struct creq_deinitialize_fw_resp resp;
	u16 cmd_flags = 0;
	int rc;

	RCFW_CMD_PREP(req, DEINITIALIZE_FW, cmd_flags);
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  NULL, 0);
	if (rc)
		return rc;

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
	struct cmdq_initialize_fw req;
	struct creq_initialize_fw_resp resp;
	u16 cmd_flags = 0, level;
	int rc;

	RCFW_CMD_PREP(req, INITIALIZE_FW, cmd_flags);
	/* Supply (log-base-2-of-host-page-size - base-page-shift)
	 * to bono to adjust the doorbell page sizes.
	 */
	req.log2_dbr_pg_size = cpu_to_le16(PAGE_SHIFT -
					   RCFW_DBR_BASE_PAGE_SHIFT);
	/*
	 * Gen P5 devices doesn't require this allocation
	 * as the L2 driver does the same for RoCE also.
	 * Also, VFs need not setup the HW context area, PF
	 * shall setup this area for VF. Skipping the
	 * HW programming
	 */
	if (is_virtfn || bnxt_qplib_is_chip_gen_p5(rcfw->res->cctx))
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
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  NULL, 0);
	if (rc)
		return rc;
	set_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->flags);
	return 0;
}

void bnxt_qplib_free_rcfw_channel(struct bnxt_qplib_rcfw *rcfw)
{
	kfree(rcfw->qp_tbl);
	kfree(rcfw->crsqe_tbl);
	bnxt_qplib_free_hwq(rcfw->pdev, &rcfw->cmdq);
	bnxt_qplib_free_hwq(rcfw->pdev, &rcfw->creq);
	rcfw->pdev = NULL;
}

int bnxt_qplib_alloc_rcfw_channel(struct pci_dev *pdev,
				  struct bnxt_qplib_rcfw *rcfw,
				  struct bnxt_qplib_ctx *ctx,
				  int qp_tbl_sz)
{
	u8 hwq_type;

	rcfw->pdev = pdev;
	rcfw->creq.max_elements = BNXT_QPLIB_CREQE_MAX_CNT;
	hwq_type = bnxt_qplib_get_hwq_type(rcfw->res);
	if (bnxt_qplib_alloc_init_hwq(rcfw->pdev, &rcfw->creq, NULL,
				      &rcfw->creq.max_elements,
				      BNXT_QPLIB_CREQE_UNITS,
				      0, PAGE_SIZE, hwq_type)) {
		dev_err(&rcfw->pdev->dev,
			"HW channel CREQ allocation failed\n");
		goto fail;
	}
	if (ctx->hwrm_intf_ver < HWRM_VERSION_RCFW_CMDQ_DEPTH_CHECK)
		rcfw->cmdq_depth = BNXT_QPLIB_CMDQE_MAX_CNT_256;
	else
		rcfw->cmdq_depth = BNXT_QPLIB_CMDQE_MAX_CNT_8192;

	rcfw->cmdq.max_elements = rcfw->cmdq_depth;
	if (bnxt_qplib_alloc_init_hwq
			(rcfw->pdev, &rcfw->cmdq, NULL,
			 &rcfw->cmdq.max_elements,
			 BNXT_QPLIB_CMDQE_UNITS, 0,
			 bnxt_qplib_cmdqe_page_size(rcfw->cmdq_depth),
			 HWQ_TYPE_CTX)) {
		dev_err(&rcfw->pdev->dev,
			"HW channel CMDQ allocation failed\n");
		goto fail;
	}

	rcfw->crsqe_tbl = kcalloc(rcfw->cmdq.max_elements,
				  sizeof(*rcfw->crsqe_tbl), GFP_KERNEL);
	if (!rcfw->crsqe_tbl)
		goto fail;

	rcfw->qp_tbl_size = qp_tbl_sz;
	rcfw->qp_tbl = kcalloc(qp_tbl_sz, sizeof(struct bnxt_qplib_qp_node),
			       GFP_KERNEL);
	if (!rcfw->qp_tbl)
		goto fail;

	return 0;

fail:
	bnxt_qplib_free_rcfw_channel(rcfw);
	return -ENOMEM;
}

void bnxt_qplib_rcfw_stop_irq(struct bnxt_qplib_rcfw *rcfw, bool kill)
{
	bool gen_p5 = bnxt_qplib_is_chip_gen_p5(rcfw->res->cctx);

	tasklet_disable(&rcfw->worker);
	/* Mask h/w interrupts */
	bnxt_qplib_ring_creq_db(rcfw->creq_bar_reg_iomem, rcfw->creq.cons,
				rcfw->creq.max_elements, rcfw->creq_ring_id,
				gen_p5);
	/* Sync with last running IRQ-handler */
	synchronize_irq(rcfw->vector);
	if (kill)
		tasklet_kill(&rcfw->worker);

	if (rcfw->requested) {
		free_irq(rcfw->vector, rcfw);
		rcfw->requested = false;
	}
}

void bnxt_qplib_disable_rcfw_channel(struct bnxt_qplib_rcfw *rcfw)
{
	unsigned long indx;

	bnxt_qplib_rcfw_stop_irq(rcfw, true);

	iounmap(rcfw->cmdq_bar_reg_iomem);
	iounmap(rcfw->creq_bar_reg_iomem);

	indx = find_first_bit(rcfw->cmdq_bitmap, rcfw->bmap_size);
	if (indx != rcfw->bmap_size)
		dev_err(&rcfw->pdev->dev,
			"disabling RCFW with pending cmd-bit %lx\n", indx);
	kfree(rcfw->cmdq_bitmap);
	rcfw->bmap_size = 0;

	rcfw->cmdq_bar_reg_iomem = NULL;
	rcfw->creq_bar_reg_iomem = NULL;
	rcfw->aeq_handler = NULL;
	rcfw->vector = 0;
}

int bnxt_qplib_rcfw_start_irq(struct bnxt_qplib_rcfw *rcfw, int msix_vector,
			      bool need_init)
{
	bool gen_p5 = bnxt_qplib_is_chip_gen_p5(rcfw->res->cctx);
	int rc;

	if (rcfw->requested)
		return -EFAULT;

	rcfw->vector = msix_vector;
	if (need_init)
		tasklet_init(&rcfw->worker,
			     bnxt_qplib_service_creq, (unsigned long)rcfw);
	else
		tasklet_enable(&rcfw->worker);
	rc = request_irq(rcfw->vector, bnxt_qplib_creq_irq, 0,
			 "bnxt_qplib_creq", rcfw);
	if (rc)
		return rc;
	rcfw->requested = true;
	bnxt_qplib_ring_creq_db_rearm(rcfw->creq_bar_reg_iomem,
				      rcfw->creq.cons, rcfw->creq.max_elements,
				      rcfw->creq_ring_id, gen_p5);

	return 0;
}

int bnxt_qplib_enable_rcfw_channel(struct pci_dev *pdev,
				   struct bnxt_qplib_rcfw *rcfw,
				   int msix_vector,
				   int cp_bar_reg_off, int virt_fn,
				   int (*aeq_handler)(struct bnxt_qplib_rcfw *,
						      void *, void *))
{
	resource_size_t res_base;
	struct cmdq_init init;
	u16 bmap_size;
	int rc;

	/* General */
	rcfw->seq_num = 0;
	set_bit(FIRMWARE_FIRST_FLAG, &rcfw->flags);
	bmap_size = BITS_TO_LONGS(rcfw->cmdq_depth) * sizeof(unsigned long);
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
		dev_err(&rcfw->pdev->dev, "CMDQ BAR region %d mapping failed\n",
			rcfw->cmdq_bar_reg);
		return -ENOMEM;
	}

	rcfw->cmdq_bar_reg_prod_off = virt_fn ? RCFW_VF_COMM_PROD_OFFSET :
					RCFW_PF_COMM_PROD_OFFSET;

	rcfw->cmdq_bar_reg_trig_off = RCFW_COMM_TRIG_OFFSET;

	/* CREQ */
	rcfw->creq_bar_reg = RCFW_COMM_CONS_PCI_BAR_REGION;
	res_base = pci_resource_start(pdev, rcfw->creq_bar_reg);
	if (!res_base)
		dev_err(&rcfw->pdev->dev,
			"CREQ BAR region %d resc start is 0!\n",
			rcfw->creq_bar_reg);
	/* Unconditionally map 8 bytes to support 57500 series */
	rcfw->creq_bar_reg_iomem = ioremap_nocache(res_base + cp_bar_reg_off,
						   8);
	if (!rcfw->creq_bar_reg_iomem) {
		dev_err(&rcfw->pdev->dev, "CREQ BAR region %d mapping failed\n",
			rcfw->creq_bar_reg);
		iounmap(rcfw->cmdq_bar_reg_iomem);
		rcfw->cmdq_bar_reg_iomem = NULL;
		return -ENOMEM;
	}
	rcfw->creq_qp_event_processed = 0;
	rcfw->creq_func_event_processed = 0;

	if (aeq_handler)
		rcfw->aeq_handler = aeq_handler;
	init_waitqueue_head(&rcfw->waitq);

	rc = bnxt_qplib_rcfw_start_irq(rcfw, msix_vector, true);
	if (rc) {
		dev_err(&rcfw->pdev->dev,
			"Failed to request IRQ for CREQ rc = 0x%x\n", rc);
		bnxt_qplib_disable_rcfw_channel(rcfw);
		return rc;
	}

	init.cmdq_pbl = cpu_to_le64(rcfw->cmdq.pbl[PBL_LVL_0].pg_map_arr[0]);
	init.cmdq_size_cmdq_lvl = cpu_to_le16(
		((rcfw->cmdq_depth << CMDQ_INIT_CMDQ_SIZE_SFT) &
		 CMDQ_INIT_CMDQ_SIZE_MASK) |
		((rcfw->cmdq.level << CMDQ_INIT_CMDQ_LVL_SFT) &
		 CMDQ_INIT_CMDQ_LVL_MASK));
	init.creq_ring_id = cpu_to_le16(rcfw->creq_ring_id);

	/* Write to the Bono mailbox register */
	__iowrite32_copy(rcfw->cmdq_bar_reg_iomem, &init, sizeof(init) / 4);
	return 0;
}

struct bnxt_qplib_rcfw_sbuf *bnxt_qplib_rcfw_alloc_sbuf(
		struct bnxt_qplib_rcfw *rcfw,
		u32 size)
{
	struct bnxt_qplib_rcfw_sbuf *sbuf;

	sbuf = kzalloc(sizeof(*sbuf), GFP_ATOMIC);
	if (!sbuf)
		return NULL;

	sbuf->size = size;
	sbuf->sb = dma_alloc_coherent(&rcfw->pdev->dev, sbuf->size,
				      &sbuf->dma_addr, GFP_ATOMIC);
	if (!sbuf->sb)
		goto bail;

	return sbuf;
bail:
	kfree(sbuf);
	return NULL;
}

void bnxt_qplib_rcfw_free_sbuf(struct bnxt_qplib_rcfw *rcfw,
			       struct bnxt_qplib_rcfw_sbuf *sbuf)
{
	if (sbuf->sb)
		dma_free_coherent(&rcfw->pdev->dev, sbuf->size,
				  sbuf->sb, sbuf->dma_addr);
	kfree(sbuf);
}
