// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/pci.h>
#include <linux/hwmon.h>

#define SENSORS_PKT_TIMEOUT		1000000	/* 1s */
#define HWMON_NR_SENSOR_TYPES		(hwmon_pwm + 1)

int hl_build_hwmon_channel_info(struct hl_device *hdev,
				struct armcp_sensor *sensors_arr)
{
	u32 counts[HWMON_NR_SENSOR_TYPES] = {0};
	u32 *sensors_by_type[HWMON_NR_SENSOR_TYPES] = {NULL};
	u32 sensors_by_type_next_index[HWMON_NR_SENSOR_TYPES] = {0};
	struct hwmon_channel_info **channels_info;
	u32 num_sensors_for_type, num_active_sensor_types = 0,
			arr_size = 0, *curr_arr;
	enum hwmon_sensor_types type;
	int rc, i, j;

	for (i = 0 ; i < ARMCP_MAX_SENSORS ; i++) {
		type = le32_to_cpu(sensors_arr[i].type);

		if ((type == 0) && (sensors_arr[i].flags == 0))
			break;

		if (type >= HWMON_NR_SENSOR_TYPES) {
			dev_err(hdev->dev,
				"Got wrong sensor type %d from device\n", type);
			return -EINVAL;
		}

		counts[type]++;
		arr_size++;
	}

	for (i = 0 ; i < HWMON_NR_SENSOR_TYPES ; i++) {
		if (counts[i] == 0)
			continue;

		num_sensors_for_type = counts[i] + 1;
		curr_arr = kcalloc(num_sensors_for_type, sizeof(*curr_arr),
				GFP_KERNEL);
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
		curr_arr[sensors_by_type_next_index[type]++] =
				le32_to_cpu(sensors_arr[i].flags);
	}

	channels_info = kcalloc(num_active_sensor_types + 1,
			sizeof(*channels_info), GFP_KERNEL);
	if (!channels_info) {
		rc = -ENOMEM;
		goto channels_info_array_err;
	}

	for (i = 0 ; i < num_active_sensor_types ; i++) {
		channels_info[i] = kzalloc(sizeof(*channels_info[i]),
				GFP_KERNEL);
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

	hdev->hl_chip_info->info =
			(const struct hwmon_channel_info **)channels_info;

	return 0;

channel_info_err:
	for (i = 0 ; i < num_active_sensor_types ; i++)
		if (channels_info[i]) {
			kfree(channels_info[i]->config);
			kfree(channels_info[i]);
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

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_max:
		case hwmon_temp_crit:
		case hwmon_temp_max_hyst:
		case hwmon_temp_crit_hyst:
			break;
		default:
			return -EINVAL;
		}

		*val = hl_get_temperature(hdev, channel, attr);
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_min:
		case hwmon_in_max:
			break;
		default:
			return -EINVAL;
		}

		*val = hl_get_voltage(hdev, channel, attr);
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
		case hwmon_curr_min:
		case hwmon_curr_max:
			break;
		default:
			return -EINVAL;
		}

		*val = hl_get_current(hdev, channel, attr);
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
		case hwmon_fan_min:
		case hwmon_fan_max:
			break;
		default:
			return -EINVAL;
		}
		*val = hl_get_fan_speed(hdev, channel, attr);
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
		case hwmon_pwm_enable:
			break;
		default:
			return -EINVAL;
		}
		*val = hl_get_pwm_info(hdev, channel, attr);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int hl_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	if (hl_device_disabled_or_in_reset(hdev))
		return -ENODEV;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
		case hwmon_pwm_enable:
			break;
		default:
			return -EINVAL;
		}
		hl_set_pwm_info(hdev, channel, attr, val);
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
			return 0444;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_min:
		case hwmon_in_max:
			return 0444;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
		case hwmon_curr_min:
		case hwmon_curr_max:
			return 0444;
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

long hl_get_temperature(struct hl_device *hdev, int sensor_index, u32 attr)
{
	struct armcp_packet pkt;
	long result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_TEMPERATURE_GET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
			SENSORS_PKT_TIMEOUT, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get temperature from sensor %d, error %d\n",
			sensor_index, rc);
		result = 0;
	}

	return result;
}

long hl_get_voltage(struct hl_device *hdev, int sensor_index, u32 attr)
{
	struct armcp_packet pkt;
	long result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_VOLTAGE_GET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					SENSORS_PKT_TIMEOUT, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get voltage from sensor %d, error %d\n",
			sensor_index, rc);
		result = 0;
	}

	return result;
}

long hl_get_current(struct hl_device *hdev, int sensor_index, u32 attr)
{
	struct armcp_packet pkt;
	long result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_CURRENT_GET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					SENSORS_PKT_TIMEOUT, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get current from sensor %d, error %d\n",
			sensor_index, rc);
		result = 0;
	}

	return result;
}

long hl_get_fan_speed(struct hl_device *hdev, int sensor_index, u32 attr)
{
	struct armcp_packet pkt;
	long result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_FAN_SPEED_GET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					SENSORS_PKT_TIMEOUT, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get fan speed from sensor %d, error %d\n",
			sensor_index, rc);
		result = 0;
	}

	return result;
}

long hl_get_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr)
{
	struct armcp_packet pkt;
	long result;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_PWM_GET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					SENSORS_PKT_TIMEOUT, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get pwm info from sensor %d, error %d\n",
			sensor_index, rc);
		result = 0;
	}

	return result;
}

void hl_set_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr,
			long value)
{
	struct armcp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(ARMCP_PACKET_PWM_SET <<
				ARMCP_PKT_CTL_OPCODE_SHIFT);
	pkt.sensor_index = __cpu_to_le16(sensor_index);
	pkt.type = __cpu_to_le16(attr);
	pkt.value = cpu_to_le64(value);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
					SENSORS_PKT_TIMEOUT, NULL);

	if (rc)
		dev_err(hdev->dev,
			"Failed to set pwm info to sensor %d, error %d\n",
			sensor_index, rc);
}

int hl_hwmon_init(struct hl_device *hdev)
{
	struct device *dev = hdev->pdev ? &hdev->pdev->dev : hdev->dev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	if ((hdev->hwmon_initialized) || !(hdev->fw_loading))
		return 0;

	if (hdev->hl_chip_info->info) {
		hdev->hl_chip_info->ops = &hl_hwmon_ops;

		hdev->hwmon_dev = hwmon_device_register_with_info(dev,
					prop->armcp_info.card_name, hdev,
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
