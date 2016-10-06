/*
 * System Control and Power Interface(SCPI) based hwmon sensor driver
 *
 * Copyright (C) 2015 ARM Ltd.
 * Punit Agrawal <punit.agrawal@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scpi_protocol.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>

struct sensor_data {
	struct scpi_sensor_info info;
	struct device_attribute dev_attr_input;
	struct device_attribute dev_attr_label;
	char input[20];
	char label[20];
};

struct scpi_thermal_zone {
	int sensor_id;
	struct scpi_sensors *scpi_sensors;
};

struct scpi_sensors {
	struct scpi_ops *scpi_ops;
	struct sensor_data *data;
	struct list_head thermal_zones;
	struct attribute **attrs;
	struct attribute_group group;
	const struct attribute_group *groups[2];
};

static int scpi_read_temp(void *dev, int *temp)
{
	struct scpi_thermal_zone *zone = dev;
	struct scpi_sensors *scpi_sensors = zone->scpi_sensors;
	struct scpi_ops *scpi_ops = scpi_sensors->scpi_ops;
	struct sensor_data *sensor = &scpi_sensors->data[zone->sensor_id];
	u64 value;
	int ret;

	ret = scpi_ops->sensor_get_value(sensor->info.sensor_id, &value);
	if (ret)
		return ret;

	*temp = value;
	return 0;
}

/* hwmon callback functions */
static ssize_t
scpi_show_sensor(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scpi_sensors *scpi_sensors = dev_get_drvdata(dev);
	struct scpi_ops *scpi_ops = scpi_sensors->scpi_ops;
	struct sensor_data *sensor;
	u64 value;
	int ret;

	sensor = container_of(attr, struct sensor_data, dev_attr_input);

	ret = scpi_ops->sensor_get_value(sensor->info.sensor_id, &value);
	if (ret)
		return ret;

	return sprintf(buf, "%llu\n", value);
}

static ssize_t
scpi_show_label(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_data *sensor;

	sensor = container_of(attr, struct sensor_data, dev_attr_label);

	return sprintf(buf, "%s\n", sensor->info.name);
}

static struct thermal_zone_of_device_ops scpi_sensor_ops = {
	.get_temp = scpi_read_temp,
};

