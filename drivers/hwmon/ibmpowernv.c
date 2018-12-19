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
#include <asm/cputhreads.h>
#include <asm/smp.h>

#define MAX_ATTR_LEN	32
#define MAX_LABEL_LEN	64

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

#define INVALID_INDEX (-1U)

static struct sensor_group {
	const char *name;
	const char *compatible;
	struct attribute_group group;
	u32 attr_count;
	u32 hwmon_index;
} sensor_groups[] = {
	{"fan", "ibm,opal-sensor-cooling-fan"},
	{"temp", "ibm,opal-sensor-amb-temp"},
	{"in", "ibm,opal-sensor-power-supply"},
	{"power", "ibm,opal-sensor-power"}
};

struct sensor_data {
	u32 id; /* An opaque id of the firmware for each sensor */
	u32 hwmon_index;
	u32 opal_index;
	enum sensors type;
	char label[MAX_LABEL_LEN];
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

static ssize_t show_label(struct device *dev, struct device_attribute *devattr,
			  char *buf)
{
	struct sensor_data *sdata = container_of(devattr, struct sensor_data,
						 dev_attr);

	return sprintf(buf, "%s\n", sdata->label);
}

static int get_logical_cpu(int hwcpu)
{
	int cpu;

	for_each_possible_cpu(cpu)
		if (get_hard_smp_processor_id(cpu) == hwcpu)
			return cpu;

	return -ENOENT;
}

static void make_sensor_label(struct device_node *np,
			      struct sensor_data *sdata, const char *label)
{
	u32 id;
	size_t n;

	n = snprintf(sdata->label, sizeof(sdata->label), "%s", label);

	/*
	 * Core temp pretty print
	 */
	if (!of_property_read_u32(np, "ibm,pir", &id)) {
		int cpuid = get_logical_cpu(id);

		if (cpuid >= 0)
			/*
			 * The digital thermal sensors are associated
			 * with a core. Let's print out the range of
			 * cpu ids corresponding to the hardware
			 * threads of the core.
			 */
			n += snprintf(sdata->label + n,
				      sizeof(sdata->label) - n, " %d-%d",
				      cpuid, cpuid + threads_per_core - 1);
		else
			n += snprintf(sdata->label + n,
				      sizeof(sdata->label) - n, " phy%d", id);
	}

	/*
	 * Membuffer pretty print
	 */
	if (!of_property_read_u32(np, "ibm,chip-id", &id))
		n += snprintf(sdata->label + n, sizeof(sdata->label) - n,
			      " %d", id & 0xffff);
}

static int get_sensor_index_attr(const char *name, u32 *index, char *attr)
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

static const char *convert_opal_attr_name(enum sensors type,
					  const char *opal_attr)
{
	const char *attr_name = NULL;

	if (!strcmp(opal_attr, DT_FAULT_ATTR_SUFFIX)) {
		attr_name = "fault";
	} else if (!strcmp(opal_attr, DT_DATA_ATTR_SUFFIX)) {
		attr_name = "input";
	} else if (!strcmp(opal_attr, DT_THRESHOLD_ATTR_SUFFIX)) {
		if (type == TEMP)
			attr_name = "max";
		else if (type == FAN)
			attr_name = "min";
	}

	return attr_name;
}

/*
 * This function translates the DT node name into the 'hwmon' attribute name.
 * IBMPOWERNV device node appear like cooling-fan#2-data, amb-temp#1-thrs etc.
 * which need to be mapped as fan2_input, temp1_max respectively before
 * populating them inside hwmon device class.
 */
static const char *parse_opal_node_name(const char *node_name,
					enum sensors type, u32 *index)
{
	char attr_suffix[MAX_ATTR_LEN];
	const char *attr_name;
	int err;

	err = get_sensor_index_attr(node_name, index, attr_suffix);
	if (err)
		return ERR_PTR(err);

	attr_name = convert_opal_attr_name(type, attr_suffix);
	if (!attr_name)
		return ERR_PTR(-ENOENT);

	return attr_name;
}

static int get_sensor_type(struct device_node *np)
{
	enum sensors type;
	const char *str;

	for (type = 0; type < MAX_SENSOR_TYPE; type++) {
		if (of_device_is_compatible(np, sensor_groups[type].compatible))
			return type;
	}

	/*
	 * Let's check if we have a newer device tree
	 */
	if (!of_device_is_compatible(np, "ibm,opal-sensor"))
		return MAX_SENSOR_TYPE;

	if (of_property_read_string(np, "sensor-type", &str))
		return MAX_SENSOR_TYPE;

	for (type = 0; type < MAX_SENSOR_TYPE; type++)
		if (!strcmp(str, sensor_groups[type].name))
			return type;

	return MAX_SENSOR_TYPE;
}

static u32 get_sensor_hwmon_index(struct sensor_data *sdata,
				  struct sensor_data *sdata_table, int count)
{
	int i;

