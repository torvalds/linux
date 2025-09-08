// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptvf.h"
#include <rvu_reg.h>

int otx2_cpt_mbox_bbuf_init(struct otx2_cptvf_dev *cptvf, struct pci_dev *pdev)
{
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *otx2_mbox;

	cptvf->bbuf_base = devm_kmalloc(&pdev->dev, MBOX_SIZE, GFP_KERNEL);
	if (!cptvf->bbuf_base)
		return -ENOMEM;
	/*
	 * Overwrite mbox mbase to point to bounce buffer, so that PF/VF
	 * prepare all mbox messages in bounce buffer instead of directly
	 * in hw mbox memory.
	 */
	otx2_mbox = &cptvf->pfvf_mbox;
	mdev = &otx2_mbox->dev[0];
	mdev->mbase = cptvf->bbuf_base;

	return 0;
}

static void otx2_cpt_sync_mbox_bbuf(struct otx2_mbox *mbox, int devid)
{
	u16 msgs_offset = ALIGN(sizeof(struct mbox_hdr), MBOX_MSG_ALIGN);
	void *hw_mbase = mbox->hwbase + (devid * MBOX_SIZE);
	struct otx2_mbox_dev *mdev = &mbox->dev[devid];
	struct mbox_hdr *hdr;
	u64 msg_size;

	if (mdev->mbase == hw_mbase)
		return;

	hdr = hw_mbase + mbox->rx_start;
	msg_size = hdr->msg_size;

	if (msg_size > mbox->rx_size - msgs_offset)
		msg_size = mbox->rx_size - msgs_offset;

	/* Copy mbox messages from mbox memory to bounce buffer */
	memcpy(mdev->mbase + mbox->rx_start,
	       hw_mbase + mbox->rx_start, msg_size + msgs_offset);
}

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
	struct otx2_cpt_kvf_limits_rsp *rsp_limits;
	struct otx2_cpt_egrp_num_rsp *rsp_grp;
	struct otx2_cpt_caps_rsp *eng_caps;
	struct cpt_rd_wr_reg_msg *rsp_reg;
	struct msix_offset_rsp *rsp_msix;
	u8 grp_num;
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
	case MBOX_MSG_GET_ENG_GRP_NUM:
		rsp_grp = (struct otx2_cpt_egrp_num_rsp *) msg;
		grp_num = rsp_grp->eng_grp_num;
		if (rsp_grp->eng_type == OTX2_CPT_SE_TYPES)
			cptvf->lfs.kcrypto_se_eng_grp_num = grp_num;
		else if (rsp_grp->eng_type == OTX2_CPT_AE_TYPES)
			cptvf->lfs.kcrypto_ae_eng_grp_num = grp_num;
		break;
	case MBOX_MSG_GET_KVF_LIMITS:
		rsp_limits = (struct otx2_cpt_kvf_limits_rsp *) msg;
		cptvf->lfs.kvf_limits = rsp_limits->kvf_limits;
		break;
	case MBOX_MSG_GET_CAPS:
		eng_caps = (struct otx2_cpt_caps_rsp *)msg;
		memcpy(cptvf->eng_caps, eng_caps->eng_caps,
		       sizeof(cptvf->eng_caps));
		break;
	case MBOX_MSG_CPT_LF_RESET:
	case MBOX_MSG_LMTST_TBL_SETUP:
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
	otx2_cpt_sync_mbox_bbuf(pfvf_mbox, 0);
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

int otx2_cptvf_send_eng_grp_num_msg(struct otx2_cptvf_dev *cptvf, int eng_type)
{
	struct otx2_mbox *mbox = &cptvf->pfvf_mbox;
	struct pci_dev *pdev = cptvf->pdev;
	struct otx2_cpt_egrp_num_msg *req;

	req = (struct otx2_cpt_egrp_num_msg *)
	      otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*req),
				      sizeof(struct otx2_cpt_egrp_num_rsp));
	if (req == NULL) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}
	req->hdr.id = MBOX_MSG_GET_ENG_GRP_NUM;
	req->hdr.sig = OTX2_MBOX_REQ_SIG;
	req->hdr.pcifunc = OTX2_CPT_RVU_PFFUNC(cptvf->pdev, cptvf->vf_id, 0);
	req->eng_type = eng_type;

	return otx2_cpt_send_mbox_msg(mbox, pdev);
}

int otx2_cptvf_send_kvf_limits_msg(struct otx2_cptvf_dev *cptvf)
{
	struct otx2_mbox *mbox = &cptvf->pfvf_mbox;
	struct pci_dev *pdev = cptvf->pdev;
	struct mbox_msghdr *req;

	req = (struct mbox_msghdr *)
	      otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*req),
				      sizeof(struct otx2_cpt_kvf_limits_rsp));
	if (req == NULL) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}
	req->id = MBOX_MSG_GET_KVF_LIMITS;
	req->sig = OTX2_MBOX_REQ_SIG;
	req->pcifunc = OTX2_CPT_RVU_PFFUNC(cptvf->pdev, cptvf->vf_id, 0);

	return otx2_cpt_send_mbox_msg(mbox, pdev);
}

int otx2_cptvf_send_caps_msg(struct otx2_cptvf_dev *cptvf)
{
	struct otx2_mbox *mbox = &cptvf->pfvf_mbox;
	struct pci_dev *pdev = cptvf->pdev;
	struct mbox_msghdr *req;

	req = (struct mbox_msghdr *)
	      otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*req),
				      sizeof(struct otx2_cpt_caps_rsp));
	if (!req) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}
	req->id = MBOX_MSG_GET_CAPS;
	req->sig = OTX2_MBOX_REQ_SIG;
	req->pcifunc = OTX2_CPT_RVU_PFFUNC(cptvf->pdev, cptvf->vf_id, 0);

	return otx2_cpt_send_mbox_msg(mbox, pdev);
}
