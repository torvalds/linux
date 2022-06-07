// SPDX-License-Identifier: GPL-2.0
/*
 * UFS hardware monitoring support
 * Copyright (c) 2021, Western Digital Corporation
 */

#include <linux/hwmon.h>
#include <linux/units.h>

#include "ufshcd.h"
#include "ufshcd-priv.h"

struct ufs_hwmon_data {
	struct ufs_hba *hba;
	u8 mask;
};

static int ufs_read_temp_enable(struct ufs_hba *hba, u8 mask, long *val)
{
	u32 ee_mask;
	int err;

	err = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR, QUERY_ATTR_IDN_EE_CONTROL, 0, 0,
				&ee_mask);
	if (err)
		return err;

	*val = (mask & ee_mask & MASK_EE_TOO_HIGH_TEMP) || (mask & ee_mask & MASK_EE_TOO_LOW_TEMP);

	return 0;
}

static int ufs_get_temp(struct ufs_hba *hba, enum attr_idn idn, long *val)
{
	u32 value;
	int err;

	err = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR, idn, 0, 0, &value);
	if (err)
		return err;

	if (value == 0)
		return -ENODATA;

	*val = ((long)value - 80) * MILLIDEGREE_PER_DEGREE;

	return 0;
}

static int ufs_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			  long *val)
{
	struct ufs_hwmon_data *data = dev_get_drvdata(dev);
	struct ufs_hba *hba = data->hba;
	int err;

	down(&hba->host_sem);

	if (!ufshcd_is_user_access_allowed(hba)) {
		up(&hba->host_sem);
		return -EBUSY;
	}

	ufshcd_rpm_get_sync(hba);

	switch (attr) {
	case hwmon_temp_enable:
		err = ufs_read_temp_enable(hba, data->mask, val);

		break;
	case hwmon_temp_crit:
		err = ufs_get_temp(hba, QUERY_ATTR_IDN_HIGH_TEMP_BOUND, val);

		break;
	case hwmon_temp_lcrit:
		err = ufs_get_temp(hba, QUERY_ATTR_IDN_LOW_TEMP_BOUND, val);

		break;
	case hwmon_temp_input:
		err = ufs_get_temp(hba, QUERY_ATTR_IDN_CASE_ROUGH_TEMP, val);

		break;
	default:
		err = -EOPNOTSUPP;

		break;
	}

	ufshcd_rpm_put_sync(hba);

	up(&hba->host_sem);

	return err;
}

static int ufs_hwmon_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			   long val)
{
	struct ufs_hwmon_data *data = dev_get_drvdata(dev);
	struct ufs_hba *hba = data->hba;
	int err;

	if (attr != hwmon_temp_enable)
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	down(&hba->host_sem);

	if (!ufshcd_is_user_access_allowed(hba)) {
		up(&hba->host_sem);
		return -EBUSY;
	}

	ufshcd_rpm_get_sync(hba);

	if (val == 1)
		err = ufshcd_update_ee_usr_mask(hba, MASK_EE_URGENT_TEMP, 0);
	else
		err = ufshcd_update_ee_usr_mask(hba, 0, MASK_EE_URGENT_TEMP);

	ufshcd_rpm_put_sync(hba);

	up(&hba->host_sem);

	return err;
}

static umode_t ufs_hwmon_is_visible(const void *_data, enum hwmon_sensor_types type, u32 attr,
				    int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_enable:
		return 0644;
	case hwmon_temp_crit:
	case hwmon_temp_lcrit:
	case hwmon_temp_input:
		return 0444;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info *ufs_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_ENABLE | HWMON_T_INPUT | HWMON_T_CRIT | HWMON_T_LCRIT),
	NULL
};

static const struct hwmon_ops ufs_hwmon_ops = {
	.is_visible	= ufs_hwmon_is_visible,
	.read		= ufs_hwmon_read,
	.write		= ufs_hwmon_write,
};

static const struct hwmon_chip_info ufs_hwmon_hba_info = {
	.ops	= &ufs_hwmon_ops,
	.info	= ufs_hwmon_info,
};

void ufs_hwmon_probe(struct ufs_hba *hba, u8 mask)
{
	struct device *dev = hba->dev;
	struct ufs_hwmon_data *data;
	struct device *hwmon;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	data->hba = hba;
	data->mask = mask;

	hwmon = hwmon_device_register_with_info(dev, "ufs", data, &ufs_hwmon_hba_info, NULL);
	if (IS_ERR(hwmon)) {
		dev_warn(dev, "Failed to instantiate hwmon device\n");
		kfree(data);
		return;
	}

	hba->hwmon_device = hwmon;
}

void ufs_hwmon_remove(struct ufs_hba *hba)
{
	struct ufs_hwmon_data *data;

	if (!hba->hwmon_device)
		return;

	data = dev_get_drvdata(hba->hwmon_device);
	hwmon_device_unregister(hba->hwmon_device);
	hba->hwmon_device = NULL;
	kfree(data);
}

void ufs_hwmon_notify_event(struct ufs_hba *hba, u8 ee_mask)
{
	if (!hba->hwmon_device)
		return;

	if (ee_mask & MASK_EE_TOO_HIGH_TEMP)
		hwmon_notify_event(hba->hwmon_device, hwmon_temp, hwmon_temp_max_alarm, 0);

	if (ee_mask & MASK_EE_TOO_LOW_TEMP)
		hwmon_notify_event(hba->hwmon_device, hwmon_temp, hwmon_temp_min_alarm, 0);
}
