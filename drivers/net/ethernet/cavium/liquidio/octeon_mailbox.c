/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#include <linux/pci.h>
#include <linux/netdevice.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_main.h"
#include "octeon_mailbox.h"

/**
 * octeon_mbox_read:
 * @oct: Pointer mailbox
 *
 * Reads the 8-bytes of data from the mbox register
 * Writes back the acknowldgement inidcating completion of read
 */
int octeon_mbox_read(struct octeon_mbox *mbox)
{
	union octeon_mbox_message msg;
	int ret = 0;

	spin_lock(&mbox->lock);

	msg.u64 = readq(mbox->mbox_read_reg);

	if ((msg.u64 == OCTEON_PFVFACK) || (msg.u64 == OCTEON_PFVFSIG)) {
		spin_unlock(&mbox->lock);
		return 0;
	}

	if (mbox->state & OCTEON_MBOX_STATE_REQUEST_RECEIVING) {
		mbox->mbox_req.data[mbox->mbox_req.recv_len - 1] = msg.u64;
		mbox->mbox_req.recv_len++;
	} else {
		if (mbox->state & OCTEON_MBOX_STATE_RESPONSE_RECEIVING) {
			mbox->mbox_resp.data[mbox->mbox_resp.recv_len - 1] =
				msg.u64;
			mbox->mbox_resp.recv_len++;
		} else {
			if ((mbox->state & OCTEON_MBOX_STATE_IDLE) &&
			    (msg.s.type == OCTEON_MBOX_REQUEST)) {
				mbox->state &= ~OCTEON_MBOX_STATE_IDLE;
				mbox->state |=
				    OCTEON_MBOX_STATE_REQUEST_RECEIVING;
				mbox->mbox_req.msg.u64 = msg.u64;
				mbox->mbox_req.q_no = mbox->q_no;
				mbox->mbox_req.recv_len = 1;
			} else {
				if ((mbox->state &
				     OCTEON_MBOX_STATE_RESPONSE_PENDING) &&
				    (msg.s.type == OCTEON_MBOX_RESPONSE)) {
					mbox->state &=
					    ~OCTEON_MBOX_STATE_RESPONSE_PENDING;
					mbox->state |=
					    OCTEON_MBOX_STATE_RESPONSE_RECEIVING
					    ;
					mbox->mbox_resp.msg.u64 = msg.u64;
					mbox->mbox_resp.q_no = mbox->q_no;
					mbox->mbox_resp.recv_len = 1;
				} else {
					writeq(OCTEON_PFVFERR,
					       mbox->mbox_read_reg);
					mbox->state |= OCTEON_MBOX_STATE_ERROR;
					spin_unlock(&mbox->lock);
					return 1;
				}
			}
		}
	}

	if (mbox->state & OCTEON_MBOX_STATE_REQUEST_RECEIVING) {
		if (mbox->mbox_req.recv_len < msg.s.len) {
			ret = 0;
		} else {
			mbox->state &= ~OCTEON_MBOX_STATE_REQUEST_RECEIVING;
			mbox->state |= OCTEON_MBOX_STATE_REQUEST_RECEIVED;
			ret = 1;
		}
	} else {
		if (mbox->state & OCTEON_MBOX_STATE_RESPONSE_RECEIVING) {
			if (mbox->mbox_resp.recv_len < msg.s.len) {
				ret = 0;
			} else {
				mbox->state &=
				    ~OCTEON_MBOX_STATE_RESPONSE_RECEIVING;
				mbox->state |=
				    OCTEON_MBOX_STATE_RESPONSE_RECEIVED;
				ret = 1;
			}
		} else {
			WARN_ON(1);
		}
	}

	writeq(OCTEON_PFVFACK, mbox->mbox_read_reg);

	spin_unlock(&mbox->lock);

	return ret;
}

/**
 * octeon_mbox_write:
 * @oct: Pointer Octeon Device
 * @mbox_cmd: Cmd to send to mailbox.
 *
 * Populates the queue specific mbox structure
 * with cmd information.
 * Write the cmd to mbox register
 */
