// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

/* File aq_drvinfo.c: Definition of common code for firmware info in sys.*/

#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/hwmon.h>
#include <linux/uaccess.h>

#include "aq_drvinfo.h"
#include "aq_nic.h"

#if IS_REACHABLE(CONFIG_HWMON)
static const char * const atl_temp_label[] = {
	"PHY Temperature",
	"MAC Temperature",
};

static int aq_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *value)
{
	struct aq_nic_s *aq_nic = dev_get_drvdata(dev);
	int err = 0;
	int temp;

	if (!aq_nic)
		return -EIO;

	if (type != hwmon_temp || attr != hwmon_temp_input)
		return -EOPNOTSUPP;

	switch (channel) {
	case 0:
		if (!aq_nic->aq_fw_ops->get_phy_temp)
			return -EOPNOTSUPP;

		err = aq_nic->aq_fw_ops->get_phy_temp(aq_nic->aq_hw, &temp);
		*value = temp;
		break;
	case 1:
		if (!aq_nic->aq_fw_ops->get_mac_temp &&
		    !aq_nic->aq_hw_ops->hw_get_mac_temp)
			return -EOPNOTSUPP;

		if (aq_nic->aq_fw_ops->get_mac_temp)
			err = aq_nic->aq_fw_ops->get_mac_temp(aq_nic->aq_hw, &temp);
		else
			err = aq_nic->aq_hw_ops->hw_get_mac_temp(aq_nic->aq_hw, &temp);
		*value = temp;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static int aq_hwmon_read_string(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, const char **str)
{
	struct aq_nic_s *aq_nic = dev_get_drvdata(dev);

	if (!aq_nic)
		return -EIO;

	if (type != hwmon_temp || attr != hwmon_temp_label)
		return -EOPNOTSUPP;

	if (channel < ARRAY_SIZE(atl_temp_label))
		*str = atl_temp_label[channel];
	else
		return -EOPNOTSUPP;

	return 0;
}

static umode_t aq_hwmon_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	const struct aq_nic_s *nic = data;

	if (type != hwmon_temp)
		return 0;

	if (channel == 0 && !nic->aq_fw_ops->get_phy_temp)
		return 0;
	else if (channel == 1 && !nic->aq_fw_ops->get_mac_temp &&
		 !nic->aq_hw_ops->hw_get_mac_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_label:
		return 0444;
	default:
		return 0;
	}
}

static const struct hwmon_ops aq_hwmon_ops = {
	.is_visible = aq_hwmon_is_visible,
	.read = aq_hwmon_read,
	.read_string = aq_hwmon_read_string,
};

static u32 aq_hwmon_temp_config[] = {
	HWMON_T_INPUT | HWMON_T_LABEL,
	HWMON_T_INPUT | HWMON_T_LABEL,
	0,
};

static const struct hwmon_channel_info aq_hwmon_temp = {
	.type = hwmon_temp,
	.config = aq_hwmon_temp_config,
};

static const struct hwmon_channel_info * const aq_hwmon_info[] = {
	&aq_hwmon_temp,
	NULL,
};

static const struct hwmon_chip_info aq_hwmon_chip_info = {
	.ops = &aq_hwmon_ops,
	.info = aq_hwmon_info,
};

int aq_drvinfo_init(struct net_device *ndev)
{
	struct aq_nic_s *aq_nic = netdev_priv(ndev);
	struct device *dev = &aq_nic->pdev->dev;
	struct device *hwmon_dev;
	int err = 0;

	hwmon_dev = devm_hwmon_device_register_with_info(dev,
							 ndev->name,
							 aq_nic,
							 &aq_hwmon_chip_info,
							 NULL);

	if (IS_ERR(hwmon_dev))
		err = PTR_ERR(hwmon_dev);

	return err;
}

#else
int aq_drvinfo_init(struct net_device *ndev) { return 0; }
#endif
