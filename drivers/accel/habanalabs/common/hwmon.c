// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/pci.h>
#include <linux/hwmon.h>

#define HWMON_NR_SENSOR_TYPES		(hwmon_max)

#ifdef _HAS_HWMON_HWMON_T_ENABLE

static u32 fixup_flags_legacy_fw(struct hl_device *hdev, enum hwmon_sensor_types type,
					u32 cpucp_flags)
{
	u32 flags;

	switch (type) {
	case hwmon_temp:
		flags = (cpucp_flags << 1) | HWMON_T_ENABLE;
		break;

	case hwmon_in:
		flags = (cpucp_flags << 1) | HWMON_I_ENABLE;
		break;

	case hwmon_curr:
		flags = (cpucp_flags << 1) | HWMON_C_ENABLE;
		break;

	case hwmon_fan:
		flags = (cpucp_flags << 1) | HWMON_F_ENABLE;
		break;

	case hwmon_power:
		flags = (cpucp_flags << 1) | HWMON_P_ENABLE;
		break;

	case hwmon_pwm:
		/* enable bit was here from day 1, so no need to adjust */
		flags = cpucp_flags;
		break;

	default:
		dev_err(hdev->dev, "unsupported h/w sensor type %d\n", type);
		flags = cpucp_flags;
		break;
	}

	return flags;
}

static u32 fixup_attr_legacy_fw(u32 attr)
{
	return (attr - 1);
}

#else

static u32 fixup_flags_legacy_fw(struct hl_device *hdev, enum hwmon_sensor_types type,
						u32 cpucp_flags)
{
	return cpucp_flags;
}

static u32 fixup_attr_legacy_fw(u32 attr)
{
	return attr;
}

#endif /* !_HAS_HWMON_HWMON_T_ENABLE */

static u32 adjust_hwmon_flags(struct hl_device *hdev, enum hwmon_sensor_types type, u32 cpucp_flags)
{
	u32 flags, cpucp_input_val;
	bool use_cpucp_enum;

	use_cpucp_enum = (hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
					CPU_BOOT_DEV_STS0_MAP_HWMON_EN) ? true : false;

	/* If f/w is using it's own enum, we need to check if the properties values are aligned.
	 * If not, it means we need to adjust the values to the new format that is used in the
	 * kernel since 5.6 (enum values were incremented by 1 by adding a new enable value).
	 */
	if (use_cpucp_enum) {
		switch (type) {
		case hwmon_temp:
			cpucp_input_val = cpucp_temp_input;
			if (cpucp_input_val == hwmon_temp_input)
				flags = cpucp_flags;
			else
				flags = (cpucp_flags << 1) | HWMON_T_ENABLE;
			break;

		case hwmon_in:
			cpucp_input_val = cpucp_in_input;
			if (cpucp_input_val == hwmon_in_input)
				flags = cpucp_flags;
			else
				flags = (cpucp_flags << 1) | HWMON_I_ENABLE;
			break;

		case hwmon_curr:
			cpucp_input_val = cpucp_curr_input;
			if (cpucp_input_val == hwmon_curr_input)
				flags = cpucp_flags;
			else
				flags = (cpucp_flags << 1) | HWMON_C_ENABLE;
			break;

		case hwmon_fan:
			cpucp_input_val = cpucp_fan_input;
			if (cpucp_input_val == hwmon_fan_input)
				flags = cpucp_flags;
			else
				flags = (cpucp_flags << 1) | HWMON_F_ENABLE;
			break;

		case hwmon_pwm:
			/* enable bit was here from day 1, so no need to adjust */
			flags = cpucp_flags;
			break;

		case hwmon_power:
			cpucp_input_val = CPUCP_POWER_INPUT;
			if (cpucp_input_val == hwmon_power_input)
				flags = cpucp_flags;
			else
				flags = (cpucp_flags << 1) | HWMON_P_ENABLE;
			break;

		default:
			dev_err(hdev->dev, "unsupported h/w sensor type %d\n", type);
			flags = cpucp_flags;
			break;
		}
	} else {
		flags = fixup_flags_legacy_fw(hdev, type, cpucp_flags);
	}

	return flags;
}

