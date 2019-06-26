// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2014-2019 aQuantia Corporation. */

/* File aq_drvinfo.c: Definition of common code for firmware info in sys.*/

#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/hwmon.h>
#include <linux/uaccess.h>

#include "aq_drvinfo.h"

#if IS_REACHABLE(CONFIG_HWMON)
static int aq_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *value)
{
	struct aq_nic_s *aq_nic = dev_get_drvdata(dev);
	int temp;
	int err;

	if (!aq_nic)
		return -EIO;

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	if (!aq_nic->aq_fw_ops->get_phy_temp)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_input:
		err = aq_nic->aq_fw_ops->get_phy_temp(aq_nic->aq_hw, &temp);
		*value = temp;
		return err;
	default:
		return -EOPNOTSUPP;
	}
}

static int aq_hwmon_read_string(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, const char **str)
{
	struct aq_nic_s *aq_nic = dev_get_drvdata(dev);

	if (!aq_nic)
		return -EIO;

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	if (!aq_nic->aq_fw_ops->get_phy_temp)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_label:
		*str = "PHY Temperature";
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t aq_hwmon_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	if (type != hwmon_temp)
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
	0,
};

static const struct hwmon_channel_info aq_hwmon_temp = {
	.type = hwmon_temp,
	.config = aq_hwmon_temp_config,
};

static const struct hwmon_channel_info *aq_hwmon_info[] = {
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
