// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include "core.h"
#include "debug.h"

static ssize_t ath12k_thermal_temp_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct ath12k *ar = dev_get_drvdata(dev);
	unsigned long time_left;
	int ret, temperature;

	guard(wiphy)(ath12k_ar_to_hw(ar)->wiphy);

	if (ar->ah->state != ATH12K_HW_STATE_ON)
		return -ENETDOWN;

	reinit_completion(&ar->thermal.wmi_sync);
	ret = ath12k_wmi_send_pdev_temperature_cmd(ar);
	if (ret) {
		ath12k_warn(ar->ab, "failed to read temperature %d\n", ret);
		return ret;
	}

	if (test_bit(ATH12K_FLAG_CRASH_FLUSH, &ar->ab->dev_flags))
		return -ESHUTDOWN;

	time_left = wait_for_completion_timeout(&ar->thermal.wmi_sync,
						ATH12K_THERMAL_SYNC_TIMEOUT_HZ);
	if (!time_left) {
		ath12k_warn(ar->ab, "failed to synchronize thermal read\n");
		return -ETIMEDOUT;
	}

	spin_lock_bh(&ar->data_lock);
	temperature = ar->thermal.temperature;
	spin_unlock_bh(&ar->data_lock);

	/* display in millidegree celsius */
	return sysfs_emit(buf, "%d\n", temperature * 1000);
}

void ath12k_thermal_event_temperature(struct ath12k *ar, int temperature)
{
	spin_lock_bh(&ar->data_lock);
	ar->thermal.temperature = temperature;
	spin_unlock_bh(&ar->data_lock);
	complete_all(&ar->thermal.wmi_sync);
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, ath12k_thermal_temp, 0);

static struct attribute *ath12k_hwmon_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ath12k_hwmon);

int ath12k_thermal_register(struct ath12k_base *ab)
{
	struct ath12k *ar;
	int i, j, ret;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return 0;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		if (!ar)
			continue;

		ar->thermal.hwmon_dev =
			hwmon_device_register_with_groups(&ar->ah->hw->wiphy->dev,
							  "ath12k_hwmon", ar,
							  ath12k_hwmon_groups);
		if (IS_ERR(ar->thermal.hwmon_dev)) {
			ret = PTR_ERR(ar->thermal.hwmon_dev);
			ar->thermal.hwmon_dev = NULL;
			ath12k_err(ar->ab, "failed to register hwmon device: %d\n",
				   ret);
			for (j = i - 1; j >= 0; j--) {
				ar = ab->pdevs[j].ar;
				if (!ar)
					continue;

				hwmon_device_unregister(ar->thermal.hwmon_dev);
				ar->thermal.hwmon_dev = NULL;
			}
			return ret;
		}
	}

	return 0;
}

void ath12k_thermal_unregister(struct ath12k_base *ab)
{
	struct ath12k *ar;
	int i;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		if (!ar)
			continue;

		if (ar->thermal.hwmon_dev) {
			hwmon_device_unregister(ar->thermal.hwmon_dev);
			ar->thermal.hwmon_dev = NULL;
		}
	}
}
