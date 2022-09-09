// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/pci.h>

static ssize_t clk_max_freq_mhz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, hdev->asic_prop.clk_pll_index, false);
	if (value < 0)
		return value;

	hdev->asic_prop.max_freq_value = value;

	return sprintf(buf, "%lu\n", (value / 1000 / 1000));
}

static ssize_t clk_max_freq_mhz_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	int rc;
	u64 value;

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto fail;
	}

	rc = kstrtoull(buf, 0, &value);
	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hdev->asic_prop.max_freq_value = value * 1000 * 1000;

	hl_fw_set_frequency(hdev, hdev->asic_prop.clk_pll_index, hdev->asic_prop.max_freq_value);

fail:
	return count;
}

static ssize_t clk_cur_freq_mhz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, hdev->asic_prop.clk_pll_index, true);
	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", (value / 1000 / 1000));
}

static DEVICE_ATTR_RW(clk_max_freq_mhz);
static DEVICE_ATTR_RO(clk_cur_freq_mhz);

static struct attribute *hl_dev_clk_attrs[] = {
	&dev_attr_clk_max_freq_mhz.attr,
	&dev_attr_clk_cur_freq_mhz.attr,
	NULL,
};

static ssize_t vrm_ver_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct cpucp_info *cpucp_info;

	cpucp_info = &hdev->asic_prop.cpucp_info;

	if (cpucp_info->infineon_second_stage_version)
		return sprintf(buf, "%#04x %#04x\n", le32_to_cpu(cpucp_info->infineon_version),
				le32_to_cpu(cpucp_info->infineon_second_stage_version));
	else
		return sprintf(buf, "%#04x\n", le32_to_cpu(cpucp_info->infineon_version));
}

static DEVICE_ATTR_RO(vrm_ver);

static struct attribute *hl_dev_vrm_attrs[] = {
	&dev_attr_vrm_ver.attr,
	NULL,
};

static ssize_t uboot_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.uboot_ver);
}

static ssize_t armcp_kernel_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s", hdev->asic_prop.cpucp_info.kernel_version);
}

static ssize_t armcp_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.cpucp_info.cpucp_version);
}

static ssize_t cpld_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "0x%08x\n",
			le32_to_cpu(hdev->asic_prop.cpucp_info.cpld_version));
}

static ssize_t cpucp_kernel_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s", hdev->asic_prop.cpucp_info.kernel_version);
}

static ssize_t cpucp_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.cpucp_info.cpucp_version);
}

static ssize_t fuse_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.cpucp_info.fuse_version);
}

static ssize_t thermal_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s", hdev->asic_prop.cpucp_info.thermal_version);
}

static ssize_t fw_os_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s", hdev->asic_prop.cpucp_info.fw_os_version);
}

static ssize_t preboot_btl_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.preboot_ver);
}

static ssize_t soft_reset_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;
	int rc;

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto out;
	}

	if (!hdev->asic_prop.allow_inference_soft_reset) {
		dev_err(hdev->dev, "Device does not support inference soft-reset\n");
		goto out;
	}

	dev_warn(hdev->dev, "Inference Soft-Reset requested through sysfs\n");

	hl_device_reset(hdev, 0);

out:
	return count;
}

static ssize_t hard_reset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;
	int rc;

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto out;
	}

	dev_warn(hdev->dev, "Hard-Reset requested through sysfs\n");

	hl_device_reset(hdev, HL_DRV_RESET_HARD);

out:
	return count;
}

static ssize_t device_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	char *str;

	switch (hdev->asic_type) {
	case ASIC_GOYA:
		str = "GOYA";
		break;
	case ASIC_GAUDI:
		str = "GAUDI";
		break;
	case ASIC_GAUDI_SEC:
		str = "GAUDI SEC";
		break;
	case ASIC_GAUDI2:
		str = "GAUDI2";
		break;
	case ASIC_GAUDI2_SEC:
		str = "GAUDI2 SEC";
		break;
	default:
		dev_err(hdev->dev, "Unrecognized ASIC type %d\n",
				hdev->asic_type);
		return -EINVAL;
	}

	return sprintf(buf, "%s\n", str);
}

static ssize_t pci_addr_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%04x:%02x:%02x.%x\n",
			pci_domain_nr(hdev->pdev->bus),
			hdev->pdev->bus->number,
			PCI_SLOT(hdev->pdev->devfn),
			PCI_FUNC(hdev->pdev->devfn));
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	char str[HL_STR_MAX];

	strscpy(str, hdev->status[hl_device_status(hdev)], HL_STR_MAX);

	/* use uppercase for backward compatibility */
	str[0] = 'A' + (str[0] - 'a');

	return sprintf(buf, "%s\n", str);
}

static ssize_t soft_reset_cnt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hdev->reset_info.compute_reset_cnt);
}

static ssize_t hard_reset_cnt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hdev->reset_info.hard_reset_cnt);
}

static ssize_t max_power_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long val;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	val = hl_fw_get_max_power(hdev);
	if (val < 0)
		return val;

	return sprintf(buf, "%lu\n", val);
}

static ssize_t max_power_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	unsigned long value;
	int rc;

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto out;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto out;
	}

	hdev->max_power = value;
	hl_fw_set_max_power(hdev);

