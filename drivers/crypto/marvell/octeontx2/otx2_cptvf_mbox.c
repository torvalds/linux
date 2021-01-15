// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptvf.h"
#include <rvu_reg.h>

irqreturn_t otx2_cptvf_pfvf_mbox_intr(int __always_unused irq, void *arg)
{
	struct otx2_cptvf_dev *cptvf = arg;
	u64 intr;

	/* Read the interrupt bits */
	intr = otx2_cpt_read64(cptvf->reg_base, BLKADDR_RVUM, 0,
			       OTX2_RVU_VF_INT);

	if (intr & 0x1ULL) {
		/* Schedule work queue function to process the MBOX request */
		queue_work(cptvf->pfvf_mbox_wq, &cptvf->pfvf_mbox_work);
		/* Clear and ack the interrupt */
		otx2_cpt_write64(cptvf->reg_base, BLKADDR_RVUM, 0,
				 OTX2_RVU_VF_INT, 0x1ULL);
	}
	return IRQ_HANDLED;
}

static void process_pfvf_mbox_mbox_msg(struct otx2_cptvf_dev *cptvf,
				       struct mbox_msghdr *msg)
{
	struct otx2_cptlfs_info *lfs = &cptvf->lfs;
	struct cpt_rd_wr_reg_msg *rsp_reg;
	struct msix_offset_rsp *rsp_msix;
	int i;

	if (msg->id >= MBOX_MSG_MAX) {
		dev_err(&cptvf->pdev->dev,
			"MBOX msg with unknown ID %d\n", msg->id);
		return;
	}
	if (msg->sig != OTX2_MBOX_RSP_SIG) {
		dev_err(&cptvf->pdev->dev,
			"MBOX msg with wrong signature %x, ID %d\n",
			msg->sig, msg->id);
		return;
	}
	switch (msg->id) {
	case MBOX_MSG_READY:
		cptvf->vf_id = ((msg->pcifunc >> RVU_PFVF_FUNC_SHIFT)
				& RVU_PFVF_FUNC_MASK) - 1;
		break;
	case MBOX_MSG_ATTACH_RESOURCES:
		/* Check if resources were successfully attached */
		if (!msg->rc)
			lfs->are_lfs_attached = 1;
		break;
	case MBOX_MSG_DETACH_RESOURCES:
		/* Check if resources were successfully detached */
		if (!msg->rc)
			lfs->are_lfs_attached = 0;
		break;
	case MBOX_MSG_MSIX_OFFSET:
		rsp_msix = (struct msix_offset_rsp *) msg;
		for (i = 0; i < rsp_msix->cptlfs; i++)
			lfs->lf[i].msix_offset = rsp_msix->cptlf_msixoff[i];
		break;
	case MBOX_MSG_CPT_RD_WR_REGISTER:
		rsp_reg = (struct cpt_rd_wr_reg_msg *) msg;
		if (msg->rc) {
			dev_err(&cptvf->pdev->dev,
				"Reg %llx rd/wr(%d) failed %d\n",
				rsp_reg->reg_offset, rsp_reg->is_write,
				msg->rc);
			return;
		}
		if (!rsp_reg->is_write)
			*rsp_reg->ret_val = rsp_reg->val;
		break;
	default:
		dev_err(&cptvf->pdev->dev, "Unsupported msg %d received.\n",
			msg->id);
		break;
	}
}

void otx2_cptvf_pfvf_mbox_handler(struct work_struct *work)
{
	struct otx2_cptvf_dev *cptvf;
	struct otx2_mbox *pfvf_mbox;
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;
	int offset, i;

	/* sync with mbox memory region */
	smp_rmb();

	cptvf = container_of(work, struct otx2_cptvf_dev, pfvf_mbox_work);
	pfvf_mbox = &cptvf->pfvf_mbox;
	mdev = &pfvf_mbox->dev[0];
	rsp_hdr = (struct mbox_hdr *)(mdev->mbase + pfvf_mbox->rx_start);
	if (rsp_hdr->num_msgs == 0)
		return;
	offset = ALIGN(sizeof(struct mbox_hdr), MBOX_MSG_ALIGN);

	for (i = 0; i < rsp_hdr->num_msgs; i++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + pfvf_mbox->rx_start +
					     offset);
		process_pfvf_mbox_mbox_msg(cptvf, msg);
		offset = msg->next_msgoff;
		mdev->msgs_acked++;
	}
	otx2_mbox_reset(pfvf_mbox, 0);
}
