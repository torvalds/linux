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

static void bnxt_qplib_service_creq(struct tasklet_struct *t);

/* Hardware communication channel */
static int __wait_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	struct bnxt_qplib_cmdq_ctx *cmdq;
	u16 cbit;
	int rc;

	cmdq = &rcfw->cmdq;
	cbit = cookie % rcfw->cmdq_depth;
	rc = wait_event_timeout(cmdq->waitq,
				!test_bit(cbit, cmdq->cmdq_bitmap),
				msecs_to_jiffies(RCFW_CMD_WAIT_TIME_MS));
	return rc ? 0 : -ETIMEDOUT;
};

static int __block_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	u32 count = RCFW_BLOCKED_CMD_WAIT_COUNT;
	struct bnxt_qplib_cmdq_ctx *cmdq;
	u16 cbit;

	cmdq = &rcfw->cmdq;
	cbit = cookie % rcfw->cmdq_depth;
	if (!test_bit(cbit, cmdq->cmdq_bitmap))
		goto done;
	do {
		udelay(1);
		bnxt_qplib_service_creq(&rcfw->creq.creq_tasklet);
	} while (test_bit(cbit, cmdq->cmdq_bitmap) && --count);
done:
	return count ? 0 : -ETIMEDOUT;
};

static int __send_message(struct bnxt_qplib_rcfw *rcfw, struct cmdq_base *req,
			  struct creq_base *resp, void *sb, u8 is_block)
{
	struct bnxt_qplib_cmdq_ctx *cmdq = &rcfw->cmdq;
	struct bnxt_qplib_hwq *hwq = &cmdq->hwq;
	struct bnxt_qplib_crsqe *crsqe;
	struct bnxt_qplib_cmdqe *cmdqe;
	u32 sw_prod, cmdq_prod;
	struct pci_dev *pdev;
	unsigned long flags;
	u32 size, opcode;
	u16 cookie, cbit;
	u8 *preq;

	pdev = rcfw->pdev;

	opcode = req->opcode;
	if (!test_bit(FIRMWARE_INITIALIZED_FLAG, &cmdq->flags) &&
	    (opcode != CMDQ_BASE_OPCODE_QUERY_FUNC &&
	     opcode != CMDQ_BASE_OPCODE_INITIALIZE_FW &&
	     opcode != CMDQ_BASE_OPCODE_QUERY_VERSION)) {
		dev_err(&pdev->dev,
			"RCFW not initialized, reject opcode 0x%x\n", opcode);
		return -EINVAL;
	}

	if (test_bit(FIRMWARE_INITIALIZED_FLAG, &cmdq->flags) &&
	    opcode == CMDQ_BASE_OPCODE_INITIALIZE_FW) {
		dev_err(&pdev->dev, "RCFW already initialized!\n");
		return -EINVAL;
	}

	if (test_bit(FIRMWARE_TIMED_OUT, &cmdq->flags))
		return -ETIMEDOUT;

	/* Cmdq are in 16-byte units, each request can consume 1 or more
	 * cmdqe
	 */
	spin_lock_irqsave(&hwq->lock, flags);
	if (req->cmd_size >= HWQ_FREE_SLOTS(hwq)) {
		dev_err(&pdev->dev, "RCFW: CMDQ is full!\n");
		spin_unlock_irqrestore(&hwq->lock, flags);
		return -EAGAIN;
	}


	cookie = cmdq->seq_num & RCFW_MAX_COOKIE_VALUE;
	cbit = cookie % rcfw->cmdq_depth;
	if (is_block)
		cookie |= RCFW_CMD_IS_BLOCKING;

