// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor thermal device RFIM control
 * Copyright (c) 2020, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "processor_thermal_device.h"

struct mmio_reg {
	int read_only;
	u32 offset;
	int bits;
	u16 mask;
	u16 shift;
};

/* These will represent sysfs attribute names */
static const char * const fivr_strings[] = {
	"vco_ref_code_lo",
	"vco_ref_code_hi",
	"spread_spectrum_pct",
	"spread_spectrum_clk_enable",
	"rfi_vco_ref_code",
	"fivr_fffc_rev",
	NULL
};

static const struct mmio_reg tgl_fivr_mmio_regs[] = {
	{ 0, 0x5A18, 3, 0x7, 12}, /* vco_ref_code_lo */
	{ 0, 0x5A18, 8, 0xFF, 16}, /* vco_ref_code_hi */
	{ 0, 0x5A08, 8, 0xFF, 0}, /* spread_spectrum_pct */
	{ 0, 0x5A08, 1, 0x1, 8}, /* spread_spectrum_clk_enable */
	{ 1, 0x5A10, 12, 0xFFF, 0}, /* rfi_vco_ref_code */
	{ 1, 0x5A14, 2, 0x3, 1}, /* fivr_fffc_rev */
};

/* These will represent sysfs attribute names */
static const char * const dvfs_strings[] = {
	"rfi_restriction_run_busy",
	"rfi_restriction_err_code",
	"rfi_restriction_data_rate",
	"rfi_restriction_data_rate_base",
	"ddr_data_rate_point_0",
	"ddr_data_rate_point_1",
	"ddr_data_rate_point_2",
	"ddr_data_rate_point_3",
	"rfi_disable",
	NULL
};

static const struct mmio_reg adl_dvfs_mmio_regs[] = {
	{ 0, 0x5A38, 1, 0x1, 31}, /* rfi_restriction_run_busy */
	{ 0, 0x5A38, 7, 0x7F, 24}, /* rfi_restriction_err_code */
	{ 0, 0x5A38, 8, 0xFF, 16}, /* rfi_restriction_data_rate */
	{ 0, 0x5A38, 16, 0xFFFF, 0}, /* rfi_restriction_data_rate_base */
	{ 0, 0x5A30, 10, 0x3FF, 0}, /* ddr_data_rate_point_0 */
	{ 0, 0x5A30, 10, 0x3FF, 10}, /* ddr_data_rate_point_1 */
	{ 0, 0x5A30, 10, 0x3FF, 20}, /* ddr_data_rate_point_2 */
	{ 0, 0x5A30, 10, 0x3FF, 30}, /* ddr_data_rate_point_3 */
	{ 0, 0x5A40, 1, 0x1, 0}, /* rfi_disable */
};

#define RFIM_SHOW(suffix, table)\
static ssize_t suffix##_show(struct device *dev,\
			      struct device_attribute *attr,\
			      char *buf)\
{\
	struct proc_thermal_device *proc_priv;\
	struct pci_dev *pdev = to_pci_dev(dev);\
	const struct mmio_reg *mmio_regs;\
	const char **match_strs;\
	u32 reg_val;\
	int ret;\
\
	proc_priv = pci_get_drvdata(pdev);\
	if (table) {\
		match_strs = (const char **)dvfs_strings;\
		mmio_regs = adl_dvfs_mmio_regs;\
	} else { \
		match_strs = (const char **)fivr_strings;\
		mmio_regs = tgl_fivr_mmio_regs;\
	} \
	\
	ret = match_string(match_strs, -1, attr->attr.name);\
	if (ret < 0)\
		return ret;\
	reg_val = readl((void __iomem *) (proc_priv->mmio_base + mmio_regs[ret].offset));\
	ret = (reg_val >> mmio_regs[ret].shift) & mmio_regs[ret].mask;\
	return sprintf(buf, "%u\n", ret);\
}

#define RFIM_STORE(suffix, table)\
static ssize_t suffix##_store(struct device *dev,\
			       struct device_attribute *attr,\
			       const char *buf, size_t count)\
{\
	struct proc_thermal_device *proc_priv;\
	struct pci_dev *pdev = to_pci_dev(dev);\
	unsigned int input;\
	const char **match_strs;\
	const struct mmio_reg *mmio_regs;\
	int ret, err;\
	u32 reg_val;\
	u32 mask;\
\
	proc_priv = pci_get_drvdata(pdev);\
	if (table) {\
		match_strs = (const char **)dvfs_strings;\
		mmio_regs = adl_dvfs_mmio_regs;\
	} else { \
		match_strs = (const char **)fivr_strings;\
		mmio_regs = tgl_fivr_mmio_regs;\
	} \
	\
	ret = match_string(match_strs, -1, attr->attr.name);\
	if (ret < 0)\
		return ret;\
	if (mmio_regs[ret].read_only)\
		return -EPERM;\
	err = kstrtouint(buf, 10, &input);\
	if (err)\
		return err;\
	mask = GENMASK(mmio_regs[ret].shift + mmio_regs[ret].bits - 1, mmio_regs[ret].shift);\
	reg_val = readl((void __iomem *) (proc_priv->mmio_base + mmio_regs[ret].offset));\
	reg_val &= ~mask;\
	reg_val |= (input << mmio_regs[ret].shift);\
	writel(reg_val, (void __iomem *) (proc_priv->mmio_base + mmio_regs[ret].offset));\
	return count;\
}

RFIM_SHOW(vco_ref_code_lo, 0)
RFIM_SHOW(vco_ref_code_hi, 0)
RFIM_SHOW(spread_spectrum_pct, 0)
RFIM_SHOW(spread_spectrum_clk_enable, 0)
RFIM_SHOW(rfi_vco_ref_code, 0)
RFIM_SHOW(fivr_fffc_rev, 0)