	/*
	 * We don't use the OPAL index on newer device trees
	 */
	if (sdata->opal_index != INVALID_INDEX) {
		for (i = 0; i < count; i++)
			if (sdata_table[i].opal_index == sdata->opal_index &&
			    sdata_table[i].type == sdata->type)
				return sdata_table[i].hwmon_index;
	}
	return ++sensor_groups[sdata->type].hwmon_index;
}

static int populate_attr_groups(struct platform_device *pdev)
{
	struct platform_data *pdata = platform_get_drvdata(pdev);
	const struct attribute_group **pgroups = pdata->attr_groups;
	struct device_node *opal, *np;
	enum sensors type;

	opal = of_find_node_by_path("/ibm,opal/sensors");
	for_each_child_of_node(opal, np) {
		const char *label;

		if (np->name == NULL)
			continue;

		type = get_sensor_type(np);
		if (type == MAX_SENSOR_TYPE)
			continue;

		sensor_groups[type].attr_count++;

		/*
		 * add a new attribute for labels
		 */
		if (!of_property_read_string(np, "label", &label))
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

static void create_hwmon_attr(struct sensor_data *sdata, const char *attr_name,
			      ssize_t (*show)(struct device *dev,
					      struct device_attribute *attr,
					      char *buf))
{
	snprintf(sdata->name, MAX_ATTR_LEN, "%s%d_%s",
		 sensor_groups[sdata->type].name, sdata->hwmon_index,
		 attr_name);

	sysfs_attr_init(&sdata->dev_attr.attr);
	sdata->dev_attr.attr.name = sdata->name;
	sdata->dev_attr.attr.mode = S_IRUGO;
	sdata->dev_attr.show = show;
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
		const char *attr_name;
		u32 opal_index;
		const char *label;

		if (np->name == NULL)
			continue;

		type = get_sensor_type(np);
		if (type == MAX_SENSOR_TYPE)
			continue;

		/*
		 * Newer device trees use a "sensor-data" property
		 * name for input.
		 */
		if (of_property_read_u32(np, "sensor-id", &sensor_id) &&
		    of_property_read_u32(np, "sensor-data", &sensor_id)) {
			dev_info(&pdev->dev,
				 "'sensor-id' missing in the node '%s'\n",
				 np->name);
			continue;
		}

		sdata[count].id = sensor_id;
		sdata[count].type = type;

		/*
		 * If we can not parse the node name, it means we are
		 * running on a newer device tree. We can just forget
		 * about the OPAL index and use a defaut value for the
		 * hwmon attribute name
		 */
		attr_name = parse_opal_node_name(np->name, type, &opal_index);
		if (IS_ERR(attr_name)) {
			attr_name = "input";
			opal_index = INVALID_INDEX;
		}

		sdata[count].opal_index = opal_index;
		sdata[count].hwmon_index =
			get_sensor_hwmon_index(&sdata[count], sdata, count);

		create_hwmon_attr(&sdata[count], attr_name, show_sensor);

		pgroups[type]->attrs[sensor_groups[type].attr_count++] =
				&sdata[count++].dev_attr.attr;

		if (!of_property_read_string(np, "label", &label)) {
			/*
			 * For the label attribute, we can reuse the
			 * "properties" of the previous "input"
			 * attribute. They are related to the same
			 * sensor.
			 */
			sdata[count].type = type;
			sdata[count].opal_index = sdata[count - 1].opal_index;
			sdata[count].hwmon_index = sdata[count - 1].hwmon_index;

			make_sensor_label(np, &sdata[count], label);

			create_hwmon_attr(&sdata[count], "label", show_label);

			pgroups[type]->attrs[sensor_groups[type].attr_count++] =
				&sdata[count++].dev_attr.attr;
		}
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

static const struct of_device_id opal_sensor_match[] = {
	{ .compatible	= "ibm,opal-sensor" },
	{ },
};
MODULE_DEVICE_TABLE(of, opal_sensor_match);

static struct platform_driver ibmpowernv_driver = {
	.probe		= ibmpowernv_probe,
	.id_table	= opal_sensor_driver_ids,
	.driver		= {
		.name	= DRVNAME,
		.of_match_table	= opal_sensor_match,
	},
};

module_platform_driver(ibmpowernv_driver);

MODULE_AUTHOR("Neelesh Gupta <neelegup@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("IBM POWERNV platform sensors");
MODULE_LICENSE("GPL");
