/*
 * IBM PowerNV platform sensors for temperature/fan/voltage/power
 * Copyright (C) 2014 IBM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#define DRVNAME		"ibmpowernv"
#define pr_fmt(fmt)	DRVNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <linux/platform_device.h>
#include <asm/opal.h>
#include <linux/err.h>

#define MAX_ATTR_LEN	32

/* Sensor suffix name from DT */
#define DT_FAULT_ATTR_SUFFIX		"faulted"
#define DT_DATA_ATTR_SUFFIX		"data"
#define DT_THRESHOLD_ATTR_SUFFIX	"thrs"

/*
 * Enumerates all the types of sensors in the POWERNV platform and does index
 * into 'struct sensor_group'
 */
enum sensors {
	FAN,
	TEMP,
	POWER_SUPPLY,
	POWER_INPUT,
	MAX_SENSOR_TYPE,
};

static struct sensor_group {
	const char *name;
	const char *compatible;
	struct attribute_group group;
	u32 attr_count;
} sensor_groups[] = {
	{"fan", "ibm,opal-sensor-cooling-fan"},
	{"temp", "ibm,opal-sensor-amb-temp"},
	{"in", "ibm,opal-sensor-power-supply"},
	{"power", "ibm,opal-sensor-power"}
};

struct sensor_data {
	u32 id; /* An opaque id of the firmware for each sensor */
	enum sensors type;
	char name[MAX_ATTR_LEN];
	struct device_attribute dev_attr;
};

struct platform_data {
	const struct attribute_group *attr_groups[MAX_SENSOR_TYPE + 1];
	u32 sensors_count; /* Total count of sensors from each group */
};

static ssize_t show_sensor(struct device *dev, struct device_attribute *devattr,
			   char *buf)
{
	struct sensor_data *sdata = container_of(devattr, struct sensor_data,
						 dev_attr);
	ssize_t ret;
	u32 x;

	ret = opal_get_sensor_data(sdata->id, &x);
	if (ret)
		return ret;

	/* Convert temperature to milli-degrees */
	if (sdata->type == TEMP)
		x *= 1000;
	/* Convert power to micro-watts */
	else if (sdata->type == POWER_INPUT)
		x *= 1000000;

	return sprintf(buf, "%u\n", x);
}

static int get_sensor_index_attr(const char *name, u32 *index,
					char *attr)
{
	char *hash_pos = strchr(name, '#');
	char buf[8] = { 0 };
	char *dash_pos;
	u32 copy_len;
	int err;

	if (!hash_pos)
		return -EINVAL;

	dash_pos = strchr(hash_pos, '-');
	if (!dash_pos)
		return -EINVAL;

	copy_len = dash_pos - hash_pos - 1;
	if (copy_len >= sizeof(buf))
		return -EINVAL;

	strncpy(buf, hash_pos + 1, copy_len);

	err = kstrtou32(buf, 10, index);
	if (err)
		return err;

	strncpy(attr, dash_pos + 1, MAX_ATTR_LEN);

	return 0;
}

/*
 * This function translates the DT node name into the 'hwmon' attribute name.
 * IBMPOWERNV device node appear like cooling-fan#2-data, amb-temp#1-thrs etc.
 * which need to be mapped as fan2_input, temp1_max respectively before
 * populating them inside hwmon device class.
 */
static int create_hwmon_attr_name(struct device *dev, enum sensors type,
					 const char *node_name,
					 char *hwmon_attr_name)
{
	char attr_suffix[MAX_ATTR_LEN];
	char *attr_name;
	u32 index;
	int err;

	err = get_sensor_index_attr(node_name, &index, attr_suffix);
	if (err) {
		dev_err(dev, "Sensor device node name '%s' is invalid\n",
			node_name);
		return err;
	}

	if (!strcmp(attr_suffix, DT_FAULT_ATTR_SUFFIX)) {
		attr_name = "fault";
	} else if (!strcmp(attr_suffix, DT_DATA_ATTR_SUFFIX)) {
		attr_name = "input";
	} else if (!strcmp(attr_suffix, DT_THRESHOLD_ATTR_SUFFIX)) {
		if (type == TEMP)
			attr_name = "max";
		else if (type == FAN)
			attr_name = "min";
		else
			return -ENOENT;
	} else {
		return -ENOENT;
	}

	snprintf(hwmon_attr_name, MAX_ATTR_LEN, "%s%d_%s",
		 sensor_groups[type].name, index, attr_name);
	return 0;
}