int hl_build_hwmon_channel_info(struct hl_device *hdev, struct cpucp_sensor *sensors_arr)
{
	u32 num_sensors_for_type, flags, num_active_sensor_types = 0, arr_size = 0, *curr_arr;
	u32 sensors_by_type_next_index[HWMON_NR_SENSOR_TYPES] = {0};
	u32 *sensors_by_type[HWMON_NR_SENSOR_TYPES] = {NULL};
	struct hwmon_channel_info **channels_info;
	u32 counts[HWMON_NR_SENSOR_TYPES] = {0};
	enum hwmon_sensor_types type;
	int rc, i, j;

	for (i = 0 ; i < CPUCP_MAX_SENSORS ; i++) {
		type = le32_to_cpu(sensors_arr[i].type);

		if ((type == 0) && (sensors_arr[i].flags == 0))
			break;

		if (type >= HWMON_NR_SENSOR_TYPES) {
			dev_err(hdev->dev, "Got wrong sensor type %d from device\n", type);
			return -EINVAL;
		}

		counts[type]++;
		arr_size++;
	}

	for (i = 0 ; i < HWMON_NR_SENSOR_TYPES ; i++) {
		if (counts[i] == 0)
			continue;

		num_sensors_for_type = counts[i] + 1;
		dev_dbg(hdev->dev, "num_sensors_for_type %d = %d\n", i, num_sensors_for_type);

		curr_arr = kcalloc(num_sensors_for_type, sizeof(*curr_arr), GFP_KERNEL);
		if (!curr_arr) {
			rc = -ENOMEM;
			goto sensors_type_err;
		}

		num_active_sensor_types++;
		sensors_by_type[i] = curr_arr;
	}

	for (i = 0 ; i < arr_size ; i++) {
		type = le32_to_cpu(sensors_arr[i].type);
		curr_arr = sensors_by_type[type];
		flags = adjust_hwmon_flags(hdev, type, le32_to_cpu(sensors_arr[i].flags));
		curr_arr[sensors_by_type_next_index[type]++] = flags;
	}

	channels_info = kcalloc(num_active_sensor_types + 1, sizeof(struct hwmon_channel_info *),
				GFP_KERNEL);
	if (!channels_info) {
		rc = -ENOMEM;
		goto channels_info_array_err;
	}

	for (i = 0 ; i < num_active_sensor_types ; i++) {
		channels_info[i] = kzalloc(sizeof(*channels_info[i]), GFP_KERNEL);
		if (!channels_info[i]) {
			rc = -ENOMEM;
			goto channel_info_err;
		}
	}

	for (i = 0, j = 0 ; i < HWMON_NR_SENSOR_TYPES ; i++) {
		if (!sensors_by_type[i])
			continue;

		channels_info[j]->type = i;
		channels_info[j]->config = sensors_by_type[i];
		j++;
	}

	hdev->hl_chip_info->info = (const struct hwmon_channel_info **)channels_info;

	return 0;

channel_info_err:
	for (i = 0 ; i < num_active_sensor_types ; i++) {
		if (channels_info[i]) {
			kfree(channels_info[i]->config);
			kfree(channels_info[i]);
		}
	}
	kfree(channels_info);

channels_info_array_err:
sensors_type_err:
	for (i = 0 ; i < HWMON_NR_SENSOR_TYPES ; i++)
		kfree(sensors_by_type[i]);

	return rc;
}

