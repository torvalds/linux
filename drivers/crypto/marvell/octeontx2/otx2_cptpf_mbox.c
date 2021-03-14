// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"
#include "rvu_reg.h"

/*
 * CPT PF driver version, It will be incremented by 1 for every feature
 * addition in CPT mailbox messages.
 */
#define OTX2_CPT_PF_DRV_VERSION 0x1

static int forward_to_af(struct otx2_cptpf_dev *cptpf,
			 struct otx2_cptvf_info *vf,
			 struct mbox_msghdr *req, int size)
{
	struct mbox_msghdr *msg;
	int ret;

	msg = otx2_mbox_alloc_msg(&cptpf->afpf_mbox, 0, size);
	if (msg == NULL)
		return -ENOMEM;

	memcpy((uint8_t *)msg + sizeof(struct mbox_msghdr),
	       (uint8_t *)req + sizeof(struct mbox_msghdr), size);
	msg->id = req->id;
	msg->pcifunc = req->pcifunc;
	msg->sig = req->sig;
	msg->ver = req->ver;

	otx2_mbox_msg_send(&cptpf->afpf_mbox, 0);
	ret = otx2_mbox_wait_for_rsp(&cptpf->afpf_mbox, 0);
	if (ret == -EIO) {
		dev_err(&cptpf->pdev->dev, "RVU MBOX timeout.\n");
		return ret;
	} else if (ret) {
		dev_err(&cptpf->pdev->dev, "RVU MBOX error: %d.\n", ret);
		return -EFAULT;
	}
	return 0;
}

static int handle_msg_get_caps(struct otx2_cptpf_dev *cptpf,
			       struct otx2_cptvf_info *vf,
			       struct mbox_msghdr *req)
{
	struct otx2_cpt_caps_rsp *rsp;

	rsp = (struct otx2_cpt_caps_rsp *)
	      otx2_mbox_alloc_msg(&cptpf->vfpf_mbox, vf->vf_id,
				  sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;

	rsp->hdr.id = MBOX_MSG_GET_CAPS;
	rsp->hdr.sig = OTX2_MBOX_RSP_SIG;
	rsp->hdr.pcifunc = req->pcifunc;
	rsp->cpt_pf_drv_version = OTX2_CPT_PF_DRV_VERSION;
	rsp->cpt_revision = cptpf->pdev->revision;
	memcpy(&rsp->eng_caps, &cptpf->eng_caps, sizeof(rsp->eng_caps));

	return 0;
}

static int handle_msg_get_eng_grp_num(struct otx2_cptpf_dev *cptpf,
				      struct otx2_cptvf_info *vf,
				      struct mbox_msghdr *req)
{
	struct otx2_cpt_egrp_num_msg *grp_req;
	struct otx2_cpt_egrp_num_rsp *rsp;

	grp_req = (struct otx2_cpt_egrp_num_msg *)req;
	rsp = (struct otx2_cpt_egrp_num_rsp *)
	       otx2_mbox_alloc_msg(&cptpf->vfpf_mbox, vf->vf_id, sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;

	rsp->hdr.id = MBOX_MSG_GET_ENG_GRP_NUM;
	rsp->hdr.sig = OTX2_MBOX_RSP_SIG;
	rsp->hdr.pcifunc = req->pcifunc;
	rsp->eng_type = grp_req->eng_type;
	rsp->eng_grp_num = otx2_cpt_get_eng_grp(&cptpf->eng_grps,
						grp_req->eng_type);

	return 0;
}

static int handle_msg_kvf_limits(struct otx2_cptpf_dev *cptpf,
				 struct otx2_cptvf_info *vf,
				 struct mbox_msghdr *req)
{
	struct otx2_cpt_kvf_limits_rsp *rsp;

	rsp = (struct otx2_cpt_kvf_limits_rsp *)
	      otx2_mbox_alloc_msg(&cptpf->vfpf_mbox, vf->vf_id, sizeof(*rsp));
	if (!rsp)
		return -ENOMEM;

