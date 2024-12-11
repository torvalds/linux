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
#include "qplib_tlv.h"

static void bnxt_qplib_service_creq(struct tasklet_struct *t);

/**
 * bnxt_qplib_map_rc  -  map return type based on opcode
 * @opcode:  roce slow path opcode
 *
 * case #1
 * Firmware initiated error recovery is a safe state machine and
 * driver can consider all the underlying rdma resources are free.
 * In this state, it is safe to return success for opcodes related to
 * destroying rdma resources (like destroy qp, destroy cq etc.).
 *
 * case #2
 * If driver detect potential firmware stall, it is not safe state machine
 * and the driver can not consider all the underlying rdma resources are
 * freed.
 * In this state, it is not safe to return success for opcodes related to
 * destroying rdma resources (like destroy qp, destroy cq etc.).
 *
 * Scope of this helper function is only for case #1.
 *
 * Returns:
 * 0 to communicate success to caller.
 * Non zero error code to communicate failure to caller.
 */
static int bnxt_qplib_map_rc(u8 opcode)
{
	switch (opcode) {
	case CMDQ_BASE_OPCODE_DESTROY_QP:
	case CMDQ_BASE_OPCODE_DESTROY_SRQ:
	case CMDQ_BASE_OPCODE_DESTROY_CQ:
	case CMDQ_BASE_OPCODE_DEALLOCATE_KEY:
	case CMDQ_BASE_OPCODE_DEREGISTER_MR:
	case CMDQ_BASE_OPCODE_DELETE_GID:
	case CMDQ_BASE_OPCODE_DESTROY_QP1:
	case CMDQ_BASE_OPCODE_DESTROY_AH:
	case CMDQ_BASE_OPCODE_DEINITIALIZE_FW:
	case CMDQ_BASE_OPCODE_MODIFY_ROCE_CC:
	case CMDQ_BASE_OPCODE_SET_LINK_AGGR_MODE:
		return 0;
	default:
		return -ETIMEDOUT;
	}
}

/**
 * bnxt_re_is_fw_stalled   -	Check firmware health
 * @rcfw:     rcfw channel instance of rdev
 * @cookie:   cookie to track the command
 *
 * If firmware has not responded any rcfw command within
 * rcfw->max_timeout, consider firmware as stalled.
 *
 * Returns:
 * 0 if firmware is responding
 * -ENODEV if firmware is not responding
 */
static int bnxt_re_is_fw_stalled(struct bnxt_qplib_rcfw *rcfw,
				 u16 cookie)
{
	struct bnxt_qplib_cmdq_ctx *cmdq;
	struct bnxt_qplib_crsqe *crsqe;

	crsqe = &rcfw->crsqe_tbl[cookie];
	cmdq = &rcfw->cmdq;

	if (time_after(jiffies, cmdq->last_seen +
		      (rcfw->max_timeout * HZ))) {
		dev_warn_ratelimited(&rcfw->pdev->dev,
				     "%s: FW STALL Detected. cmdq[%#x]=%#x waited (%d > %d) msec active %d ",
				     __func__, cookie, crsqe->opcode,
				     jiffies_to_msecs(jiffies - cmdq->last_seen),
				     rcfw->max_timeout * 1000,
				     crsqe->is_in_used);
		return -ENODEV;
	}

	return 0;
}

/**
 * __wait_for_resp   -	Don't hold the cpu context and wait for response
 * @rcfw:    rcfw channel instance of rdev
 * @cookie:  cookie to track the command
 *
 * Wait for command completion in sleepable context.
 *
 * Returns:
 * 0 if command is completed by firmware.
 * Non zero error code for rest of the case.
 */
static int __wait_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	struct bnxt_qplib_cmdq_ctx *cmdq;
	struct bnxt_qplib_crsqe *crsqe;
	int ret;

	cmdq = &rcfw->cmdq;
	crsqe = &rcfw->crsqe_tbl[cookie];

	do {
		if (test_bit(ERR_DEVICE_DETACHED, &cmdq->flags))
			return bnxt_qplib_map_rc(crsqe->opcode);
		if (test_bit(FIRMWARE_STALL_DETECTED, &cmdq->flags))
			return -ETIMEDOUT;

		wait_event_timeout(cmdq->waitq,
				   !crsqe->is_in_used ||
				   test_bit(ERR_DEVICE_DETACHED, &cmdq->flags),
				   msecs_to_jiffies(rcfw->max_timeout * 1000));

		if (!crsqe->is_in_used)
			return 0;

		bnxt_qplib_service_creq(&rcfw->creq.creq_tasklet);

		if (!crsqe->is_in_used)
			return 0;

		ret = bnxt_re_is_fw_stalled(rcfw, cookie);
		if (ret)
			return ret;

	} while (true);
};