static int hl_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	bool use_cpucp_enum;
	u32 cpucp_attr;
	int rc;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	use_cpucp_enum = (hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
					CPU_BOOT_DEV_STS0_MAP_HWMON_EN) ? true : false;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			cpucp_attr = cpucp_temp_input;
			break;
		case hwmon_temp_max:
			cpucp_attr = cpucp_temp_max;
			break;
		case hwmon_temp_crit:
			cpucp_attr = cpucp_temp_crit;
			break;
		case hwmon_temp_max_hyst:
			cpucp_attr = cpucp_temp_max_hyst;
			break;
		case hwmon_temp_crit_hyst:
			cpucp_attr = cpucp_temp_crit_hyst;
			break;
		case hwmon_temp_offset:
			cpucp_attr = cpucp_temp_offset;
			break;
		case hwmon_temp_highest:
			cpucp_attr = cpucp_temp_highest;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			rc = hl_get_temperature(hdev, channel, cpucp_attr, val);
		else
			rc = hl_get_temperature(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			cpucp_attr = cpucp_in_input;
			break;
		case hwmon_in_min:
			cpucp_attr = cpucp_in_min;
			break;
		case hwmon_in_max:
			cpucp_attr = cpucp_in_max;
			break;
		case hwmon_in_highest:
			cpucp_attr = cpucp_in_highest;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			rc = hl_get_voltage(hdev, channel, cpucp_attr, val);
		else
			rc = hl_get_voltage(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			cpucp_attr = cpucp_curr_input;
			break;
		case hwmon_curr_min:
			cpucp_attr = cpucp_curr_min;
			break;
		case hwmon_curr_max:
			cpucp_attr = cpucp_curr_max;
			break;
		case hwmon_curr_highest:
			cpucp_attr = cpucp_curr_highest;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			rc = hl_get_current(hdev, channel, cpucp_attr, val);
		else
			rc = hl_get_current(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			cpucp_attr = cpucp_fan_input;
			break;
		case hwmon_fan_min:
			cpucp_attr = cpucp_fan_min;
			break;
		case hwmon_fan_max:
			cpucp_attr = cpucp_fan_max;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			rc = hl_get_fan_speed(hdev, channel, cpucp_attr, val);
		else
			rc = hl_get_fan_speed(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			cpucp_attr = cpucp_pwm_input;
			break;
		case hwmon_pwm_enable:
			cpucp_attr = cpucp_pwm_enable;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			rc = hl_get_pwm_info(hdev, channel, cpucp_attr, val);
		else
			/* no need for fixup as pwm was aligned from day 1 */
			rc = hl_get_pwm_info(hdev, channel, attr, val);
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
			cpucp_attr = CPUCP_POWER_INPUT;
			break;
		case hwmon_power_input_highest:
			cpucp_attr = CPUCP_POWER_INPUT_HIGHEST;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			rc = hl_get_power(hdev, channel, cpucp_attr, val);
		else
			rc = hl_get_power(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	default:
		return -EINVAL;
	}
	return rc;
}

static int hl_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	u32 cpucp_attr;
	bool use_cpucp_enum = (hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
				CPU_BOOT_DEV_STS0_MAP_HWMON_EN) ? true : false;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_offset:
			cpucp_attr = cpucp_temp_offset;
			break;
		case hwmon_temp_reset_history:
			cpucp_attr = cpucp_temp_reset_history;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			hl_set_temperature(hdev, channel, cpucp_attr, val);
		else
			hl_set_temperature(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			cpucp_attr = cpucp_pwm_input;
			break;
		case hwmon_pwm_enable:
			cpucp_attr = cpucp_pwm_enable;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			hl_set_pwm_info(hdev, channel, cpucp_attr, val);
		else
			/* no need for fixup as pwm was aligned from day 1 */
			hl_set_pwm_info(hdev, channel, attr, val);
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_reset_history:
			cpucp_attr = cpucp_in_reset_history;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			hl_set_voltage(hdev, channel, cpucp_attr, val);
		else
			hl_set_voltage(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_reset_history:
			cpucp_attr = cpucp_curr_reset_history;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			hl_set_current(hdev, channel, cpucp_attr, val);
		else
			hl_set_current(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_reset_history:
			cpucp_attr = CPUCP_POWER_RESET_INPUT_HISTORY;
			break;
		default:
			return -EINVAL;
		}

		if (use_cpucp_enum)
			hl_set_power(hdev, channel, cpucp_attr, val);
		else
			hl_set_power(hdev, channel, fixup_attr_legacy_fw(attr), val);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static umode_t hl_is_visible(const void *data, enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_max:
		case hwmon_temp_max_hyst:
		case hwmon_temp_crit:
		case hwmon_temp_crit_hyst:
		case hwmon_temp_highest:
			return 0444;
		case hwmon_temp_offset:
			return 0644;
		case hwmon_temp_reset_history:
			return 0200;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_min:
		case hwmon_in_max:
		case hwmon_in_highest:
			return 0444;
		case hwmon_in_reset_history:
			return 0200;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
		case hwmon_curr_min:
		case hwmon_curr_max:
		case hwmon_curr_highest:
			return 0444;
		case hwmon_curr_reset_history:
			return 0200;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
		case hwmon_fan_min:
		case hwmon_fan_max:
			return 0444;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
		case hwmon_pwm_enable:
			return 0644;
		}
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
		case hwmon_power_input_highest:
			return 0444;
		case hwmon_power_reset_history:
			return 0200;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_ops hl_hwmon_ops = {
	.is_visible = hl_is_visible,
	.read = hl_read,
	.write = hl_write
};

int hl_get_temperature(struct hl_device *hdev,
			int sensor_index, u32 attr, long *value)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_TEMPERATURE_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	dev_dbg(hdev->dev, "get temp, ctl 0x%x, sensor %d, type %d\n",
		pkt.ctl, pkt.sensor_index, pkt.type);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	*value = (long) result;

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get temperature from sensor %d, error %d\n",
			sensor_index, rc);
		*value = 0;
	}

	return rc;
}

int hl_set_temperature(struct hl_device *hdev,
			int sensor_index, u32 attr, long value)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_TEMPERATURE_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);
	pkt.value = __cpu_to_le64(value);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);

	if (rc)
		dev_err(hdev->dev,
			"Failed to set temperature of sensor %d, error %d\n",
			sensor_index, rc);

	return rc;
}

