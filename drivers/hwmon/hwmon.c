/*
 * hwmon.c - part of lm_sensors, Linux kernel modules for hardware monitoring
 *
 * This file defines the sysfs class "hwmon", for use by sensors drivers.
 *
 * Copyright (C) 2005 Mark M. Hoffman <mhoffman@lightlink.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/hwmon.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/thermal.h>

#define HWMON_ID_PREFIX "hwmon"
#define HWMON_ID_FORMAT HWMON_ID_PREFIX "%d"

struct hwmon_device {
	const char *name;
	struct device dev;
	const struct hwmon_chip_info *chip;

	struct attribute_group group;
	const struct attribute_group **groups;
};

#define to_hwmon_device(d) container_of(d, struct hwmon_device, dev)

#define MAX_SYSFS_ATTR_NAME_LENGTH	32

struct hwmon_device_attribute {
	struct device_attribute dev_attr;
	const struct hwmon_ops *ops;
	enum hwmon_sensor_types type;
	u32 attr;
	int index;
	char name[MAX_SYSFS_ATTR_NAME_LENGTH];
};

#define to_hwmon_attr(d) \
	container_of(d, struct hwmon_device_attribute, dev_attr)

/*
 * Thermal zone information
 * In addition to the reference to the hwmon device,
 * also provides the sensor index.
 */
struct hwmon_thermal_data {
	struct hwmon_device *hwdev;	/* Reference to hwmon device */
	int index;			/* sensor index */
};

static ssize_t
name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_hwmon_device(dev)->name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *hwmon_dev_attrs[] = {
	&dev_attr_name.attr,
	NULL
};

static umode_t hwmon_dev_name_is_visible(struct kobject *kobj,
					 struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);

	if (to_hwmon_device(dev)->name == NULL)
		return 0;

	return attr->mode;
}

static const struct attribute_group hwmon_dev_attr_group = {
	.attrs		= hwmon_dev_attrs,
	.is_visible	= hwmon_dev_name_is_visible,
};

static const struct attribute_group *hwmon_dev_attr_groups[] = {
	&hwmon_dev_attr_group,
	NULL
};

static void hwmon_dev_release(struct device *dev)
{
	kfree(to_hwmon_device(dev));
}

static struct class hwmon_class = {
	.name = "hwmon",
	.owner = THIS_MODULE,
	.dev_groups = hwmon_dev_attr_groups,
	.dev_release = hwmon_dev_release,
};

static DEFINE_IDA(hwmon_ida);

/* Thermal zone handling */

/*
 * The complex conditional is necessary to avoid a cyclic dependency
 * between hwmon and thermal_sys modules.
 */
#if IS_REACHABLE(CONFIG_THERMAL) && defined(CONFIG_THERMAL_OF) && \
	(!defined(CONFIG_THERMAL_HWMON) || \
	 !(defined(MODULE) && IS_MODULE(CONFIG_THERMAL)))
static int hwmon_thermal_get_temp(void *data, int *temp)
{
	struct hwmon_thermal_data *tdata = data;
	struct hwmon_device *hwdev = tdata->hwdev;
	int ret;
	long t;

	ret = hwdev->chip->ops->read(&hwdev->dev, hwmon_temp, hwmon_temp_input,
				     tdata->index, &t);
	if (ret < 0)
		return ret;

	*temp = t;

	return 0;
}

static struct thermal_zone_of_device_ops hwmon_thermal_ops = {
	.get_temp = hwmon_thermal_get_temp,
};

static int hwmon_thermal_add_sensor(struct device *dev,
				    struct hwmon_device *hwdev, int index)
{
	struct hwmon_thermal_data *tdata;

	tdata = devm_kzalloc(dev, sizeof(*tdata), GFP_KERNEL);
	if (!tdata)
		return -ENOMEM;

	tdata->hwdev = hwdev;
	tdata->index = index;

	devm_thermal_zone_of_sensor_register(&hwdev->dev, index, tdata,
					     &hwmon_thermal_ops);

	return 0;
}
#else
static int hwmon_thermal_add_sensor(struct device *dev,
				    struct hwmon_device *hwdev, int index)
{
	return 0;
}
#endif /* IS_REACHABLE(CONFIG_THERMAL) && ... */