int octeon_mbox_write(struct octeon_device *oct,
		      struct octeon_mbox_cmd *mbox_cmd)
{
	struct octeon_mbox *mbox = oct->mbox[mbox_cmd->q_no];
	u32 count, i, ret = OCTEON_MBOX_STATUS_SUCCESS;
	long timeout = LIO_MBOX_WRITE_WAIT_TIME;
	unsigned long flags;

	spin_lock_irqsave(&mbox->lock, flags);

	if ((mbox_cmd->msg.s.type == OCTEON_MBOX_RESPONSE) &&
	    !(mbox->state & OCTEON_MBOX_STATE_REQUEST_RECEIVED)) {
		spin_unlock_irqrestore(&mbox->lock, flags);
		return OCTEON_MBOX_STATUS_FAILED;
	}

	if ((mbox_cmd->msg.s.type == OCTEON_MBOX_REQUEST) &&
	    !(mbox->state & OCTEON_MBOX_STATE_IDLE)) {
		spin_unlock_irqrestore(&mbox->lock, flags);
		return OCTEON_MBOX_STATUS_BUSY;
	}

	if (mbox_cmd->msg.s.type == OCTEON_MBOX_REQUEST) {
		memcpy(&mbox->mbox_resp, mbox_cmd,
		       sizeof(struct octeon_mbox_cmd));
		mbox->state = OCTEON_MBOX_STATE_RESPONSE_PENDING;
	}

	spin_unlock_irqrestore(&mbox->lock, flags);

	count = 0;

	while (readq(mbox->mbox_write_reg) != OCTEON_PFVFSIG) {
		schedule_timeout_uninterruptible(timeout);
		if (count++ == LIO_MBOX_WRITE_WAIT_CNT) {
			ret = OCTEON_MBOX_STATUS_FAILED;
			break;
		}
	}

	if (ret == OCTEON_MBOX_STATUS_SUCCESS) {
		writeq(mbox_cmd->msg.u64, mbox->mbox_write_reg);
		for (i = 0; i < (u32)(mbox_cmd->msg.s.len - 1); i++) {
			count = 0;
			while (readq(mbox->mbox_write_reg) !=
			       OCTEON_PFVFACK) {
				schedule_timeout_uninterruptible(timeout);
				if (count++ == LIO_MBOX_WRITE_WAIT_CNT) {
					ret = OCTEON_MBOX_STATUS_FAILED;
					break;
				}
			}
			writeq(mbox_cmd->data[i], mbox->mbox_write_reg);
		}
	}

	spin_lock_irqsave(&mbox->lock, flags);
	if (mbox_cmd->msg.s.type == OCTEON_MBOX_RESPONSE) {
		mbox->state = OCTEON_MBOX_STATE_IDLE;
		writeq(OCTEON_PFVFSIG, mbox->mbox_read_reg);
	} else {
		if ((!mbox_cmd->msg.s.resp_needed) ||
		    (ret == OCTEON_MBOX_STATUS_FAILED)) {
			mbox->state &= ~OCTEON_MBOX_STATE_RESPONSE_PENDING;
			if (!(mbox->state &
			      (OCTEON_MBOX_STATE_REQUEST_RECEIVING |
			       OCTEON_MBOX_STATE_REQUEST_RECEIVED)))
				mbox->state = OCTEON_MBOX_STATE_IDLE;
		}
	}
	spin_unlock_irqrestore(&mbox->lock, flags);

	return ret;
}

/**
 * octeon_mbox_process_cmd:
 * @mbox: Pointer mailbox
 * @mbox_cmd: Pointer to command received
 *
 * Process the cmd received in mbox
 */
static int octeon_mbox_process_cmd(struct octeon_mbox *mbox,
				   struct octeon_mbox_cmd *mbox_cmd)
{
	struct octeon_device *oct = mbox->oct_dev;