/**
 * __block_for_resp   -	hold the cpu context and wait for response
 * @rcfw:    rcfw channel instance of rdev
 * @cookie:  cookie to track the command
 *
 * This function will hold the cpu (non-sleepable context) and
 * wait for command completion. Maximum holding interval is 8 second.
 *
 * Returns:
 * -ETIMEOUT if command is not completed in specific time interval.
 * 0 if command is completed by firmware.
 */
static int __block_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	struct bnxt_qplib_cmdq_ctx *cmdq = &rcfw->cmdq;
	struct bnxt_qplib_crsqe *crsqe;
	unsigned long issue_time = 0;

	issue_time = jiffies;
	crsqe = &rcfw->crsqe_tbl[cookie];

	do {
		if (test_bit(ERR_DEVICE_DETACHED, &cmdq->flags))
			return bnxt_qplib_map_rc(crsqe->opcode);
		if (test_bit(FIRMWARE_STALL_DETECTED, &cmdq->flags))
			return -ETIMEDOUT;

		udelay(1);

		bnxt_qplib_service_creq(&rcfw->creq.creq_tasklet);
		if (!crsqe->is_in_used)
			return 0;

	} while (time_before(jiffies, issue_time + (8 * HZ)));

	return -ETIMEDOUT;
};

/*  __send_message_no_waiter -	get cookie and post the message.
 * @rcfw:   rcfw channel instance of rdev
 * @msg:    qplib message internal
 *
 * This function will just post and don't bother about completion.
 * Current design of this function is -
 * user must hold the completion queue hwq->lock.
 * user must have used existing completion and free the resources.
 * this function will not check queue full condition.
 * this function will explicitly set is_waiter_alive=false.
 * current use case is - send destroy_ah if create_ah is return
 * after waiter of create_ah is lost. It can be extended for other
 * use case as well.
 *
 * Returns: Nothing
 *
 */
static void __send_message_no_waiter(struct bnxt_qplib_rcfw *rcfw,
				     struct bnxt_qplib_cmdqmsg *msg)
{
	struct bnxt_qplib_cmdq_ctx *cmdq = &rcfw->cmdq;
	struct bnxt_qplib_hwq *hwq = &cmdq->hwq;
	struct bnxt_qplib_crsqe *crsqe;
	struct bnxt_qplib_cmdqe *cmdqe;
	u32 sw_prod, cmdq_prod;
	u16 cookie;
	u32 bsize;
	u8 *preq;

	cookie = cmdq->seq_num & RCFW_MAX_COOKIE_VALUE;
	__set_cmdq_base_cookie(msg->req, msg->req_sz, cpu_to_le16(cookie));
	crsqe = &rcfw->crsqe_tbl[cookie];

	/* Set cmd_size in terms of 16B slots in req. */
	bsize = bnxt_qplib_set_cmd_slots(msg->req);
	/* GET_CMD_SIZE would return number of slots in either case of tlv
	 * and non-tlv commands after call to bnxt_qplib_set_cmd_slots()
	 */
	crsqe->is_internal_cmd = true;
	crsqe->is_waiter_alive = false;
	crsqe->is_in_used = true;
	crsqe->req_size = __get_cmdq_base_cmd_size(msg->req, msg->req_sz);

	preq = (u8 *)msg->req;
	do {
		/* Locate the next cmdq slot */
		sw_prod = HWQ_CMP(hwq->prod, hwq);
		cmdqe = bnxt_qplib_get_qe(hwq, sw_prod, NULL);
		/* Copy a segment of the req cmd to the cmdq */
		memset(cmdqe, 0, sizeof(*cmdqe));
		memcpy(cmdqe, preq, min_t(u32, bsize, sizeof(*cmdqe)));
		preq += min_t(u32, bsize, sizeof(*cmdqe));
		bsize -= min_t(u32, bsize, sizeof(*cmdqe));
		hwq->prod++;
	} while (bsize > 0);
	cmdq->seq_num++;

