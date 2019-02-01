// SPDX-License-Identifier: GPL-2.0+
/* Copyright 2019 NXP
 */
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/fsl/ptp_qoriq.h>

static int ptp_qoriq_fiper1_lpbk_get(void *data, u64 *val)
{
	struct qoriq_ptp *qoriq_ptp = data;
	struct qoriq_ptp_registers *regs = &qoriq_ptp->regs;
	u32 ctrl;

	ctrl = qoriq_read(&regs->ctrl_regs->tmr_ctrl);
	*val = ctrl & PP1L ? 1 : 0;

	return 0;
}

static int ptp_qoriq_fiper1_lpbk_set(void *data, u64 val)
{
	struct qoriq_ptp *qoriq_ptp = data;
	struct qoriq_ptp_registers *regs = &qoriq_ptp->regs;
	u32 ctrl;

	ctrl = qoriq_read(&regs->ctrl_regs->tmr_ctrl);
	if (val == 0)
		ctrl &= ~PP1L;
	else
		ctrl |= PP1L;

	qoriq_write(&regs->ctrl_regs->tmr_ctrl, ctrl);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(ptp_qoriq_fiper1_fops, ptp_qoriq_fiper1_lpbk_get,
			 ptp_qoriq_fiper1_lpbk_set, "%llu\n");

static int ptp_qoriq_fiper2_lpbk_get(void *data, u64 *val)
{
	struct qoriq_ptp *qoriq_ptp = data;
	struct qoriq_ptp_registers *regs = &qoriq_ptp->regs;
	u32 ctrl;

	ctrl = qoriq_read(&regs->ctrl_regs->tmr_ctrl);
	*val = ctrl & PP2L ? 1 : 0;

	return 0;
}

static int ptp_qoriq_fiper2_lpbk_set(void *data, u64 val)
{
	struct qoriq_ptp *qoriq_ptp = data;
	struct qoriq_ptp_registers *regs = &qoriq_ptp->regs;
	u32 ctrl;

	ctrl = qoriq_read(&regs->ctrl_regs->tmr_ctrl);
	if (val == 0)
		ctrl &= ~PP2L;
	else
		ctrl |= PP2L;

	qoriq_write(&regs->ctrl_regs->tmr_ctrl, ctrl);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(ptp_qoriq_fiper2_fops, ptp_qoriq_fiper2_lpbk_get,
			 ptp_qoriq_fiper2_lpbk_set, "%llu\n");

void ptp_qoriq_create_debugfs(struct qoriq_ptp *qoriq_ptp)
{
	struct dentry *root;

	root = debugfs_create_dir(dev_name(qoriq_ptp->dev), NULL);
	if (IS_ERR(root))
		return;
	if (!root)
		goto err_root;

	qoriq_ptp->debugfs_root = root;

	if (!debugfs_create_file_unsafe("fiper1-loopback", 0600, root,
					qoriq_ptp, &ptp_qoriq_fiper1_fops))
		goto err_node;
	if (!debugfs_create_file_unsafe("fiper2-loopback", 0600, root,
					qoriq_ptp, &ptp_qoriq_fiper2_fops))
		goto err_node;
	return;

err_node:
	debugfs_remove_recursive(root);
	qoriq_ptp->debugfs_root = NULL;
err_root:
	dev_err(qoriq_ptp->dev, "failed to initialize debugfs\n");
}

void ptp_qoriq_remove_debugfs(struct qoriq_ptp *qoriq_ptp)
{
	debugfs_remove_recursive(qoriq_ptp->debugfs_root);
	qoriq_ptp->debugfs_root = NULL;
}
