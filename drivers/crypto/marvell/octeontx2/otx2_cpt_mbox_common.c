// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"

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

int otx2_cpt_send_af_reg_requests(struct otx2_mbox *mbox, struct pci_dev *pdev)
{
	return otx2_cpt_send_mbox_msg(mbox, pdev);
}

int otx2_cpt_add_read_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			     u64 reg, u64 *val)
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

	return 0;
}

int otx2_cpt_add_write_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			      u64 reg, u64 val)
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

	return 0;
}

int otx2_cpt_read_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			 u64 reg, u64 *val)
{
	int ret;

	ret = otx2_cpt_add_read_af_reg(mbox, pdev, reg, val);
	if (ret)
		return ret;

	return otx2_cpt_send_mbox_msg(mbox, pdev);
}

int otx2_cpt_write_af_reg(struct otx2_mbox *mbox, struct pci_dev *pdev,
			  u64 reg, u64 val)
{
	int ret;

	ret = otx2_cpt_add_write_af_reg(mbox, pdev, reg, val);
	if (ret)
		return ret;

	return otx2_cpt_send_mbox_msg(mbox, pdev);
}