/* sysfs attribute management */

static ssize_t hwmon_attr_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct hwmon_device_attribute *hattr = to_hwmon_attr(devattr);
	long val;
	int ret;

	ret = hattr->ops->read(dev, hattr->type, hattr->attr, hattr->index,
			       &val);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%ld\n", val);
}

static ssize_t hwmon_attr_show_string(struct device *dev,
				      struct device_attribute *devattr,
				      char *buf)
{
	struct hwmon_device_attribute *hattr = to_hwmon_attr(devattr);
	const char *s;
	int ret;

	ret = hattr->ops->read_string(dev, hattr->type, hattr->attr,
				      hattr->index, &s);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%s\n", s);
}

static ssize_t hwmon_attr_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct hwmon_device_attribute *hattr = to_hwmon_attr(devattr);
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = hattr->ops->write(dev, hattr->type, hattr->attr, hattr->index,
				val);
	if (ret < 0)
		return ret;

	return count;
}

static int hwmon_attr_base(enum hwmon_sensor_types type)
{
	if (type == hwmon_in)
		return 0;
	return 1;
}

static bool is_string_attr(enum hwmon_sensor_types type, u32 attr)
{
	return (type == hwmon_temp && attr == hwmon_temp_label) ||
	       (type == hwmon_in && attr == hwmon_in_label) ||
	       (type == hwmon_curr && attr == hwmon_curr_label) ||
	       (type == hwmon_power && attr == hwmon_power_label) ||
	       (type == hwmon_energy && attr == hwmon_energy_label) ||
	       (type == hwmon_humidity && attr == hwmon_humidity_label) ||
	       (type == hwmon_fan && attr == hwmon_fan_label);
}

static struct attribute *hwmon_genattr(struct device *dev,
				       const void *drvdata,
				       enum hwmon_sensor_types type,
				       u32 attr,
				       int index,
				       const char *template,
				       const struct hwmon_ops *ops)
{
	struct hwmon_device_attribute *hattr;
	struct device_attribute *dattr;
	struct attribute *a;
	umode_t mode;
	char *name;
	bool is_string = is_string_attr(type, attr);

	/* The attribute is invisible if there is no template string */
	if (!template)
		return ERR_PTR(-ENOENT);

	mode = ops->is_visible(drvdata, type, attr, index);
	if (!mode)
		return ERR_PTR(-ENOENT);

	if ((mode & S_IRUGO) && ((is_string && !ops->read_string) ||
				 (!is_string && !ops->read)))
		return ERR_PTR(-EINVAL);
	if ((mode & S_IWUGO) && !ops->write)
		return ERR_PTR(-EINVAL);

	hattr = devm_kzalloc(dev, sizeof(*hattr), GFP_KERNEL);
	if (!hattr)
		return ERR_PTR(-ENOMEM);

	if (type == hwmon_chip) {
		name = (char *)template;
	} else {
		scnprintf(hattr->name, sizeof(hattr->name), template,
			  index + hwmon_attr_base(type));
		name = hattr->name;
	}

	hattr->type = type;
	hattr->attr = attr;
	hattr->index = index;
	hattr->ops = ops;

	dattr = &hattr->dev_attr;
	dattr->show = is_string ? hwmon_attr_show_string : hwmon_attr_show;
	dattr->store = hwmon_attr_store;

	a = &dattr->attr;
	sysfs_attr_init(a);
	a->name = name;
	a->mode = mode;

	return a;
}

/*
 * Chip attributes are not attribute templates but actual sysfs attributes.
 * See hwmon_genattr() for special handling.
 */
static const char * const hwmon_chip_attrs[] = {
	[hwmon_chip_temp_reset_history] = "temp_reset_history",
	[hwmon_chip_in_reset_history] = "in_reset_history",
	[hwmon_chip_curr_reset_history] = "curr_reset_history",
	[hwmon_chip_power_reset_history] = "power_reset_history",
	[hwmon_chip_update_interval] = "update_interval",
	[hwmon_chip_alarms] = "alarms",
};

