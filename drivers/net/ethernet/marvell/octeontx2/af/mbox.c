// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#include "rvu_reg.h"
#include "mbox.h"

static const u16 msgs_offset = ALIGN(sizeof(struct mbox_hdr), MBOX_MSG_ALIGN);

void otx2_mbox_reset(struct otx2_mbox *mbox, int devid)
{
	void *hw_mbase = mbox->hwbase + (devid * MBOX_SIZE);
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	struct mbox_hdr *tx_hdr, *rx_hdr;

	tx_hdr = hw_mbase + mbox->tx_start;
	rx_hdr = hw_mbase + mbox->rx_start;

	spin_lock(&mdev->mbox_lock);
	mdev->msg_size = 0;
	mdev->rsp_size = 0;
	tx_hdr->num_msgs = 0;
	tx_hdr->msg_size = 0;
	rx_hdr->num_msgs = 0;
	rx_hdr->msg_size = 0;
	spin_unlock(&mdev->mbox_lock);
}
EXPORT_SYMBOL(otx2_mbox_reset);

void otx2_mbox_destroy(struct otx2_mbox *mbox)
{
	mbox->reg_base = NULL;
	mbox->hwbase = NULL;

	kfree(mbox->dev);
	mbox->dev = NULL;
}
EXPORT_SYMBOL(otx2_mbox_destroy);

int otx2_mbox_init(struct otx2_mbox *mbox, void *hwbase, struct pci_dev *pdev,
		   void *reg_base, int direction, int ndevs)
{
	struct otx2_mbox_dev *mdev;
	int devid;

	switch (direction) {
	case MBOX_DIR_AFPF:
	case MBOX_DIR_PFVF:
		mbox->tx_start = MBOX_DOWN_TX_START;
		mbox->rx_start = MBOX_DOWN_RX_START;
		mbox->tx_size  = MBOX_DOWN_TX_SIZE;
		mbox->rx_size  = MBOX_DOWN_RX_SIZE;
		break;
	case MBOX_DIR_PFAF:
	case MBOX_DIR_VFPF:
		mbox->tx_start = MBOX_DOWN_RX_START;
		mbox->rx_start = MBOX_DOWN_TX_START;
		mbox->tx_size  = MBOX_DOWN_RX_SIZE;
		mbox->rx_size  = MBOX_DOWN_TX_SIZE;
		break;
	case MBOX_DIR_AFPF_UP:
	case MBOX_DIR_PFVF_UP:
		mbox->tx_start = MBOX_UP_TX_START;
		mbox->rx_start = MBOX_UP_RX_START;
		mbox->tx_size  = MBOX_UP_TX_SIZE;
		mbox->rx_size  = MBOX_UP_RX_SIZE;
		break;
	case MBOX_DIR_PFAF_UP:
	case MBOX_DIR_VFPF_UP:
		mbox->tx_start = MBOX_UP_RX_START;
		mbox->rx_start = MBOX_UP_TX_START;
		mbox->tx_size  = MBOX_UP_RX_SIZE;
		mbox->rx_size  = MBOX_UP_TX_SIZE;
		break;
	default:
		return -ENODEV;
	}

	switch (direction) {
	case MBOX_DIR_AFPF:
	case MBOX_DIR_AFPF_UP:
		mbox->trigger = RVU_AF_AFPF_MBOX0;
		mbox->tr_shift = 4;
		break;
	case MBOX_DIR_PFAF:
	case MBOX_DIR_PFAF_UP:
		mbox->trigger = RVU_PF_PFAF_MBOX1;
		mbox->tr_shift = 0;
		break;
	case MBOX_DIR_PFVF:
	case MBOX_DIR_PFVF_UP:
		mbox->trigger = RVU_PF_VFX_PFVF_MBOX0;
		mbox->tr_shift = 12;
		break;
	case MBOX_DIR_VFPF:
	case MBOX_DIR_VFPF_UP:
		mbox->trigger = RVU_VF_VFPF_MBOX1;
		mbox->tr_shift = 0;
		break;
	default:
		return -ENODEV;
	}

	mbox->reg_base = reg_base;
	mbox->hwbase = hwbase;
	mbox->pdev = pdev;

	mbox->dev = kcalloc(ndevs, sizeof(struct otx2_mbox_dev), GFP_KERNEL);
	if (!mbox->dev) {
		otx2_mbox_destroy(mbox);
		return -ENOMEM;
	}

	mbox->ndevs = ndevs;
	for (devid = 0; devid < ndevs; devid++) {
		mdev = &mbox->dev[devid];
		mdev->mbase = mbox->hwbase + (devid * MBOX_SIZE);
		spin_lock_init(&mdev->mbox_lock);
		/* Init header to reset value */
		otx2_mbox_reset(mbox, devid);
	}

	return 0;
}
EXPORT_SYMBOL(otx2_mbox_init);

