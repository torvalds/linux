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

static const struct ath12k_wmi_tt_level_config_param
tt_level_configs[ATH12K_TT_CFG_IDX_MAX][ENHANCED_THERMAL_LEVELS] = {
	[ATH12K_TT_CFG_IDX_IPA] = {
		[0] = {	.tmplwm = -100, .tmphwm = 115, .dcoffpercent = 0,
			.pout_reduction_db = 0 },
		[1] = { .tmplwm = 110, .tmphwm = 120, .dcoffpercent = 0,
			.pout_reduction_db = 12	},
		[2] = { .tmplwm = 115, .tmphwm = 125, .dcoffpercent = 50,
			.pout_reduction_db = 12	},
		[3] = { .tmplwm = 120, .tmphwm = 130, .dcoffpercent = 90,
			.pout_reduction_db = 12	},
		[4] = { .tmplwm = 125, .tmphwm = 130, .dcoffpercent = 100,
			.pout_reduction_db = 12	},
	},
	[ATH12K_TT_CFG_IDX_XFEM] = {
		[0] = {	.tmplwm = -100,	.tmphwm = 105, .dcoffpercent = 0,
			.pout_reduction_db = 0 },
		[1] = { .tmplwm = 100, .tmphwm = 110, .dcoffpercent = 0,
			.pout_reduction_db = 0 },
		[2] = { .tmplwm = 105, .tmphwm = 115, .dcoffpercent = 50,
			.pout_reduction_db = 0 },
		[3] = {	.tmplwm = 110, .tmphwm = 120, .dcoffpercent = 90,
			.pout_reduction_db = 0 },
		[4] = { .tmplwm = 115, .tmphwm = 120, .dcoffpercent = 100,
			.pout_reduction_db = 0 },
	},
};

static enum ath12k_thermal_cfg_idx ath12k_thermal_cfg_index(struct ath12k *ar)
{
	if (test_bit(WMI_TLV_SERVICE_IS_TARGET_IPA, ar->ab->wmi_ab.svc_map))
		return ATH12K_TT_CFG_IDX_IPA;

	return ATH12K_TT_CFG_IDX_XFEM;
}

int ath12k_thermal_throttling_config_default(struct ath12k *ar)
{
	struct ath12k_wmi_thermal_mitigation_arg param = {};
	int ret;

	if (test_bit(WMI_TLV_SERVICE_THERM_THROT_5_LEVELS, ar->ab->wmi_ab.svc_map))
		param.num_levels = ENHANCED_THERMAL_LEVELS;
	else
		param.num_levels = THERMAL_LEVELS;

	param.levelconf = ar->thermal.tt_level_configs;

	ret = ath12k_wmi_send_thermal_mitigation_cmd(ar, &param);
	if (ret)
		ath12k_warn(ar->ab,
			    "failed to send thermal mitigation cmd for default config: %d\n",
			    ret);
	return ret;
}

void ath12k_thermal_init_configs(struct ath12k *ar)
{
	enum ath12k_thermal_cfg_idx cfg_idx;

	cfg_idx = ath12k_thermal_cfg_index(ar);
	ar->thermal.tt_level_configs = &tt_level_configs[cfg_idx][0];
}

static int
ath12k_thermal_get_max_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	*state = ATH12K_THERMAL_THROTTLE_MAX;

	return 0;
}

static int
ath12k_thermal_get_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long *state)
{
	struct ath12k *ar = cdev->devdata;

	mutex_lock(&ar->thermal.lock);
	*state = ar->thermal.throttle_state;
	mutex_unlock(&ar->thermal.lock);

	return 0;
}

int ath12k_thermal_set_throttling(struct ath12k *ar, u32 throttle_state)
{
	struct ath12k_wmi_thermal_mitigation_arg param = {};
	struct ath12k_wmi_tt_level_config_param cfg = {};
	int ret;

	param.num_levels = 1;
	cfg.dcoffpercent = throttle_state;
	param.levelconf = &cfg;

	ret = ath12k_wmi_send_thermal_mitigation_cmd(ar, &param);
	if (ret)
		ath12k_warn(ar->ab, "failed to send thermal mitigation cmd: %d\n",
			    ret);

	return ret;
}

