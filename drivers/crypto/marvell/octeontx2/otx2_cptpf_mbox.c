// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptpf.h"
#include "rvu_reg.h"

/* Fastpath ipsec opcode with inplace processing */
#define CPT_INLINE_RX_OPCODE (0x26 | (1 << 6))
#define CN10K_CPT_INLINE_RX_OPCODE (0x29 | (1 << 6))

#define cpt_inline_rx_opcode(pdev)                      \
({                                                      \
	u8 opcode;                                      \
	if (is_dev_otx2(pdev))                          \
		opcode = CPT_INLINE_RX_OPCODE;          \
	else                                            \
		opcode = CN10K_CPT_INLINE_RX_OPCODE;    \
	(opcode);                                       \
})

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

	mutex_lock(&cptpf->lock);
	msg = otx2_mbox_alloc_msg(&cptpf->afpf_mbox, 0, size);
	if (msg == NULL) {
		mutex_unlock(&cptpf->lock);
		return -ENOMEM;
	}

	memcpy((uint8_t *)msg + sizeof(struct mbox_msghdr),
	       (uint8_t *)req + sizeof(struct mbox_msghdr), size);
	msg->id = req->id;
	msg->pcifunc = req->pcifunc;
	msg->sig = req->sig;
	msg->ver = req->ver;

	ret = otx2_cpt_sync_mbox_msg(&cptpf->afpf_mbox);
	/* Error code -EIO indicate there is a communication failure
	 * to the AF. Rest of the error codes indicate that AF processed
	 * VF messages and set the error codes in response messages
	 * (if any) so simply forward responses to VF.
	 */
	if (ret == -EIO) {
		dev_warn(&cptpf->pdev->dev,
			 "AF not responding to VF%d messages\n", vf->vf_id);
		mutex_unlock(&cptpf->lock);
		return ret;
	}
	mutex_unlock(&cptpf->lock);
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
	rsp->cpt_revision = cptpf->eng_grps.rid;
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

static int send_inline_ipsec_inbound_msg(struct otx2_cptpf_dev *cptpf,
					 int sso_pf_func, u8 slot)
{
	struct cpt_inline_ipsec_cfg_msg *req;
	struct pci_dev *pdev = cptpf->pdev;

	req = (struct cpt_inline_ipsec_cfg_msg *)
	      otx2_mbox_alloc_msg_rsp(&cptpf->afpf_mbox, 0,
				      sizeof(*req), sizeof(struct msg_rsp));
	if (req == NULL) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}
	memset(req, 0, sizeof(*req));
	req->hdr.id = MBOX_MSG_CPT_INLINE_IPSEC_CFG;
	req->hdr.sig = OTX2_MBOX_REQ_SIG;
	req->hdr.pcifunc = OTX2_CPT_RVU_PFFUNC(cptpf->pdev, cptpf->pf_id, 0);
	req->dir = CPT_INLINE_INBOUND;
	req->slot = slot;
	req->sso_pf_func_ovrd = cptpf->sso_pf_func_ovrd;
	req->sso_pf_func = sso_pf_func;
	req->enable = 1;

	return otx2_cpt_send_mbox_msg(&cptpf->afpf_mbox, pdev);
}

static int rx_inline_ipsec_lf_cfg(struct otx2_cptpf_dev *cptpf, u8 egrp,
				  struct otx2_cpt_rx_inline_lf_cfg *req)
{
	struct nix_inline_ipsec_cfg *nix_req;
	struct pci_dev *pdev = cptpf->pdev;
	int ret;