static int get_sensor_type(struct device_node *np)
{
	enum sensors type;

	for (type = 0; type < MAX_SENSOR_TYPE; type++) {
		if (of_device_is_compatible(np, sensor_groups[type].compatible))
			return type;
	}
	return MAX_SENSOR_TYPE;
}

static int populate_attr_groups(struct platform_device *pdev)
{
	struct platform_data *pdata = platform_get_drvdata(pdev);
	const struct attribute_group **pgroups = pdata->attr_groups;
	struct device_node *opal, *np;
	enum sensors type;

	opal = of_find_node_by_path("/ibm,opal/sensors");
	for_each_child_of_node(opal, np) {
		if (np->name == NULL)
			continue;

		type = get_sensor_type(np);
		if (type != MAX_SENSOR_TYPE)
			sensor_groups[type].attr_count++;
	}

	of_node_put(opal);

	for (type = 0; type < MAX_SENSOR_TYPE; type++) {
		sensor_groups[type].group.attrs = devm_kzalloc(&pdev->dev,
					sizeof(struct attribute *) *
					(sensor_groups[type].attr_count + 1),
					GFP_KERNEL);
		if (!sensor_groups[type].group.attrs)
			return -ENOMEM;

		pgroups[type] = &sensor_groups[type].group;
		pdata->sensors_count += sensor_groups[type].attr_count;
		sensor_groups[type].attr_count = 0;
	}

	return 0;
}

/*
 * Iterate through the device tree for each child of 'sensors' node, create
 * a sysfs attribute file, the file is named by translating the DT node name
 * to the name required by the higher 'hwmon' driver like fan1_input, temp1_max
 * etc..
 */
static int create_device_attrs(struct platform_device *pdev)
{
	struct platform_data *pdata = platform_get_drvdata(pdev);
	const struct attribute_group **pgroups = pdata->attr_groups;
	struct device_node *opal, *np;
	struct sensor_data *sdata;
	u32 sensor_id;
	enum sensors type;
	u32 count = 0;
	int err = 0;

	opal = of_find_node_by_path("/ibm,opal/sensors");
	sdata = devm_kzalloc(&pdev->dev, pdata->sensors_count * sizeof(*sdata),
			     GFP_KERNEL);
	if (!sdata) {
		err = -ENOMEM;
		goto exit_put_node;
	}

	for_each_child_of_node(opal, np) {
		if (np->name == NULL)
			continue;

		type = get_sensor_type(np);
		if (type == MAX_SENSOR_TYPE)
			continue;

		if (of_property_read_u32(np, "sensor-id", &sensor_id)) {
			dev_info(&pdev->dev,
				 "'sensor-id' missing in the node '%s'\n",
				 np->name);
			continue;
		}

		sdata[count].id = sensor_id;
		sdata[count].type = type;
		err = create_hwmon_attr_name(&pdev->dev, type, np->name,
					     sdata[count].name);
		if (err)
			goto exit_put_node;

		sysfs_attr_init(&sdata[count].dev_attr.attr);
		sdata[count].dev_attr.attr.name = sdata[count].name;
		sdata[count].dev_attr.attr.mode = S_IRUGO;
		sdata[count].dev_attr.show = show_sensor;

		pgroups[type]->attrs[sensor_groups[type].attr_count++] =
				&sdata[count++].dev_attr.attr;
	}

exit_put_node:
	of_node_put(opal);
	return err;
}

static int ibmpowernv_probe(struct platform_device *pdev)
{
	struct platform_data *pdata;
	struct device *hwmon_dev;
	int err;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, pdata);
	pdata->sensors_count = 0;
	err = populate_attr_groups(pdev);
	if (err)
		return err;

	/* Create sysfs attribute data for each sensor found in the DT */
	err = create_device_attrs(pdev);
	if (err)
		return err;

	/* Finally, register with hwmon */
	hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev, DRVNAME,
							   pdata,
							   pdata->attr_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct platform_device_id opal_sensor_driver_ids[] = {
	{
		.name = "opal-sensor",
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, opal_sensor_driver_ids);

static struct platform_driver ibmpowernv_driver = {
	.probe		= ibmpowernv_probe,
	.id_table	= opal_sensor_driver_ids,
	.driver		= {
		.name	= DRVNAME,
	},
};

module_platform_driver(ibmpowernv_driver);

MODULE_AUTHOR("Neelesh Gupta <neelegup@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("IBM POWERNV platform sensors");
MODULE_LICENSE("GPL");
