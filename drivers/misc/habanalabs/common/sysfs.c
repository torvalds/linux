// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/pci.h>

long hl_get_frequency(struct hl_device *hdev, u32 pll_index, bool curr)
{
	struct cpucp_packet pkt;
	long result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	if (curr)
		pkt.ctl = cpu_to_le32(CPUCP_PACKET_FREQUENCY_CURR_GET <<
						CPUCP_PKT_CTL_OPCODE_SHIFT);
	else
		pkt.ctl = cpu_to_le32(CPUCP_PACKET_FREQUENCY_GET <<
						CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.pll_index = cpu_to_le32(pll_index);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get frequency of PLL %d, error %d\n",
			pll_index, rc);
		result = rc;
	}

	return result;
}

void hl_set_frequency(struct hl_device *hdev, u32 pll_index, u64 freq)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_FREQUENCY_SET <<
					CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.pll_index = cpu_to_le32(pll_index);
	pkt.value = cpu_to_le64(freq);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);

	if (rc)
		dev_err(hdev->dev,
			"Failed to set frequency to PLL %d, error %d\n",
			pll_index, rc);
}

u64 hl_get_max_power(struct hl_device *hdev)
{
	struct cpucp_packet pkt;
	long result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_MAX_POWER_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	if (rc) {
		dev_err(hdev->dev, "Failed to get max power, error %d\n", rc);
		result = rc;
	}

	return result;
}

void hl_set_max_power(struct hl_device *hdev)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_MAX_POWER_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(hdev->max_power);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);

	if (rc)
		dev_err(hdev->dev, "Failed to set max power, error %d\n", rc);
}

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
			hdev->asic_prop.cpucp_info.cpld_version);
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

static ssize_t infineon_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "0x%04x\n",
			hdev->asic_prop.cpucp_info.infineon_version);
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

	if (!hdev->supports_soft_reset) {
		dev_err(hdev->dev, "Device does not support soft-reset\n");
		goto out;
	}

	dev_warn(hdev->dev, "Soft-Reset requested through sysfs\n");

	hl_device_reset(hdev, false, false);

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

	hl_device_reset(hdev, true, false);

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
	char *str;

	if (atomic_read(&hdev->in_reset))
		str = "In reset";
	else if (hdev->disabled)
		str = "Malfunction";
	else
		str = "Operational";

	return sprintf(buf, "%s\n", str);
}

static ssize_t soft_reset_cnt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hdev->soft_reset_cnt);
}

static ssize_t hard_reset_cnt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hdev->hard_reset_cnt);
}

static ssize_t max_power_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long val;

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	val = hl_get_max_power(hdev);

	return sprintf(buf, "%lu\n", val);
}

static ssize_t max_power_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	unsigned long value;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev)) {
		count = -ENODEV;
		goto out;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto out;
	}

	hdev->max_power = value;
	hl_set_max_power(hdev);

out:
	return count;
}

static ssize_t eeprom_read_handler(struct file *filp, struct kobject *kobj,
			struct bin_attribute *attr, char *buf, loff_t offset,
			size_t max_size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct hl_device *hdev = dev_get_drvdata(dev);
	char *data;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev))
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
static DEVICE_ATTR_RO(infineon_ver);
static DEVICE_ATTR_RW(max_power);
static DEVICE_ATTR_RO(pci_addr);
static DEVICE_ATTR_RO(preboot_btl_ver);
static DEVICE_ATTR_WO(soft_reset);
static DEVICE_ATTR_RO(soft_reset_cnt);
static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_RO(thermal_ver);
static DEVICE_ATTR_RO(uboot_ver);

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
	&dev_attr_infineon_ver.attr,
	&dev_attr_max_power.attr,
	&dev_attr_pci_addr.attr,
	&dev_attr_preboot_btl_ver.attr,
	&dev_attr_soft_reset.attr,
	&dev_attr_soft_reset_cnt.attr,
	&dev_attr_status.attr,
	&dev_attr_thermal_ver.attr,
	&dev_attr_uboot_ver.attr,
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

static const struct attribute_group *hl_dev_attr_groups[] = {
	&hl_dev_attr_group,
	&hl_dev_clks_attr_group,
	NULL,
};

int hl_sysfs_init(struct hl_device *hdev)
{
	int rc;

	if (hdev->asic_type == ASIC_GOYA)
		hdev->pm_mng_profile = PM_AUTO;
	else
		hdev->pm_mng_profile = PM_MANUAL;

	hdev->max_power = hdev->asic_prop.max_power_default;

	hdev->asic_funcs->add_device_attr(hdev, &hl_dev_clks_attr_group);

	rc = device_add_groups(hdev->dev, hl_dev_attr_groups);
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
}
