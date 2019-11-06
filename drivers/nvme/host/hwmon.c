// SPDX-License-Identifier: GPL-2.0
/*
 * NVM Express hardware monitoring support
 * Copyright (c) 2019, Guenter Roeck
 */

#include <linux/hwmon.h>
#include <asm/unaligned.h>

#include "nvme.h"

struct nvme_hwmon_data {
	struct nvme_ctrl *ctrl;
	struct nvme_smart_log log;
	struct mutex read_lock;
};

static int nvme_hwmon_get_smart_log(struct nvme_hwmon_data *data)
{
	int ret;

	ret = nvme_get_log(data->ctrl, NVME_NSID_ALL, NVME_LOG_SMART, 0,
			   &data->log, sizeof(data->log), 0);

	return ret <= 0 ? ret : -EIO;
}

static int nvme_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct nvme_hwmon_data *data = dev_get_drvdata(dev);
	struct nvme_smart_log *log = &data->log;
	int temp;
	int err;

	/*
	 * First handle attributes which don't require us to read
	 * the smart log.
	 */
	switch (attr) {
	case hwmon_temp_max:
		*val = (data->ctrl->wctemp - 273) * 1000;
		return 0;
	case hwmon_temp_crit:
		*val = (data->ctrl->cctemp - 273) * 1000;
		return 0;
	default:
		break;
	}

	mutex_lock(&data->read_lock);
	err = nvme_hwmon_get_smart_log(data);
	if (err)
		goto unlock;

	switch (attr) {
	case hwmon_temp_input:
		if (!channel)
			temp = get_unaligned_le16(log->temperature);
		else
			temp = le16_to_cpu(log->temp_sensor[channel - 1]);
		*val = (temp - 273) * 1000;
		break;
	case hwmon_temp_alarm:
		*val = !!(log->critical_warning & NVME_SMART_CRIT_TEMPERATURE);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
unlock:
	mutex_unlock(&data->read_lock);
	return err;
}

static const char * const nvme_hwmon_sensor_names[] = {
	"Composite",
	"Sensor 1",
	"Sensor 2",
	"Sensor 3",
	"Sensor 4",
	"Sensor 5",
	"Sensor 6",
	"Sensor 7",
	"Sensor 8",
};

static int nvme_hwmon_read_string(struct device *dev,
				  enum hwmon_sensor_types type, u32 attr,
				  int channel, const char **str)
{
	*str = nvme_hwmon_sensor_names[channel];
	return 0;
}

static umode_t nvme_hwmon_is_visible(const void *_data,
				     enum hwmon_sensor_types type,
				     u32 attr, int channel)
{
	const struct nvme_hwmon_data *data = _data;

	switch (attr) {
	case hwmon_temp_crit:
		if (!channel && data->ctrl->cctemp)
			return 0444;
		break;
	case hwmon_temp_max:
		if (!channel && data->ctrl->wctemp)
			return 0444;
		break;
	case hwmon_temp_alarm:
		if (!channel)
			return 0444;
		break;
	case hwmon_temp_input:
	case hwmon_temp_label:
		if (!channel || data->log.temp_sensor[channel - 1])
			return 0444;
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info *nvme_hwmon_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
				HWMON_T_LABEL | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_ops nvme_hwmon_ops = {
	.is_visible	= nvme_hwmon_is_visible,
	.read		= nvme_hwmon_read,
	.read_string	= nvme_hwmon_read_string,
};

static const struct hwmon_chip_info nvme_hwmon_chip_info = {
	.ops	= &nvme_hwmon_ops,
	.info	= nvme_hwmon_info,
};

void nvme_hwmon_init(struct nvme_ctrl *ctrl)
{
	struct device *dev = ctrl->dev;
	struct nvme_hwmon_data *data;
	struct device *hwmon;
	int err;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	data->ctrl = ctrl;
	mutex_init(&data->read_lock);

	err = nvme_hwmon_get_smart_log(data);
	if (err) {
		dev_warn(dev, "Failed to read smart log (error %d)\n", err);
		devm_kfree(dev, data);
		return;
	}

	hwmon = devm_hwmon_device_register_with_info(dev, "nvme", data,
						     &nvme_hwmon_chip_info,
						     NULL);
	if (IS_ERR(hwmon)) {
		dev_warn(dev, "Failed to instantiate hwmon device\n");
		devm_kfree(dev, data);
	}
}