static const char * const hwmon_temp_attr_templates[] = {
	[hwmon_temp_input] = "temp%d_input",
	[hwmon_temp_type] = "temp%d_type",
	[hwmon_temp_lcrit] = "temp%d_lcrit",
	[hwmon_temp_lcrit_hyst] = "temp%d_lcrit_hyst",
	[hwmon_temp_min] = "temp%d_min",
	[hwmon_temp_min_hyst] = "temp%d_min_hyst",
	[hwmon_temp_max] = "temp%d_max",
	[hwmon_temp_max_hyst] = "temp%d_max_hyst",
	[hwmon_temp_crit] = "temp%d_crit",
	[hwmon_temp_crit_hyst] = "temp%d_crit_hyst",
	[hwmon_temp_emergency] = "temp%d_emergency",
	[hwmon_temp_emergency_hyst] = "temp%d_emergency_hyst",
	[hwmon_temp_alarm] = "temp%d_alarm",
	[hwmon_temp_lcrit_alarm] = "temp%d_lcrit_alarm",
	[hwmon_temp_min_alarm] = "temp%d_min_alarm",
	[hwmon_temp_max_alarm] = "temp%d_max_alarm",
	[hwmon_temp_crit_alarm] = "temp%d_crit_alarm",
	[hwmon_temp_emergency_alarm] = "temp%d_emergency_alarm",
	[hwmon_temp_fault] = "temp%d_fault",
	[hwmon_temp_offset] = "temp%d_offset",
	[hwmon_temp_label] = "temp%d_label",
	[hwmon_temp_lowest] = "temp%d_lowest",
	[hwmon_temp_highest] = "temp%d_highest",
	[hwmon_temp_reset_history] = "temp%d_reset_history",
};

static const char * const hwmon_in_attr_templates[] = {
	[hwmon_in_input] = "in%d_input",
	[hwmon_in_min] = "in%d_min",
	[hwmon_in_max] = "in%d_max",
	[hwmon_in_lcrit] = "in%d_lcrit",
	[hwmon_in_crit] = "in%d_crit",
	[hwmon_in_average] = "in%d_average",
	[hwmon_in_lowest] = "in%d_lowest",
	[hwmon_in_highest] = "in%d_highest",
	[hwmon_in_reset_history] = "in%d_reset_history",
	[hwmon_in_label] = "in%d_label",
	[hwmon_in_alarm] = "in%d_alarm",
	[hwmon_in_min_alarm] = "in%d_min_alarm",
	[hwmon_in_max_alarm] = "in%d_max_alarm",
	[hwmon_in_lcrit_alarm] = "in%d_lcrit_alarm",
	[hwmon_in_crit_alarm] = "in%d_crit_alarm",
};

static const char * const hwmon_curr_attr_templates[] = {
	[hwmon_curr_input] = "curr%d_input",
	[hwmon_curr_min] = "curr%d_min",
	[hwmon_curr_max] = "curr%d_max",
	[hwmon_curr_lcrit] = "curr%d_lcrit",
	[hwmon_curr_crit] = "curr%d_crit",
	[hwmon_curr_average] = "curr%d_average",
	[hwmon_curr_lowest] = "curr%d_lowest",
	[hwmon_curr_highest] = "curr%d_highest",
	[hwmon_curr_reset_history] = "curr%d_reset_history",
	[hwmon_curr_label] = "curr%d_label",
	[hwmon_curr_alarm] = "curr%d_alarm",
	[hwmon_curr_min_alarm] = "curr%d_min_alarm",
	[hwmon_curr_max_alarm] = "curr%d_max_alarm",
	[hwmon_curr_lcrit_alarm] = "curr%d_lcrit_alarm",
	[hwmon_curr_crit_alarm] = "curr%d_crit_alarm",
};