static int
ath12k_thermal_set_cur_throttle_state(struct thermal_cooling_device *cdev,
				      unsigned long throttle_state)
{
	struct ath12k *ar = cdev->devdata;

	if (throttle_state > ATH12K_THERMAL_THROTTLE_MAX)
		return -EINVAL;

	scoped_guard(mutex, &ar->thermal.lock) {
		if (ar->thermal.throttle_state == throttle_state)
			return 0;
		ar->thermal.throttle_state = throttle_state;
	}

	if (throttle_state == 0)
		return ath12k_thermal_throttling_config_default(ar);

	return ath12k_thermal_set_throttling(ar, throttle_state);
}

static const struct thermal_cooling_device_ops ath12k_thermal_ops = {
	.get_max_state = ath12k_thermal_get_max_throttle_state,
	.get_cur_state = ath12k_thermal_get_cur_throttle_state,
	.set_cur_state = ath12k_thermal_set_cur_throttle_state,
};

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

static int ath12k_thermal_setup_radio(struct ath12k_base *ab, int i)
{
	char pdev_name[20];
	struct ath12k *ar;
	int ret;

	ar = ab->pdevs[i].ar;
	if (!ar)
		return 0;

	ar->thermal.cdev =
		thermal_cooling_device_register("ath12k_thermal", ar,
						&ath12k_thermal_ops);
	if (IS_ERR(ar->thermal.cdev)) {
		ret = PTR_ERR(ar->thermal.cdev);
		ar->thermal.cdev = NULL;
		ath12k_err(ar->ab, "failed to register cooling device: %d\n",
			   ret);
		return ret;
	}

	scnprintf(pdev_name, sizeof(pdev_name), "cooling_device%u",
		  ar->hw_link_id);

	ret = sysfs_create_link(&ar->ah->hw->wiphy->dev.kobj,
				&ar->thermal.cdev->device.kobj, pdev_name);
	if (ret) {
		ath12k_err(ab, "failed to create cooling device symlink: %d\n",
			   ret);
		goto unregister_cdev;
	}

	ar->thermal.hwmon_dev =
		hwmon_device_register_with_groups(&ar->ah->hw->wiphy->dev,
						  "ath12k_hwmon", ar,
						  ath12k_hwmon_groups);
	if (IS_ERR(ar->thermal.hwmon_dev)) {
		ret = PTR_ERR(ar->thermal.hwmon_dev);
		ar->thermal.hwmon_dev = NULL;
		ath12k_err(ar->ab, "failed to register hwmon device: %d\n",
			   ret);
		goto remove_sysfs;
	}

	return 0;

remove_sysfs:
	sysfs_remove_link(&ar->ah->hw->wiphy->dev.kobj, pdev_name);
unregister_cdev:
	thermal_cooling_device_unregister(ar->thermal.cdev);
	ar->thermal.cdev = NULL;
	return ret;
}

static void ath12k_thermal_cleanup_radio(struct ath12k_base *ab, int i)
{
	char pdev_name[20];
	struct ath12k *ar;

	ar = ab->pdevs[i].ar;
	if (!ar)
		return;

	hwmon_device_unregister(ar->thermal.hwmon_dev);
	ar->thermal.hwmon_dev = NULL;

	scnprintf(pdev_name, sizeof(pdev_name), "cooling_device%u",
		  ar->hw_link_id);
	sysfs_remove_link(&ar->ah->hw->wiphy->dev.kobj, pdev_name);

	thermal_cooling_device_unregister(ar->thermal.cdev);
	ar->thermal.cdev = NULL;
}

int ath12k_thermal_register(struct ath12k_base *ab)
{
	int i, ret;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return 0;

	for (i = 0; i < ab->num_radios; i++) {
		ret = ath12k_thermal_setup_radio(ab, i);
		if (ret)
			goto out;
	}

	return 0;
out:
	for (i--; i >= 0; i--)
		ath12k_thermal_cleanup_radio(ab, i);

	return ret;
}

void ath12k_thermal_unregister(struct ath12k_base *ab)
{
	int i;

	if (!IS_REACHABLE(CONFIG_HWMON))
		return;

	for (i = 0; i < ab->num_radios; i++)
		ath12k_thermal_cleanup_radio(ab, i);
}