	cmdq_prod = hwq->prod;
	atomic_inc(&rcfw->timeout_send);
	/* ring CMDQ DB */
	wmb();
	writel(cmdq_prod, cmdq->cmdq_mbox.prod);
	writel(RCFW_CMDQ_TRIG_VAL, cmdq->cmdq_mbox.db);
}

static int __send_message(struct bnxt_qplib_rcfw *rcfw,
			  struct bnxt_qplib_cmdqmsg *msg, u8 opcode)
{
	u32 bsize, free_slots, required_slots;
	struct bnxt_qplib_cmdq_ctx *cmdq;
	struct bnxt_qplib_crsqe *crsqe;
	struct bnxt_qplib_cmdqe *cmdqe;
	struct bnxt_qplib_hwq *hwq;
	u32 sw_prod, cmdq_prod;
	struct pci_dev *pdev;
	u16 cookie;
	u8 *preq;

	cmdq = &rcfw->cmdq;
	hwq = &cmdq->hwq;
	pdev = rcfw->pdev;

	/* Cmdq are in 16-byte units, each request can consume 1 or more
	 * cmdqe
	 */
	spin_lock_bh(&hwq->lock);
	required_slots = bnxt_qplib_get_cmd_slots(msg->req);
	free_slots = HWQ_FREE_SLOTS(hwq);
	cookie = cmdq->seq_num & RCFW_MAX_COOKIE_VALUE;
	crsqe = &rcfw->crsqe_tbl[cookie];

	if (required_slots >= free_slots) {
		dev_info_ratelimited(&pdev->dev,
				     "CMDQ is full req/free %d/%d!",
				     required_slots, free_slots);
		spin_unlock_bh(&hwq->lock);
		return -EAGAIN;
	}
	if (msg->block)
		cookie |= RCFW_CMD_IS_BLOCKING;
	__set_cmdq_base_cookie(msg->req, msg->req_sz, cpu_to_le16(cookie));

	bsize = bnxt_qplib_set_cmd_slots(msg->req);
	crsqe->free_slots = free_slots;
	crsqe->resp = (struct creq_qp_event *)msg->resp;
	crsqe->resp->cookie = cpu_to_le16(cookie);
	crsqe->is_internal_cmd = false;
	crsqe->is_waiter_alive = true;
	crsqe->is_in_used = true;
	crsqe->opcode = opcode;

	crsqe->req_size = __get_cmdq_base_cmd_size(msg->req, msg->req_sz);
	if (__get_cmdq_base_resp_size(msg->req, msg->req_sz) && msg->sb) {
		struct bnxt_qplib_rcfw_sbuf *sbuf = msg->sb;

		__set_cmdq_base_resp_addr(msg->req, msg->req_sz,
					  cpu_to_le64(sbuf->dma_addr));
		__set_cmdq_base_resp_size(msg->req, msg->req_sz,
					  ALIGN(sbuf->size,
						BNXT_QPLIB_CMDQE_UNITS) /
						BNXT_QPLIB_CMDQE_UNITS);
	}

	preq = (u8 *)msg->req;
	do {
		/* Locate the next cmdq slot */
		sw_prod = HWQ_CMP(hwq->prod, hwq);
		cmdqe = bnxt_qplib_get_qe(hwq, sw_prod, NULL);
		/* Copy a segment of the req cmd to the cmdq */
		memset(cmdqe, 0, sizeof(*cmdqe));
		memcpy(cmdqe, preq, min_t(u32, bsize, sizeof(*cmdqe)));
		preq += min_t(u32, bsize, sizeof(*cmdqe));
		bsize -= min_t(u32, bsize, sizeof(*cmdqe));
		hwq->prod++;
	} while (bsize > 0);
	cmdq->seq_num++;

	cmdq_prod = hwq->prod & 0xFFFF;
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
	spin_unlock_bh(&hwq->lock);
	/* Return the CREQ response pointer */
	return 0;
}

