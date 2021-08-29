/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2020 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"

void bnxt_hwrm_cmd_hdr_init(struct bnxt *bp, void *request, u16 req_type,
			    u16 cmpl_ring, u16 target_id)
{
	struct input *req = request;

	req->req_type = cpu_to_le16(req_type);
	req->cmpl_ring = cpu_to_le16(cmpl_ring);
	req->target_id = cpu_to_le16(target_id);
	req->resp_addr = cpu_to_le64(bp->hwrm_cmd_resp_dma_addr);
}

static int bnxt_hwrm_to_stderr(u32 hwrm_err)
{
	switch (hwrm_err) {
	case HWRM_ERR_CODE_SUCCESS:
		return 0;
	case HWRM_ERR_CODE_RESOURCE_LOCKED:
		return -EROFS;
	case HWRM_ERR_CODE_RESOURCE_ACCESS_DENIED:
		return -EACCES;
	case HWRM_ERR_CODE_RESOURCE_ALLOC_ERROR:
		return -ENOSPC;
	case HWRM_ERR_CODE_INVALID_PARAMS:
	case HWRM_ERR_CODE_INVALID_FLAGS:
	case HWRM_ERR_CODE_INVALID_ENABLES:
	case HWRM_ERR_CODE_UNSUPPORTED_TLV:
	case HWRM_ERR_CODE_UNSUPPORTED_OPTION_ERR:
		return -EINVAL;
	case HWRM_ERR_CODE_NO_BUFFER:
		return -ENOMEM;
	case HWRM_ERR_CODE_HOT_RESET_PROGRESS:
	case HWRM_ERR_CODE_BUSY:
		return -EAGAIN;
	case HWRM_ERR_CODE_CMD_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

static int bnxt_hwrm_do_send_msg(struct bnxt *bp, void *msg, u32 msg_len,
				 int timeout, bool silent)
{
	int i, intr_process, rc, tmo_count;
	struct input *req = msg;
	u32 *data = msg;
	u8 *valid;
	u16 cp_ring_id, len = 0;
	struct hwrm_err_output *resp = bp->hwrm_cmd_resp_addr;
	u16 max_req_len = BNXT_HWRM_MAX_REQ_LEN;
	struct hwrm_short_input short_input = {0};
	u32 doorbell_offset = BNXT_GRCPF_REG_CHIMP_COMM_TRIGGER;
	u32 bar_offset = BNXT_GRCPF_REG_CHIMP_COMM;
	u16 dst = BNXT_HWRM_CHNL_CHIMP;

	if (BNXT_NO_FW_ACCESS(bp) &&
	    le16_to_cpu(req->req_type) != HWRM_FUNC_RESET)
		return -EBUSY;

	if (msg_len > BNXT_HWRM_MAX_REQ_LEN) {
		if (msg_len > bp->hwrm_max_ext_req_len ||
		    !bp->hwrm_short_cmd_req_addr)
			return -EINVAL;
	}

	if (bnxt_kong_hwrm_message(bp, req)) {
		dst = BNXT_HWRM_CHNL_KONG;
		bar_offset = BNXT_GRCPF_REG_KONG_COMM;
		doorbell_offset = BNXT_GRCPF_REG_KONG_COMM_TRIGGER;
	}

	memset(resp, 0, PAGE_SIZE);
	cp_ring_id = le16_to_cpu(req->cmpl_ring);
	intr_process = (cp_ring_id == INVALID_HW_RING_ID) ? 0 : 1;

	req->seq_id = cpu_to_le16(bnxt_get_hwrm_seq_id(bp, dst));
	/* currently supports only one outstanding message */
	if (intr_process)
		bp->hwrm_intr_seq_id = le16_to_cpu(req->seq_id);

	if ((bp->fw_cap & BNXT_FW_CAP_SHORT_CMD) ||
	    msg_len > BNXT_HWRM_MAX_REQ_LEN) {
		void *short_cmd_req = bp->hwrm_short_cmd_req_addr;
		u16 max_msg_len;

		/* Set boundary for maximum extended request length for short
		 * cmd format. If passed up from device use the max supported
		 * internal req length.
		 */
		max_msg_len = bp->hwrm_max_ext_req_len;

		memcpy(short_cmd_req, req, msg_len);
		if (msg_len < max_msg_len)
			memset(short_cmd_req + msg_len, 0,
			       max_msg_len - msg_len);

		short_input.req_type = req->req_type;
		short_input.signature =
				cpu_to_le16(SHORT_REQ_SIGNATURE_SHORT_CMD);
		short_input.size = cpu_to_le16(msg_len);
		short_input.req_addr =
			cpu_to_le64(bp->hwrm_short_cmd_req_dma_addr);

		data = (u32 *)&short_input;
		msg_len = sizeof(short_input);

		/* Sync memory write before updating doorbell */
		wmb();

		max_req_len = BNXT_HWRM_SHORT_REQ_LEN;
	}

	/* Write request msg to hwrm channel */
	__iowrite32_copy(bp->bar0 + bar_offset, data, msg_len / 4);

	for (i = msg_len; i < max_req_len; i += 4)
		writel(0, bp->bar0 + bar_offset + i);

	/* Ring channel doorbell */
	writel(1, bp->bar0 + doorbell_offset);

	if (!pci_is_enabled(bp->pdev))
		return -ENODEV;

	if (!timeout)
		timeout = DFLT_HWRM_CMD_TIMEOUT;
	/* Limit timeout to an upper limit */
	timeout = min(timeout, HWRM_CMD_MAX_TIMEOUT);
	/* convert timeout to usec */
	timeout *= 1000;

	i = 0;
	/* Short timeout for the first few iterations:
	 * number of loops = number of loops for short timeout +
	 * number of loops for standard timeout.
	 */
	tmo_count = HWRM_SHORT_TIMEOUT_COUNTER;
	timeout = timeout - HWRM_SHORT_MIN_TIMEOUT * HWRM_SHORT_TIMEOUT_COUNTER;
	tmo_count += DIV_ROUND_UP(timeout, HWRM_MIN_TIMEOUT);

	if (intr_process) {
		u16 seq_id = bp->hwrm_intr_seq_id;

		/* Wait until hwrm response cmpl interrupt is processed */
		while (bp->hwrm_intr_seq_id != (u16)~seq_id &&
		       i++ < tmo_count) {
			/* Abort the wait for completion if the FW health
			 * check has failed.
			 */
			if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
				return -EBUSY;
			/* on first few passes, just barely sleep */
			if (i < HWRM_SHORT_TIMEOUT_COUNTER) {
				usleep_range(HWRM_SHORT_MIN_TIMEOUT,
					     HWRM_SHORT_MAX_TIMEOUT);
			} else {
				if (HWRM_WAIT_MUST_ABORT(bp, req))
					break;
				usleep_range(HWRM_MIN_TIMEOUT,
					     HWRM_MAX_TIMEOUT);
			}
		}

		if (bp->hwrm_intr_seq_id != (u16)~seq_id) {
			if (!silent)
				netdev_err(bp->dev, "Resp cmpl intr err msg: 0x%x\n",
					   le16_to_cpu(req->req_type));
			return -EBUSY;
		}
		len = le16_to_cpu(resp->resp_len);
		valid = ((u8 *)resp) + len - 1;
	} else {
		int j;

		/* Check if response len is updated */
		for (i = 0; i < tmo_count; i++) {
			/* Abort the wait for completion if the FW health
			 * check has failed.
			 */
			if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
				return -EBUSY;
			len = le16_to_cpu(resp->resp_len);
			if (len)
				break;
			/* on first few passes, just barely sleep */
			if (i < HWRM_SHORT_TIMEOUT_COUNTER) {
				usleep_range(HWRM_SHORT_MIN_TIMEOUT,
					     HWRM_SHORT_MAX_TIMEOUT);
			} else {
				if (HWRM_WAIT_MUST_ABORT(bp, req))
					goto timeout_abort;
				usleep_range(HWRM_MIN_TIMEOUT,
					     HWRM_MAX_TIMEOUT);
			}
		}

		if (i >= tmo_count) {
timeout_abort:
			if (!silent)
				netdev_err(bp->dev, "Error (timeout: %d) msg {0x%x 0x%x} len:%d\n",
					   HWRM_TOTAL_TIMEOUT(i),
					   le16_to_cpu(req->req_type),
					   le16_to_cpu(req->seq_id), len);
			return -EBUSY;
		}

		/* Last byte of resp contains valid bit */
		valid = ((u8 *)resp) + len - 1;
		for (j = 0; j < HWRM_VALID_BIT_DELAY_USEC; j++) {
			/* make sure we read from updated DMA memory */
			dma_rmb();
			if (*valid)
				break;
			usleep_range(1, 5);
		}

		if (j >= HWRM_VALID_BIT_DELAY_USEC) {
			if (!silent)
				netdev_err(bp->dev, "Error (timeout: %d) msg {0x%x 0x%x} len:%d v:%d\n",
					   HWRM_TOTAL_TIMEOUT(i),
					   le16_to_cpu(req->req_type),
					   le16_to_cpu(req->seq_id), len,
					   *valid);
			return -EBUSY;
		}
	}

	/* Zero valid bit for compatibility.  Valid bit in an older spec
	 * may become a new field in a newer spec.  We must make sure that
	 * a new field not implemented by old spec will read zero.
	 */
	*valid = 0;
	rc = le16_to_cpu(resp->error_code);
	if (rc && !silent)
		netdev_err(bp->dev, "hwrm req_type 0x%x seq id 0x%x error 0x%x\n",
			   le16_to_cpu(resp->req_type),
			   le16_to_cpu(resp->seq_id), rc);
	return bnxt_hwrm_to_stderr(rc);
}

int _hwrm_send_message(struct bnxt *bp, void *msg, u32 msg_len, int timeout)
{
	return bnxt_hwrm_do_send_msg(bp, msg, msg_len, timeout, false);
}

int _hwrm_send_message_silent(struct bnxt *bp, void *msg, u32 msg_len,
			      int timeout)
{
	return bnxt_hwrm_do_send_msg(bp, msg, msg_len, timeout, true);
}

int hwrm_send_message(struct bnxt *bp, void *msg, u32 msg_len, int timeout)
{
	int rc;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, msg, msg_len, timeout);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

int hwrm_send_message_silent(struct bnxt *bp, void *msg, u32 msg_len,
			     int timeout)
{
	int rc;

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = bnxt_hwrm_do_send_msg(bp, msg, msg_len, timeout, true);
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}
