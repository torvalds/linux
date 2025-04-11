// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A hwmon driver for ACPI 4.0 power meters
 * Copyright (C) 2009 IBM
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */

#include <linux/module.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/acpi.h>

#define ACPI_POWER_METER_NAME		"power_meter"
#define ACPI_POWER_METER_DEVICE_NAME	"Power Meter"
#define ACPI_POWER_METER_CLASS		"pwr_meter_resource"

#define NUM_SENSORS			17

#define POWER_METER_CAN_MEASURE	(1 << 0)
#define POWER_METER_CAN_TRIP	(1 << 1)
#define POWER_METER_CAN_CAP	(1 << 2)
#define POWER_METER_CAN_NOTIFY	(1 << 3)
#define POWER_METER_IS_BATTERY	(1 << 8)
#define UNKNOWN_HYSTERESIS	0xFFFFFFFF
#define UNKNOWN_POWER		0xFFFFFFFF

#define METER_NOTIFY_CONFIG	0x80
#define METER_NOTIFY_TRIP	0x81
#define METER_NOTIFY_CAP	0x82
#define METER_NOTIFY_CAPPING	0x83
#define METER_NOTIFY_INTERVAL	0x84

#define POWER_AVERAGE_NAME	"power1_average"
#define POWER_CAP_NAME		"power1_cap"
#define POWER_AVG_INTERVAL_NAME	"power1_average_interval"
#define POWER_ALARM_NAME	"power1_alarm"

static int cap_in_hardware;
static bool force_cap_on;

static int can_cap_in_hardware(void)
{
	return force_cap_on || cap_in_hardware;
}