out:
	return count;
}

static ssize_t eeprom_read_handler(struct file *filp, struct kobject *kobj,
			struct bin_attribute *attr, char *buf, loff_t offset,
			size_t max_size)
{
	struct device *dev = kobj_to_dev(kobj);
	struct hl_device *hdev = dev_get_drvdata(dev);
	char *data;
	int rc;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	if (!max_size)
		return -EINVAL;

	data = kzalloc(max_size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	rc = hdev->asic_funcs->get_eeprom_data(hdev, data, max_size);
	if (rc)
		goto out;

	memcpy(buf, data, max_size);

out:
	kfree(data);

	return max_size;
}

static DEVICE_ATTR_RO(armcp_kernel_ver);
static DEVICE_ATTR_RO(armcp_ver);
static DEVICE_ATTR_RO(cpld_ver);
static DEVICE_ATTR_RO(cpucp_kernel_ver);
static DEVICE_ATTR_RO(cpucp_ver);
static DEVICE_ATTR_RO(device_type);
static DEVICE_ATTR_RO(fuse_ver);
static DEVICE_ATTR_WO(hard_reset);
static DEVICE_ATTR_RO(hard_reset_cnt);
static DEVICE_ATTR_RW(max_power);
static DEVICE_ATTR_RO(pci_addr);
static DEVICE_ATTR_RO(preboot_btl_ver);
static DEVICE_ATTR_WO(soft_reset);
static DEVICE_ATTR_RO(soft_reset_cnt);
static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_RO(thermal_ver);
static DEVICE_ATTR_RO(uboot_ver);
static DEVICE_ATTR_RO(fw_os_ver);

static struct bin_attribute bin_attr_eeprom = {
	.attr = {.name = "eeprom", .mode = (0444)},
	.size = PAGE_SIZE,
	.read = eeprom_read_handler
};

static struct attribute *hl_dev_attrs[] = {
	&dev_attr_armcp_kernel_ver.attr,
	&dev_attr_armcp_ver.attr,
	&dev_attr_cpld_ver.attr,
	&dev_attr_cpucp_kernel_ver.attr,
	&dev_attr_cpucp_ver.attr,
	&dev_attr_device_type.attr,
	&dev_attr_fuse_ver.attr,
	&dev_attr_hard_reset.attr,
	&dev_attr_hard_reset_cnt.attr,
	&dev_attr_max_power.attr,
	&dev_attr_pci_addr.attr,
	&dev_attr_preboot_btl_ver.attr,
	&dev_attr_status.attr,
	&dev_attr_thermal_ver.attr,
	&dev_attr_uboot_ver.attr,
	&dev_attr_fw_os_ver.attr,
	NULL,
};

static struct bin_attribute *hl_dev_bin_attrs[] = {
	&bin_attr_eeprom,
	NULL
};

static struct attribute_group hl_dev_attr_group = {
	.attrs = hl_dev_attrs,
	.bin_attrs = hl_dev_bin_attrs,
};

static struct attribute_group hl_dev_clks_attr_group;
static struct attribute_group hl_dev_vrm_attr_group;

static const struct attribute_group *hl_dev_attr_groups[] = {
	&hl_dev_attr_group,
	&hl_dev_clks_attr_group,
	&hl_dev_vrm_attr_group,
	NULL,
};

static struct attribute *hl_dev_inference_attrs[] = {
	&dev_attr_soft_reset.attr,
	&dev_attr_soft_reset_cnt.attr,
	NULL,
};

static struct attribute_group hl_dev_inference_attr_group = {
	.attrs = hl_dev_inference_attrs,
};

static const struct attribute_group *hl_dev_inference_attr_groups[] = {
	&hl_dev_inference_attr_group,
	NULL,
};

void hl_sysfs_add_dev_clk_attr(struct hl_device *hdev, struct attribute_group *dev_clk_attr_grp)
{
	dev_clk_attr_grp->attrs = hl_dev_clk_attrs;
}

void hl_sysfs_add_dev_vrm_attr(struct hl_device *hdev, struct attribute_group *dev_vrm_attr_grp)
{
	dev_vrm_attr_grp->attrs = hl_dev_vrm_attrs;
}

int hl_sysfs_init(struct hl_device *hdev)
{
	int rc;

	hdev->max_power = hdev->asic_prop.max_power_default;

	hdev->asic_funcs->add_device_attr(hdev, &hl_dev_clks_attr_group, &hl_dev_vrm_attr_group);

	rc = device_add_groups(hdev->dev, hl_dev_attr_groups);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add groups to device, error %d\n", rc);
		return rc;
	}

	if (!hdev->asic_prop.allow_inference_soft_reset)
		return 0;

	rc = device_add_groups(hdev->dev, hl_dev_inference_attr_groups);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add groups to device, error %d\n", rc);
		return rc;
	}

	return 0;
}

void hl_sysfs_fini(struct hl_device *hdev)
{
	device_remove_groups(hdev->dev, hl_dev_attr_groups);

	if (!hdev->asic_prop.allow_inference_soft_reset)
		return;

	device_remove_groups(hdev->dev, hl_dev_inference_attr_groups);
}
