/*
 * Copyright (C) 2016 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include "cptvf.h"

static void cptvf_send_msg_to_pf(struct cpt_vf *cptvf, struct cpt_mbox *mbx)
{
	/* Writing mbox(1) causes interrupt */
	cpt_write_csr64(cptvf->reg_base, CPTX_VFX_PF_MBOXX(0, 0, 0),
			mbx->msg);
	cpt_write_csr64(cptvf->reg_base, CPTX_VFX_PF_MBOXX(0, 0, 1),
			mbx->data);
}

/* Interrupt handler to handle mailbox messages from VFs */
void cptvf_handle_mbox_intr(struct cpt_vf *cptvf)
{
	struct cpt_mbox mbx = {};

	/*
	 * MBOX[0] contains msg
	 * MBOX[1] contains data
	 */
	mbx.msg  = cpt_read_csr64(cptvf->reg_base, CPTX_VFX_PF_MBOXX(0, 0, 0));
	mbx.data = cpt_read_csr64(cptvf->reg_base, CPTX_VFX_PF_MBOXX(0, 0, 1));
	dev_dbg(&cptvf->pdev->dev, "%s: Mailbox msg 0x%llx from PF\n",
		__func__, mbx.msg);
	switch (mbx.msg) {
	case CPT_MSG_READY:
	{
		cptvf->pf_acked = true;
		cptvf->vfid = mbx.data;
		dev_dbg(&cptvf->pdev->dev, "Received VFID %d\n", cptvf->vfid);
		break;
	}
	case CPT_MSG_QBIND_GRP:
		cptvf->pf_acked = true;
		cptvf->vftype = mbx.data;
		dev_dbg(&cptvf->pdev->dev, "VF %d type %s group %d\n",
			cptvf->vfid, ((mbx.data == SE_TYPES) ? "SE" : "AE"),
			cptvf->vfgrp);
		break;
	case CPT_MBOX_MSG_TYPE_ACK:
		cptvf->pf_acked = true;
		break;
	case CPT_MBOX_MSG_TYPE_NACK:
		cptvf->pf_nacked = true;
		break;
	default:
		dev_err(&cptvf->pdev->dev, "Invalid msg from PF, msg 0x%llx\n",
			mbx.msg);
		break;
	}
}

static int cptvf_send_msg_to_pf_timeout(struct cpt_vf *cptvf,
					struct cpt_mbox *mbx)
{
	int timeout = CPT_MBOX_MSG_TIMEOUT;
	int sleep = 10;

	cptvf->pf_acked = false;
	cptvf->pf_nacked = false;
	cptvf_send_msg_to_pf(cptvf, mbx);
	/* Wait for previous message to be acked, timeout 2sec */
	while (!cptvf->pf_acked) {
		if (cptvf->pf_nacked)
			return -EINVAL;
		msleep(sleep);
		if (cptvf->pf_acked)
			break;
		timeout -= sleep;
		if (!timeout) {
			dev_err(&cptvf->pdev->dev, "PF didn't ack to mbox msg %llx from VF%u\n",
				(mbx->msg & 0xFF), cptvf->vfid);
			return -EBUSY;
		}
	}

	return 0;
}

/*
 * Checks if VF is able to comminicate with PF
 * and also gets the CPT number this VF is associated to.
 */
int cptvf_check_pf_ready(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct cpt_mbox mbx = {};

	mbx.msg = CPT_MSG_READY;
	if (cptvf_send_msg_to_pf_timeout(cptvf, &mbx)) {
		dev_err(&pdev->dev, "PF didn't respond to READY msg\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * Communicate VQs size to PF to program CPT(0)_PF_Q(0-15)_CTL of the VF.
 * Must be ACKed.
 */
int cptvf_send_vq_size_msg(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct cpt_mbox mbx = {};

	mbx.msg = CPT_MSG_QLEN;
	mbx.data = cptvf->qsize;
	if (cptvf_send_msg_to_pf_timeout(cptvf, &mbx)) {
		dev_err(&pdev->dev, "PF didn't respond to vq_size msg\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * Communicate VF group required to PF and get the VQ binded to that group
 */
int cptvf_send_vf_to_grp_msg(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct cpt_mbox mbx = {};

	mbx.msg = CPT_MSG_QBIND_GRP;
	/* Convey group of the VF */
	mbx.data = cptvf->vfgrp;
	if (cptvf_send_msg_to_pf_timeout(cptvf, &mbx)) {
		dev_err(&pdev->dev, "PF didn't respond to vf_type msg\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * Communicate VF group required to PF and get the VQ binded to that group
 */
int cptvf_send_vf_priority_msg(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct cpt_mbox mbx = {};

	mbx.msg = CPT_MSG_VQ_PRIORITY;
	/* Convey group of the VF */
	mbx.data = cptvf->priority;
	if (cptvf_send_msg_to_pf_timeout(cptvf, &mbx)) {
		dev_err(&pdev->dev, "PF didn't respond to vf_type msg\n");
		return -EBUSY;
	}
	return 0;
}

/*
 * Communicate to PF that VF is UP and running
 */
int cptvf_send_vf_up(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct cpt_mbox mbx = {};

	mbx.msg = CPT_MSG_VF_UP;
	if (cptvf_send_msg_to_pf_timeout(cptvf, &mbx)) {
		dev_err(&pdev->dev, "PF didn't respond to UP msg\n");
		return -EBUSY;
	}

	return 0;
}

/*
 * Communicate to PF that VF is DOWN and running
 */
int cptvf_send_vf_down(struct cpt_vf *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	struct cpt_mbox mbx = {};

	mbx.msg = CPT_MSG_VF_DOWN;
	if (cptvf_send_msg_to_pf_timeout(cptvf, &mbx)) {
		dev_err(&pdev->dev, "PF didn't respond to DOWN msg\n");
		return -EBUSY;
	}

	return 0;
}