static const struct acpi_device_id power_meter_ids[] = {
	{"ACPI000D", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, power_meter_ids);

struct acpi_power_meter_capabilities {
	u64		flags;
	u64		units;
	u64		type;
	u64		accuracy;
	u64		sampling_time;
	u64		min_avg_interval;
	u64		max_avg_interval;
	u64		hysteresis;
	u64		configurable_cap;
	u64		min_cap;
	u64		max_cap;
};

struct acpi_power_meter_resource {
	struct acpi_device	*acpi_dev;
	acpi_bus_id		name;
	struct mutex		lock;
	struct device		*hwmon_dev;
	struct acpi_power_meter_capabilities	caps;
	acpi_string		model_number;
	acpi_string		serial_number;
	acpi_string		oem_info;
	u64		power;
	u64		cap;
	u64		avg_interval;
	bool		power_alarm;
	int			sensors_valid;
	unsigned long		sensors_last_updated;
#define POWER_METER_TRIP_AVERAGE_MIN_IDX	0
#define POWER_METER_TRIP_AVERAGE_MAX_IDX	1
	s64			trip[2];
	int			num_domain_devices;
	struct acpi_device	**domain_devices;
	struct kobject		*holders_dir;
};

/* Averaging interval */
static int update_avg_interval(struct acpi_power_meter_resource *resource)
{
	unsigned long long data;
	acpi_status status;

	status = acpi_evaluate_integer(resource->acpi_dev->handle, "_GAI",
				       NULL, &data);
	if (ACPI_FAILURE(status)) {
		acpi_evaluation_failure_warn(resource->acpi_dev->handle, "_GAI",
					     status);
		return -ENODEV;
	}

	resource->avg_interval = data;
	return 0;
}

/* Cap functions */
static int update_cap(struct acpi_power_meter_resource *resource)
{
	unsigned long long data;
	acpi_status status;

	status = acpi_evaluate_integer(resource->acpi_dev->handle, "_GHL",
				       NULL, &data);
	if (ACPI_FAILURE(status)) {
		acpi_evaluation_failure_warn(resource->acpi_dev->handle, "_GHL",
					     status);
		return -ENODEV;
	}

	resource->cap = data;
	return 0;
}

/* Power meter trip points */
static int set_acpi_trip(struct acpi_power_meter_resource *resource)
{
	union acpi_object arg_objs[] = {
		{ACPI_TYPE_INTEGER},
		{ACPI_TYPE_INTEGER}
	};
	struct acpi_object_list args = { 2, arg_objs };
	unsigned long long data;
	acpi_status status;

	/* Both trip levels must be set */
	if (resource->trip[0] < 0 || resource->trip[1] < 0)
		return 0;

	/* This driver stores min, max; ACPI wants max, min. */
	arg_objs[0].integer.value = resource->trip[1];
	arg_objs[1].integer.value = resource->trip[0];

	status = acpi_evaluate_integer(resource->acpi_dev->handle, "_PTP",
				       &args, &data);
	if (ACPI_FAILURE(status)) {
		acpi_evaluation_failure_warn(resource->acpi_dev->handle, "_PTP",
					     status);
		return -EINVAL;
	}

	/* _PTP returns 0 on success, nonzero otherwise */
	if (data)
		return -EINVAL;

	return 0;
}

/* Power meter */
static int update_meter(struct acpi_power_meter_resource *resource)
{
	unsigned long long data;
	acpi_status status;
	unsigned long local_jiffies = jiffies;

	if (time_before(local_jiffies, resource->sensors_last_updated +
			msecs_to_jiffies(resource->caps.sampling_time)) &&
			resource->sensors_valid)
		return 0;

	status = acpi_evaluate_integer(resource->acpi_dev->handle, "_PMM",
				       NULL, &data);
	if (ACPI_FAILURE(status)) {
		acpi_evaluation_failure_warn(resource->acpi_dev->handle, "_PMM",
					     status);
		return -ENODEV;
	}

	resource->power = data;
	resource->sensors_valid = 1;
	resource->sensors_last_updated = jiffies;
	return 0;
}

/* Read power domain data */
static void remove_domain_devices(struct acpi_power_meter_resource *resource)
{
	int i;

	if (!resource->num_domain_devices)
		return;

	for (i = 0; i < resource->num_domain_devices; i++) {
		struct acpi_device *obj = resource->domain_devices[i];

		if (!obj)
			continue;

		sysfs_remove_link(resource->holders_dir,
				  kobject_name(&obj->dev.kobj));
		acpi_dev_put(obj);
	}

	kfree(resource->domain_devices);
	kobject_put(resource->holders_dir);
	resource->num_domain_devices = 0;
}

static int read_domain_devices(struct acpi_power_meter_resource *resource)
{
	int res = 0;
	int i;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *pss;
	acpi_status status;

	status = acpi_evaluate_object(resource->acpi_dev->handle, "_PMD", NULL,
				      &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_evaluation_failure_warn(resource->acpi_dev->handle, "_PMD",
					     status);
		return -ENODEV;
	}

	pss = buffer.pointer;
	if (!pss ||
	    pss->type != ACPI_TYPE_PACKAGE) {
		dev_err(&resource->acpi_dev->dev, ACPI_POWER_METER_NAME
			"Invalid _PMD data\n");
		res = -EFAULT;
		goto end;
	}

	if (!pss->package.count)
		goto end;

	resource->domain_devices = kcalloc(pss->package.count,
					   sizeof(struct acpi_device *),
					   GFP_KERNEL);
	if (!resource->domain_devices) {
		res = -ENOMEM;
		goto end;
	}

	resource->holders_dir = kobject_create_and_add("measures",
						       &resource->acpi_dev->dev.kobj);
	if (!resource->holders_dir) {
		res = -ENOMEM;
		goto exit_free;
	}

	resource->num_domain_devices = pss->package.count;

	for (i = 0; i < pss->package.count; i++) {
		struct acpi_device *obj;
		union acpi_object *element = &pss->package.elements[i];

		/* Refuse non-references */
		if (element->type != ACPI_TYPE_LOCAL_REFERENCE)
			continue;

		/* Create a symlink to domain objects */
		obj = acpi_get_acpi_dev(element->reference.handle);
		resource->domain_devices[i] = obj;
		if (!obj)
			continue;

		res = sysfs_create_link(resource->holders_dir, &obj->dev.kobj,
					kobject_name(&obj->dev.kobj));
		if (res) {
			acpi_dev_put(obj);
			resource->domain_devices[i] = NULL;
		}
	}

	res = 0;
	goto end;

exit_free:
	kfree(resource->domain_devices);
end:
	kfree(buffer.pointer);
	return res;
}

static int set_trip(struct acpi_power_meter_resource *resource, u16 trip_idx,
		    unsigned long trip)
{
	unsigned long trip_bk;
	int ret;

	trip = DIV_ROUND_CLOSEST(trip, 1000);
	trip_bk = resource->trip[trip_idx];

	resource->trip[trip_idx] = trip;
	ret = set_acpi_trip(resource);
	if (ret) {
		dev_err(&resource->acpi_dev->dev, "set %s failed.\n",
			(trip_idx == POWER_METER_TRIP_AVERAGE_MIN_IDX) ?
			 "power1_average_min" : "power1_average_max");
		resource->trip[trip_idx] = trip_bk;
	}

	return ret;
}

static int set_cap(struct acpi_power_meter_resource *resource,
		   unsigned long cap)
{
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };
	unsigned long long data;
	acpi_status status;

	cap = DIV_ROUND_CLOSEST(cap, 1000);
	if (cap > resource->caps.max_cap || cap < resource->caps.min_cap)
		return -EINVAL;

	arg0.integer.value = cap;
	status = acpi_evaluate_integer(resource->acpi_dev->handle, "_SHL",
				       &args, &data);
	if (ACPI_FAILURE(status)) {
		acpi_evaluation_failure_warn(resource->acpi_dev->handle, "_SHL",
					     status);
		return -EINVAL;
	}
	resource->cap = cap;

	/* _SHL returns 0 on success, nonzero otherwise */
	if (data)
		return -EINVAL;

	return 0;
}

