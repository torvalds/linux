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
	ret = wait_for_mbox_ready(proc_priv);
	if (ret)
		return ret;

	writel(data, (proc_priv->mmio_base + MBOX_OFFSET_DATA));
	/* Write command register */
	reg_data = BIT_ULL(MBOX_BUSY_BIT) | id;
	writel(reg_data, (proc_priv->mmio_base + MBOX_OFFSET_INTERFACE));

	return wait_for_mbox_ready(proc_priv);
}

static int send_mbox_read_cmd(struct pci_dev *pdev, u16 id, u64 *resp)
{
	struct proc_thermal_device *proc_priv;
	u32 reg_data;
	int ret;

	proc_priv = pci_get_drvdata(pdev);
	ret = wait_for_mbox_ready(proc_priv);
	if (ret)
		return ret;

	/* Write command register */
	reg_data = BIT_ULL(MBOX_BUSY_BIT) | id;
	writel(reg_data, (proc_priv->mmio_base + MBOX_OFFSET_INTERFACE));

	ret = wait_for_mbox_ready(proc_priv);
	if (ret)
		return ret;

	if (id == MBOX_CMD_WORKLOAD_TYPE_READ)
		*resp = readl(proc_priv->mmio_base + MBOX_OFFSET_DATA);
	else
		*resp = readq(proc_priv->mmio_base + MBOX_OFFSET_DATA);

	return 0;
}

int processor_thermal_send_mbox_read_cmd(struct pci_dev *pdev, u16 id, u64 *resp)
{
	int ret;

	mutex_lock(&mbox_lock);
	ret = send_mbox_read_cmd(pdev, id, resp);
	mutex_unlock(&mbox_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(processor_thermal_send_mbox_read_cmd, INT340X_THERMAL);

int processor_thermal_send_mbox_write_cmd(struct pci_dev *pdev, u16 id, u32 data)
{
	int ret;

	mutex_lock(&mbox_lock);
	ret = send_mbox_write_cmd(pdev, id, data);
	mutex_unlock(&mbox_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(processor_thermal_send_mbox_write_cmd, INT340X_THERMAL);

#define MBOX_CAMARILLO_RD_INTR_CONFIG	0x1E
#define MBOX_CAMARILLO_WR_INTR_CONFIG	0x1F
#define WLT_TW_MASK			GENMASK_ULL(30, 24)
#define SOC_PREDICTION_TW_SHIFT		24

int processor_thermal_mbox_interrupt_config(struct pci_dev *pdev, bool enable,
					    int enable_bit, int time_window)
{
	u64 data;
	int ret;

	if (!pdev)
		return -ENODEV;

	mutex_lock(&mbox_lock);

	/* Do read modify write for MBOX_CAMARILLO_RD_INTR_CONFIG */

	ret = send_mbox_read_cmd(pdev, MBOX_CAMARILLO_RD_INTR_CONFIG,  &data);
	if (ret) {
		dev_err(&pdev->dev, "MBOX_CAMARILLO_RD_INTR_CONFIG failed\n");
		goto unlock;
	}

	if (time_window >= 0) {
		data &= ~WLT_TW_MASK;

		/* Program notification delay */
		data |= ((u64)time_window << SOC_PREDICTION_TW_SHIFT) & WLT_TW_MASK;
	}

	if (enable)
		data |= BIT(enable_bit);
	else
		data &= ~BIT(enable_bit);

	ret = send_mbox_write_cmd(pdev, MBOX_CAMARILLO_WR_INTR_CONFIG, data);
	if (ret)
		dev_err(&pdev->dev, "MBOX_CAMARILLO_WR_INTR_CONFIG failed\n");

unlock:
	mutex_unlock(&mbox_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(processor_thermal_mbox_interrupt_config, INT340X_THERMAL);

MODULE_LICENSE("GPL v2");