	nix_req = (struct nix_inline_ipsec_cfg *)
		   otx2_mbox_alloc_msg_rsp(&cptpf->afpf_mbox, 0,
					   sizeof(*nix_req),
					   sizeof(struct msg_rsp));
	if (nix_req == NULL) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}
	memset(nix_req, 0, sizeof(*nix_req));
	nix_req->hdr.id = MBOX_MSG_NIX_INLINE_IPSEC_CFG;
	nix_req->hdr.sig = OTX2_MBOX_REQ_SIG;
	nix_req->enable = 1;
	nix_req->credit_th = req->credit_th;
	nix_req->bpid = req->bpid;
	if (!req->credit || req->credit > OTX2_CPT_INST_QLEN_MSGS)
		nix_req->cpt_credit = OTX2_CPT_INST_QLEN_MSGS - 1;
	else
		nix_req->cpt_credit = req->credit - 1;
	nix_req->gen_cfg.egrp = egrp;
	if (req->opcode)
		nix_req->gen_cfg.opcode = req->opcode;
	else
		nix_req->gen_cfg.opcode = cpt_inline_rx_opcode(pdev);
	nix_req->gen_cfg.param1 = req->param1;
	nix_req->gen_cfg.param2 = req->param2;
	nix_req->inst_qsel.cpt_pf_func =
		OTX2_CPT_RVU_PFFUNC(cptpf->pdev, cptpf->pf_id, 0);
	nix_req->inst_qsel.cpt_slot = 0;
	ret = otx2_cpt_send_mbox_msg(&cptpf->afpf_mbox, pdev);
	if (ret)
		return ret;

	if (cptpf->has_cpt1) {
		ret = send_inline_ipsec_inbound_msg(cptpf, req->sso_pf_func, 1);
		if (ret)
			return ret;
	}

	return send_inline_ipsec_inbound_msg(cptpf, req->sso_pf_func, 0);
}

int
otx2_inline_cptlf_setup(struct otx2_cptpf_dev *cptpf,
			struct otx2_cptlfs_info *lfs, u8 egrp, int num_lfs)
{
	int ret;

	ret = otx2_cptlf_init(lfs, 1 << egrp, OTX2_CPT_QUEUE_HI_PRIO, 1);
	if (ret) {
		dev_err(&cptpf->pdev->dev,
			"LF configuration failed for RX inline ipsec.\n");
		return ret;
	}

	/* Get msix offsets for attached LFs */
	ret = otx2_cpt_msix_offset_msg(lfs);
	if (ret)
		goto cleanup_lf;

	/* Register for CPT LF Misc interrupts */
	ret = otx2_cptlf_register_misc_interrupts(lfs);
	if (ret)
		goto free_irq;

	return 0;
free_irq:
	otx2_cptlf_unregister_misc_interrupts(lfs);
cleanup_lf:
	otx2_cptlf_shutdown(lfs);
	return ret;
}

void
otx2_inline_cptlf_cleanup(struct otx2_cptlfs_info *lfs)
{
	/* Unregister misc interrupt */
	otx2_cptlf_unregister_misc_interrupts(lfs);

	/* Cleanup LFs */
	otx2_cptlf_shutdown(lfs);
}

static int handle_msg_rx_inline_ipsec_lf_cfg(struct otx2_cptpf_dev *cptpf,
					     struct mbox_msghdr *req)
{
	struct otx2_cpt_rx_inline_lf_cfg *cfg_req;
	int num_lfs = 1, ret;
	u8 egrp;

	cfg_req = (struct otx2_cpt_rx_inline_lf_cfg *)req;
	if (cptpf->lfs.lfs_num) {
		dev_err(&cptpf->pdev->dev,
			"LF is already configured for RX inline ipsec.\n");
		return -EEXIST;
	}
	/*
	 * Allow LFs to execute requests destined to only grp IE_TYPES and
	 * set queue priority of each LF to high
	 */
	egrp = otx2_cpt_get_eng_grp(&cptpf->eng_grps, OTX2_CPT_IE_TYPES);
	if (egrp == OTX2_CPT_INVALID_CRYPTO_ENG_GRP) {
		dev_err(&cptpf->pdev->dev,
			"Engine group for inline ipsec is not available\n");
		return -ENOENT;
	}

	cptpf->lfs.global_slot = 0;
	cptpf->lfs.ctx_ilen_ovrd = cfg_req->ctx_ilen_valid;
	cptpf->lfs.ctx_ilen = cfg_req->ctx_ilen;

