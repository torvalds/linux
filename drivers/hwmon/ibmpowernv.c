/*
 * hwmon driver for temperature/power/fan on IBM PowerNV platform
 * Copyright (C) 2013 IBM
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <asm/opal.h>
#include <linux/err.h>

MODULE_DESCRIPTION("IBM PowerNV Platform power/temp/fan sensor hwmon module");
MODULE_LICENSE("GPL");

#define MAX_ATTR_LENGTH		32

/* Device tree sensor name prefixes. The device tree has the names in the
 * format "cooling-fan#2-faulted" where the "cooling-fan" is the sensor type,
 * 2 is the sensor count, and "faulted" is the sensor data attribute type.
 */
#define DT_FAULT_ATTR_SUFFIX		"faulted"
#define DT_DATA_ATTR_SUFFIX		"data"
#define DT_THRESHOLD_ATTR_SUFFIX	"thrs"

enum sensors {
	FAN,
	TEMPERATURE,
	POWERSUPPLY,
	POWER,
	MAX_SENSOR_TYPE,
};

enum attributes {
	INPUT,
	MINIMUM,
	MAXIMUM,
	FAULT,
	MAX_ATTR_TYPES
};

static struct sensor_name {
	char *name;
	char *compaible;
} sensor_names[] = {
		{"fan-sensor", "ibm,opal-sensor-cooling-fan"},
		{"amb-temp-sensor", "ibm,opal-sensor-amb-temp"},
		{"power-sensor", "ibm,opal-sensor-power-supply"},
		{"power", "ibm,opal-sensor-power"}
};

static const char * const attribute_type_table[] = {
	"input",
	"min",
	"max",
	"fault",
	NULL
};

struct pdev_entry {
	struct list_head list;
	struct platform_device *pdev;
	enum sensors type;
};

static LIST_HEAD(pdev_list);

/* The sensors are categorised on type.
 *
 * The sensors of same type are categorised under a common platform device.
 * So, The pdev is shared by all sensors of same type.
 * Ex : temp1_input, temp1_max, temp2_input,temp2_max all share same platform
 * device.
 *
 * "sensor_data" is the Platform device specific data.
 * There is one hwmon_device instance for all the sensors of same type.
 * This also holds the list of all sensors with same type but different
 * attribute and index.
 */
struct sensor_specific_data {
	u32 sensor_id; /* The hex value as in the device tree */
	u32 sensor_index; /* The sensor instance index */
	struct sensor_device_attribute sd_attr;
	enum attributes attr_type;
	char attr_name[64];
};

struct sensor_data {
	struct device *hwmon_dev;
	struct list_head sensor_list;
	struct device_attribute name_attr;
};

struct  sensor_entry {
	struct list_head list;
	struct sensor_specific_data *sensor_data;
};

static struct platform_device *powernv_sensor_get_pdev(enum sensors type)
{
	struct pdev_entry *p;
	list_for_each_entry(p, &pdev_list, list)
		if (p->type == type)
			return p->pdev;

	return NULL;
}

static struct sensor_specific_data *powernv_sensor_get_sensor_data(
					struct sensor_data *pdata,
					int index, enum attributes attr_type)
{
	struct sensor_entry *p;
	list_for_each_entry(p, &pdata->sensor_list, list)
		if ((p->sensor_data->sensor_index == index) &&
		    (attr_type == p->sensor_data->attr_type))
			return p->sensor_data;

	return NULL;
}

static ssize_t show_name(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);

	return sprintf(buf, "%s\n", pdev->name);
}

/* Note: Data from the sensors for each sensor type needs to be converted to
 * the dimension appropriate.
 */
static ssize_t show_sensor(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(devattr);
	struct platform_device *pdev = to_platform_device(dev);
	struct sensor_data *pdata = platform_get_drvdata(pdev);
	struct sensor_specific_data *tdata = NULL;
	enum sensors sensor_type = pdev->id;
	u32 x = -1;
	int ret;

	if (sd_attr && sd_attr->dev_attr.attr.name) {
		char *pos = strchr(sd_attr->dev_attr.attr.name, '_');
		int i;

		for (i = 0; i < MAX_ATTR_TYPES; i++) {
			if (strcmp(pos+1, attribute_type_table[i]) == 0) {
				tdata = powernv_sensor_get_sensor_data(pdata,
						sd_attr->index, i);
				break;
			}
		}
	}

	if (tdata) {
		ret = opal_get_sensor_data(tdata->sensor_id, &x);
		if (ret)
			x = -1;
	}

	if (sensor_type == TEMPERATURE && x > 0) {
		/* Temperature comes in Degrees and convert it to
		 * milli-degrees.
		 */
		x = x*1000;
	} else if (sensor_type == POWER && x > 0) {
		/* Power value comes in watts, convert to micro-watts */
		x = x * 1000000;
	}

	return sprintf(buf, "%d\n", x);
}

