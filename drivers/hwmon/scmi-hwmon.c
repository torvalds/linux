// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface(SCMI) based hwmon sensor driver
 *
 * Copyright (C) 2018-2021 ARM Ltd.
 * Sudeep Holla <sudeep.holla@arm.com>
 */

#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/scmi_protocol.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>

static const struct scmi_sensor_proto_ops *sensor_ops;

struct scmi_sensors {
	const struct scmi_protocol_handle *ph;
	const struct scmi_sensor_info **info[hwmon_max];
};

static inline u64 __pow10(u8 x)
{
	u64 r = 1;

	while (x--)
		r *= 10;

	return r;
}

static int scmi_hwmon_scale(const struct scmi_sensor_info *sensor, u64 *value)
{
	int scale = sensor->scale;
	u64 f;

	switch (sensor->type) {
	case TEMPERATURE_C:
	case VOLTAGE:
	case CURRENT:
		scale += 3;
		break;
	case POWER:
	case ENERGY:
		scale += 6;
		break;
	default:
		break;
	}

	if (scale == 0)
		return 0;

	if (abs(scale) > 19)
		return -E2BIG;

	f = __pow10(abs(scale));
	if (scale > 0)
		*value *= f;
	else
		*value = div64_u64(*value, f);

	return 0;
}

static int scmi_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	int ret;
	u64 value;
	const struct scmi_sensor_info *sensor;
	struct scmi_sensors *scmi_sensors = dev_get_drvdata(dev);

	sensor = *(scmi_sensors->info[type] + channel);
	ret = sensor_ops->reading_get(scmi_sensors->ph, sensor->id, &value);
	if (ret)
		return ret;

	ret = scmi_hwmon_scale(sensor, &value);
	if (!ret)
		*val = value;

	return ret;
}

static int
scmi_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, const char **str)
{
	const struct scmi_sensor_info *sensor;
	struct scmi_sensors *scmi_sensors = dev_get_drvdata(dev);

	sensor = *(scmi_sensors->info[type] + channel);
	*str = sensor->name;

	return 0;
}

static umode_t
scmi_hwmon_is_visible(const void *drvdata, enum hwmon_sensor_types type,
		      u32 attr, int channel)
{
	const struct scmi_sensor_info *sensor;
	const struct scmi_sensors *scmi_sensors = drvdata;

	sensor = *(scmi_sensors->info[type] + channel);
	if (sensor)
		return 0444;

	return 0;
}

static const struct hwmon_ops scmi_hwmon_ops = {
	.is_visible = scmi_hwmon_is_visible,
	.read = scmi_hwmon_read,
	.read_string = scmi_hwmon_read_string,
};

static struct hwmon_chip_info scmi_chip_info = {
	.ops = &scmi_hwmon_ops,
	.info = NULL,
};

static int scmi_hwmon_add_chan_info(struct hwmon_channel_info *scmi_hwmon_chan,
				    struct device *dev, int num,
				    enum hwmon_sensor_types type, u32 config)
{
	int i;
	u32 *cfg = devm_kcalloc(dev, num + 1, sizeof(*cfg), GFP_KERNEL);

	if (!cfg)
		return -ENOMEM;

	scmi_hwmon_chan->type = type;
	scmi_hwmon_chan->config = cfg;
	for (i = 0; i < num; i++, cfg++)
		*cfg = config;

	return 0;
}

static enum hwmon_sensor_types scmi_types[] = {
	[TEMPERATURE_C] = hwmon_temp,
	[VOLTAGE] = hwmon_in,
	[CURRENT] = hwmon_curr,
	[POWER] = hwmon_power,
	[ENERGY] = hwmon_energy,
};

static u32 hwmon_attributes[hwmon_max] = {
	[hwmon_chip] = HWMON_C_REGISTER_TZ,
	[hwmon_temp] = HWMON_T_INPUT | HWMON_T_LABEL,
	[hwmon_in] = HWMON_I_INPUT | HWMON_I_LABEL,
	[hwmon_curr] = HWMON_C_INPUT | HWMON_C_LABEL,
	[hwmon_power] = HWMON_P_INPUT | HWMON_P_LABEL,
	[hwmon_energy] = HWMON_E_INPUT | HWMON_E_LABEL,
};