	switch (mbox_cmd->msg.s.cmd) {
	case OCTEON_VF_ACTIVE:
		dev_dbg(&oct->pci_dev->dev, "got vfactive sending data back\n");
		mbox_cmd->msg.s.type = OCTEON_MBOX_RESPONSE;
		mbox_cmd->msg.s.resp_needed = 1;
		mbox_cmd->msg.s.len = 2;
		mbox_cmd->data[0] = 0; /* VF version is in mbox_cmd->data[0] */
		((struct lio_version *)&mbox_cmd->data[0])->major =
			LIQUIDIO_BASE_MAJOR_VERSION;
		((struct lio_version *)&mbox_cmd->data[0])->minor =
			LIQUIDIO_BASE_MINOR_VERSION;
		((struct lio_version *)&mbox_cmd->data[0])->micro =
			LIQUIDIO_BASE_MICRO_VERSION;
		memcpy(mbox_cmd->msg.s.params, (uint8_t *)&oct->pfvf_hsword, 6);
		/* Sending core cofig info to the corresponding active VF.*/
		octeon_mbox_write(oct, mbox_cmd);
		break;

	case OCTEON_VF_FLR_REQUEST:
		dev_info(&oct->pci_dev->dev,
			 "got a request for FLR from VF that owns DPI ring %u\n",
			 mbox->q_no);
		pcie_capability_set_word(
			oct->sriov_info.dpiring_to_vfpcidev_lut[mbox->q_no],
			PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_BCR_FLR);
		break;

	case OCTEON_PF_CHANGED_VF_MACADDR:
		if (OCTEON_CN23XX_VF(oct))
			octeon_pf_changed_vf_macaddr(oct,
						     mbox_cmd->msg.s.params);
		break;

	default:
		break;
	}
	return 0;
}

/**
 *octeon_mbox_process_message:
 *
 * Process the received mbox message.
 */
int octeon_mbox_process_message(struct octeon_mbox *mbox)
{
	struct octeon_mbox_cmd mbox_cmd;
	unsigned long flags;

	spin_lock_irqsave(&mbox->lock, flags);

	if (mbox->state & OCTEON_MBOX_STATE_ERROR) {
		if (mbox->state & (OCTEON_MBOX_STATE_RESPONSE_PENDING |
				   OCTEON_MBOX_STATE_RESPONSE_RECEIVING)) {
			memcpy(&mbox_cmd, &mbox->mbox_resp,
			       sizeof(struct octeon_mbox_cmd));
			mbox->state = OCTEON_MBOX_STATE_IDLE;
			writeq(OCTEON_PFVFSIG, mbox->mbox_read_reg);
			spin_unlock_irqrestore(&mbox->lock, flags);
			mbox_cmd.recv_status = 1;
			if (mbox_cmd.fn)
				mbox_cmd.fn(mbox->oct_dev, &mbox_cmd,
					    mbox_cmd.fn_arg);
			return 0;
		}

		mbox->state = OCTEON_MBOX_STATE_IDLE;
		writeq(OCTEON_PFVFSIG, mbox->mbox_read_reg);
		spin_unlock_irqrestore(&mbox->lock, flags);
		return 0;
	}

	if (mbox->state & OCTEON_MBOX_STATE_RESPONSE_RECEIVED) {
		memcpy(&mbox_cmd, &mbox->mbox_resp,
		       sizeof(struct octeon_mbox_cmd));
		mbox->state = OCTEON_MBOX_STATE_IDLE;
		writeq(OCTEON_PFVFSIG, mbox->mbox_read_reg);
		spin_unlock_irqrestore(&mbox->lock, flags);
		mbox_cmd.recv_status = 0;
		if (mbox_cmd.fn)
			mbox_cmd.fn(mbox->oct_dev, &mbox_cmd, mbox_cmd.fn_arg);
		return 0;
	}

	if (mbox->state & OCTEON_MBOX_STATE_REQUEST_RECEIVED) {
		memcpy(&mbox_cmd, &mbox->mbox_req,
		       sizeof(struct octeon_mbox_cmd));
		if (!mbox_cmd.msg.s.resp_needed) {
			mbox->state &= ~OCTEON_MBOX_STATE_REQUEST_RECEIVED;
			if (!(mbox->state &
			      OCTEON_MBOX_STATE_RESPONSE_PENDING))
				mbox->state = OCTEON_MBOX_STATE_IDLE;
			writeq(OCTEON_PFVFSIG, mbox->mbox_read_reg);
		}

		spin_unlock_irqrestore(&mbox->lock, flags);
		octeon_mbox_process_cmd(mbox, &mbox_cmd);
		return 0;
	}

	spin_unlock_irqrestore(&mbox->lock, flags);
	WARN_ON(1);

	return 0;
}