/**
 * __poll_for_resp   -	self poll completion for rcfw command
 * @rcfw:     rcfw channel instance of rdev
 * @cookie:   cookie to track the command
 *
 * It works same as __wait_for_resp except this function will
 * do self polling in sort interval since interrupt is disabled.
 * This function can not be called from non-sleepable context.
 *
 * Returns:
 * -ETIMEOUT if command is not completed in specific time interval.
 * 0 if command is completed by firmware.
 */
static int __poll_for_resp(struct bnxt_qplib_rcfw *rcfw, u16 cookie)
{
	struct bnxt_qplib_cmdq_ctx *cmdq = &rcfw->cmdq;
	struct bnxt_qplib_crsqe *crsqe;
	unsigned long issue_time;
	int ret;

	issue_time = jiffies;
	crsqe = &rcfw->crsqe_tbl[cookie];

	do {
		if (test_bit(ERR_DEVICE_DETACHED, &cmdq->flags))
			return bnxt_qplib_map_rc(crsqe->opcode);
		if (test_bit(FIRMWARE_STALL_DETECTED, &cmdq->flags))
			return -ETIMEDOUT;

		usleep_range(1000, 1001);

		bnxt_qplib_service_creq(&rcfw->creq.creq_tasklet);
		if (!crsqe->is_in_used)
			return 0;
		if (jiffies_to_msecs(jiffies - issue_time) >
		    (rcfw->max_timeout * 1000)) {
			ret = bnxt_re_is_fw_stalled(rcfw, cookie);
			if (ret)
				return ret;
		}
	} while (true);
};

static int __send_message_basic_sanity(struct bnxt_qplib_rcfw *rcfw,
				       struct bnxt_qplib_cmdqmsg *msg,
				       u8 opcode)
{
	struct bnxt_qplib_cmdq_ctx *cmdq;

	cmdq = &rcfw->cmdq;

	/* Prevent posting if f/w is not in a state to process */
	if (test_bit(ERR_DEVICE_DETACHED, &rcfw->cmdq.flags))
		return bnxt_qplib_map_rc(opcode);
	if (test_bit(FIRMWARE_STALL_DETECTED, &cmdq->flags))
		return -ETIMEDOUT;

	if (test_bit(FIRMWARE_INITIALIZED_FLAG, &cmdq->flags) &&
	    opcode == CMDQ_BASE_OPCODE_INITIALIZE_FW) {
		dev_err(&rcfw->pdev->dev, "QPLIB: RCFW already initialized!");
		return -EINVAL;
	}

	if (!test_bit(FIRMWARE_INITIALIZED_FLAG, &cmdq->flags) &&
	    (opcode != CMDQ_BASE_OPCODE_QUERY_FUNC &&
	     opcode != CMDQ_BASE_OPCODE_INITIALIZE_FW &&
	     opcode != CMDQ_BASE_OPCODE_QUERY_VERSION)) {
		dev_err(&rcfw->pdev->dev,
			"QPLIB: RCFW not initialized, reject opcode 0x%x",
			opcode);
		return -EOPNOTSUPP;
	}

	return 0;
}

/* This function will just post and do not bother about completion */
static void __destroy_timedout_ah(struct bnxt_qplib_rcfw *rcfw,
				  struct creq_create_ah_resp *create_ah_resp)
{
	struct bnxt_qplib_cmdqmsg msg = {};
	struct cmdq_destroy_ah req = {};

	bnxt_qplib_rcfw_cmd_prep((struct cmdq_base *)&req,
				 CMDQ_BASE_OPCODE_DESTROY_AH,
				 sizeof(req));
	req.ah_cid = create_ah_resp->xid;
	msg.req = (struct cmdq_base *)&req;
	msg.req_sz = sizeof(req);
	__send_message_no_waiter(rcfw, &msg);
	dev_info_ratelimited(&rcfw->pdev->dev,
			     "From %s: ah_cid = %d timeout_send %d\n",
			     __func__, req.ah_cid,
			     atomic_read(&rcfw->timeout_send));
}

/**
 * __bnxt_qplib_rcfw_send_message   -	qplib interface to send
 * and complete rcfw command.
 * @rcfw:   rcfw channel instance of rdev
 * @msg:    qplib message internal
 *
 * This function does not account shadow queue depth. It will send
 * all the command unconditionally as long as send queue is not full.
 *
 * Returns:
 * 0 if command completed by firmware.
 * Non zero if the command is not completed by firmware.
 */