int hl_get_voltage(struct hl_device *hdev,
			int sensor_index, u32 attr, long *value)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_VOLTAGE_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	*value = (long) result;

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get voltage from sensor %d, error %d\n",
			sensor_index, rc);
		*value = 0;
	}

	return rc;
}

int hl_get_current(struct hl_device *hdev,
			int sensor_index, u32 attr, long *value)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_CURRENT_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	*value = (long) result;

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get current from sensor %d, error %d\n",
			sensor_index, rc);
		*value = 0;
	}

	return rc;
}

int hl_get_fan_speed(struct hl_device *hdev,
			int sensor_index, u32 attr, long *value)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_FAN_SPEED_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	*value = (long) result;

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get fan speed from sensor %d, error %d\n",
			sensor_index, rc);
		*value = 0;
	}

	return rc;
}

int hl_get_pwm_info(struct hl_device *hdev,
			int sensor_index, u32 attr, long *value)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_PWM_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	*value = (long) result;

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get pwm info from sensor %d, error %d\n",
			sensor_index, rc);
		*value = 0;
	}

	return rc;
}

void hl_set_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr,
			long value)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_PWM_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);
	pkt.value = cpu_to_le64(value);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);

	if (rc)
		dev_err(hdev->dev,
			"Failed to set pwm info to sensor %d, error %d\n",
			sensor_index, rc);
}

int hl_set_voltage(struct hl_device *hdev,
			int sensor_index, u32 attr, long value)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_VOLTAGE_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);
	pkt.value = __cpu_to_le64(value);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);

	if (rc)
		dev_err(hdev->dev,
			"Failed to set voltage of sensor %d, error %d\n",
			sensor_index, rc);

	return rc;
}

int hl_set_current(struct hl_device *hdev,
			int sensor_index, u32 attr, long value)
{
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_CURRENT_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);
	pkt.value = __cpu_to_le64(value);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);

	if (rc)
		dev_err(hdev->dev,
			"Failed to set current of sensor %d, error %d\n",
			sensor_index, rc);

	return rc;
}

int hl_set_power(struct hl_device *hdev,
			int sensor_index, u32 attr, long value)
{
	struct cpucp_packet pkt;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	if (prop->use_get_power_for_reset_history)
		pkt.ctl = cpu_to_le32(CPUCP_PACKET_POWER_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	else
		pkt.ctl = cpu_to_le32(CPUCP_PACKET_POWER_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);

	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);
	pkt.value = __cpu_to_le64(value);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);

	if (rc)
		dev_err(hdev->dev,
			"Failed to set power of sensor %d, error %d\n",
			sensor_index, rc);

	return rc;
}

int hl_get_power(struct hl_device *hdev,
			int sensor_index, u32 attr, long *value)
{
	struct cpucp_packet pkt;
	u64 result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_POWER_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	*value = (long) result;

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get power of sensor %d, error %d\n",
			sensor_index, rc);
		*value = 0;
	}

	return rc;
}

int hl_hwmon_init(struct hl_device *hdev)
{
	struct device *dev = hdev->pdev ? &hdev->pdev->dev : hdev->dev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	if ((hdev->hwmon_initialized) || !(hdev->cpu_queues_enable))
		return 0;

	if (hdev->hl_chip_info->info) {
		hdev->hl_chip_info->ops = &hl_hwmon_ops;

		hdev->hwmon_dev = hwmon_device_register_with_info(dev,
					prop->cpucp_info.card_name, hdev,
					hdev->hl_chip_info, NULL);
		if (IS_ERR(hdev->hwmon_dev)) {
			rc = PTR_ERR(hdev->hwmon_dev);
			dev_err(hdev->dev,
				"Unable to register hwmon device: %d\n", rc);
			return rc;
		}

		dev_info(hdev->dev, "%s: add sensors information\n",
			dev_name(hdev->hwmon_dev));

		hdev->hwmon_initialized = true;
	} else {
		dev_info(hdev->dev, "no available sensors\n");
	}

	return 0;
}

void hl_hwmon_fini(struct hl_device *hdev)
{
	if (!hdev->hwmon_initialized)
		return;

	hwmon_device_unregister(hdev->hwmon_dev);
}

void hl_hwmon_release_resources(struct hl_device *hdev)
{
	const struct hwmon_channel_info * const *channel_info_arr;
	int i = 0;

	if (!hdev->hl_chip_info->info)
		return;

	channel_info_arr = hdev->hl_chip_info->info;

	while (channel_info_arr[i]) {
		kfree(channel_info_arr[i]->config);
		kfree(channel_info_arr[i]);
		i++;
	}

	kfree(channel_info_arr);

	hdev->hl_chip_info->info = NULL;
}