static int scmi_hwmon_probe(struct scmi_device *sdev)
{
	int i, idx;
	u16 nr_sensors;
	enum hwmon_sensor_types type;
	struct scmi_sensors *scmi_sensors;
	const struct scmi_sensor_info *sensor;
	int nr_count[hwmon_max] = {0}, nr_types = 0;
	const struct hwmon_chip_info *chip_info;
	struct device *hwdev, *dev = &sdev->dev;
	struct hwmon_channel_info *scmi_hwmon_chan;
	const struct hwmon_channel_info **ptr_scmi_ci;
	const struct scmi_handle *handle = sdev->handle;
	struct scmi_protocol_handle *ph;

	if (!handle)
		return -ENODEV;

	sensor_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_SENSOR, &ph);
	if (IS_ERR(sensor_ops))
		return PTR_ERR(sensor_ops);

	nr_sensors = sensor_ops->count_get(ph);
	if (!nr_sensors)
		return -EIO;

	scmi_sensors = devm_kzalloc(dev, sizeof(*scmi_sensors), GFP_KERNEL);
	if (!scmi_sensors)
		return -ENOMEM;

	scmi_sensors->ph = ph;

	for (i = 0; i < nr_sensors; i++) {
		sensor = sensor_ops->info_get(ph, i);
		if (!sensor)
			return -EINVAL;

		switch (sensor->type) {
		case TEMPERATURE_C:
		case VOLTAGE:
		case CURRENT:
		case POWER:
		case ENERGY:
			type = scmi_types[sensor->type];
			if (!nr_count[type])
				nr_types++;
			nr_count[type]++;
			break;
		}
	}

	if (nr_count[hwmon_temp]) {
		nr_count[hwmon_chip]++;
		nr_types++;
	}

	scmi_hwmon_chan = devm_kcalloc(dev, nr_types, sizeof(*scmi_hwmon_chan),
				       GFP_KERNEL);
	if (!scmi_hwmon_chan)
		return -ENOMEM;

	ptr_scmi_ci = devm_kcalloc(dev, nr_types + 1, sizeof(*ptr_scmi_ci),
				   GFP_KERNEL);
	if (!ptr_scmi_ci)
		return -ENOMEM;

	scmi_chip_info.info = ptr_scmi_ci;
	chip_info = &scmi_chip_info;

	for (type = 0; type < hwmon_max; type++) {
		if (!nr_count[type])
			continue;

		scmi_hwmon_add_chan_info(scmi_hwmon_chan, dev, nr_count[type],
					 type, hwmon_attributes[type]);
		*ptr_scmi_ci++ = scmi_hwmon_chan++;

		scmi_sensors->info[type] =
			devm_kcalloc(dev, nr_count[type],
				     sizeof(*scmi_sensors->info), GFP_KERNEL);
		if (!scmi_sensors->info[type])
			return -ENOMEM;
	}

	for (i = nr_sensors - 1; i >= 0 ; i--) {
		sensor = sensor_ops->info_get(ph, i);
		if (!sensor)
			continue;

		switch (sensor->type) {
		case TEMPERATURE_C:
		case VOLTAGE:
		case CURRENT:
		case POWER:
		case ENERGY:
			type = scmi_types[sensor->type];
			idx = --nr_count[type];
			*(scmi_sensors->info[type] + idx) = sensor;
			break;
		}
	}

	hwdev = devm_hwmon_device_register_with_info(dev, "scmi_sensors",
						     scmi_sensors, chip_info,
						     NULL);

	return PTR_ERR_OR_ZERO(hwdev);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_SENSOR, "hwmon" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_hwmon_drv = {
	.name		= "scmi-hwmon",
	.probe		= scmi_hwmon_probe,
	.id_table	= scmi_id_table,
};
module_scmi_driver(scmi_hwmon_drv);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCMI HWMON interface driver");
MODULE_LICENSE("GPL v2");
