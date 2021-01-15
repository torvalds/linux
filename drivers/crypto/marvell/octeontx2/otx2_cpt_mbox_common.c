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