	ret = otx2_inline_cptlf_setup(cptpf, &cptpf->lfs, egrp, num_lfs);
	if (ret) {
		dev_err(&cptpf->pdev->dev, "Inline-Ipsec CPT0 LF setup failed.\n");
		return ret;
	}

	if (cptpf->has_cpt1) {
		cptpf->rsrc_req_blkaddr = BLKADDR_CPT1;
		cptpf->cpt1_lfs.global_slot = num_lfs;
		cptpf->cpt1_lfs.ctx_ilen_ovrd = cfg_req->ctx_ilen_valid;
		cptpf->cpt1_lfs.ctx_ilen = cfg_req->ctx_ilen;
		ret = otx2_inline_cptlf_setup(cptpf, &cptpf->cpt1_lfs, egrp,
					      num_lfs);
		if (ret) {
			dev_err(&cptpf->pdev->dev, "Inline CPT1 LF setup failed.\n");
			goto lf_cleanup;
		}
		cptpf->rsrc_req_blkaddr = 0;
	}

	ret = rx_inline_ipsec_lf_cfg(cptpf, egrp, cfg_req);
	if (ret)
		goto lf1_cleanup;

	return 0;

lf1_cleanup:
	otx2_inline_cptlf_cleanup(&cptpf->cpt1_lfs);
lf_cleanup:
	otx2_inline_cptlf_cleanup(&cptpf->lfs);
	return ret;
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
	case MBOX_MSG_RX_INLINE_IPSEC_LF_CFG:
		err = handle_msg_rx_inline_ipsec_lf_cfg(cptpf, req);
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
		msg->pcifunc = rvu_make_pcifunc(cptpf->pdev, cptpf->pf_id,
						(vf->vf_id + 1));
		err = cptpf_handle_vf_req(cptpf, vf, msg,
					  msg->next_msgoff - offset);
		/*
		 * Behave as the AF, drop the msg if there is
		 * no memory, timeout handling also goes here
		 */
		if (err == -ENOMEM || err == -EIO)
			break;
		offset = msg->next_msgoff;
		/* Write barrier required for VF responses which are handled by
		 * PF driver and not forwarded to AF.
		 */
		smp_wmb();
	}
	/* Send mbox responses to VF */
	if (mdev->num_msgs)
		otx2_mbox_msg_send(mbox, vf->vf_id);
}

irqreturn_t otx2_cptpf_afpf_mbox_intr(int __always_unused irq, void *arg)
{
	struct otx2_cptpf_dev *cptpf = arg;
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *mbox;
	struct mbox_hdr *hdr;
	u64 intr;

	/* Read the interrupt bits */
	intr = otx2_cpt_read64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT);

	if (intr & 0x1ULL) {
		mbox = &cptpf->afpf_mbox;
		mdev = &mbox->dev[0];
		hdr = mdev->mbase + mbox->rx_start;
		if (hdr->num_msgs)
			/* Schedule work queue function to process the MBOX request */
			queue_work(cptpf->afpf_mbox_wq, &cptpf->afpf_mbox_work);

		mbox = &cptpf->afpf_mbox_up;
		mdev = &mbox->dev[0];
		hdr = mdev->mbase + mbox->rx_start;
		if (hdr->num_msgs)
			/* Schedule work queue function to process the MBOX request */
			queue_work(cptpf->afpf_mbox_wq, &cptpf->afpf_mbox_up_work);
		/* Clear and ack the interrupt */
		otx2_cpt_write64(cptpf->reg_base, BLKADDR_RVUM, 0, RVU_PF_INT,
				 0x1ULL);
	}
	return IRQ_HANDLED;
}

static void process_afpf_mbox_msg(struct otx2_cptpf_dev *cptpf,
				  struct mbox_msghdr *msg)
{
	struct otx2_cptlfs_info *lfs = &cptpf->lfs;
	struct device *dev = &cptpf->pdev->dev;
	struct cpt_rd_wr_reg_msg *rsp_rd_wr;
	struct msix_offset_rsp *rsp_msix;
	int i;