static int scpi_hwmon_probe(struct platform_device *pdev)
{
	u16 nr_sensors, i;
	int num_temp = 0, num_volt = 0, num_current = 0, num_power = 0;
	int num_energy = 0;
	struct scpi_ops *scpi_ops;
	struct device *hwdev, *dev = &pdev->dev;
	struct scpi_sensors *scpi_sensors;
	int idx, ret;

	scpi_ops = get_scpi_ops();
	if (!scpi_ops)
		return -EPROBE_DEFER;

	ret = scpi_ops->sensor_get_capability(&nr_sensors);
	if (ret)
		return ret;

	if (!nr_sensors)
		return -ENODEV;

	scpi_sensors = devm_kzalloc(dev, sizeof(*scpi_sensors), GFP_KERNEL);
	if (!scpi_sensors)
		return -ENOMEM;

	scpi_sensors->data = devm_kcalloc(dev, nr_sensors,
				   sizeof(*scpi_sensors->data), GFP_KERNEL);
	if (!scpi_sensors->data)
		return -ENOMEM;

	scpi_sensors->attrs = devm_kcalloc(dev, (nr_sensors * 2) + 1,
				   sizeof(*scpi_sensors->attrs), GFP_KERNEL);
	if (!scpi_sensors->attrs)
		return -ENOMEM;

	scpi_sensors->scpi_ops = scpi_ops;

	for (i = 0, idx = 0; i < nr_sensors; i++) {
		struct sensor_data *sensor = &scpi_sensors->data[idx];

		ret = scpi_ops->sensor_get_info(i, &sensor->info);
		if (ret)
			return ret;

		switch (sensor->info.class) {
		case TEMPERATURE:
			snprintf(sensor->input, sizeof(sensor->input),
				 "temp%d_input", num_temp + 1);
			snprintf(sensor->label, sizeof(sensor->input),
				 "temp%d_label", num_temp + 1);
			num_temp++;
			break;
		case VOLTAGE:
			snprintf(sensor->input, sizeof(sensor->input),
				 "in%d_input", num_volt);
			snprintf(sensor->label, sizeof(sensor->input),
				 "in%d_label", num_volt);
			num_volt++;
			break;
		case CURRENT:
			snprintf(sensor->input, sizeof(sensor->input),
				 "curr%d_input", num_current + 1);
			snprintf(sensor->label, sizeof(sensor->input),
				 "curr%d_label", num_current + 1);
			num_current++;
			break;
		case POWER:
			snprintf(sensor->input, sizeof(sensor->input),
				 "power%d_input", num_power + 1);
			snprintf(sensor->label, sizeof(sensor->input),
				 "power%d_label", num_power + 1);
			num_power++;
			break;
		case ENERGY:
			snprintf(sensor->input, sizeof(sensor->input),
				 "energy%d_input", num_energy + 1);
			snprintf(sensor->label, sizeof(sensor->input),
				 "energy%d_label", num_energy + 1);
			num_energy++;
			break;
		default:
			continue;
		}

		sensor->dev_attr_input.attr.mode = S_IRUGO;
		sensor->dev_attr_input.show = scpi_show_sensor;
		sensor->dev_attr_input.attr.name = sensor->input;

		sensor->dev_attr_label.attr.mode = S_IRUGO;
		sensor->dev_attr_label.show = scpi_show_label;
		sensor->dev_attr_label.attr.name = sensor->label;

		scpi_sensors->attrs[idx << 1] = &sensor->dev_attr_input.attr;
		scpi_sensors->attrs[(idx << 1) + 1] = &sensor->dev_attr_label.attr;

		sysfs_attr_init(scpi_sensors->attrs[idx << 1]);
		sysfs_attr_init(scpi_sensors->attrs[(idx << 1) + 1]);
		idx++;
	}

	scpi_sensors->group.attrs = scpi_sensors->attrs;
	scpi_sensors->groups[0] = &scpi_sensors->group;

	platform_set_drvdata(pdev, scpi_sensors);

	hwdev = devm_hwmon_device_register_with_groups(dev,
			"scpi_sensors", scpi_sensors, scpi_sensors->groups);

	if (IS_ERR(hwdev))
		return PTR_ERR(hwdev);

	/*
	 * Register the temperature sensors with the thermal framework
	 * to allow their usage in setting up the thermal zones from
	 * device tree.
	 *
	 * NOTE: Not all temperature sensors maybe used for thermal
	 * control
	 */
	INIT_LIST_HEAD(&scpi_sensors->thermal_zones);
	for (i = 0; i < nr_sensors; i++) {
		struct sensor_data *sensor = &scpi_sensors->data[i];
		struct thermal_zone_device *z;
		struct scpi_thermal_zone *zone;

		if (sensor->info.class != TEMPERATURE)
			continue;

		zone = devm_kzalloc(dev, sizeof(*zone), GFP_KERNEL);
		if (!zone)
			return -ENOMEM;

		zone->sensor_id = i;
		zone->scpi_sensors = scpi_sensors;
		z = devm_thermal_zone_of_sensor_register(dev,
							 sensor->info.sensor_id,
							 zone,
							 &scpi_sensor_ops);
		/*
		 * The call to thermal_zone_of_sensor_register returns
		 * an error for sensors that are not associated with
		 * any thermal zones or if the thermal subsystem is
		 * not configured.
		 */
		if (IS_ERR(z)) {
			devm_kfree(dev, zone);
			continue;
		}
	}

	return 0;
}

static const struct of_device_id scpi_of_match[] = {
	{.compatible = "arm,scpi-sensors"},
	{},
};

static struct platform_driver scpi_hwmon_platdrv = {
	.driver = {
		.name	= "scpi-hwmon",
		.of_match_table = scpi_of_match,
	},
	.probe		= scpi_hwmon_probe,
};
module_platform_driver(scpi_hwmon_platdrv);

MODULE_AUTHOR("Punit Agrawal <punit.agrawal@arm.com>");
MODULE_DESCRIPTION("ARM SCPI HWMON interface driver");
MODULE_LICENSE("GPL v2");