static u32 get_sensor_index_from_name(const char *name)
{
	char *hash_position = strchr(name, '#');
	u32 index = 0, copy_length;
	char newbuf[8];

	if (hash_position) {
		copy_length = strchr(hash_position, '-') - hash_position - 1;
		if (copy_length < sizeof(newbuf)) {
			strncpy(newbuf, hash_position + 1, copy_length);
			sscanf(newbuf, "%d", &index);
		}
	}

	return index;
}

static inline void get_sensor_suffix_from_name(const char *name, char *suffix)
{
	char *dash_position = strrchr(name, '-');
	if (dash_position)
		strncpy(suffix, dash_position+1, MAX_ATTR_LENGTH);
	else
		strcpy(suffix,"");
}

static int get_sensor_attr_properties(const char *sensor_name,
		enum sensors sensor_type, enum attributes *attr_type,
		u32 *sensor_index)
{
	char suffix[MAX_ATTR_LENGTH];

	*attr_type = MAX_ATTR_TYPES;
	*sensor_index = get_sensor_index_from_name(sensor_name);
	if (*sensor_index == 0)
		return -EINVAL;

	get_sensor_suffix_from_name(sensor_name, suffix);
	if (strcmp(suffix, "") == 0)
		return -EINVAL;

	if (strcmp(suffix, DT_FAULT_ATTR_SUFFIX) == 0)
		*attr_type = FAULT;
	else if (strcmp(suffix, DT_DATA_ATTR_SUFFIX) == 0)
		*attr_type = INPUT;
	else if ((sensor_type == TEMPERATURE) &&
			(strcmp(suffix, DT_THRESHOLD_ATTR_SUFFIX) == 0))
		*attr_type = MAXIMUM;
	else if ((sensor_type == FAN) &&
			(strcmp(suffix, DT_THRESHOLD_ATTR_SUFFIX) == 0))
		*attr_type = MINIMUM;
	else
		return -ENOENT;

	if (((sensor_type == FAN) && ((*attr_type == INPUT) ||
				    (*attr_type == MINIMUM)))
	    || ((sensor_type == TEMPERATURE) && ((*attr_type == INPUT) ||
						 (*attr_type == MAXIMUM)))
	    || ((sensor_type == POWER) && ((*attr_type == INPUT))))
		return 0;

	return -ENOENT;
}

static int create_sensor_attr(struct sensor_specific_data *tdata,
		struct device *dev, enum sensors sensor_type,
		enum attributes attr_type)
{
	int err = 0;
	char temp_file_prefix[50];
	static const char *const file_name_format = "%s%d_%s";

	tdata->attr_type = attr_type;

	if (sensor_type == FAN)
		strcpy(temp_file_prefix, "fan");
	else if (sensor_type == TEMPERATURE)
		strcpy(temp_file_prefix, "temp");
	else if (sensor_type == POWERSUPPLY)
		strcpy(temp_file_prefix, "powersupply");
	else if (sensor_type == POWER)
		strcpy(temp_file_prefix, "power");

	snprintf(tdata->attr_name, sizeof(tdata->attr_name), file_name_format,
		 temp_file_prefix, tdata->sensor_index,
		 attribute_type_table[tdata->attr_type]);

	sysfs_attr_init(&tdata->sd_attr.dev_attr.attr);
	tdata->sd_attr.dev_attr.attr.name = tdata->attr_name;
	tdata->sd_attr.dev_attr.attr.mode = S_IRUGO;
	tdata->sd_attr.dev_attr.show = show_sensor;

	tdata->sd_attr.index = tdata->sensor_index;
	err = device_create_file(dev, &tdata->sd_attr.dev_attr);

	return err;
}

static int create_name_attr(struct sensor_data *pdata,
				struct device *dev)
{
	sysfs_attr_init(&pdata->name_attr.attr);
	pdata->name_attr.attr.name = "name";
	pdata->name_attr.attr.mode = S_IRUGO;
	pdata->name_attr.show = show_name;
	return device_create_file(dev, &pdata->name_attr);
}

static int create_platform_device(enum sensors sensor_type,
					struct platform_device **pdev)
{
	struct pdev_entry *pdev_entry = NULL;
	int err;

	*pdev = platform_device_alloc(sensor_names[sensor_type].name,
			sensor_type);
	if (!*pdev) {
		pr_err("Device allocation failed\n");
		err = -ENOMEM;
		goto exit;
	}

	pdev_entry = kzalloc(sizeof(struct pdev_entry), GFP_KERNEL);
	if (!pdev_entry) {
		pr_err("Device allocation failed\n");
		err = -ENOMEM;
		goto exit_device_put;
	}

	err = platform_device_add(*pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
		goto exit_device_free;
	}

	pdev_entry->pdev = *pdev;
	pdev_entry->type = (*pdev)->id;

	list_add_tail(&pdev_entry->list, &pdev_list);