RFIM_STORE(vco_ref_code_lo, 0)
RFIM_STORE(vco_ref_code_hi, 0)
RFIM_STORE(spread_spectrum_pct, 0)
RFIM_STORE(spread_spectrum_clk_enable, 0)
RFIM_STORE(rfi_vco_ref_code, 0)
RFIM_STORE(fivr_fffc_rev, 0)

static DEVICE_ATTR_RW(vco_ref_code_lo);
static DEVICE_ATTR_RW(vco_ref_code_hi);
static DEVICE_ATTR_RW(spread_spectrum_pct);
static DEVICE_ATTR_RW(spread_spectrum_clk_enable);
static DEVICE_ATTR_RW(rfi_vco_ref_code);
static DEVICE_ATTR_RW(fivr_fffc_rev);

static struct attribute *fivr_attrs[] = {
	&dev_attr_vco_ref_code_lo.attr,
	&dev_attr_vco_ref_code_hi.attr,
	&dev_attr_spread_spectrum_pct.attr,
	&dev_attr_spread_spectrum_clk_enable.attr,
	&dev_attr_rfi_vco_ref_code.attr,
	&dev_attr_fivr_fffc_rev.attr,
	NULL
};

static const struct attribute_group fivr_attribute_group = {
	.attrs = fivr_attrs,
	.name = "fivr"
};

RFIM_SHOW(rfi_restriction_run_busy, 1)
RFIM_SHOW(rfi_restriction_err_code, 1)
RFIM_SHOW(rfi_restriction_data_rate, 1)
RFIM_SHOW(ddr_data_rate_point_0, 1)
RFIM_SHOW(ddr_data_rate_point_1, 1)
RFIM_SHOW(ddr_data_rate_point_2, 1)
RFIM_SHOW(ddr_data_rate_point_3, 1)
RFIM_SHOW(rfi_disable, 1)

RFIM_STORE(rfi_restriction_run_busy, 1)
RFIM_STORE(rfi_restriction_err_code, 1)
RFIM_STORE(rfi_restriction_data_rate, 1)
RFIM_STORE(rfi_disable, 1)

static DEVICE_ATTR_RW(rfi_restriction_run_busy);
static DEVICE_ATTR_RW(rfi_restriction_err_code);
static DEVICE_ATTR_RW(rfi_restriction_data_rate);
static DEVICE_ATTR_RO(ddr_data_rate_point_0);
static DEVICE_ATTR_RO(ddr_data_rate_point_1);
static DEVICE_ATTR_RO(ddr_data_rate_point_2);
static DEVICE_ATTR_RO(ddr_data_rate_point_3);
static DEVICE_ATTR_RW(rfi_disable);

static ssize_t rfi_restriction_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	u16 cmd_id = 0x0008;
	u64 cmd_resp;
	u32 input;
	int ret;

	ret = kstrtou32(buf, 10, &input);
	if (ret)
		return ret;

	ret = processor_thermal_send_mbox_cmd(to_pci_dev(dev), cmd_id, input, &cmd_resp);
	if (ret)
		return ret;

	return count;
}

static ssize_t rfi_restriction_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	u16 cmd_id = 0x0007;
	u64 cmd_resp;
	int ret;

	ret = processor_thermal_send_mbox_cmd(to_pci_dev(dev), cmd_id, 0, &cmd_resp);
	if (ret)
		return ret;

	return sprintf(buf, "%llu\n", cmd_resp);
}

static ssize_t ddr_data_rate_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	u16 cmd_id = 0x0107;
	u64 cmd_resp;
	int ret;

	ret = processor_thermal_send_mbox_cmd(to_pci_dev(dev), cmd_id, 0, &cmd_resp);
	if (ret)
		return ret;

	return sprintf(buf, "%llu\n", cmd_resp);
}

static DEVICE_ATTR_RW(rfi_restriction);
static DEVICE_ATTR_RO(ddr_data_rate);

static struct attribute *dvfs_attrs[] = {
	&dev_attr_rfi_restriction_run_busy.attr,
	&dev_attr_rfi_restriction_err_code.attr,
	&dev_attr_rfi_restriction_data_rate.attr,
	&dev_attr_ddr_data_rate_point_0.attr,
	&dev_attr_ddr_data_rate_point_1.attr,
	&dev_attr_ddr_data_rate_point_2.attr,
	&dev_attr_ddr_data_rate_point_3.attr,
	&dev_attr_rfi_disable.attr,
	&dev_attr_ddr_data_rate.attr,
	&dev_attr_rfi_restriction.attr,
	NULL
};

static const struct attribute_group dvfs_attribute_group = {
	.attrs = dvfs_attrs,
	.name = "dvfs"
};

int proc_thermal_rfim_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	int ret;

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_FIVR) {
		ret = sysfs_create_group(&pdev->dev.kobj, &fivr_attribute_group);
		if (ret)
			return ret;
	}

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_DVFS) {
		ret = sysfs_create_group(&pdev->dev.kobj, &dvfs_attribute_group);
		if (ret && proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_FIVR) {
			sysfs_remove_group(&pdev->dev.kobj, &fivr_attribute_group);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(proc_thermal_rfim_add);

void proc_thermal_rfim_remove(struct pci_dev *pdev)
{
	struct proc_thermal_device *proc_priv = pci_get_drvdata(pdev);

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_FIVR)
		sysfs_remove_group(&pdev->dev.kobj, &fivr_attribute_group);

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_DVFS)
		sysfs_remove_group(&pdev->dev.kobj, &dvfs_attribute_group);
}
EXPORT_SYMBOL_GPL(proc_thermal_rfim_remove);

MODULE_LICENSE("GPL v2");