int otx2_mbox_wait_for_rsp(struct otx2_mbox *mbox, int devid)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(MBOX_RSP_TIMEOUT);
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	struct device *sender = &mbox->pdev->dev;

	while (!time_after(jiffies, timeout)) {
		if (mdev->num_msgs == mdev->msgs_acked)
			return 0;
		usleep_range(800, 1000);
	}
	dev_dbg(sender, "timed out while waiting for rsp\n");
	return -EIO;
}
EXPORT_SYMBOL(otx2_mbox_wait_for_rsp);

int otx2_mbox_busy_poll_for_rsp(struct otx2_mbox *mbox, int devid)
{
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	unsigned long timeout = jiffies + 1 * HZ;

	while (!time_after(jiffies, timeout)) {
		if (mdev->num_msgs == mdev->msgs_acked)
			return 0;
		cpu_relax();
	}
	return -EIO;
}
EXPORT_SYMBOL(otx2_mbox_busy_poll_for_rsp);

void otx2_mbox_msg_send(struct otx2_mbox *mbox, int devid)
{
	void *hw_mbase = mbox->hwbase + (devid * MBOX_SIZE);
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	struct mbox_hdr *tx_hdr, *rx_hdr;

	tx_hdr = hw_mbase + mbox->tx_start;
	rx_hdr = hw_mbase + mbox->rx_start;

	/* If bounce buffer is implemented copy mbox messages from
	 * bounce buffer to hw mbox memory.
	 */
	if (mdev->mbase != hw_mbase)
		memcpy(hw_mbase + mbox->tx_start + msgs_offset,
		       mdev->mbase + mbox->tx_start + msgs_offset,
		       mdev->msg_size);

	spin_lock(&mdev->mbox_lock);

	tx_hdr->msg_size = mdev->msg_size;

	/* Reset header for next messages */
	mdev->msg_size = 0;
	mdev->rsp_size = 0;
	mdev->msgs_acked = 0;

	/* Sync mbox data into memory */
	smp_wmb();

	/* num_msgs != 0 signals to the peer that the buffer has a number of
	 * messages.  So this should be written after writing all the messages
	 * to the shared memory.
	 */
	tx_hdr->num_msgs = mdev->num_msgs;
	rx_hdr->num_msgs = 0;
	spin_unlock(&mdev->mbox_lock);

	/* The interrupt should be fired after num_msgs is written
	 * to the shared memory
	 */
	writeq(1, (void __iomem *)mbox->reg_base +
	       (mbox->trigger | (devid << mbox->tr_shift)));
}
EXPORT_SYMBOL(otx2_mbox_msg_send);

struct mbox_msghdr *otx2_mbox_alloc_msg_rsp(struct otx2_mbox *mbox, int devid,
					    int size, int size_rsp)
{
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	struct mbox_msghdr *msghdr = NULL;

	spin_lock(&mdev->mbox_lock);
	size = ALIGN(size, MBOX_MSG_ALIGN);
	size_rsp = ALIGN(size_rsp, MBOX_MSG_ALIGN);
	/* Check if there is space in mailbox */
	if ((mdev->msg_size + size) > mbox->tx_size - msgs_offset)
		goto exit;
	if ((mdev->rsp_size + size_rsp) > mbox->rx_size - msgs_offset)
		goto exit;

	if (mdev->msg_size == 0)
		mdev->num_msgs = 0;
	mdev->num_msgs++;

	msghdr = mdev->mbase + mbox->tx_start + msgs_offset + mdev->msg_size;

	/* Clear the whole msg region */
	memset(msghdr, 0, size);
	/* Init message header with reset values */
	msghdr->ver = OTX2_MBOX_VERSION;
	mdev->msg_size += size;
	mdev->rsp_size += size_rsp;
	msghdr->next_msgoff = mdev->msg_size + msgs_offset;
exit:
	spin_unlock(&mdev->mbox_lock);

	return msghdr;
}
EXPORT_SYMBOL(otx2_mbox_alloc_msg_rsp);