static const char * const hwmon_power_attr_templates[] = {
	[hwmon_power_average] = "power%d_average",
	[hwmon_power_average_interval] = "power%d_average_interval",
	[hwmon_power_average_interval_max] = "power%d_interval_max",
	[hwmon_power_average_interval_min] = "power%d_interval_min",
	[hwmon_power_average_highest] = "power%d_average_highest",
	[hwmon_power_average_lowest] = "power%d_average_lowest",
	[hwmon_power_average_max] = "power%d_average_max",
	[hwmon_power_average_min] = "power%d_average_min",
	[hwmon_power_input] = "power%d_input",
	[hwmon_power_input_highest] = "power%d_input_highest",
	[hwmon_power_input_lowest] = "power%d_input_lowest",
	[hwmon_power_reset_history] = "power%d_reset_history",
	[hwmon_power_accuracy] = "power%d_accuracy",
	[hwmon_power_cap] = "power%d_cap",
	[hwmon_power_cap_hyst] = "power%d_cap_hyst",
	[hwmon_power_cap_max] = "power%d_cap_max",
	[hwmon_power_cap_min] = "power%d_cap_min",
	[hwmon_power_max] = "power%d_max",
	[hwmon_power_crit] = "power%d_crit",
	[hwmon_power_label] = "power%d_label",
	[hwmon_power_alarm] = "power%d_alarm",
	[hwmon_power_cap_alarm] = "power%d_cap_alarm",
	[hwmon_power_max_alarm] = "power%d_max_alarm",
	[hwmon_power_crit_alarm] = "power%d_crit_alarm",
};

static const char * const hwmon_energy_attr_templates[] = {
	[hwmon_energy_input] = "energy%d_input",
	[hwmon_energy_label] = "energy%d_label",
};

static const char * const hwmon_humidity_attr_templates[] = {
	[hwmon_humidity_input] = "humidity%d_input",
	[hwmon_humidity_label] = "humidity%d_label",
	[hwmon_humidity_min] = "humidity%d_min",
	[hwmon_humidity_min_hyst] = "humidity%d_min_hyst",
	[hwmon_humidity_max] = "humidity%d_max",
	[hwmon_humidity_max_hyst] = "humidity%d_max_hyst",
	[hwmon_humidity_alarm] = "humidity%d_alarm",
	[hwmon_humidity_fault] = "humidity%d_fault",
};

static const char * const hwmon_fan_attr_templates[] = {
	[hwmon_fan_input] = "fan%d_input",
	[hwmon_fan_label] = "fan%d_label",
	[hwmon_fan_min] = "fan%d_min",
	[hwmon_fan_max] = "fan%d_max",
	[hwmon_fan_div] = "fan%d_div",
	[hwmon_fan_pulses] = "fan%d_pulses",
	[hwmon_fan_target] = "fan%d_target",
	[hwmon_fan_alarm] = "fan%d_alarm",
	[hwmon_fan_min_alarm] = "fan%d_min_alarm",
	[hwmon_fan_max_alarm] = "fan%d_max_alarm",
	[hwmon_fan_fault] = "fan%d_fault",
};

static const char * const hwmon_pwm_attr_templates[] = {
	[hwmon_pwm_input] = "pwm%d",
	[hwmon_pwm_enable] = "pwm%d_enable",
	[hwmon_pwm_mode] = "pwm%d_mode",
	[hwmon_pwm_freq] = "pwm%d_freq",
};

static const char * const *__templates[] = {
	[hwmon_chip] = hwmon_chip_attrs,
	[hwmon_temp] = hwmon_temp_attr_templates,
	[hwmon_in] = hwmon_in_attr_templates,
	[hwmon_curr] = hwmon_curr_attr_templates,
	[hwmon_power] = hwmon_power_attr_templates,
	[hwmon_energy] = hwmon_energy_attr_templates,
	[hwmon_humidity] = hwmon_humidity_attr_templates,
	[hwmon_fan] = hwmon_fan_attr_templates,
	[hwmon_pwm] = hwmon_pwm_attr_templates,
};

static const int __templates_size[] = {
	[hwmon_chip] = ARRAY_SIZE(hwmon_chip_attrs),
	[hwmon_temp] = ARRAY_SIZE(hwmon_temp_attr_templates),
	[hwmon_in] = ARRAY_SIZE(hwmon_in_attr_templates),
	[hwmon_curr] = ARRAY_SIZE(hwmon_curr_attr_templates),
	[hwmon_power] = ARRAY_SIZE(hwmon_power_attr_templates),
	[hwmon_energy] = ARRAY_SIZE(hwmon_energy_attr_templates),
	[hwmon_humidity] = ARRAY_SIZE(hwmon_humidity_attr_templates),
	[hwmon_fan] = ARRAY_SIZE(hwmon_fan_attr_templates),
	[hwmon_pwm] = ARRAY_SIZE(hwmon_pwm_attr_templates),
};