	rsp->hdr.id = MBOX_MSG_GET_KVF_LIMITS;
	rsp->hdr.sig = OTX2_MBOX_RSP_SIG;
	rsp->hdr.pcifunc = req->pcifunc;
	rsp->kvf_limits = cptpf->kvf_limits;

	return 0;
}

static int cptpf_handle_vf_req(struct otx2_cptpf_dev *cptpf,
			       struct otx2_cptvf_info *vf,
			       struct mbox_msghdr *req, int size)
{
	int err = 0;

	/* Check if msg is valid, if not reply with an invalid msg */
	if (req->sig != OTX2_MBOX_REQ_SIG)
		goto inval_msg;

	switch (req->id) {
	case MBOX_MSG_GET_ENG_GRP_NUM:
		err = handle_msg_get_eng_grp_num(cptpf, vf, req);
		break;
	case MBOX_MSG_GET_CAPS:
		err = handle_msg_get_caps(cptpf, vf, req);
		break;
	case MBOX_MSG_GET_KVF_LIMITS:
		err = handle_msg_kvf_limits(cptpf, vf, req);
		break;
	default:
		err = forward_to_af(cptpf, vf, req, size);
		break;
	}
	return err;

inval_msg:
	otx2_reply_invalid_msg(&cptpf->vfpf_mbox, vf->vf_id, 0, req->id);
	otx2_mbox_msg_send(&cptpf->vfpf_mbox, vf->vf_id);
	return err;
}

irqreturn_t otx2_cptpf_vfpf_mbox_intr(int __always_unused irq, void *arg)
{
	struct otx2_cptpf_dev *cptpf = arg;
	struct otx2_cptvf_info *vf;
	int i, vf_idx;
	u64 intr;

	/*
	 * Check which VF has raised an interrupt and schedule
	 * corresponding work queue to process the messages
	 */
	for (i = 0; i < 2; i++) {
		/* Read the interrupt bits */
		intr = otx2_cpt_read64(cptpf->reg_base, BLKADDR_RVUM, 0,
				       RVU_PF_VFPF_MBOX_INTX(i));

		for (vf_idx = i * 64; vf_idx < cptpf->enabled_vfs; vf_idx++) {
			vf = &cptpf->vf[vf_idx];
			if (intr & (1ULL << vf->intr_idx)) {
				queue_work(cptpf->vfpf_mbox_wq,
					   &vf->vfpf_mbox_work);
				/* Clear the interrupt */
				otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM,
						 0, RVU_PF_VFPF_MBOX_INTX(i),
						 BIT_ULL(vf->intr_idx));
			}
		}
	}
	return IRQ_HANDLED;
}

void otx2_cptpf_vfpf_mbox_handler(struct work_struct *work)
{
	struct otx2_cptpf_dev *cptpf;
	struct otx2_cptvf_info *vf;
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *req_hdr;
	struct mbox_msghdr *msg;
	struct otx2_mbox *mbox;
	int offset, i, err;

	vf = container_of(work, struct otx2_cptvf_info, vfpf_mbox_work);
	cptpf = vf->cptpf;
	mbox = &cptpf->vfpf_mbox;
	/* sync with mbox memory region */
	smp_rmb();
	mdev = &mbox->dev[vf->vf_id];
	/* Process received mbox messages */
	req_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
	offset = mbox->rx_start + ALIGN(sizeof(*req_hdr), MBOX_MSG_ALIGN);

	for (i = 0; i < req_hdr->num_msgs; i++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + offset);

		/* Set which VF sent this message based on mbox IRQ */
		msg->pcifunc = ((u16)cptpf->pf_id << RVU_PFVF_PF_SHIFT) |
				((vf->vf_id + 1) & RVU_PFVF_FUNC_MASK);

		err = cptpf_handle_vf_req(cptpf, vf, msg,
					  msg->next_msgoff - offset);
		/*
		 * Behave as the AF, drop the msg if there is
		 * no memory, timeout handling also goes here
		 */
		if (err == -ENOMEM || err == -EIO)
			break;
		offset = msg->next_msgoff;
	}
	/* Send mbox responses to VF */
	if (mdev->num_msgs)
		otx2_mbox_msg_send(mbox, vf->vf_id);
}

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
	struct cpt_rd_wr_reg_msg *rsp_rd_wr;

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
	case MBOX_MSG_CPT_RD_WR_REGISTER:
		rsp_rd_wr = (struct cpt_rd_wr_reg_msg *)msg;
		if (msg->rc) {
			dev_err(dev, "Reg %llx rd/wr(%d) failed %d\n",
				rsp_rd_wr->reg_offset, rsp_rd_wr->is_write,
				msg->rc);
			return;
		}
		if (!rsp_rd_wr->is_write)
			*rsp_rd_wr->ret_val = rsp_rd_wr->val;
		break;
	case MBOX_MSG_ATTACH_RESOURCES:
		if (!msg->rc)
			cptpf->lfs.are_lfs_attached = 1;
		break;
	case MBOX_MSG_DETACH_RESOURCES:
		if (!msg->rc)
			cptpf->lfs.are_lfs_attached = 0;
		break;

	default:
		dev_err(dev,
			"Unsupported msg %d received.\n", msg->id);
		break;
	}
}