	set_bit(cbit, cmdq->cmdq_bitmap);
	req->cookie = cpu_to_le16(cookie);
	crsqe = &rcfw->crsqe_tbl[cbit];
	if (crsqe->resp) {
		spin_unlock_irqrestore(&hwq->lock, flags);
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

	preq = (u8 *)req;
	do {
		/* Locate the next cmdq slot */
		sw_prod = HWQ_CMP(hwq->prod, hwq);
		cmdqe = bnxt_qplib_get_qe(hwq, sw_prod, NULL);
		if (!cmdqe) {
			dev_err(&pdev->dev,
				"RCFW request failed with no cmdqe!\n");
			goto done;
		}
		/* Copy a segment of the req cmd to the cmdq */
		memset(cmdqe, 0, sizeof(*cmdqe));
		memcpy(cmdqe, preq, min_t(u32, size, sizeof(*cmdqe)));
		preq += min_t(u32, size, sizeof(*cmdqe));
		size -= min_t(u32, size, sizeof(*cmdqe));
		hwq->prod++;
	} while (size > 0);
	cmdq->seq_num++;

	cmdq_prod = hwq->prod;
	if (test_bit(FIRMWARE_FIRST_FLAG, &cmdq->flags)) {
		/* The very first doorbell write
		 * is required to set this flag
		 * which prompts the FW to reset
		 * its internal pointers
		 */
		cmdq_prod |= BIT(FIRMWARE_FIRST_FLAG);
		clear_bit(FIRMWARE_FIRST_FLAG, &cmdq->flags);
	}

	/* ring CMDQ DB */
	wmb();
	writel(cmdq_prod, cmdq->cmdq_mbox.prod);
	writel(RCFW_CMDQ_TRIG_VAL, cmdq->cmdq_mbox.db);
done:
	spin_unlock_irqrestore(&hwq->lock, flags);
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

	/* Prevent posting if f/w is not in a state to process */
	if (test_bit(ERR_DEVICE_DETACHED, &rcfw->cmdq.flags))
		return 0;

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
		set_bit(FIRMWARE_TIMED_OUT, &rcfw->cmdq.flags);
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
	int rc;

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

	rc = rcfw->creq.aeq_handler(rcfw, (void *)func_event, NULL);
	return rc;
}

static int bnxt_qplib_process_qp_event(struct bnxt_qplib_rcfw *rcfw,
				       struct creq_qp_event *qp_event)
{
	struct creq_qp_error_notification *err_event;
	struct bnxt_qplib_hwq *hwq = &rcfw->cmdq.hwq;
	struct bnxt_qplib_crsqe *crsqe;
	struct bnxt_qplib_qp *qp;
	u16 cbit, blocked = 0;
	struct pci_dev *pdev;
	unsigned long flags;
	__le16  mcookie;
	u16 cookie;
	int rc = 0;
	u32 qp_id, tbl_indx;

	pdev = rcfw->pdev;
	switch (qp_event->event) {
	case CREQ_QP_EVENT_EVENT_QP_ERROR_NOTIFICATION:
		err_event = (struct creq_qp_error_notification *)qp_event;
		qp_id = le32_to_cpu(err_event->xid);
		tbl_indx = map_qp_id_to_tbl_indx(qp_id, rcfw);
		qp = rcfw->qp_tbl[tbl_indx].qp_handle;
		dev_dbg(&pdev->dev, "Received QP error notification\n");
		dev_dbg(&pdev->dev,
			"qpid 0x%x, req_err=0x%x, resp_err=0x%x\n",
			qp_id, err_event->req_err_state_reason,
			err_event->res_err_state_reason);
		if (!qp)
			break;
		bnxt_qplib_mark_qp_error(qp);
		rc = rcfw->creq.aeq_handler(rcfw, qp_event, qp);
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

		spin_lock_irqsave_nested(&hwq->lock, flags,
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
				dev_err(&pdev->dev,
					"CMD %s cookie sent=%#x, recd=%#x\n",
					crsqe->resp ? "mismatch" : "collision",
					crsqe->resp ? crsqe->resp->cookie : 0,
					mcookie);
		}
		if (!test_and_clear_bit(cbit, rcfw->cmdq.cmdq_bitmap))
			dev_warn(&pdev->dev,
				 "CMD bit %d was not requested\n", cbit);
		hwq->cons += crsqe->req_size;
		crsqe->req_size = 0;

		if (!blocked)
			wake_up(&rcfw->cmdq.waitq);
		spin_unlock_irqrestore(&hwq->lock, flags);
	}
	return rc;
}

/* SP - CREQ Completion handlers */
static void bnxt_qplib_service_creq(struct tasklet_struct *t)
{
	struct bnxt_qplib_rcfw *rcfw = from_tasklet(rcfw, t, creq.creq_tasklet);
	struct bnxt_qplib_creq_ctx *creq = &rcfw->creq;
	u32 type, budget = CREQ_ENTRY_POLL_BUDGET;
	struct bnxt_qplib_hwq *hwq = &creq->hwq;
	struct creq_base *creqe;
	u32 sw_cons, raw_cons;
	unsigned long flags;

	/* Service the CREQ until budget is over */
	spin_lock_irqsave(&hwq->lock, flags);
	raw_cons = hwq->cons;
	while (budget > 0) {
		sw_cons = HWQ_CMP(raw_cons, hwq);
		creqe = bnxt_qplib_get_qe(hwq, sw_cons, NULL);
		if (!CREQ_CMP_VALID(creqe, raw_cons, hwq->max_elements))
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
			creq->stats.creq_qp_event_processed++;
			break;
		case CREQ_BASE_TYPE_FUNC_EVENT:
			if (!bnxt_qplib_process_func_event
			    (rcfw, (struct creq_func_event *)creqe))
				creq->stats.creq_func_event_processed++;
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

	if (hwq->cons != raw_cons) {
		hwq->cons = raw_cons;
		bnxt_qplib_ring_nq_db(&creq->creq_db.dbinfo,
				      rcfw->res->cctx, true);
	}
	spin_unlock_irqrestore(&hwq->lock, flags);
}

static irqreturn_t bnxt_qplib_creq_irq(int irq, void *dev_instance)
{
	struct bnxt_qplib_rcfw *rcfw = dev_instance;
	struct bnxt_qplib_creq_ctx *creq;
	struct bnxt_qplib_hwq *hwq;
	u32 sw_cons;

	creq = &rcfw->creq;
	hwq = &creq->hwq;
	/* Prefetch the CREQ element */
	sw_cons = HWQ_CMP(hwq->cons, hwq);
	prefetch(bnxt_qplib_get_qe(hwq, sw_cons, NULL));

	tasklet_schedule(&creq->creq_tasklet);

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

	clear_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->cmdq.flags);
	return 0;
}

int bnxt_qplib_init_rcfw(struct bnxt_qplib_rcfw *rcfw,
			 struct bnxt_qplib_ctx *ctx, int is_virtfn)
{
	struct creq_initialize_fw_resp resp;
	struct cmdq_initialize_fw req;
	u16 cmd_flags = 0;
	u8 pgsz, lvl;
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
	if (is_virtfn)
		goto skip_ctx_setup;
	if (bnxt_qplib_is_chip_gen_p5(rcfw->res->cctx))
		goto config_vf_res;

