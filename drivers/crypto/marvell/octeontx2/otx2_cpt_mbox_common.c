// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptlf.h"

int otx2_cpt_send_mbox_msg(struct otx2_mbox *mbox, struct pci_dev *pdev)
{
	int ret;

	otx2_mbox_msg_send(mbox, 0);
	ret = otx2_mbox_wait_for_rsp(mbox, 0);
	if (ret == -EIO) {
		dev_err(&pdev->dev, "RVU MBOX timeout.\n");
		return ret;
	} else if (ret) {
		dev_err(&pdev->dev, "RVU MBOX error: %d.\n", ret);
		return -EFAULT;
	}
	return ret;
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_send_mbox_msg, CRYPTO_DEV_OCTEONTX2_CPT);

int otx2_cpt_send_ready_msg(struct otx2_mbox *mbox, struct pci_dev *pdev)
{
	struct mbox_msghdr *req;

	req = otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*req),
				      sizeof(struct ready_msg_rsp));
	if (req == NULL) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}
	req->id = MBOX_MSG_READY;
	req->sig = OTX2_MBOX_REQ_SIG;
	req->pcifunc = 0;

	return otx2_cpt_send_mbox_msg(mbox, pdev);
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_send_ready_msg, CRYPTO_DEV_OCTEONTX2_CPT);

int otx2_cpt_send_af_reg_requests(struct otx2_mbox *mbox, struct pci_dev *pdev)
{
	return otx2_cpt_send_mbox_msg(mbox, pdev);
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_send_af_reg_requests, CRYPTO_DEV_OCTEONTX2_CPT);

static int otx2_cpt_add_read_af_reg(struct otx2_mbox *mbox,
				    struct pci_dev *pdev, u64 reg,
				    u64 *val, int blkaddr)
{
	struct cpt_rd_wr_reg_msg *reg_msg;

	reg_msg = (struct cpt_rd_wr_reg_msg *)
			otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*reg_msg),
						sizeof(*reg_msg));
	if (reg_msg == NULL) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}

	reg_msg->hdr.id = MBOX_MSG_CPT_RD_WR_REGISTER;
	reg_msg->hdr.sig = OTX2_MBOX_REQ_SIG;
	reg_msg->hdr.pcifunc = 0;

	reg_msg->is_write = 0;
	reg_msg->reg_offset = reg;
	reg_msg->ret_val = val;
	reg_msg->blkaddr = blkaddr;

	return 0;
}

int otx2_cpt_add_write_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			      u64 reg, u64 val, int blkaddr)
{
	struct cpt_rd_wr_reg_msg *reg_msg;

	reg_msg = (struct cpt_rd_wr_reg_msg *)
			otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*reg_msg),
						sizeof(*reg_msg));
	if (reg_msg == NULL) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}

	reg_msg->hdr.id = MBOX_MSG_CPT_RD_WR_REGISTER;
	reg_msg->hdr.sig = OTX2_MBOX_REQ_SIG;
	reg_msg->hdr.pcifunc = 0;

	reg_msg->is_write = 1;
	reg_msg->reg_offset = reg;
	reg_msg->val = val;
	reg_msg->blkaddr = blkaddr;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_add_write_af_reg, CRYPTO_DEV_OCTEONTX2_CPT);

int otx2_cpt_read_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			 u64 reg, u64 *val, int blkaddr)
{
	int ret;

	ret = otx2_cpt_add_read_af_reg(mbox, pdev, reg, val, blkaddr);
	if (ret)
		return ret;

	return otx2_cpt_send_mbox_msg(mbox, pdev);
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_read_af_reg, CRYPTO_DEV_OCTEONTX2_CPT);

int otx2_cpt_write_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			  u64 reg, u64 val, int blkaddr)
{
	int ret;

	ret = otx2_cpt_add_write_af_reg(mbox, pdev, reg, val, blkaddr);
	if (ret)
		return ret;

	return otx2_cpt_send_mbox_msg(mbox, pdev);
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_write_af_reg, CRYPTO_DEV_OCTEONTX2_CPT);

int otx2_cpt_attach_rscrs_msg(struct otx2_cptlfs_info *lfs)
{
	struct otx2_mbox *mbox = lfs->mbox;
	struct rsrc_attach *req;
	int ret;

	req = (struct rsrc_attach *)
			otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*req),
						sizeof(struct msg_rsp));
	if (req == NULL) {
		dev_err(&lfs->pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}

	req->hdr.id = MBOX_MSG_ATTACH_RESOURCES;
	req->hdr.sig = OTX2_MBOX_REQ_SIG;
	req->hdr.pcifunc = 0;
	req->cptlfs = lfs->lfs_num;
	req->cpt_blkaddr = lfs->blkaddr;
	req->modify = 1;
	ret = otx2_cpt_send_mbox_msg(mbox, lfs->pdev);
	if (ret)
		return ret;

	if (!lfs->are_lfs_attached)
		ret = -EINVAL;

	return ret;
}

int otx2_cpt_detach_rsrcs_msg(struct otx2_cptlfs_info *lfs)
{
	struct otx2_mbox *mbox = lfs->mbox;
	struct rsrc_detach *req;
	int ret;

	req = (struct rsrc_detach *)
				otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*req),
							sizeof(struct msg_rsp));
	if (req == NULL) {
		dev_err(&lfs->pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}

	req->hdr.id = MBOX_MSG_DETACH_RESOURCES;
	req->hdr.sig = OTX2_MBOX_REQ_SIG;
	req->hdr.pcifunc = 0;
	req->cptlfs = 1;
	ret = otx2_cpt_send_mbox_msg(mbox, lfs->pdev);
	if (ret)
		return ret;

	if (lfs->are_lfs_attached)
		ret = -EINVAL;

	return ret;
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_detach_rsrcs_msg, CRYPTO_DEV_OCTEONTX2_CPT);

int otx2_cpt_msix_offset_msg(struct otx2_cptlfs_info *lfs)
{
	struct otx2_mbox *mbox = lfs->mbox;
	struct pci_dev *pdev = lfs->pdev;
	struct mbox_msghdr *req;
	int ret, i;

	req = otx2_mbox_alloc_msg_rsp(mbox, 0, sizeof(*req),
				      sizeof(struct msix_offset_rsp));
	if (req == NULL) {
		dev_err(&pdev->dev, "RVU MBOX failed to get message.\n");
		return -EFAULT;
	}

	req->id = MBOX_MSG_MSIX_OFFSET;
	req->sig = OTX2_MBOX_REQ_SIG;
	req->pcifunc = 0;
	ret = otx2_cpt_send_mbox_msg(mbox, pdev);
	if (ret)
		return ret;

	for (i = 0; i < lfs->lfs_num; i++) {
		if (lfs->lf[i].msix_offset == MSIX_VECTOR_INVALID) {
			dev_err(&pdev->dev,
				"Invalid msix offset %d for LF %d\n",
				lfs->lf[i].msix_offset, i);
			return -EINVAL;
		}
	}
	return ret;
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_msix_offset_msg, CRYPTO_DEV_OCTEONTX2_CPT);

int otx2_cpt_sync_mbox_msg(struct otx2_mbox *mbox)
{
	int err;

	if (!otx2_mbox_nonempty(mbox, 0))
		return 0;
	otx2_mbox_msg_send(mbox, 0);
	err = otx2_mbox_wait_for_rsp(mbox, 0);
	if (err)
		return err;

	return otx2_mbox_check_rsp_msgs(mbox, 0);
}
EXPORT_SYMBOL_NS_GPL(otx2_cpt_sync_mbox_msg, CRYPTO_DEV_OCTEONTX2_CPT);