static int set_avg_interval(struct acpi_power_meter_resource *resource,
			    unsigned long val)
{
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };
	unsigned long long data;
	acpi_status status;

	if (val > resource->caps.max_avg_interval ||
	    val < resource->caps.min_avg_interval)
		return -EINVAL;

	arg0.integer.value = val;
	status = acpi_evaluate_integer(resource->acpi_dev->handle, "_PAI",
				       &args, &data);
	if (ACPI_FAILURE(status)) {
		acpi_evaluation_failure_warn(resource->acpi_dev->handle, "_PAI",
					     status);
		return -EINVAL;
	}
	resource->avg_interval = val;

	/* _PAI returns 0 on success, nonzero otherwise */
	if (data)
		return -EINVAL;

	return 0;
}

static int get_power_alarm_state(struct acpi_power_meter_resource *resource,
				 long *val)
{
	int ret;

	ret = update_meter(resource);
	if (ret)
		return ret;

	/* need to update cap if not to support the notification. */
	if (!(resource->caps.flags & POWER_METER_CAN_NOTIFY)) {
		ret = update_cap(resource);
		if (ret)
			return ret;
		resource->power_alarm = resource->power > resource->cap;
		*val = resource->power_alarm;
	} else {
		*val = resource->power_alarm || resource->power > resource->cap;
		resource->power_alarm = resource->power > resource->cap;
	}

	return 0;
}