	lvl = ctx->qpc_tbl.level;
	pgsz = bnxt_qplib_base_pg_size(&ctx->qpc_tbl);
	req.qpc_pg_size_qpc_lvl = (pgsz << CMDQ_INITIALIZE_FW_QPC_PG_SIZE_SFT) |
				   lvl;
	lvl = ctx->mrw_tbl.level;
	pgsz = bnxt_qplib_base_pg_size(&ctx->mrw_tbl);
	req.mrw_pg_size_mrw_lvl = (pgsz << CMDQ_INITIALIZE_FW_QPC_PG_SIZE_SFT) |
				   lvl;
	lvl = ctx->srqc_tbl.level;
	pgsz = bnxt_qplib_base_pg_size(&ctx->srqc_tbl);
	req.srq_pg_size_srq_lvl = (pgsz << CMDQ_INITIALIZE_FW_QPC_PG_SIZE_SFT) |
				   lvl;
	lvl = ctx->cq_tbl.level;
	pgsz = bnxt_qplib_base_pg_size(&ctx->cq_tbl);
	req.cq_pg_size_cq_lvl = (pgsz << CMDQ_INITIALIZE_FW_QPC_PG_SIZE_SFT) |
				 lvl;
	lvl = ctx->tim_tbl.level;
	pgsz = bnxt_qplib_base_pg_size(&ctx->tim_tbl);
	req.tim_pg_size_tim_lvl = (pgsz << CMDQ_INITIALIZE_FW_QPC_PG_SIZE_SFT) |
				   lvl;
	lvl = ctx->tqm_ctx.pde.level;
	pgsz = bnxt_qplib_base_pg_size(&ctx->tqm_ctx.pde);
	req.tqm_pg_size_tqm_lvl = (pgsz << CMDQ_INITIALIZE_FW_QPC_PG_SIZE_SFT) |
				   lvl;
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
		cpu_to_le64(ctx->tqm_ctx.pde.pbl[PBL_LVL_0].pg_map_arr[0]);

