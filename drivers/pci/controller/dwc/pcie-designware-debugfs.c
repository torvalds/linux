// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe controller debugfs driver
 *
 * Copyright (C) 2025 Samsung Electronics Co., Ltd.
 *		 http://www.samsung.com
 *
 * Author: Shradha Todi <shradha.t@samsung.com>
 */

#include <linux/debugfs.h>

#include "pcie-designware.h"

#define SD_STATUS_L1LANE_REG		0xb0
#define PIPE_RXVALID			BIT(18)
#define PIPE_DETECT_LANE		BIT(17)
#define LANE_SELECT			GENMASK(3, 0)

#define DWC_DEBUGFS_BUF_MAX		128

/**
 * struct dwc_pcie_rasdes_info - Stores controller common information
 * @ras_cap_offset: RAS DES vendor specific extended capability offset
 * @reg_event_lock: Mutex used for RAS DES shadow event registers
 *
 * Any parameter constant to all files of the debugfs hierarchy for a single
 * controller will be stored in this struct. It is allocated and assigned to
 * controller specific struct dw_pcie during initialization.
 */
struct dwc_pcie_rasdes_info {
	u32 ras_cap_offset;
	struct mutex reg_event_lock;
};

static ssize_t lane_detect_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct dw_pcie *pci = file->private_data;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	char debugfs_buf[DWC_DEBUGFS_BUF_MAX];
	ssize_t pos;
	u32 val;

	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + SD_STATUS_L1LANE_REG);
	val = FIELD_GET(PIPE_DETECT_LANE, val);
	if (val)
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "Lane Detected\n");
	else
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "Lane Undetected\n");

	return simple_read_from_buffer(buf, count, ppos, debugfs_buf, pos);
}

static ssize_t lane_detect_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct dw_pcie *pci = file->private_data;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	u32 lane, val;

	val = kstrtou32_from_user(buf, count, 0, &lane);
	if (val)
		return val;

	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + SD_STATUS_L1LANE_REG);
	val &= ~(LANE_SELECT);
	val |= FIELD_PREP(LANE_SELECT, lane);
	dw_pcie_writel_dbi(pci, rinfo->ras_cap_offset + SD_STATUS_L1LANE_REG, val);

	return count;
}

static ssize_t rx_valid_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct dw_pcie *pci = file->private_data;
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;
	char debugfs_buf[DWC_DEBUGFS_BUF_MAX];
	ssize_t pos;
	u32 val;

	val = dw_pcie_readl_dbi(pci, rinfo->ras_cap_offset + SD_STATUS_L1LANE_REG);
	val = FIELD_GET(PIPE_RXVALID, val);
	if (val)
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "RX Valid\n");
	else
		pos = scnprintf(debugfs_buf, DWC_DEBUGFS_BUF_MAX, "RX Invalid\n");

	return simple_read_from_buffer(buf, count, ppos, debugfs_buf, pos);
}

static ssize_t rx_valid_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	return lane_detect_write(file, buf, count, ppos);
}

#define dwc_debugfs_create(name)			\
debugfs_create_file(#name, 0644, rasdes_debug, pci,	\
			&dbg_ ## name ## _fops)

#define DWC_DEBUGFS_FOPS(name)					\
static const struct file_operations dbg_ ## name ## _fops = {	\
	.open = simple_open,				\
	.read = name ## _read,				\
	.write = name ## _write				\
}

DWC_DEBUGFS_FOPS(lane_detect);
DWC_DEBUGFS_FOPS(rx_valid);

static void dwc_pcie_rasdes_debugfs_deinit(struct dw_pcie *pci)
{
	struct dwc_pcie_rasdes_info *rinfo = pci->debugfs->rasdes_info;

	mutex_destroy(&rinfo->reg_event_lock);
}

static int dwc_pcie_rasdes_debugfs_init(struct dw_pcie *pci, struct dentry *dir)
{
	struct dentry *rasdes_debug;
	struct dwc_pcie_rasdes_info *rasdes_info;
	struct device *dev = pci->dev;
	int ras_cap;

	/*
	 * If a given SoC has no RAS DES capability, the following call is
	 * bound to return an error, breaking some existing platforms. So,
	 * return 0 here, as this is not necessarily an error.
	 */
	ras_cap = dw_pcie_find_rasdes_capability(pci);
	if (!ras_cap) {
		dev_dbg(dev, "no RAS DES capability available\n");
		return 0;
	}

	rasdes_info = devm_kzalloc(dev, sizeof(*rasdes_info), GFP_KERNEL);
	if (!rasdes_info)
		return -ENOMEM;

	/* Create subdirectories for Debug, Error Injection, Statistics. */
	rasdes_debug = debugfs_create_dir("rasdes_debug", dir);

	mutex_init(&rasdes_info->reg_event_lock);
	rasdes_info->ras_cap_offset = ras_cap;
	pci->debugfs->rasdes_info = rasdes_info;

	/* Create debugfs files for Debug subdirectory. */
	dwc_debugfs_create(lane_detect);
	dwc_debugfs_create(rx_valid);

	return 0;
}

void dwc_pcie_debugfs_deinit(struct dw_pcie *pci)
{
	if (!pci->debugfs)
		return;

	dwc_pcie_rasdes_debugfs_deinit(pci);
	debugfs_remove_recursive(pci->debugfs->debug_dir);
}

void dwc_pcie_debugfs_init(struct dw_pcie *pci)
{
	char dirname[DWC_DEBUGFS_BUF_MAX];
	struct device *dev = pci->dev;
	struct debugfs_info *debugfs;
	struct dentry *dir;
	int err;

	/* Create main directory for each platform driver. */
	snprintf(dirname, DWC_DEBUGFS_BUF_MAX, "dwc_pcie_%s", dev_name(dev));
	dir = debugfs_create_dir(dirname, NULL);
	debugfs = devm_kzalloc(dev, sizeof(*debugfs), GFP_KERNEL);
	if (!debugfs)
		return;

	debugfs->debug_dir = dir;
	pci->debugfs = debugfs;
	err = dwc_pcie_rasdes_debugfs_init(pci, dir);
	if (err)
		dev_err(dev, "failed to initialize RAS DES debugfs, err=%d\n",
			err);
}
