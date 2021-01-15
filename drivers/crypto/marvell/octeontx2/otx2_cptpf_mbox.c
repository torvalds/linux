// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"
#include "rvu_reg.h"

irqreturn_t otx2_cptpf_afpf_mbox_intr(int __always_unused irq, void *arg)
{
	struct otx2_cptpf_dev *cptpf = arg;
	u64 intr;

	/* Read the interrupt bits */
	intr = otx2_cpt_read64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT);

	if (intr & 0x1ULL) {
		/* Schedule work queue function to process the MBOX request */
		queue_work(cptpf->afpf_mbox_wq, &cptpf->afpf_mbox_work);
		/* Clear and ack the interrupt */
		otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT,
				 0x1ULL);
	}
	return IRQ_HANDLED;
}

static void process_afpf_mbox_msg(struct otx2_cptpf_dev *cptpf,
				  struct mbox_msghdr *msg)
{
	struct device *dev = &cptpf->pdev->dev;

	if (msg->id >= MBOX_MSG_MAX) {
		dev_err(dev, "MBOX msg with unknown ID %d\n", msg->id);
		return;
	}
	if (msg->sig != OTX2_MBOX_RSP_SIG) {
		dev_err(dev, "MBOX msg with wrong signature %x, ID %d\n",
			msg->sig, msg->id);
		return;
	}

	switch (msg->id) {
	case MBOX_MSG_READY:
		cptpf->pf_id = (msg->pcifunc >> RVU_PFVF_PF_SHIFT) &
				RVU_PFVF_PF_MASK;
		break;
	default:
		dev_err(dev,
			"Unsupported msg %d received.\n", msg->id);
		break;
	}
}

/* Handle mailbox messages received from AF */
void otx2_cptpf_afpf_mbox_handler(struct work_struct *work)
{
	struct otx2_cptpf_dev *cptpf;
	struct otx2_mbox *afpf_mbox;
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;
	int offset, i;

	cptpf = container_of(work, struct otx2_cptpf_dev, afpf_mbox_work);
	afpf_mbox = &cptpf->afpf_mbox;
	mdev = &afpf_mbox->dev[0];
	/* Sync mbox data into memory */
	smp_wmb();

	rsp_hdr = (struct mbox_hdr *)(mdev->mbase + afpf_mbox->rx_start);
	offset = ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);

	for (i = 0; i < rsp_hdr->num_msgs; i++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + afpf_mbox->rx_start +
					     offset);
		process_afpf_mbox_msg(cptpf, msg);
		offset = msg->next_msgoff;
		mdev->msgs_acked++;
	}
	otx2_mbox_reset(afpf_mbox, 0);
}