	req.number_of_qp = cpu_to_le32(ctx->qpc_tbl.max_elements);
	req.number_of_mrw = cpu_to_le32(ctx->mrw_tbl.max_elements);
	req.number_of_srq = cpu_to_le32(ctx->srqc_tbl.max_elements);
	req.number_of_cq = cpu_to_le32(ctx->cq_tbl.max_elements);

config_vf_res:
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
	set_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->cmdq.flags);
	return 0;
}

void bnxt_qplib_free_rcfw_channel(struct bnxt_qplib_rcfw *rcfw)
{
	kfree(rcfw->cmdq.cmdq_bitmap);
	kfree(rcfw->qp_tbl);
	kfree(rcfw->crsqe_tbl);
	bnxt_qplib_free_hwq(rcfw->res, &rcfw->cmdq.hwq);
	bnxt_qplib_free_hwq(rcfw->res, &rcfw->creq.hwq);
	rcfw->pdev = NULL;
}

int bnxt_qplib_alloc_rcfw_channel(struct bnxt_qplib_res *res,
				  struct bnxt_qplib_rcfw *rcfw,
				  struct bnxt_qplib_ctx *ctx,
				  int qp_tbl_sz)
{
	struct bnxt_qplib_hwq_attr hwq_attr = {};
	struct bnxt_qplib_sg_info sginfo = {};
	struct bnxt_qplib_cmdq_ctx *cmdq;
	struct bnxt_qplib_creq_ctx *creq;
	u32 bmap_size = 0;

	rcfw->pdev = res->pdev;
	cmdq = &rcfw->cmdq;
	creq = &rcfw->creq;
	rcfw->res = res;

	sginfo.pgsize = PAGE_SIZE;
	sginfo.pgshft = PAGE_SHIFT;

	hwq_attr.sginfo = &sginfo;
	hwq_attr.res = rcfw->res;
	hwq_attr.depth = BNXT_QPLIB_CREQE_MAX_CNT;
	hwq_attr.stride = BNXT_QPLIB_CREQE_UNITS;
	hwq_attr.type = bnxt_qplib_get_hwq_type(res);

	if (bnxt_qplib_alloc_init_hwq(&creq->hwq, &hwq_attr)) {
		dev_err(&rcfw->pdev->dev,
			"HW channel CREQ allocation failed\n");
		goto fail;
	}
	if (ctx->hwrm_intf_ver < HWRM_VERSION_RCFW_CMDQ_DEPTH_CHECK)
		rcfw->cmdq_depth = BNXT_QPLIB_CMDQE_MAX_CNT_256;
	else
		rcfw->cmdq_depth = BNXT_QPLIB_CMDQE_MAX_CNT_8192;

	sginfo.pgsize = bnxt_qplib_cmdqe_page_size(rcfw->cmdq_depth);
	hwq_attr.depth = rcfw->cmdq_depth;
	hwq_attr.stride = BNXT_QPLIB_CMDQE_UNITS;
	hwq_attr.type = HWQ_TYPE_CTX;
	if (bnxt_qplib_alloc_init_hwq(&cmdq->hwq, &hwq_attr)) {
		dev_err(&rcfw->pdev->dev,
			"HW channel CMDQ allocation failed\n");
		goto fail;
	}

	rcfw->crsqe_tbl = kcalloc(cmdq->hwq.max_elements,
				  sizeof(*rcfw->crsqe_tbl), GFP_KERNEL);
	if (!rcfw->crsqe_tbl)
		goto fail;

	bmap_size = BITS_TO_LONGS(rcfw->cmdq_depth) * sizeof(unsigned long);
	cmdq->cmdq_bitmap = kzalloc(bmap_size, GFP_KERNEL);
	if (!cmdq->cmdq_bitmap)
		goto fail;

	cmdq->bmap_size = bmap_size;

	/* Allocate one extra to hold the QP1 entries */
	rcfw->qp_tbl_size = qp_tbl_sz + 1;
	rcfw->qp_tbl = kcalloc(rcfw->qp_tbl_size, sizeof(struct bnxt_qplib_qp_node),
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
	struct bnxt_qplib_creq_ctx *creq;

	creq = &rcfw->creq;
	tasklet_disable(&creq->creq_tasklet);
	/* Mask h/w interrupts */
	bnxt_qplib_ring_nq_db(&creq->creq_db.dbinfo, rcfw->res->cctx, false);
	/* Sync with last running IRQ-handler */
	synchronize_irq(creq->msix_vec);
	if (kill)
		tasklet_kill(&creq->creq_tasklet);

	if (creq->requested) {
		free_irq(creq->msix_vec, rcfw);
		creq->requested = false;
	}
}

void bnxt_qplib_disable_rcfw_channel(struct bnxt_qplib_rcfw *rcfw)
{
	struct bnxt_qplib_creq_ctx *creq;
	struct bnxt_qplib_cmdq_ctx *cmdq;
	unsigned long indx;

	creq = &rcfw->creq;
	cmdq = &rcfw->cmdq;
	/* Make sure the HW channel is stopped! */
	bnxt_qplib_rcfw_stop_irq(rcfw, true);

	iounmap(cmdq->cmdq_mbox.reg.bar_reg);
	iounmap(creq->creq_db.reg.bar_reg);

	indx = find_first_bit(cmdq->cmdq_bitmap, cmdq->bmap_size);
	if (indx != cmdq->bmap_size)
		dev_err(&rcfw->pdev->dev,
			"disabling RCFW with pending cmd-bit %lx\n", indx);

	cmdq->cmdq_mbox.reg.bar_reg = NULL;
	creq->creq_db.reg.bar_reg = NULL;
	creq->aeq_handler = NULL;
	creq->msix_vec = 0;
}

int bnxt_qplib_rcfw_start_irq(struct bnxt_qplib_rcfw *rcfw, int msix_vector,
			      bool need_init)
{
	struct bnxt_qplib_creq_ctx *creq;
	int rc;

	creq = &rcfw->creq;

	if (creq->requested)
		return -EFAULT;

	creq->msix_vec = msix_vector;
	if (need_init)
		tasklet_setup(&creq->creq_tasklet, bnxt_qplib_service_creq);
	else
		tasklet_enable(&creq->creq_tasklet);
	rc = request_irq(creq->msix_vec, bnxt_qplib_creq_irq, 0,
			 "bnxt_qplib_creq", rcfw);
	if (rc)
		return rc;
	creq->requested = true;

	bnxt_qplib_ring_nq_db(&creq->creq_db.dbinfo, rcfw->res->cctx, true);

	return 0;
}

static int bnxt_qplib_map_cmdq_mbox(struct bnxt_qplib_rcfw *rcfw, bool is_vf)
{
	struct bnxt_qplib_cmdq_mbox *mbox;
	resource_size_t bar_reg;
	struct pci_dev *pdev;
	u16 prod_offt;
	int rc = 0;

	pdev = rcfw->pdev;
	mbox = &rcfw->cmdq.cmdq_mbox;

	mbox->reg.bar_id = RCFW_COMM_PCI_BAR_REGION;
	mbox->reg.len = RCFW_COMM_SIZE;
	mbox->reg.bar_base = pci_resource_start(pdev, mbox->reg.bar_id);
	if (!mbox->reg.bar_base) {
		dev_err(&pdev->dev,
			"QPLIB: CMDQ BAR region %d resc start is 0!\n",
			mbox->reg.bar_id);
		return -ENOMEM;
	}

	bar_reg = mbox->reg.bar_base + RCFW_COMM_BASE_OFFSET;
	mbox->reg.len = RCFW_COMM_SIZE;
	mbox->reg.bar_reg = ioremap(bar_reg, mbox->reg.len);
	if (!mbox->reg.bar_reg) {
		dev_err(&pdev->dev,
			"QPLIB: CMDQ BAR region %d mapping failed\n",
			mbox->reg.bar_id);
		return -ENOMEM;
	}

	prod_offt = is_vf ? RCFW_VF_COMM_PROD_OFFSET :
			    RCFW_PF_COMM_PROD_OFFSET;
	mbox->prod = (void  __iomem *)(mbox->reg.bar_reg + prod_offt);
	mbox->db = (void __iomem *)(mbox->reg.bar_reg + RCFW_COMM_TRIG_OFFSET);
	return rc;
}

static int bnxt_qplib_map_creq_db(struct bnxt_qplib_rcfw *rcfw, u32 reg_offt)
{
	struct bnxt_qplib_creq_db *creq_db;
	resource_size_t bar_reg;
	struct pci_dev *pdev;

	pdev = rcfw->pdev;
	creq_db = &rcfw->creq.creq_db;

	creq_db->reg.bar_id = RCFW_COMM_CONS_PCI_BAR_REGION;
	creq_db->reg.bar_base = pci_resource_start(pdev, creq_db->reg.bar_id);
	if (!creq_db->reg.bar_id)
		dev_err(&pdev->dev,
			"QPLIB: CREQ BAR region %d resc start is 0!",
			creq_db->reg.bar_id);

	bar_reg = creq_db->reg.bar_base + reg_offt;
	/* Unconditionally map 8 bytes to support 57500 series */
	creq_db->reg.len = 8;
	creq_db->reg.bar_reg = ioremap(bar_reg, creq_db->reg.len);
	if (!creq_db->reg.bar_reg) {
		dev_err(&pdev->dev,
			"QPLIB: CREQ BAR region %d mapping failed",
			creq_db->reg.bar_id);
		return -ENOMEM;
	}
	creq_db->dbinfo.db = creq_db->reg.bar_reg;
	creq_db->dbinfo.hwq = &rcfw->creq.hwq;
	creq_db->dbinfo.xid = rcfw->creq.ring_id;
	return 0;
}

static void bnxt_qplib_start_rcfw(struct bnxt_qplib_rcfw *rcfw)
{
	struct bnxt_qplib_cmdq_ctx *cmdq;
	struct bnxt_qplib_creq_ctx *creq;
	struct bnxt_qplib_cmdq_mbox *mbox;
	struct cmdq_init init = {0};

	cmdq = &rcfw->cmdq;
	creq = &rcfw->creq;
	mbox = &cmdq->cmdq_mbox;

	init.cmdq_pbl = cpu_to_le64(cmdq->hwq.pbl[PBL_LVL_0].pg_map_arr[0]);
	init.cmdq_size_cmdq_lvl =
			cpu_to_le16(((rcfw->cmdq_depth <<
				      CMDQ_INIT_CMDQ_SIZE_SFT) &
				    CMDQ_INIT_CMDQ_SIZE_MASK) |
				    ((cmdq->hwq.level <<
				      CMDQ_INIT_CMDQ_LVL_SFT) &
				    CMDQ_INIT_CMDQ_LVL_MASK));
	init.creq_ring_id = cpu_to_le16(creq->ring_id);
	/* Write to the Bono mailbox register */
	__iowrite32_copy(mbox->reg.bar_reg, &init, sizeof(init) / 4);
}

int bnxt_qplib_enable_rcfw_channel(struct bnxt_qplib_rcfw *rcfw,
				   int msix_vector,
				   int cp_bar_reg_off, int virt_fn,
				   aeq_handler_t aeq_handler)
{
	struct bnxt_qplib_cmdq_ctx *cmdq;
	struct bnxt_qplib_creq_ctx *creq;
	int rc;

	cmdq = &rcfw->cmdq;
	creq = &rcfw->creq;

	/* Clear to defaults */

	cmdq->seq_num = 0;
	set_bit(FIRMWARE_FIRST_FLAG, &cmdq->flags);
	init_waitqueue_head(&cmdq->waitq);

	creq->stats.creq_qp_event_processed = 0;
	creq->stats.creq_func_event_processed = 0;
	creq->aeq_handler = aeq_handler;

	rc = bnxt_qplib_map_cmdq_mbox(rcfw, virt_fn);
	if (rc)
		return rc;

	rc = bnxt_qplib_map_creq_db(rcfw, cp_bar_reg_off);
	if (rc)
		return rc;

	rc = bnxt_qplib_rcfw_start_irq(rcfw, msix_vector, true);
	if (rc) {
		dev_err(&rcfw->pdev->dev,
			"Failed to request IRQ for CREQ rc = 0x%x\n", rc);
		bnxt_qplib_disable_rcfw_channel(rcfw);
		return rc;
	}

	bnxt_qplib_start_rcfw(rcfw);

	return 0;
}

struct bnxt_qplib_rcfw_sbuf *bnxt_qplib_rcfw_alloc_sbuf(
		struct bnxt_qplib_rcfw *rcfw,
		u32 size)
{
	struct bnxt_qplib_rcfw_sbuf *sbuf;

	sbuf = kzalloc(sizeof(*sbuf), GFP_KERNEL);
	if (!sbuf)
		return NULL;

	sbuf->size = size;
	sbuf->sb = dma_alloc_coherent(&rcfw->pdev->dev, sbuf->size,
				      &sbuf->dma_addr, GFP_KERNEL);
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