static umode_t power_meter_is_visible(const void *data,
				      enum hwmon_sensor_types type,
				      u32 attr, int channel)
{
	const struct acpi_power_meter_resource *res = data;

	if (type != hwmon_power)
		return 0;

	switch (attr) {
	case hwmon_power_average:
	case hwmon_power_average_interval_min:
	case hwmon_power_average_interval_max:
		if (res->caps.flags & POWER_METER_CAN_MEASURE)
			return 0444;
		break;
	case hwmon_power_average_interval:
		if (res->caps.flags & POWER_METER_CAN_MEASURE)
			return 0644;
		break;
	case hwmon_power_cap_min:
	case hwmon_power_cap_max:
	case hwmon_power_alarm:
		if (res->caps.flags & POWER_METER_CAN_CAP && can_cap_in_hardware())
			return 0444;
		break;
	case hwmon_power_cap:
		if (res->caps.flags & POWER_METER_CAN_CAP && can_cap_in_hardware()) {
			if (res->caps.configurable_cap)
				return 0644;
			else
				return 0444;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int power_meter_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *val)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);
	int ret = 0;

	if (type != hwmon_power)
		return -EINVAL;

	guard(mutex)(&res->lock);

	switch (attr) {
	case hwmon_power_average:
		ret = update_meter(res);
		if (ret)
			return ret;
		if (res->power == UNKNOWN_POWER)
			return -ENODATA;
		*val = res->power * 1000;
		break;
	case hwmon_power_average_interval_min:
		*val = res->caps.min_avg_interval;
		break;
	case hwmon_power_average_interval_max:
		*val = res->caps.max_avg_interval;
		break;
	case hwmon_power_average_interval:
		ret = update_avg_interval(res);
		if (ret)
			return ret;
		*val = (res)->avg_interval;
		break;
	case hwmon_power_cap_min:
		*val = res->caps.min_cap * 1000;
		break;
	case hwmon_power_cap_max:
		*val = res->caps.max_cap * 1000;
		break;
	case hwmon_power_alarm:
		ret = get_power_alarm_state(res, val);
		if (ret)
			return ret;
		break;
	case hwmon_power_cap:
		ret = update_cap(res);
		if (ret)
			return ret;
		*val = res->cap * 1000;
		break;
	default:
		break;
	}

	return 0;
}

static int power_meter_write(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long val)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);
	int ret;

	if (type != hwmon_power)
		return -EINVAL;

	guard(mutex)(&res->lock);
	switch (attr) {
	case hwmon_power_cap:
		ret = set_cap(res, val);
		break;
	case hwmon_power_average_interval:
		ret = set_avg_interval(res, val);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static const struct hwmon_channel_info * const power_meter_info[] = {
	HWMON_CHANNEL_INFO(power, HWMON_P_AVERAGE |
		HWMON_P_AVERAGE_INTERVAL | HWMON_P_AVERAGE_INTERVAL_MIN |
		HWMON_P_AVERAGE_INTERVAL_MAX | HWMON_P_CAP | HWMON_P_CAP_MIN |
		HWMON_P_CAP_MAX | HWMON_P_ALARM),
	NULL
};

static const struct hwmon_ops power_meter_ops = {
	.is_visible = power_meter_is_visible,
	.read = power_meter_read,
	.write = power_meter_write,
};

static const struct hwmon_chip_info power_meter_chip_info = {
	.ops = &power_meter_ops,
	.info = power_meter_info,
};

static ssize_t power1_average_max_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);
	unsigned long trip;
	int ret;

	ret = kstrtoul(buf, 10, &trip);
	if (ret)
		return ret;

	mutex_lock(&res->lock);
	ret = set_trip(res, POWER_METER_TRIP_AVERAGE_MAX_IDX, trip);
	mutex_unlock(&res->lock);

	return ret == 0 ? count : ret;
}

static ssize_t power1_average_min_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);
	unsigned long trip;
	int ret;

	ret = kstrtoul(buf, 10, &trip);
	if (ret)
		return ret;

	mutex_lock(&res->lock);
	ret = set_trip(res, POWER_METER_TRIP_AVERAGE_MIN_IDX, trip);
	mutex_unlock(&res->lock);

	return ret == 0 ? count : ret;
}

static ssize_t power1_average_min_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);

	if (res->trip[POWER_METER_TRIP_AVERAGE_MIN_IDX] < 0)
		return sysfs_emit(buf, "unknown\n");

	return sysfs_emit(buf, "%lld\n",
			  res->trip[POWER_METER_TRIP_AVERAGE_MIN_IDX] * 1000);
}

static ssize_t power1_average_max_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);

	if (res->trip[POWER_METER_TRIP_AVERAGE_MAX_IDX] < 0)
		return sysfs_emit(buf, "unknown\n");

	return sysfs_emit(buf, "%lld\n",
			  res->trip[POWER_METER_TRIP_AVERAGE_MAX_IDX] * 1000);
}

static ssize_t power1_cap_hyst_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);

	if (res->caps.hysteresis == UNKNOWN_HYSTERESIS)
		return sysfs_emit(buf, "unknown\n");

	return sysfs_emit(buf, "%llu\n", res->caps.hysteresis * 1000);
}