static int __bnxt_qplib_rcfw_send_message(struct bnxt_qplib_rcfw *rcfw,
					  struct bnxt_qplib_cmdqmsg *msg)
{
	struct creq_qp_event *evnt = (struct creq_qp_event *)msg->resp;
	struct bnxt_qplib_crsqe *crsqe;
	u16 cookie;
	int rc;
	u8 opcode;

	opcode = __get_cmdq_base_opcode(msg->req, msg->req_sz);

	rc = __send_message_basic_sanity(rcfw, msg, opcode);
	if (rc)
		return rc;

	rc = __send_message(rcfw, msg, opcode);
	if (rc)
		return rc;

	cookie = le16_to_cpu(__get_cmdq_base_cookie(msg->req, msg->req_sz))
				& RCFW_MAX_COOKIE_VALUE;

	if (msg->block)
		rc = __block_for_resp(rcfw, cookie);
	else if (atomic_read(&rcfw->rcfw_intr_enabled))
		rc = __wait_for_resp(rcfw, cookie);
	else
		rc = __poll_for_resp(rcfw, cookie);

	if (rc) {
		spin_lock_bh(&rcfw->cmdq.hwq.lock);
		crsqe = &rcfw->crsqe_tbl[cookie];
		crsqe->is_waiter_alive = false;
		if (rc == -ENODEV)
			set_bit(FIRMWARE_STALL_DETECTED, &rcfw->cmdq.flags);
		spin_unlock_bh(&rcfw->cmdq.hwq.lock);
		return -ETIMEDOUT;
	}

	if (evnt->status) {
		/* failed with status */
		dev_err(&rcfw->pdev->dev, "cmdq[%#x]=%#x status %#x\n",
			cookie, opcode, evnt->status);
		rc = -EIO;
	}

	return rc;
}

/**
 * bnxt_qplib_rcfw_send_message   -	qplib interface to send
 * and complete rcfw command.
 * @rcfw:   rcfw channel instance of rdev
 * @msg:    qplib message internal
 *
 * Driver interact with Firmware through rcfw channel/slow path in two ways.
 * a. Blocking rcfw command send. In this path, driver cannot hold
 * the context for longer period since it is holding cpu until
 * command is not completed.
 * b. Non-blocking rcfw command send. In this path, driver can hold the
 * context for longer period. There may be many pending command waiting
 * for completion because of non-blocking nature.
 *
 * Driver will use shadow queue depth. Current queue depth of 8K
 * (due to size of rcfw message there can be actual ~4K rcfw outstanding)
 * is not optimal for rcfw command processing in firmware.
 *
 * Restrict at max #RCFW_CMD_NON_BLOCKING_SHADOW_QD Non-Blocking rcfw commands.
 * Allow all blocking commands until there is no queue full.
 *
 * Returns:
 * 0 if command completed by firmware.
 * Non zero if the command is not completed by firmware.
 */
int bnxt_qplib_rcfw_send_message(struct bnxt_qplib_rcfw *rcfw,
				 struct bnxt_qplib_cmdqmsg *msg)
{
	int ret;

	if (!msg->block) {
		down(&rcfw->rcfw_inflight);
		ret = __bnxt_qplib_rcfw_send_message(rcfw, msg);
		up(&rcfw->rcfw_inflight);
	} else {
		ret = __bnxt_qplib_rcfw_send_message(rcfw, msg);
	}