static void forward_to_vf(struct otx2_cptpf_dev *cptpf, struct mbox_msghdr *msg,
			  int vf_id, int size)
{
	struct otx2_mbox *vfpf_mbox;
	struct mbox_msghdr *fwd;

	if (msg->id >= MBOX_MSG_MAX) {
		dev_err(&cptpf->pdev->dev,
			"MBOX msg with unknown ID %d\n", msg->id);
		return;
	}
	if (msg->sig != OTX2_MBOX_RSP_SIG) {
		dev_err(&cptpf->pdev->dev,
			"MBOX msg with wrong signature %x, ID %d\n",
			msg->sig, msg->id);
		return;
	}
	vfpf_mbox = &cptpf->vfpf_mbox;
	vf_id--;
	if (vf_id >= cptpf->enabled_vfs) {
		dev_err(&cptpf->pdev->dev,
			"MBOX msg to unknown VF: %d >= %d\n",
			vf_id, cptpf->enabled_vfs);
		return;
	}
	if (msg->id == MBOX_MSG_VF_FLR)
		return;

	fwd = otx2_mbox_alloc_msg(vfpf_mbox, vf_id, size);
	if (!fwd) {
		dev_err(&cptpf->pdev->dev,
			"Forwarding to VF%d failed.\n", vf_id);
		return;
	}
	memcpy((uint8_t *)fwd + sizeof(struct mbox_msghdr),
		(uint8_t *)msg + sizeof(struct mbox_msghdr), size);
	fwd->id = msg->id;
	fwd->pcifunc = msg->pcifunc;
	fwd->sig = msg->sig;
	fwd->ver = msg->ver;
	fwd->rc = msg->rc;
}

/* Handle mailbox messages received from AF */
void otx2_cptpf_afpf_mbox_handler(struct work_struct *work)
{
	struct otx2_cptpf_dev *cptpf;
	struct otx2_mbox *afpf_mbox;
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;
	int offset, vf_id, i;

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
		vf_id = (msg->pcifunc >> RVU_PFVF_FUNC_SHIFT) &
			 RVU_PFVF_FUNC_MASK;
		if (vf_id > 0)
			forward_to_vf(cptpf, msg, vf_id,
				      msg->next_msgoff - offset);
		else
			process_afpf_mbox_msg(cptpf, msg);

		offset = msg->next_msgoff;
		mdev->msgs_acked++;
	}
	otx2_mbox_reset(afpf_mbox, 0);
}
