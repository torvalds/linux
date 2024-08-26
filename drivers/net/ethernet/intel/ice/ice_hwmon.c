// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023, Intel Corporation. */

#include "ice.h"
#include "ice_hwmon.h"
#include "ice_adminq_cmd.h"

#include <linux/hwmon.h>

#define TEMP_FROM_REG(reg) ((reg) * 1000)

static const struct hwmon_channel_info *ice_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_EMERGENCY),
	NULL
};

static int ice_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *val)
{
	struct ice_aqc_get_sensor_reading_resp resp;
	struct ice_pf *pf = dev_get_drvdata(dev);
	int ret;

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	ret = ice_aq_get_sensor_reading(&pf->hw, &resp);
	if (ret) {
		dev_warn_ratelimited(dev,
				     "%s HW read failure (%d)\n",
				     __func__,
				     ret);
		return ret;
	}

	switch (attr) {
	case hwmon_temp_input:
		*val = TEMP_FROM_REG(resp.data.s0f0.temp);
		break;
	case hwmon_temp_max:
		*val = TEMP_FROM_REG(resp.data.s0f0.temp_warning_threshold);
		break;
	case hwmon_temp_crit:
		*val = TEMP_FROM_REG(resp.data.s0f0.temp_critical_threshold);
		break;
	case hwmon_temp_emergency:
		*val = TEMP_FROM_REG(resp.data.s0f0.temp_fatal_threshold);
		break;
	default:
		dev_dbg(dev, "%s unsupported attribute (%d)\n",
			__func__, attr);
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t ice_hwmon_is_visible(const void *data,
				    enum hwmon_sensor_types type, u32 attr,
				    int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_crit:
	case hwmon_temp_max:
	case hwmon_temp_emergency:
		return 0444;
	}

	return 0;
}

static const struct hwmon_ops ice_hwmon_ops = {
	.is_visible = ice_hwmon_is_visible,
	.read = ice_hwmon_read
};

static const struct hwmon_chip_info ice_chip_info = {
	.ops = &ice_hwmon_ops,
	.info = ice_hwmon_info
};

static bool ice_is_internal_reading_supported(struct ice_pf *pf)
{
	/* Only the first PF will report temperature for a chip.
	 * Note that internal temp reading is not supported
	 * for older FW (< v4.30).
	 */
	if (pf->hw.pf_id)
		return false;

	unsigned long sensors = pf->hw.dev_caps.supported_sensors;

	return test_bit(ICE_SENSOR_SUPPORT_E810_INT_TEMP_BIT, &sensors);
};

void ice_hwmon_init(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct device *hdev;

	if (!ice_is_internal_reading_supported(pf))
		return;

	hdev = hwmon_device_register_with_info(dev, "ice", pf, &ice_chip_info,
					       NULL);
	if (IS_ERR(hdev)) {
		dev_warn(dev,
			 "hwmon_device_register_with_info returns error (%ld)",
			 PTR_ERR(hdev));
		return;
	}
	pf->hwmon_dev = hdev;
}

void ice_hwmon_exit(struct ice_pf *pf)
{
	if (!pf->hwmon_dev)
		return;
	hwmon_device_unregister(pf->hwmon_dev);
}