static int hwmon_num_channel_attrs(const struct hwmon_channel_info *info)
{
	int i, n;

	for (i = n = 0; info->config[i]; i++)
		n += hweight32(info->config[i]);

	return n;
}

static int hwmon_genattrs(struct device *dev,
			  const void *drvdata,
			  struct attribute **attrs,
			  const struct hwmon_ops *ops,
			  const struct hwmon_channel_info *info)
{
	const char * const *templates;
	int template_size;
	int i, aindex = 0;

	if (info->type >= ARRAY_SIZE(__templates))
		return -EINVAL;

	templates = __templates[info->type];
	template_size = __templates_size[info->type];

	for (i = 0; info->config[i]; i++) {
		u32 attr_mask = info->config[i];
		u32 attr;

		while (attr_mask) {
			struct attribute *a;

			attr = __ffs(attr_mask);
			attr_mask &= ~BIT(attr);
			if (attr >= template_size)
				return -EINVAL;
			a = hwmon_genattr(dev, drvdata, info->type, attr, i,
					  templates[attr], ops);
			if (IS_ERR(a)) {
				if (PTR_ERR(a) != -ENOENT)
					return PTR_ERR(a);
				continue;
			}
			attrs[aindex++] = a;
		}
	}
	return aindex;
}

static struct attribute **
__hwmon_create_attrs(struct device *dev, const void *drvdata,
		     const struct hwmon_chip_info *chip)
{
	int ret, i, aindex = 0, nattrs = 0;
	struct attribute **attrs;

	for (i = 0; chip->info[i]; i++)
		nattrs += hwmon_num_channel_attrs(chip->info[i]);

	if (nattrs == 0)
		return ERR_PTR(-EINVAL);

	attrs = devm_kcalloc(dev, nattrs + 1, sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; chip->info[i]; i++) {
		ret = hwmon_genattrs(dev, drvdata, &attrs[aindex], chip->ops,
				     chip->info[i]);
		if (ret < 0)
			return ERR_PTR(ret);
		aindex += ret;
	}

	return attrs;
}