static ssize_t power1_accuracy_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);
	unsigned int acc = res->caps.accuracy;

	return sysfs_emit(buf, "%u.%u%%\n", acc / 1000, acc % 1000);
}

static ssize_t power1_is_battery_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n",
			  res->caps.flags & POWER_METER_IS_BATTERY ? 1 : 0);
}

static ssize_t power1_model_number_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", res->model_number);
}

static ssize_t power1_oem_info_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", res->oem_info);
}

static ssize_t power1_serial_number_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", res->serial_number);
}

/* depend on POWER_METER_CAN_TRIP */
static DEVICE_ATTR_RW(power1_average_max);
static DEVICE_ATTR_RW(power1_average_min);

/* depend on POWER_METER_CAN_CAP */
static DEVICE_ATTR_RO(power1_cap_hyst);

/* depend on POWER_METER_CAN_MEASURE */
static DEVICE_ATTR_RO(power1_accuracy);
static DEVICE_ATTR_RO(power1_is_battery);

static DEVICE_ATTR_RO(power1_model_number);
static DEVICE_ATTR_RO(power1_oem_info);
static DEVICE_ATTR_RO(power1_serial_number);

static umode_t power_extra_is_visible(struct kobject *kobj,
				      struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct acpi_power_meter_resource *res = dev_get_drvdata(dev);

	if (attr == &dev_attr_power1_is_battery.attr ||
	    attr == &dev_attr_power1_accuracy.attr) {
		if ((res->caps.flags & POWER_METER_CAN_MEASURE) == 0)
			return 0;
	}

	if (attr == &dev_attr_power1_cap_hyst.attr) {
		if ((res->caps.flags & POWER_METER_CAN_CAP) == 0) {
			return 0;
		} else if (!can_cap_in_hardware()) {
			dev_warn(&res->acpi_dev->dev,
				 "Ignoring unsafe software power cap!\n");
			return 0;
		}
	}

	if (attr == &dev_attr_power1_average_max.attr ||
	    attr == &dev_attr_power1_average_min.attr) {
		if ((res->caps.flags & POWER_METER_CAN_TRIP) == 0)
			return 0;
	}

	return attr->mode;
}

static struct attribute *power_extra_attrs[] = {
	&dev_attr_power1_average_max.attr,
	&dev_attr_power1_average_min.attr,
	&dev_attr_power1_cap_hyst.attr,
	&dev_attr_power1_accuracy.attr,
	&dev_attr_power1_is_battery.attr,
	&dev_attr_power1_model_number.attr,
	&dev_attr_power1_oem_info.attr,
	&dev_attr_power1_serial_number.attr,
	NULL
};

static const struct attribute_group power_extra_group = {
	.attrs = power_extra_attrs,
	.is_visible = power_extra_is_visible,
};

__ATTRIBUTE_GROUPS(power_extra);

static void free_capabilities(struct acpi_power_meter_resource *resource)
{
	acpi_string *str;
	int i;

	str = &resource->model_number;
	for (i = 0; i < 3; i++, str++) {
		kfree(*str);
		*str = NULL;
	}
}