	return ret;
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
				       struct creq_qp_event *qp_event,
				       u32 *num_wait)
{
	struct creq_qp_error_notification *err_event;
	struct bnxt_qplib_hwq *hwq = &rcfw->cmdq.hwq;
	struct bnxt_qplib_crsqe *crsqe;
	u32 qp_id, tbl_indx, req_size;
	struct bnxt_qplib_qp *qp;
	u16 cookie, blocked = 0;
	bool is_waiter_alive;
	struct pci_dev *pdev;
	u32 wait_cmds = 0;
	int rc = 0;

	pdev = rcfw->pdev;
	switch (qp_event->event) {
	case CREQ_QP_EVENT_EVENT_QP_ERROR_NOTIFICATION:
		err_event = (struct creq_qp_error_notification *)qp_event;
		qp_id = le32_to_cpu(err_event->xid);
		spin_lock(&rcfw->tbl_lock);
		tbl_indx = map_qp_id_to_tbl_indx(qp_id, rcfw);
		qp = rcfw->qp_tbl[tbl_indx].qp_handle;
		if (!qp) {
			spin_unlock(&rcfw->tbl_lock);
			break;
		}
		bnxt_qplib_mark_qp_error(qp);
		rc = rcfw->creq.aeq_handler(rcfw, qp_event, qp);
		spin_unlock(&rcfw->tbl_lock);
		dev_dbg(&pdev->dev, "Received QP error notification\n");
		dev_dbg(&pdev->dev,
			"qpid 0x%x, req_err=0x%x, resp_err=0x%x\n",
			qp_id, err_event->req_err_state_reason,
			err_event->res_err_state_reason);
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

		spin_lock_nested(&hwq->lock, SINGLE_DEPTH_NESTING);
		cookie = le16_to_cpu(qp_event->cookie);
		blocked = cookie & RCFW_CMD_IS_BLOCKING;
		cookie &= RCFW_MAX_COOKIE_VALUE;
		crsqe = &rcfw->crsqe_tbl[cookie];

		if (WARN_ONCE(test_bit(FIRMWARE_STALL_DETECTED,
				       &rcfw->cmdq.flags),
		    "QPLIB: Unreponsive rcfw channel detected.!!")) {
			dev_info(&pdev->dev,
				 "rcfw timedout: cookie = %#x, free_slots = %d",
				 cookie, crsqe->free_slots);
			spin_unlock(&hwq->lock);
			return rc;
		}

		if (crsqe->is_internal_cmd && !qp_event->status)
			atomic_dec(&rcfw->timeout_send);

		if (crsqe->is_waiter_alive) {
			if (crsqe->resp) {
				memcpy(crsqe->resp, qp_event, sizeof(*qp_event));
				/* Insert write memory barrier to ensure that
				 * response data is copied before clearing the
				 * flags
				 */
				smp_wmb();
			}
			if (!blocked)
				wait_cmds++;
		}

		req_size = crsqe->req_size;
		is_waiter_alive = crsqe->is_waiter_alive;

		crsqe->req_size = 0;
		if (!is_waiter_alive)
			crsqe->resp = NULL;

		crsqe->is_in_used = false;

		hwq->cons += req_size;

		/* This is a case to handle below scenario -
		 * Create AH is completed successfully by firmware,
		 * but completion took more time and driver already lost
		 * the context of create_ah from caller.
		 * We have already return failure for create_ah verbs,
		 * so let's destroy the same address vector since it is
		 * no more used in stack. We don't care about completion
		 * in __send_message_no_waiter.
		 * If destroy_ah is failued by firmware, there will be AH
		 * resource leak and relatively not critical +  unlikely
		 * scenario. Current design is not to handle such case.
		 */
		if (!is_waiter_alive && !qp_event->status &&
		    qp_event->event == CREQ_QP_EVENT_EVENT_CREATE_AH)
			__destroy_timedout_ah(rcfw,
					      (struct creq_create_ah_resp *)
					      qp_event);
		spin_unlock(&hwq->lock);
	}
	*num_wait += wait_cmds;
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
	u32 num_wakeup = 0;
	u32 hw_polled = 0;

	/* Service the CREQ until budget is over */
	spin_lock_bh(&hwq->lock);
	while (budget > 0) {
		creqe = bnxt_qplib_get_qe(hwq, hwq->cons, NULL);
		if (!CREQ_CMP_VALID(creqe, creq->creq_db.dbinfo.flags))
			break;
		/* The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();
		rcfw->cmdq.last_seen = jiffies;

		type = creqe->type & CREQ_BASE_TYPE_MASK;
		switch (type) {
		case CREQ_BASE_TYPE_QP_EVENT:
			bnxt_qplib_process_qp_event
				(rcfw, (struct creq_qp_event *)creqe,
				 &num_wakeup);
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
		budget--;
		hw_polled++;
		bnxt_qplib_hwq_incr_cons(hwq->max_elements, &hwq->cons,
					 1, &creq->creq_db.dbinfo.flags);
	}

	if (hw_polled)
		bnxt_qplib_ring_nq_db(&creq->creq_db.dbinfo,
				      rcfw->res->cctx, true);
	spin_unlock_bh(&hwq->lock);
	if (num_wakeup)
		wake_up_nr(&rcfw->cmdq.waitq, num_wakeup);
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
	struct creq_deinitialize_fw_resp resp = {};
	struct cmdq_deinitialize_fw req = {};
	struct bnxt_qplib_cmdqmsg msg = {};
	int rc;

	bnxt_qplib_rcfw_cmd_prep((struct cmdq_base *)&req,
				 CMDQ_BASE_OPCODE_DEINITIALIZE_FW,
				 sizeof(req));
	bnxt_qplib_fill_cmdqmsg(&msg, &req, &resp, NULL,
				sizeof(req), sizeof(resp), 0);
	rc = bnxt_qplib_rcfw_send_message(rcfw, &msg);
	if (rc)
		return rc;

	clear_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->cmdq.flags);
	return 0;
}

int bnxt_qplib_init_rcfw(struct bnxt_qplib_rcfw *rcfw,
			 struct bnxt_qplib_ctx *ctx, int is_virtfn)
{
	struct creq_initialize_fw_resp resp = {};
	struct cmdq_initialize_fw req = {};
	struct bnxt_qplib_cmdqmsg msg = {};
	u16 flags = 0;
	u8 pgsz, lvl;
	int rc;

	bnxt_qplib_rcfw_cmd_prep((struct cmdq_base *)&req,
				 CMDQ_BASE_OPCODE_INITIALIZE_FW,
				 sizeof(req));
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
	if (is_virtfn || bnxt_qplib_is_chip_gen_p5_p7(rcfw->res->cctx))
		goto skip_ctx_setup;

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

skip_ctx_setup:
	if (BNXT_RE_HW_RETX(rcfw->res->dattr->dev_cap_flags))
		flags |= CMDQ_INITIALIZE_FW_FLAGS_HW_REQUESTER_RETX_SUPPORTED;
	if (_is_optimize_modify_qp_supported(rcfw->res->dattr->dev_cap_flags2))
		flags |= CMDQ_INITIALIZE_FW_FLAGS_OPTIMIZE_MODIFY_QP_SUPPORTED;
	if (rcfw->res->en_dev->flags & BNXT_EN_FLAG_ROCE_VF_RES_MGMT)
		flags |= CMDQ_INITIALIZE_FW_FLAGS_L2_VF_RESOURCE_MGMT;
	req.flags |= cpu_to_le16(flags);
	req.stat_ctx_id = cpu_to_le32(ctx->stats.fw_id);
	bnxt_qplib_fill_cmdqmsg(&msg, &req, &resp, NULL, sizeof(req), sizeof(resp), 0);
	rc = bnxt_qplib_rcfw_send_message(rcfw, &msg);
	if (rc)
		return rc;
	set_bit(FIRMWARE_INITIALIZED_FLAG, &rcfw->cmdq.flags);
	return 0;
}

void bnxt_qplib_free_rcfw_channel(struct bnxt_qplib_rcfw *rcfw)
{
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

	rcfw->cmdq_depth = BNXT_QPLIB_CMDQE_MAX_CNT;

	sginfo.pgsize = bnxt_qplib_cmdqe_page_size(rcfw->cmdq_depth);
	hwq_attr.depth = rcfw->cmdq_depth & 0x7FFFFFFF;
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

	/* Allocate one extra to hold the QP1 entries */
	rcfw->qp_tbl_size = qp_tbl_sz + 1;
	rcfw->qp_tbl = kcalloc(rcfw->qp_tbl_size, sizeof(struct bnxt_qplib_qp_node),
			       GFP_KERNEL);
	if (!rcfw->qp_tbl)
		goto fail;
	spin_lock_init(&rcfw->tbl_lock);

	rcfw->max_timeout = res->cctx->hwrm_cmd_max_timeout;

	return 0;

fail:
	bnxt_qplib_free_rcfw_channel(rcfw);
	return -ENOMEM;
}

void bnxt_qplib_rcfw_stop_irq(struct bnxt_qplib_rcfw *rcfw, bool kill)
{
	struct bnxt_qplib_creq_ctx *creq;

	creq = &rcfw->creq;

	if (!creq->requested)
		return;

	creq->requested = false;
	/* Mask h/w interrupts */
	bnxt_qplib_ring_nq_db(&creq->creq_db.dbinfo, rcfw->res->cctx, false);
	/* Sync with last running IRQ-handler */
	synchronize_irq(creq->msix_vec);
	free_irq(creq->msix_vec, rcfw);
	kfree(creq->irq_name);
	creq->irq_name = NULL;
	atomic_set(&rcfw->rcfw_intr_enabled, 0);
	if (kill)
		tasklet_kill(&creq->creq_tasklet);
	tasklet_disable(&creq->creq_tasklet);
}

void bnxt_qplib_disable_rcfw_channel(struct bnxt_qplib_rcfw *rcfw)
{
	struct bnxt_qplib_creq_ctx *creq;
	struct bnxt_qplib_cmdq_ctx *cmdq;

	creq = &rcfw->creq;
	cmdq = &rcfw->cmdq;
	/* Make sure the HW channel is stopped! */
	bnxt_qplib_rcfw_stop_irq(rcfw, true);

	iounmap(cmdq->cmdq_mbox.reg.bar_reg);
	iounmap(creq->creq_db.reg.bar_reg);

	cmdq->cmdq_mbox.reg.bar_reg = NULL;
	creq->creq_db.reg.bar_reg = NULL;
	creq->aeq_handler = NULL;
	creq->msix_vec = 0;
}

int bnxt_qplib_rcfw_start_irq(struct bnxt_qplib_rcfw *rcfw, int msix_vector,
			      bool need_init)
{
	struct bnxt_qplib_creq_ctx *creq;
	struct bnxt_qplib_res *res;
	int rc;

	creq = &rcfw->creq;
	res = rcfw->res;

	if (creq->requested)
		return -EFAULT;

	creq->msix_vec = msix_vector;
	if (need_init)
		tasklet_setup(&creq->creq_tasklet, bnxt_qplib_service_creq);
	else
		tasklet_enable(&creq->creq_tasklet);

	creq->irq_name = kasprintf(GFP_KERNEL, "bnxt_re-creq@pci:%s",
				   pci_name(res->pdev));
	if (!creq->irq_name)
		return -ENOMEM;
	rc = request_irq(creq->msix_vec, bnxt_qplib_creq_irq, 0,
			 creq->irq_name, rcfw);
	if (rc) {
		kfree(creq->irq_name);
		creq->irq_name = NULL;
		tasklet_disable(&creq->creq_tasklet);
		return rc;
	}
	creq->requested = true;

	bnxt_qplib_ring_nq_db(&creq->creq_db.dbinfo, res->cctx, true);
	atomic_inc(&rcfw->rcfw_intr_enabled);

	return 0;
}

static int bnxt_qplib_map_cmdq_mbox(struct bnxt_qplib_rcfw *rcfw)
{
	struct bnxt_qplib_cmdq_mbox *mbox;
	resource_size_t bar_reg;
	struct pci_dev *pdev;

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

	mbox->prod = (void  __iomem *)(mbox->reg.bar_reg +
			RCFW_PF_VF_COMM_PROD_OFFSET);
	mbox->db = (void __iomem *)(mbox->reg.bar_reg + RCFW_COMM_TRIG_OFFSET);
	return 0;
}

static int bnxt_qplib_map_creq_db(struct bnxt_qplib_rcfw *rcfw, u32 reg_offt)
{
	struct bnxt_qplib_creq_db *creq_db;
	resource_size_t bar_reg;
	struct pci_dev *pdev;

	pdev = rcfw->pdev;
	creq_db = &rcfw->creq.creq_db;

	creq_db->dbinfo.flags = 0;
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
				   int cp_bar_reg_off,
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

	rc = bnxt_qplib_map_cmdq_mbox(rcfw);
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

	sema_init(&rcfw->rcfw_inflight, RCFW_CMD_NON_BLOCKING_SHADOW_QD);
	bnxt_qplib_start_rcfw(rcfw);

	return 0;
}