static struct device *
__hwmon_device_register(struct device *dev, const char *name, void *drvdata,
			const struct hwmon_chip_info *chip,
			const struct attribute_group **groups)
{
	struct hwmon_device *hwdev;
	struct device *hdev;
	int i, j, err, id;

	/* Complain about invalid characters in hwmon name attribute */
	if (name && (!strlen(name) || strpbrk(name, "-* \t\n")))
		dev_warn(dev,
			 "hwmon: '%s' is not a valid name attribute, please fix\n",
			 name);

	id = ida_simple_get(&hwmon_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	hwdev = kzalloc(sizeof(*hwdev), GFP_KERNEL);
	if (hwdev == NULL) {
		err = -ENOMEM;
		goto ida_remove;
	}

	hdev = &hwdev->dev;

	if (chip) {
		struct attribute **attrs;
		int ngroups = 2; /* terminating NULL plus &hwdev->groups */

		if (groups)
			for (i = 0; groups[i]; i++)
				ngroups++;

		hwdev->groups = devm_kcalloc(dev, ngroups, sizeof(*groups),
					     GFP_KERNEL);
		if (!hwdev->groups) {
			err = -ENOMEM;
			goto free_hwmon;
		}

		attrs = __hwmon_create_attrs(dev, drvdata, chip);
		if (IS_ERR(attrs)) {
			err = PTR_ERR(attrs);
			goto free_hwmon;
		}

		hwdev->group.attrs = attrs;
		ngroups = 0;
		hwdev->groups[ngroups++] = &hwdev->group;

		if (groups) {
			for (i = 0; groups[i]; i++)
				hwdev->groups[ngroups++] = groups[i];
		}

		hdev->groups = hwdev->groups;
	} else {
		hdev->groups = groups;
	}

	hwdev->name = name;
	hdev->class = &hwmon_class;
	hdev->parent = dev;
	hdev->of_node = dev ? dev->of_node : NULL;
	hwdev->chip = chip;
	dev_set_drvdata(hdev, drvdata);
	dev_set_name(hdev, HWMON_ID_FORMAT, id);
	err = device_register(hdev);
	if (err)
		goto free_hwmon;

	if (dev && chip && chip->ops->read &&
	    chip->info[0]->type == hwmon_chip &&
	    (chip->info[0]->config[0] & HWMON_C_REGISTER_TZ)) {
		const struct hwmon_channel_info **info = chip->info;

		for (i = 1; info[i]; i++) {
			if (info[i]->type != hwmon_temp)
				continue;

			for (j = 0; info[i]->config[j]; j++) {
				if (!chip->ops->is_visible(drvdata, hwmon_temp,
							   hwmon_temp_input, j))
					continue;
				if (info[i]->config[j] & HWMON_T_INPUT)
					hwmon_thermal_add_sensor(dev, hwdev, j);
			}
		}
	}

	return hdev;

free_hwmon:
	kfree(hwdev);
ida_remove:
	ida_simple_remove(&hwmon_ida, id);
	return ERR_PTR(err);
}

/**
 * hwmon_device_register_with_groups - register w/ hwmon
 * @dev: the parent device
 * @name: hwmon name attribute
 * @drvdata: driver data to attach to created device
 * @groups: List of attribute groups to create
 *
 * hwmon_device_unregister() must be called when the device is no
 * longer needed.
 *
 * Returns the pointer to the new device.
 */
struct device *
hwmon_device_register_with_groups(struct device *dev, const char *name,
				  void *drvdata,
				  const struct attribute_group **groups)
{
	if (!name)
		return ERR_PTR(-EINVAL);

	return __hwmon_device_register(dev, name, drvdata, NULL, groups);
}
EXPORT_SYMBOL_GPL(hwmon_device_register_with_groups);

/**
 * hwmon_device_register_with_info - register w/ hwmon
 * @dev: the parent device
 * @name: hwmon name attribute
 * @drvdata: driver data to attach to created device
 * @info: pointer to hwmon chip information
 * @extra_groups: pointer to list of additional non-standard attribute groups
 *
 * hwmon_device_unregister() must be called when the device is no
 * longer needed.
 *
 * Returns the pointer to the new device.
 */
struct device *
hwmon_device_register_with_info(struct device *dev, const char *name,
				void *drvdata,
				const struct hwmon_chip_info *chip,
				const struct attribute_group **extra_groups)
{
	if (!name)
		return ERR_PTR(-EINVAL);

	if (chip && (!chip->ops || !chip->ops->is_visible || !chip->info))
		return ERR_PTR(-EINVAL);

	return __hwmon_device_register(dev, name, drvdata, chip, extra_groups);
}
EXPORT_SYMBOL_GPL(hwmon_device_register_with_info);

/**
 * hwmon_device_register - register w/ hwmon
 * @dev: the device to register
 *
 * hwmon_device_unregister() must be called when the device is no
 * longer needed.
 *
 * Returns the pointer to the new device.
 */
struct device *hwmon_device_register(struct device *dev)
{
	dev_warn(dev,
		 "hwmon_device_register() is deprecated. Please convert the driver to use hwmon_device_register_with_info().\n");

	return __hwmon_device_register(dev, NULL, NULL, NULL, NULL);
}
EXPORT_SYMBOL_GPL(hwmon_device_register);

/**
 * hwmon_device_unregister - removes the previously registered class device
 *
 * @dev: the class device to destroy
 */
void hwmon_device_unregister(struct device *dev)
{
	int id;

	if (likely(sscanf(dev_name(dev), HWMON_ID_FORMAT, &id) == 1)) {
		device_unregister(dev);
		ida_simple_remove(&hwmon_ida, id);
	} else
		dev_dbg(dev->parent,
			"hwmon_device_unregister() failed: bad class ID!\n");
}
EXPORT_SYMBOL_GPL(hwmon_device_unregister);

static void devm_hwmon_release(struct device *dev, void *res)
{
	struct device *hwdev = *(struct device **)res;

	hwmon_device_unregister(hwdev);
}

/**
 * devm_hwmon_device_register_with_groups - register w/ hwmon
 * @dev: the parent device
 * @name: hwmon name attribute
 * @drvdata: driver data to attach to created device
 * @groups: List of attribute groups to create
 *
 * Returns the pointer to the new device. The new device is automatically
 * unregistered with the parent device.
 */
struct device *
devm_hwmon_device_register_with_groups(struct device *dev, const char *name,
				       void *drvdata,
				       const struct attribute_group **groups)
{
	struct device **ptr, *hwdev;

	if (!dev)
		return ERR_PTR(-EINVAL);

	ptr = devres_alloc(devm_hwmon_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	hwdev = hwmon_device_register_with_groups(dev, name, drvdata, groups);
	if (IS_ERR(hwdev))
		goto error;

	*ptr = hwdev;
	devres_add(dev, ptr);
	return hwdev;

error:
	devres_free(ptr);
	return hwdev;
}
EXPORT_SYMBOL_GPL(devm_hwmon_device_register_with_groups);

/**
 * devm_hwmon_device_register_with_info - register w/ hwmon
 * @dev: the parent device
 * @name: hwmon name attribute
 * @drvdata: driver data to attach to created device
 * @info: Pointer to hwmon chip information
 * @groups - pointer to list of driver specific attribute groups
 *
 * Returns the pointer to the new device. The new device is automatically
 * unregistered with the parent device.
 */
struct device *
devm_hwmon_device_register_with_info(struct device *dev, const char *name,
				     void *drvdata,
				     const struct hwmon_chip_info *chip,
				     const struct attribute_group **groups)
{
	struct device **ptr, *hwdev;

	if (!dev)
		return ERR_PTR(-EINVAL);

	ptr = devres_alloc(devm_hwmon_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	hwdev = hwmon_device_register_with_info(dev, name, drvdata, chip,
						groups);
	if (IS_ERR(hwdev))
		goto error;

	*ptr = hwdev;
	devres_add(dev, ptr);

	return hwdev;

error:
	devres_free(ptr);
	return hwdev;
}
EXPORT_SYMBOL_GPL(devm_hwmon_device_register_with_info);

static int devm_hwmon_match(struct device *dev, void *res, void *data)
{
	struct device **hwdev = res;

	return *hwdev == data;
}

/**
 * devm_hwmon_device_unregister - removes a previously registered hwmon device
 *
 * @dev: the parent device of the device to unregister
 */
void devm_hwmon_device_unregister(struct device *dev)
{
	WARN_ON(devres_release(dev, devm_hwmon_release, devm_hwmon_match, dev));
}
EXPORT_SYMBOL_GPL(devm_hwmon_device_unregister);

static void __init hwmon_pci_quirks(void)
{
#if defined CONFIG_X86 && defined CONFIG_PCI
	struct pci_dev *sb;
	u16 base;
	u8 enable;

	/* Open access to 0x295-0x296 on MSI MS-7031 */
	sb = pci_get_device(PCI_VENDOR_ID_ATI, 0x436c, NULL);
	if (sb) {
		if (sb->subsystem_vendor == 0x1462 &&	/* MSI */
		    sb->subsystem_device == 0x0031) {	/* MS-7031 */
			pci_read_config_byte(sb, 0x48, &enable);
			pci_read_config_word(sb, 0x64, &base);

			if (base == 0 && !(enable & BIT(2))) {
				dev_info(&sb->dev,
					 "Opening wide generic port at 0x295\n");
				pci_write_config_word(sb, 0x64, 0x295);
				pci_write_config_byte(sb, 0x48,
						      enable | BIT(2));
			}
		}
		pci_dev_put(sb);
	}
#endif
}

static int __init hwmon_init(void)
{
	int err;

	hwmon_pci_quirks();

	err = class_register(&hwmon_class);
	if (err) {
		pr_err("couldn't register hwmon sysfs class\n");
		return err;
	}
	return 0;
}

static void __exit hwmon_exit(void)
{
	class_unregister(&hwmon_class);
}

subsys_initcall(hwmon_init);
module_exit(hwmon_exit);

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>");
MODULE_DESCRIPTION("hardware monitoring sysfs/class support");
MODULE_LICENSE("GPL");