static int read_capabilities(struct acpi_power_meter_resource *resource)
{
	int res = 0;
	int i;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer state = { 0, NULL };
	struct acpi_buffer format = { sizeof("NNNNNNNNNNN"), "NNNNNNNNNNN" };
	union acpi_object *pss;
	acpi_string *str;
	acpi_status status;

	status = acpi_evaluate_object(resource->acpi_dev->handle, "_PMC", NULL,
				      &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_evaluation_failure_warn(resource->acpi_dev->handle, "_PMC",
					     status);
		return -ENODEV;
	}

	pss = buffer.pointer;
	if (!pss ||
	    pss->type != ACPI_TYPE_PACKAGE ||
	    pss->package.count != 14) {
		dev_err(&resource->acpi_dev->dev, ACPI_POWER_METER_NAME
			"Invalid _PMC data\n");
		res = -EFAULT;
		goto end;
	}

	/* Grab all the integer data at once */
	state.length = sizeof(struct acpi_power_meter_capabilities);
	state.pointer = &resource->caps;

	status = acpi_extract_package(pss, &format, &state);
	if (ACPI_FAILURE(status)) {
		dev_err(&resource->acpi_dev->dev, ACPI_POWER_METER_NAME
			"_PMC package parsing failed: %s\n",
			acpi_format_exception(status));
		res = -EFAULT;
		goto end;
	}

	if (resource->caps.units) {
		dev_err(&resource->acpi_dev->dev, ACPI_POWER_METER_NAME
			"Unknown units %llu.\n",
			resource->caps.units);
		res = -EINVAL;
		goto end;
	}

	/* Grab the string data */
	str = &resource->model_number;

	for (i = 11; i < 14; i++) {
		union acpi_object *element = &pss->package.elements[i];

		if (element->type != ACPI_TYPE_STRING) {
			res = -EINVAL;
			goto error;
		}

		*str = kmemdup_nul(element->string.pointer, element->string.length,
				   GFP_KERNEL);
		if (!*str) {
			res = -ENOMEM;
			goto error;
		}

		str++;
	}

	dev_info(&resource->acpi_dev->dev, "Found ACPI power meter.\n");
	goto end;
error:
	free_capabilities(resource);
end:
	kfree(buffer.pointer);
	return res;
}

/* Handle ACPI event notifications */
static void acpi_power_meter_notify(struct acpi_device *device, u32 event)
{
	struct acpi_power_meter_resource *resource;
	int res;

	if (!device || !acpi_driver_data(device))
		return;

	resource = acpi_driver_data(device);

	switch (event) {
	case METER_NOTIFY_CONFIG:
		mutex_lock(&resource->lock);
		free_capabilities(resource);
		remove_domain_devices(resource);
		hwmon_device_unregister(resource->hwmon_dev);
		res = read_capabilities(resource);
		if (res)
			dev_err_once(&device->dev, "read capabilities failed.\n");
		res = read_domain_devices(resource);
		if (res && res != -ENODEV)
			dev_err_once(&device->dev, "read domain devices failed.\n");
		resource->hwmon_dev =
			hwmon_device_register_with_info(&device->dev,
							ACPI_POWER_METER_NAME,
							resource,
							&power_meter_chip_info,
							power_extra_groups);
		if (IS_ERR(resource->hwmon_dev))
			dev_err_once(&device->dev, "register hwmon device failed.\n");
		mutex_unlock(&resource->lock);
		break;
	case METER_NOTIFY_TRIP:
		sysfs_notify(&device->dev.kobj, NULL, POWER_AVERAGE_NAME);
		break;
	case METER_NOTIFY_CAP:
		mutex_lock(&resource->lock);
		res = update_cap(resource);
		if (res)
			dev_err_once(&device->dev, "update cap failed when capping value is changed.\n");
		mutex_unlock(&resource->lock);
		sysfs_notify(&device->dev.kobj, NULL, POWER_CAP_NAME);
		break;
	case METER_NOTIFY_INTERVAL:
		sysfs_notify(&device->dev.kobj, NULL, POWER_AVG_INTERVAL_NAME);
		break;
	case METER_NOTIFY_CAPPING:
		mutex_lock(&resource->lock);
		resource->power_alarm = true;
		mutex_unlock(&resource->lock);
		sysfs_notify(&device->dev.kobj, NULL, POWER_ALARM_NAME);
		dev_info(&device->dev, "Capping in progress.\n");
		break;
	default:
		WARN(1, "Unexpected event %d\n", event);
		break;
	}

	acpi_bus_generate_netlink_event(ACPI_POWER_METER_CLASS,
					dev_name(&device->dev), event, 0);
}