struct mbox_msghdr *otx2_mbox_get_rsp(struct otx2_mbox *mbox, int devid,
				      struct mbox_msghdr *msg)
{
	unsigned long imsg = mbox->tx_start + msgs_offset;
	unsigned long irsp = mbox->rx_start + msgs_offset;
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	u16 msgs;

	spin_lock(&mdev->mbox_lock);

	if (mdev->num_msgs != mdev->msgs_acked)
		goto error;

	for (msgs = 0; msgs < mdev->msgs_acked; msgs++) {
		struct mbox_msghdr *pmsg = mdev->mbase + imsg;
		struct mbox_msghdr *prsp = mdev->mbase + irsp;

		if (msg == pmsg) {
			if (pmsg->id != prsp->id)
				goto error;
			spin_unlock(&mdev->mbox_lock);
			return prsp;
		}

		imsg = mbox->tx_start + pmsg->next_msgoff;
		irsp = mbox->rx_start + prsp->next_msgoff;
	}

error:
	spin_unlock(&mdev->mbox_lock);
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL(otx2_mbox_get_rsp);

int otx2_mbox_check_rsp_msgs(struct otx2_mbox *mbox, int devid)
{
	unsigned long ireq = mbox->tx_start + msgs_offset;
	unsigned long irsp = mbox->rx_start + msgs_offset;
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	int rc = -ENODEV;
	u16 msgs;

	spin_lock(&mdev->mbox_lock);

	if (mdev->num_msgs != mdev->msgs_acked)
		goto exit;

	for (msgs = 0; msgs < mdev->msgs_acked; msgs++) {
		struct mbox_msghdr *preq = mdev->mbase + ireq;
		struct mbox_msghdr *prsp = mdev->mbase + irsp;

		if (preq->id != prsp->id)
			goto exit;
		if (prsp->rc) {
			rc = prsp->rc;
			goto exit;
		}

		ireq = mbox->tx_start + preq->next_msgoff;
		irsp = mbox->rx_start + prsp->next_msgoff;
	}
	rc = 0;
exit:
	spin_unlock(&mdev->mbox_lock);
	return rc;
}
EXPORT_SYMBOL(otx2_mbox_check_rsp_msgs);

int
otx2_reply_invalid_msg(struct otx2_mbox *mbox, int devid, u16 pcifunc, u16 id)
{
	struct msg_rsp *rsp;

	rsp = (struct msg_rsp *)
	       otx2_mbox_alloc_msg(mbox, devid, sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;
	rsp->hdr.id = id;
	rsp->hdr.sig = OTX2_MBOX_RSP_SIG;
	rsp->hdr.rc = MBOX_MSG_INVALID;
	rsp->hdr.pcifunc = pcifunc;
	return 0;
}
EXPORT_SYMBOL(otx2_reply_invalid_msg);

bool otx2_mbox_nonempty(struct otx2_mbox *mbox, int devid)
{
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	bool ret;

	spin_lock(&mdev->mbox_lock);
	ret = mdev->num_msgs != 0;
	spin_unlock(&mdev->mbox_lock);

	return ret;
}
EXPORT_SYMBOL(otx2_mbox_nonempty);

const char *otx2_mbox_id2name(u16 id)
{
	switch (id) {
#define M(_name, _id, _1, _2, _3) case _id: return # _name;
	MBOX_MESSAGES
#undef M
	default:
		return "INVALID ID";
	}
}
EXPORT_SYMBOL(otx2_mbox_id2name);

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL v2");