	if (msg->id >= MBOX_MSG_MAX) {
		dev_err(dev, "MBOX msg with unknown ID %d\n", msg->id);
		return;
	}
	if (msg->sig != OTX2_MBOX_RSP_SIG) {
		dev_err(dev, "MBOX msg with wrong signature %x, ID %d\n",
			msg->sig, msg->id);
		return;
	}
	if (cptpf->rsrc_req_blkaddr == BLKADDR_CPT1)
		lfs = &cptpf->cpt1_lfs;

	switch (msg->id) {
	case MBOX_MSG_READY:
		cptpf->pf_id = rvu_get_pf(cptpf->pdev, msg->pcifunc);
		break;
	case MBOX_MSG_MSIX_OFFSET:
		rsp_msix = (struct msix_offset_rsp *) msg;
		for (i = 0; i < rsp_msix->cptlfs; i++)
			lfs->lf[i].msix_offset = rsp_msix->cptlf_msixoff[i];

		for (i = 0; i < rsp_msix->cpt1_lfs; i++)
			lfs->lf[i].msix_offset = rsp_msix->cpt1_lf_msixoff[i];
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
			lfs->are_lfs_attached = 1;
		break;
	case MBOX_MSG_DETACH_RESOURCES:
		if (!msg->rc)
			lfs->are_lfs_attached = 0;
		break;
	case MBOX_MSG_CPT_INLINE_IPSEC_CFG:
	case MBOX_MSG_NIX_INLINE_IPSEC_CFG:
	case MBOX_MSG_CPT_LF_RESET:
	case MBOX_MSG_LMTST_TBL_SETUP:
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
		/* Sync VF response ready to be sent */
		smp_wmb();
		mdev->msgs_acked++;
	}
	otx2_mbox_reset(afpf_mbox, 0);
}

static void handle_msg_cpt_inst_lmtst(struct otx2_cptpf_dev *cptpf,
				      struct mbox_msghdr *msg)
{
	struct cpt_inst_lmtst_req *req = (struct cpt_inst_lmtst_req *)msg;
	struct otx2_cptlfs_info *lfs = &cptpf->lfs;
	struct msg_rsp *rsp;

	if (cptpf->lfs.lfs_num)
		lfs->ops->send_cmd((union otx2_cpt_inst_s *)req->inst, 1,
				   &lfs->lf[0]);

	rsp = (struct msg_rsp *)otx2_mbox_alloc_msg(&cptpf->afpf_mbox_up, 0,
						    sizeof(*rsp));
	if (!rsp)
		return;

	rsp->hdr.id = msg->id;
	rsp->hdr.sig = OTX2_MBOX_RSP_SIG;
	rsp->hdr.pcifunc = 0;
	rsp->hdr.rc = 0;
}

static void process_afpf_mbox_up_msg(struct otx2_cptpf_dev *cptpf,
				     struct mbox_msghdr *msg)
{
	if (msg->id >= MBOX_MSG_MAX) {
		dev_err(&cptpf->pdev->dev,
			"MBOX msg with unknown ID %d\n", msg->id);
		return;
	}

	switch (msg->id) {
	case MBOX_MSG_CPT_INST_LMTST:
		handle_msg_cpt_inst_lmtst(cptpf, msg);
		break;
	default:
		otx2_reply_invalid_msg(&cptpf->afpf_mbox_up, 0, 0, msg->id);
	}
}

void otx2_cptpf_afpf_mbox_up_handler(struct work_struct *work)
{
	struct otx2_cptpf_dev *cptpf;
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;
	struct otx2_mbox *mbox;
	int offset, i;

	cptpf = container_of(work, struct otx2_cptpf_dev, afpf_mbox_up_work);
	mbox = &cptpf->afpf_mbox_up;
	mdev = &mbox->dev[0];
	/* Sync mbox data into memory */
	smp_wmb();

	rsp_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
	offset = mbox->rx_start + ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);

	for (i = 0; i < rsp_hdr->num_msgs; i++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + offset);

		process_afpf_mbox_up_msg(cptpf, msg);

		offset = mbox->rx_start + msg->next_msgoff;
	}
	otx2_mbox_msg_send(mbox, 0);
}
