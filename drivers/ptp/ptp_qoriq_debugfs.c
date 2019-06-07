// SPDX-License-Identifier: GPL-2.0+
/* Copyright 2019 NXP
 */
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/fsl/ptp_qoriq.h>

static int ptp_qoriq_fiper1_lpbk_get(void *data, u64 *val)
{
	struct ptp_qoriq *ptp_qoriq = data;
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u32 ctrl;

	ctrl = ptp_qoriq->read(&regs->ctrl_regs->tmr_ctrl);
	*val = ctrl & PP1L ? 1 : 0;

	return 0;
}

static int ptp_qoriq_fiper1_lpbk_set(void *data, u64 val)
{
	struct ptp_qoriq *ptp_qoriq = data;
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u32 ctrl;

	ctrl = ptp_qoriq->read(&regs->ctrl_regs->tmr_ctrl);
	if (val == 0)
		ctrl &= ~PP1L;
	else
		ctrl |= PP1L;

	ptp_qoriq->write(&regs->ctrl_regs->tmr_ctrl, ctrl);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(ptp_qoriq_fiper1_fops, ptp_qoriq_fiper1_lpbk_get,
			 ptp_qoriq_fiper1_lpbk_set, "%llu\n");

static int ptp_qoriq_fiper2_lpbk_get(void *data, u64 *val)
{
	struct ptp_qoriq *ptp_qoriq = data;
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u32 ctrl;

	ctrl = ptp_qoriq->read(&regs->ctrl_regs->tmr_ctrl);
	*val = ctrl & PP2L ? 1 : 0;

	return 0;
}

static int ptp_qoriq_fiper2_lpbk_set(void *data, u64 val)
{
	struct ptp_qoriq *ptp_qoriq = data;
	struct ptp_qoriq_registers *regs = &ptp_qoriq->regs;
	u32 ctrl;

	ctrl = ptp_qoriq->read(&regs->ctrl_regs->tmr_ctrl);
	if (val == 0)
		ctrl &= ~PP2L;
	else
		ctrl |= PP2L;

	ptp_qoriq->write(&regs->ctrl_regs->tmr_ctrl, ctrl);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(ptp_qoriq_fiper2_fops, ptp_qoriq_fiper2_lpbk_get,
			 ptp_qoriq_fiper2_lpbk_set, "%llu\n");

void ptp_qoriq_create_debugfs(struct ptp_qoriq *ptp_qoriq)
{
	struct dentry *root;

	root = debugfs_create_dir(dev_name(ptp_qoriq->dev), NULL);
	if (IS_ERR(root))
		return;
	if (!root)
		goto err_root;

	ptp_qoriq->debugfs_root = root;

	if (!debugfs_create_file_unsafe("fiper1-loopback", 0600, root,
					ptp_qoriq, &ptp_qoriq_fiper1_fops))
		goto err_node;
	if (!debugfs_create_file_unsafe("fiper2-loopback", 0600, root,
					ptp_qoriq, &ptp_qoriq_fiper2_fops))
		goto err_node;
	return;

err_node:
	debugfs_remove_recursive(root);
	ptp_qoriq->debugfs_root = NULL;
err_root:
	dev_err(ptp_qoriq->dev, "failed to initialize debugfs\n");
}

void ptp_qoriq_remove_debugfs(struct ptp_qoriq *ptp_qoriq)
{
	debugfs_remove_recursive(ptp_qoriq->debugfs_root);
	ptp_qoriq->debugfs_root = NULL;
}