	return 0;
exit_device_free:
	kfree(pdev_entry);
exit_device_put:
	platform_device_put(*pdev);
exit:
	return err;
}

static int create_sensor_data(struct platform_device *pdev)
{
	struct sensor_data *pdata = NULL;
	int err = 0;

	pdata = kzalloc(sizeof(struct sensor_data), GFP_KERNEL);
	if (!pdata) {
		err = -ENOMEM;
		goto exit;
	}

	err = create_name_attr(pdata, &pdev->dev);
	if (err)
		goto exit_free;

	pdata->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(pdata->hwmon_dev)) {
		err = PTR_ERR(pdata->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n",
			err);
		goto exit_name;
	}

	INIT_LIST_HEAD(&pdata->sensor_list);
	platform_set_drvdata(pdev, pdata);

	return 0;

exit_name:
	device_remove_file(&pdev->dev, &pdata->name_attr);
exit_free:
	kfree(pdata);
exit:
	return err;
}

static void delete_sensor_attr(struct sensor_data *pdata)
{
	struct sensor_entry *s, *l;

	list_for_each_entry_safe(s, l, &pdata->sensor_list, list) {
		struct sensor_specific_data *tdata = s->sensor_data;
			kfree(tdata);
			list_del(&s->list);
			kfree(s);
		}
}

static int powernv_sensor_init(u32 sensor_id, const struct device_node *np,
		enum sensors sensor_type, enum attributes attr_type,
		u32 sensor_index)
{
	struct platform_device *pdev = powernv_sensor_get_pdev(sensor_type);
	struct sensor_specific_data *tdata;
	struct sensor_entry *sensor_entry;
	struct sensor_data *pdata;
	int err = 0;

	if (!pdev) {
		err = create_platform_device(sensor_type, &pdev);
		if (err)
			goto exit;

		err = create_sensor_data(pdev);
		if (err)
			goto exit;
	}

	pdata = platform_get_drvdata(pdev);
	if (!pdata) {
		err = -ENOMEM;
		goto exit;
	}

	tdata = kzalloc(sizeof(struct sensor_specific_data), GFP_KERNEL);
	if (!tdata) {
		err = -ENOMEM;
		goto exit;
	}

	tdata->sensor_id = sensor_id;
	tdata->sensor_index = sensor_index;

	err = create_sensor_attr(tdata, &pdev->dev, sensor_type, attr_type);
	if (err)
		goto exit_free;

	sensor_entry = kzalloc(sizeof(struct sensor_entry), GFP_KERNEL);
	if (!sensor_entry) {
		err = -ENOMEM;
		goto exit_attr;
	}

	sensor_entry->sensor_data = tdata;

	list_add_tail(&sensor_entry->list, &pdata->sensor_list);

	return 0;
exit_attr:
	device_remove_file(&pdev->dev, &tdata->sd_attr.dev_attr);
exit_free:
	kfree(tdata);
exit:
	return err;
}

static void delete_unregister_sensors(void)
{
	struct pdev_entry *p, *n;

	list_for_each_entry_safe(p, n, &pdev_list, list) {
		struct sensor_data *pdata = platform_get_drvdata(p->pdev);
			if (pdata) {
				delete_sensor_attr(pdata);

				hwmon_device_unregister(pdata->hwmon_dev);
				kfree(pdata);
			}
		platform_device_unregister(p->pdev);
		list_del(&p->list);
		kfree(p);
	}
}

static int __init powernv_hwmon_init(void)
{
	struct device_node *opal, *np = NULL;
	enum attributes attr_type;
	enum sensors type;
	const u32 *sensor_id;
	u32 sensor_index;
	int err;

	opal = of_find_node_by_path("/ibm,opal/sensors");
	if (!opal) {
		pr_err("%s: Opal 'sensors' node not found\n", __func__);
		return -ENXIO;
	}

	for_each_child_of_node(opal, np) {
		if (np->name == NULL)
			continue;

		for (type = 0; type < MAX_SENSOR_TYPE; type++)
			if (of_device_is_compatible(np,
					sensor_names[type].compaible))
				break;

		if (type == MAX_SENSOR_TYPE)
			continue;

		if (get_sensor_attr_properties(np->name, type, &attr_type,
				&sensor_index))
			continue;

		sensor_id = of_get_property(np, "sensor-id", NULL);
		if (!sensor_id) {
			pr_info("%s: %s doesn't have sensor-id\n", __func__,
					np->name);
			continue;
		}

		err = powernv_sensor_init(*sensor_id, np, type, attr_type,
				sensor_index);
		if (err) {
			of_node_put(opal);
			goto exit;
		}
	}
	of_node_put(opal);

	return 0;
exit:
	delete_unregister_sensors();
	return err;

}

static void powernv_hwmon_exit(void)
{
	delete_unregister_sensors();
}

module_init(powernv_hwmon_init);
module_exit(powernv_hwmon_exit);
