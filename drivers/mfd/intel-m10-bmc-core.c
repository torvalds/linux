// SPDX-License-Identifier: GPL-2.0
/*
 * Intel MAX 10 Board Management Controller chip - common code
 *
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel-m10-bmc.h>
#include <linux/module.h>

/*
 * This function helps to simplify the accessing of the system registers.
 *
 * The base of the system registers is configured through the struct
 * csr_map.
 */
int m10bmc_sys_read(struct intel_m10bmc *m10bmc, unsigned int offset, unsigned int *val)
{
	const struct m10bmc_csr_map *csr_map = m10bmc->info->csr_map;

	return m10bmc_raw_read(m10bmc, csr_map->base + offset, val);
}
EXPORT_SYMBOL_NS_GPL(m10bmc_sys_read, INTEL_M10_BMC_CORE);

int m10bmc_sys_update_bits(struct intel_m10bmc *m10bmc, unsigned int offset,
			   unsigned int msk, unsigned int val)
{
	const struct m10bmc_csr_map *csr_map = m10bmc->info->csr_map;

	return regmap_update_bits(m10bmc->regmap, csr_map->base + offset, msk, val);
}
EXPORT_SYMBOL_NS_GPL(m10bmc_sys_update_bits, INTEL_M10_BMC_CORE);

static ssize_t bmc_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct intel_m10bmc *ddata = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = m10bmc_sys_read(ddata, ddata->info->csr_map->build_version, &val);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", val);
}
static DEVICE_ATTR_RO(bmc_version);

static ssize_t bmcfw_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct intel_m10bmc *ddata = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = m10bmc_sys_read(ddata, ddata->info->csr_map->fw_version, &val);
	if (ret)
		return ret;

	return sprintf(buf, "0x%x\n", val);
}
static DEVICE_ATTR_RO(bmcfw_version);

static ssize_t mac_address_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct intel_m10bmc *ddata = dev_get_drvdata(dev);
	unsigned int macaddr_low, macaddr_high;
	int ret;

	ret = m10bmc_sys_read(ddata, ddata->info->csr_map->mac_low, &macaddr_low);
	if (ret)
		return ret;

	ret = m10bmc_sys_read(ddata, ddata->info->csr_map->mac_high, &macaddr_high);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			  (u8)FIELD_GET(M10BMC_N3000_MAC_BYTE1, macaddr_low),
			  (u8)FIELD_GET(M10BMC_N3000_MAC_BYTE2, macaddr_low),
			  (u8)FIELD_GET(M10BMC_N3000_MAC_BYTE3, macaddr_low),
			  (u8)FIELD_GET(M10BMC_N3000_MAC_BYTE4, macaddr_low),
			  (u8)FIELD_GET(M10BMC_N3000_MAC_BYTE5, macaddr_high),
			  (u8)FIELD_GET(M10BMC_N3000_MAC_BYTE6, macaddr_high));
}
static DEVICE_ATTR_RO(mac_address);

static ssize_t mac_count_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct intel_m10bmc *ddata = dev_get_drvdata(dev);
	unsigned int macaddr_high;
	int ret;

	ret = m10bmc_sys_read(ddata, ddata->info->csr_map->mac_high, &macaddr_high);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", (u8)FIELD_GET(M10BMC_N3000_MAC_COUNT, macaddr_high));
}
static DEVICE_ATTR_RO(mac_count);

static struct attribute *m10bmc_attrs[] = {
	&dev_attr_bmc_version.attr,
	&dev_attr_bmcfw_version.attr,
	&dev_attr_mac_address.attr,
	&dev_attr_mac_count.attr,
	NULL,
};

static const struct attribute_group m10bmc_group = {
	.attrs = m10bmc_attrs,
};

const struct attribute_group *m10bmc_dev_groups[] = {
	&m10bmc_group,
	NULL,
};
EXPORT_SYMBOL_NS_GPL(m10bmc_dev_groups, INTEL_M10_BMC_CORE);

int m10bmc_dev_init(struct intel_m10bmc *m10bmc, const struct intel_m10bmc_platform_info *info)
{
	int ret;

	m10bmc->info = info;
	dev_set_drvdata(m10bmc->dev, m10bmc);

	ret = devm_mfd_add_devices(m10bmc->dev, PLATFORM_DEVID_AUTO,
				   info->cells, info->n_cells,
				   NULL, 0, NULL);
	if (ret)
		dev_err(m10bmc->dev, "Failed to register sub-devices: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(m10bmc_dev_init, INTEL_M10_BMC_CORE);

MODULE_DESCRIPTION("Intel MAX 10 BMC core driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