static int acpi_power_meter_add(struct acpi_device *device)
{
	int res;
	struct acpi_power_meter_resource *resource;

	if (!device)
		return -EINVAL;

	resource = kzalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	resource->sensors_valid = 0;
	resource->acpi_dev = device;
	mutex_init(&resource->lock);
	strcpy(acpi_device_name(device), ACPI_POWER_METER_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_POWER_METER_CLASS);
	device->driver_data = resource;

#if IS_REACHABLE(CONFIG_ACPI_IPMI)
	/*
	 * On Dell systems several methods of acpi_power_meter access
	 * variables in IPMI region, so wait until IPMI space handler is
	 * installed by acpi_ipmi and also wait until SMI is selected to make
	 * the space handler fully functional.
	 */
	if (dmi_match(DMI_SYS_VENDOR, "Dell Inc.")) {
		struct acpi_device *ipi_device = acpi_dev_get_first_match_dev("IPI0001", NULL, -1);

		if (ipi_device && acpi_wait_for_acpi_ipmi())
			dev_warn(&device->dev, "Waiting for ACPI IPMI timeout");
		acpi_dev_put(ipi_device);
	}
#endif

	res = read_capabilities(resource);
	if (res)
		goto exit_free;

	resource->trip[0] = -1;
	resource->trip[1] = -1;

	/* _PMD method is optional. */
	res = read_domain_devices(resource);
	if (res && res != -ENODEV)
		goto exit_free_capability;

	resource->hwmon_dev =
		hwmon_device_register_with_info(&device->dev,
						ACPI_POWER_METER_NAME, resource,
						&power_meter_chip_info,
						power_extra_groups);
	if (IS_ERR(resource->hwmon_dev)) {
		res = PTR_ERR(resource->hwmon_dev);
		goto exit_remove;
	}

	res = 0;
	goto exit;

exit_remove:
	remove_domain_devices(resource);
exit_free_capability:
	free_capabilities(resource);
exit_free:
	kfree(resource);
exit:
	return res;
}

static void acpi_power_meter_remove(struct acpi_device *device)
{
	struct acpi_power_meter_resource *resource;

	if (!device || !acpi_driver_data(device))
		return;

	resource = acpi_driver_data(device);
	hwmon_device_unregister(resource->hwmon_dev);

	remove_domain_devices(resource);
	free_capabilities(resource);

	kfree(resource);
}

static int acpi_power_meter_resume(struct device *dev)
{
	struct acpi_power_meter_resource *resource;

	if (!dev)
		return -EINVAL;

	resource = acpi_driver_data(to_acpi_device(dev));
	if (!resource)
		return -EINVAL;

	free_capabilities(resource);
	read_capabilities(resource);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(acpi_power_meter_pm, NULL,
				acpi_power_meter_resume);

static struct acpi_driver acpi_power_meter_driver = {
	.name = "power_meter",
	.class = ACPI_POWER_METER_CLASS,
	.ids = power_meter_ids,
	.ops = {
		.add = acpi_power_meter_add,
		.remove = acpi_power_meter_remove,
		.notify = acpi_power_meter_notify,
		},
	.drv.pm = pm_sleep_ptr(&acpi_power_meter_pm),
};

/* Module init/exit routines */
static int __init enable_cap_knobs(const struct dmi_system_id *d)
{
	cap_in_hardware = 1;
	return 0;
}

static const struct dmi_system_id pm_dmi_table[] __initconst = {
	{
		enable_cap_knobs, "IBM Active Energy Manager",
		{
			DMI_MATCH(DMI_SYS_VENDOR, "IBM")
		},
	},
	{}
};

static int __init acpi_power_meter_init(void)
{
	int result;

	if (acpi_disabled)
		return -ENODEV;

	dmi_check_system(pm_dmi_table);

	result = acpi_bus_register_driver(&acpi_power_meter_driver);
	if (result < 0)
		return result;

	return 0;
}

static void __exit acpi_power_meter_exit(void)
{
	acpi_bus_unregister_driver(&acpi_power_meter_driver);
}

MODULE_AUTHOR("Darrick J. Wong <darrick.wong@oracle.com>");
MODULE_DESCRIPTION("ACPI 4.0 power meter driver");
MODULE_LICENSE("GPL");

module_param(force_cap_on, bool, 0644);
MODULE_PARM_DESC(force_cap_on, "Enable power cap even it is unsafe to do so.");

module_init(acpi_power_meter_init);
module_exit(acpi_power_meter_exit);
