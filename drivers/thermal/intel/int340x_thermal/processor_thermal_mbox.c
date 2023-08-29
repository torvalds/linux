// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor thermal device mailbox driver for Workload type hints
 * Copyright (c) 2020, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include "processor_thermal_device.h"

#define MBOX_OFFSET_DATA		0x5810
#define MBOX_OFFSET_INTERFACE		0x5818

#define MBOX_BUSY_BIT			31
#define MBOX_RETRY_COUNT		100

static DEFINE_MUTEX(mbox_lock);

static int wait_for_mbox_ready(struct proc_thermal_device *proc_priv)
{
	u32 retries, data;
	int ret;

	/* Poll for rb bit == 0 */
	retries = MBOX_RETRY_COUNT;
	do {
		data = readl(proc_priv->mmio_base + MBOX_OFFSET_INTERFACE);
		if (data & BIT_ULL(MBOX_BUSY_BIT)) {
			ret = -EBUSY;
			continue;
		}
		ret = 0;
		break;
	} while (--retries);

	return ret;
}

static int send_mbox_write_cmd(struct pci_dev *pdev, u16 id, u32 data)
{
	struct proc_thermal_device *proc_priv;
	u32 reg_data;
	int ret;

	proc_priv = pci_get_drvdata(pdev);

	mutex_lock(&mbox_lock);

	ret = wait_for_mbox_ready(proc_priv);
	if (ret)
		goto unlock_mbox;

	writel(data, (proc_priv->mmio_base + MBOX_OFFSET_DATA));
	/* Write command register */
	reg_data = BIT_ULL(MBOX_BUSY_BIT) | id;
	writel(reg_data, (proc_priv->mmio_base + MBOX_OFFSET_INTERFACE));

	ret = wait_for_mbox_ready(proc_priv);

unlock_mbox:
	mutex_unlock(&mbox_lock);
	return ret;
}

static int send_mbox_read_cmd(struct pci_dev *pdev, u16 id, u64 *resp)
{
	struct proc_thermal_device *proc_priv;
	u32 reg_data;
	int ret;

	proc_priv = pci_get_drvdata(pdev);

	mutex_lock(&mbox_lock);

	ret = wait_for_mbox_ready(proc_priv);
	if (ret)
		goto unlock_mbox;

	/* Write command register */
	reg_data = BIT_ULL(MBOX_BUSY_BIT) | id;
	writel(reg_data, (proc_priv->mmio_base + MBOX_OFFSET_INTERFACE));

	ret = wait_for_mbox_ready(proc_priv);
	if (ret)
		goto unlock_mbox;

	if (id == MBOX_CMD_WORKLOAD_TYPE_READ)
		*resp = readl(proc_priv->mmio_base + MBOX_OFFSET_DATA);
	else
		*resp = readq(proc_priv->mmio_base + MBOX_OFFSET_DATA);

unlock_mbox:
	mutex_unlock(&mbox_lock);
	return ret;
}

int processor_thermal_send_mbox_read_cmd(struct pci_dev *pdev, u16 id, u64 *resp)
{
	return send_mbox_read_cmd(pdev, id, resp);
}
EXPORT_SYMBOL_NS_GPL(processor_thermal_send_mbox_read_cmd, INT340X_THERMAL);

int processor_thermal_send_mbox_write_cmd(struct pci_dev *pdev, u16 id, u32 data)
{
	return send_mbox_write_cmd(pdev, id, data);
}
EXPORT_SYMBOL_NS_GPL(processor_thermal_send_mbox_write_cmd, INT340X_THERMAL);

MODULE_LICENSE("GPL v2");
