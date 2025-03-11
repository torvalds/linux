// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright (c) 2024, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * Description: Debugfs component of the bnxt_re driver
 */

#include <linux/debugfs.h>
#include <linux/pci.h>
#include <rdma/ib_addr.h>

#include "bnxt_ulp.h"
#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_sp.h"
#include "qplib_fp.h"
#include "qplib_rcfw.h"
#include "bnxt_re.h"
#include "ib_verbs.h"
#include "debugfs.h"

static struct dentry *bnxt_re_debugfs_root;

static inline const char *bnxt_re_qp_state_str(u8 state)
{
	switch (state) {
	case CMDQ_MODIFY_QP_NEW_STATE_RESET:
		return "RST";
	case CMDQ_MODIFY_QP_NEW_STATE_INIT:
		return "INIT";
	case CMDQ_MODIFY_QP_NEW_STATE_RTR:
		return "RTR";
	case CMDQ_MODIFY_QP_NEW_STATE_RTS:
		return "RTS";
	case CMDQ_MODIFY_QP_NEW_STATE_SQE:
		return "SQER";
	case CMDQ_MODIFY_QP_NEW_STATE_SQD:
		return "SQD";
	case CMDQ_MODIFY_QP_NEW_STATE_ERR:
		return "ERR";
	default:
		return "Invalid QP state";
	}
}

static inline const char *bnxt_re_qp_type_str(u8 type)
{
	switch (type) {
	case CMDQ_CREATE_QP1_TYPE_GSI: return "QP1";
	case CMDQ_CREATE_QP_TYPE_GSI: return "QP1";
	case CMDQ_CREATE_QP_TYPE_RC: return "RC";
	case CMDQ_CREATE_QP_TYPE_UD: return "UD";
	case CMDQ_CREATE_QP_TYPE_RAW_ETHERTYPE: return "RAW_ETHERTYPE";
	default: return "Invalid transport type";
	}
}

static ssize_t qp_info_read(struct file *filep,
			    char __user *buffer,
			    size_t count, loff_t *ppos)
{
	struct bnxt_re_qp *qp = filep->private_data;
	char *buf;
	int len;

	if (*ppos)
		return 0;

	buf = kasprintf(GFP_KERNEL,
			"QPN\t\t: %d\n"
			"transport\t: %s\n"
			"state\t\t: %s\n"
			"mtu\t\t: %d\n"
			"timeout\t\t: %d\n"
			"remote QPN\t: %d\n",
			qp->qplib_qp.id,
			bnxt_re_qp_type_str(qp->qplib_qp.type),
			bnxt_re_qp_state_str(qp->qplib_qp.state),
			qp->qplib_qp.mtu,
			qp->qplib_qp.timeout,
			qp->qplib_qp.dest_qpn);
	if (!buf)
		return -ENOMEM;
	if (count < strlen(buf)) {
		kfree(buf);
		return -ENOSPC;
	}
	len = simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
	kfree(buf);
	return len;
}

static const struct file_operations debugfs_qp_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = qp_info_read,
};

void bnxt_re_debug_add_qpinfo(struct bnxt_re_dev *rdev, struct bnxt_re_qp *qp)
{
	char resn[32];

	sprintf(resn, "0x%x", qp->qplib_qp.id);
	qp->dentry = debugfs_create_file(resn, 0400, rdev->qp_debugfs, qp, &debugfs_qp_fops);
}

void bnxt_re_debug_rem_qpinfo(struct bnxt_re_dev *rdev, struct bnxt_re_qp *qp)
{
	debugfs_remove(qp->dentry);
}

void bnxt_re_debugfs_add_pdev(struct bnxt_re_dev *rdev)
{
	struct pci_dev *pdev = rdev->en_dev->pdev;

	rdev->dbg_root = debugfs_create_dir(dev_name(&pdev->dev), bnxt_re_debugfs_root);

	rdev->qp_debugfs = debugfs_create_dir("QPs", rdev->dbg_root);
}

void bnxt_re_debugfs_rem_pdev(struct bnxt_re_dev *rdev)
{
	debugfs_remove_recursive(rdev->qp_debugfs);

	debugfs_remove_recursive(rdev->dbg_root);
	rdev->dbg_root = NULL;
}

void bnxt_re_register_debugfs(void)
{
	bnxt_re_debugfs_root = debugfs_create_dir("bnxt_re", NULL);
}

void bnxt_re_unregister_debugfs(void)
{
	debugfs_remove(bnxt_re_debugfs_root);
}
