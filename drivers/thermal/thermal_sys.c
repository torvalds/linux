/*
 *  thermal.c - Generic Thermal Management Sysfs support.
 *
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/thermal.h>
#include <linux/spinlock.h>

MODULE_AUTHOR("Zhang Rui");
MODULE_DESCRIPTION("Generic thermal management sysfs support");
MODULE_LICENSE("GPL");

#define PREFIX "Thermal: "

struct thermal_cooling_device_instance {
	int id;
	char name[THERMAL_NAME_LENGTH];
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev;
	int trip;
	char attr_name[THERMAL_NAME_LENGTH];
	struct device_attribute attr;
	struct list_head node;
};

static DEFINE_IDR(thermal_tz_idr);
static DEFINE_IDR(thermal_cdev_idr);
static DEFINE_MUTEX(thermal_idr_lock);

static LIST_HEAD(thermal_tz_list);
static LIST_HEAD(thermal_cdev_list);
static DEFINE_MUTEX(thermal_list_lock);

static int get_idr(struct idr *idr, struct mutex *lock, int *id)
{
	int err;

      again:
	if (unlikely(idr_pre_get(idr, GFP_KERNEL) == 0))
		return -ENOMEM;

	if (lock)
		mutex_lock(lock);
	err = idr_get_new(idr, NULL, id);
	if (lock)
		mutex_unlock(lock);
	if (unlikely(err == -EAGAIN))
		goto again;
	else if (unlikely(err))
		return err;

	*id = *id & MAX_ID_MASK;
	return 0;
}

static void release_idr(struct idr *idr, struct mutex *lock, int id)
{
	if (lock)
		mutex_lock(lock);
	idr_remove(idr, id);
	if (lock)
		mutex_unlock(lock);
}

/* sys I/F for thermal zone */

#define to_thermal_zone(_dev) \
	container_of(_dev, struct thermal_zone_device, device)

static ssize_t
type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);

	return sprintf(buf, "%s\n", tz->type);
}

static ssize_t
temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);

	if (!tz->ops->get_temp)
		return -EPERM;

	return tz->ops->get_temp(tz, buf);
}

static ssize_t
mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);

	if (!tz->ops->get_mode)
		return -EPERM;

	return tz->ops->get_mode(tz, buf);
}

static ssize_t
mode_store(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int result;

	if (!tz->ops->set_mode)
		return -EPERM;

	result = tz->ops->set_mode(tz, buf);
	if (result)
		return result;

	return count;
}

static ssize_t
trip_point_type_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip;

	if (!tz->ops->get_trip_type)
		return -EPERM;

	if (!sscanf(attr->attr.name, "trip_point_%d_type", &trip))
		return -EINVAL;

	return tz->ops->get_trip_type(tz, trip, buf);
}

static ssize_t
trip_point_temp_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct thermal_zone_device *tz = to_thermal_zone(dev);
	int trip;

	if (!tz->ops->get_trip_temp)
		return -EPERM;

	if (!sscanf(attr->attr.name, "trip_point_%d_temp", &trip))
		return -EINVAL;

	return tz->ops->get_trip_temp(tz, trip, buf);
}

static DEVICE_ATTR(type, 0444, type_show, NULL);
static DEVICE_ATTR(temp, 0444, temp_show, NULL);
static DEVICE_ATTR(mode, 0644, mode_show, mode_store);

static struct device_attribute trip_point_attrs[] = {
	__ATTR(trip_point_0_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_0_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_1_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_1_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_2_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_2_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_3_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_3_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_4_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_4_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_5_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_5_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_6_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_6_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_7_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_7_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_8_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_8_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_9_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_9_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_10_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_10_temp, 0444, trip_point_temp_show, NULL),
	__ATTR(trip_point_11_type, 0444, trip_point_type_show, NULL),
	__ATTR(trip_point_11_temp, 0444, trip_point_temp_show, NULL),
};

#define TRIP_POINT_ATTR_ADD(_dev, _index, result)     \
do {    \
	result = device_create_file(_dev,	\
				&trip_point_attrs[_index * 2]);	\
	if (result)	\
		break;	\
	result = device_create_file(_dev,	\
			&trip_point_attrs[_index * 2 + 1]);	\
} while (0)

#define TRIP_POINT_ATTR_REMOVE(_dev, _index)	\
do {	\
	device_remove_file(_dev, &trip_point_attrs[_index * 2]);	\
	device_remove_file(_dev, &trip_point_attrs[_index * 2 + 1]);	\
} while (0)

/* sys I/F for cooling device */
#define to_cooling_device(_dev)	\
	container_of(_dev, struct thermal_cooling_device, device)

static ssize_t
thermal_cooling_device_type_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);

	return sprintf(buf, "%s\n", cdev->type);
}

static ssize_t
thermal_cooling_device_max_state_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);

	return cdev->ops->get_max_state(cdev, buf);
}

static ssize_t
thermal_cooling_device_cur_state_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);

	return cdev->ops->get_cur_state(cdev, buf);
}

static ssize_t
thermal_cooling_device_cur_state_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct thermal_cooling_device *cdev = to_cooling_device(dev);
	int state;
	int result;

	if (!sscanf(buf, "%d\n", &state))
		return -EINVAL;

	if (state < 0)
		return -EINVAL;

	result = cdev->ops->set_cur_state(cdev, state);
	if (result)
		return result;
	return count;
}

static struct device_attribute dev_attr_cdev_type =
__ATTR(type, 0444, thermal_cooling_device_type_show, NULL);
static DEVICE_ATTR(max_state, 0444,
		   thermal_cooling_device_max_state_show, NULL);
static DEVICE_ATTR(cur_state, 0644,
		   thermal_cooling_device_cur_state_show,
		   thermal_cooling_device_cur_state_store);

static ssize_t
thermal_cooling_device_trip_point_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device_instance *instance;

	instance =
	    container_of(attr, struct thermal_cooling_device_instance, attr);

	if (instance->trip == THERMAL_TRIPS_NONE)
		return sprintf(buf, "-1\n");
	else
		return sprintf(buf, "%d\n", instance->trip);
}

/* Device management */

#if defined(CONFIG_THERMAL_HWMON)

/* hwmon sys I/F */
#include <linux/hwmon.h>
static LIST_HEAD(thermal_hwmon_list);

static ssize_t
name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_hwmon_device *hwmon = dev->driver_data;
	return sprintf(buf, "%s\n", hwmon->type);
}
static DEVICE_ATTR(name, 0444, name_show, NULL);

static ssize_t
temp_input_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_hwmon_attr *hwmon_attr
			= container_of(attr, struct thermal_hwmon_attr, attr);
	struct thermal_zone_device *tz
			= container_of(hwmon_attr, struct thermal_zone_device,
				       temp_input);

	return tz->ops->get_temp(tz, buf);
}

static ssize_t
temp_crit_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct thermal_hwmon_attr *hwmon_attr
			= container_of(attr, struct thermal_hwmon_attr, attr);
	struct thermal_zone_device *tz
			= container_of(hwmon_attr, struct thermal_zone_device,
				       temp_crit);

	return tz->ops->get_trip_temp(tz, 0, buf);
}


static int
thermal_add_hwmon_sysfs(struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon;
	int new_hwmon_device = 1;
	int result;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(hwmon, &thermal_hwmon_list, node)
		if (!strcmp(hwmon->type, tz->type)) {
			new_hwmon_device = 0;
			mutex_unlock(&thermal_list_lock);
			goto register_sys_interface;
		}
	mutex_unlock(&thermal_list_lock);

	hwmon = kzalloc(sizeof(struct thermal_hwmon_device), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	INIT_LIST_HEAD(&hwmon->tz_list);
	strlcpy(hwmon->type, tz->type, THERMAL_NAME_LENGTH);
	hwmon->device = hwmon_device_register(NULL);
	if (IS_ERR(hwmon->device)) {
		result = PTR_ERR(hwmon->device);
		goto free_mem;
	}
	hwmon->device->driver_data = hwmon;
	result = device_create_file(hwmon->device, &dev_attr_name);
	if (result)
		goto unregister_hwmon_device;

 register_sys_interface:
	tz->hwmon = hwmon;
	hwmon->count++;

	snprintf(tz->temp_input.name, THERMAL_NAME_LENGTH,
		 "temp%d_input", hwmon->count);
	tz->temp_input.attr.attr.name = tz->temp_input.name;
	tz->temp_input.attr.attr.mode = 0444;
	tz->temp_input.attr.show = temp_input_show;
	result = device_create_file(hwmon->device, &tz->temp_input.attr);
	if (result)
		goto unregister_hwmon_device;

	if (tz->ops->get_crit_temp) {
		unsigned long temperature;
		if (!tz->ops->get_crit_temp(tz, &temperature)) {
			snprintf(tz->temp_crit.name, THERMAL_NAME_LENGTH,
				"temp%d_crit", hwmon->count);
			tz->temp_crit.attr.attr.name = tz->temp_crit.name;
			tz->temp_crit.attr.attr.mode = 0444;
			tz->temp_crit.attr.show = temp_crit_show;
			result = device_create_file(hwmon->device,
						    &tz->temp_crit.attr);
			if (result)
				goto unregister_hwmon_device;
		}
	}

	mutex_lock(&thermal_list_lock);
	if (new_hwmon_device)
		list_add_tail(&hwmon->node, &thermal_hwmon_list);
	list_add_tail(&tz->hwmon_node, &hwmon->tz_list);
	mutex_unlock(&thermal_list_lock);

	return 0;

 unregister_hwmon_device:
	device_remove_file(hwmon->device, &tz->temp_crit.attr);
	device_remove_file(hwmon->device, &tz->temp_input.attr);
	if (new_hwmon_device) {
		device_remove_file(hwmon->device, &dev_attr_name);
		hwmon_device_unregister(hwmon->device);
	}
 free_mem:
	if (new_hwmon_device)
		kfree(hwmon);

	return result;
}

static void
thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz)
{
	struct thermal_hwmon_device *hwmon = tz->hwmon;

	tz->hwmon = NULL;
	device_remove_file(hwmon->device, &tz->temp_input.attr);
	device_remove_file(hwmon->device, &tz->temp_crit.attr);

	mutex_lock(&thermal_list_lock);
	list_del(&tz->hwmon_node);
	if (!list_empty(&hwmon->tz_list)) {
		mutex_unlock(&thermal_list_lock);
		return;
	}
	list_del(&hwmon->node);
	mutex_unlock(&thermal_list_lock);

	device_remove_file(hwmon->device, &dev_attr_name);
	hwmon_device_unregister(hwmon->device);
	kfree(hwmon);
}
#else
static int
thermal_add_hwmon_sysfs(struct thermal_zone_device *tz)
{
	return 0;
}

static void
thermal_remove_hwmon_sysfs(struct thermal_zone_device *tz)
{
}
#endif


/**
 * thermal_zone_bind_cooling_device - bind a cooling device to a thermal zone
 * @tz:		thermal zone device
 * @trip:	indicates which trip point the cooling devices is
 *		associated with in this thermal zone.
 * @cdev:	thermal cooling device
 *
 * This function is usually called in the thermal zone device .bind callback.
 */
int thermal_zone_bind_cooling_device(struct thermal_zone_device *tz,
				     int trip,
				     struct thermal_cooling_device *cdev)
{
	struct thermal_cooling_device_instance *dev;
	struct thermal_cooling_device_instance *pos;
	struct thermal_zone_device *pos1;
	struct thermal_cooling_device *pos2;
	int result;

	if (trip >= tz->trips || (trip < 0 && trip != THERMAL_TRIPS_NONE))
		return -EINVAL;

	list_for_each_entry(pos1, &thermal_tz_list, node) {
		if (pos1 == tz)
			break;
	}
	list_for_each_entry(pos2, &thermal_cdev_list, node) {
		if (pos2 == cdev)
			break;
	}

	if (tz != pos1 || cdev != pos2)
		return -EINVAL;

	dev =
	    kzalloc(sizeof(struct thermal_cooling_device_instance), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->tz = tz;
	dev->cdev = cdev;
	dev->trip = trip;
	result = get_idr(&tz->idr, &tz->lock, &dev->id);
	if (result)
		goto free_mem;

	sprintf(dev->name, "cdev%d", dev->id);
	result =
	    sysfs_create_link(&tz->device.kobj, &cdev->device.kobj, dev->name);
	if (result)
		goto release_idr;

	sprintf(dev->attr_name, "cdev%d_trip_point", dev->id);
	dev->attr.attr.name = dev->attr_name;
	dev->attr.attr.mode = 0444;
	dev->attr.show = thermal_cooling_device_trip_point_show;
	result = device_create_file(&tz->device, &dev->attr);
	if (result)
		goto remove_symbol_link;

	mutex_lock(&tz->lock);
	list_for_each_entry(pos, &tz->cooling_devices, node)
	    if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
		result = -EEXIST;
		break;
	}
	if (!result)
		list_add_tail(&dev->node, &tz->cooling_devices);
	mutex_unlock(&tz->lock);

	if (!result)
		return 0;

	device_remove_file(&tz->device, &dev->attr);
      remove_symbol_link:
	sysfs_remove_link(&tz->device.kobj, dev->name);
      release_idr:
	release_idr(&tz->idr, &tz->lock, dev->id);
      free_mem:
	kfree(dev);
	return result;
}

EXPORT_SYMBOL(thermal_zone_bind_cooling_device);

/**
 * thermal_zone_unbind_cooling_device - unbind a cooling device from a thermal zone
 * @tz:		thermal zone device
 * @trip:	indicates which trip point the cooling devices is
 *		associated with in this thermal zone.
 * @cdev:	thermal cooling device
 *
 * This function is usually called in the thermal zone device .unbind callback.
 */
int thermal_zone_unbind_cooling_device(struct thermal_zone_device *tz,
				       int trip,
				       struct thermal_cooling_device *cdev)
{
	struct thermal_cooling_device_instance *pos, *next;

	mutex_lock(&tz->lock);
	list_for_each_entry_safe(pos, next, &tz->cooling_devices, node) {
		if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
			list_del(&pos->node);
			mutex_unlock(&tz->lock);
			goto unbind;
		}
	}
	mutex_unlock(&tz->lock);

	return -ENODEV;

      unbind:
	device_remove_file(&tz->device, &pos->attr);
	sysfs_remove_link(&tz->device.kobj, pos->name);
	release_idr(&tz->idr, &tz->lock, pos->id);
	kfree(pos);
	return 0;
}

EXPORT_SYMBOL(thermal_zone_unbind_cooling_device);

static void thermal_release(struct device *dev)
{
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev;

	if (!strncmp(dev->bus_id, "thermal_zone", sizeof "thermal_zone" - 1)) {
		tz = to_thermal_zone(dev);
		kfree(tz);
	} else {
		cdev = to_cooling_device(dev);
		kfree(cdev);
	}
}

static struct class thermal_class = {
	.name = "thermal",
	.dev_release = thermal_release,
};

/**
 * thermal_cooling_device_register - register a new thermal cooling device
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:		standard thermal cooling devices callbacks.
 */
struct thermal_cooling_device *thermal_cooling_device_register(char *type,
							       void *devdata,
							       struct
							       thermal_cooling_device_ops
							       *ops)
{
	struct thermal_cooling_device *cdev;
	struct thermal_zone_device *pos;
	int result;

	if (strlen(type) >= THERMAL_NAME_LENGTH)
		return ERR_PTR(-EINVAL);

	if (!ops || !ops->get_max_state || !ops->get_cur_state ||
	    !ops->set_cur_state)
		return ERR_PTR(-EINVAL);

	cdev = kzalloc(sizeof(struct thermal_cooling_device), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	result = get_idr(&thermal_cdev_idr, &thermal_idr_lock, &cdev->id);
	if (result) {
		kfree(cdev);
		return ERR_PTR(result);
	}

	strcpy(cdev->type, type);
	cdev->ops = ops;
	cdev->device.class = &thermal_class;
	cdev->devdata = devdata;
	sprintf(cdev->device.bus_id, "cooling_device%d", cdev->id);
	result = device_register(&cdev->device);
	if (result) {
		release_idr(&thermal_cdev_idr, &thermal_idr_lock, cdev->id);
		kfree(cdev);
		return ERR_PTR(result);
	}

	/* sys I/F */
	if (type) {
		result = device_create_file(&cdev->device, &dev_attr_cdev_type);
		if (result)
			goto unregister;
	}

	result = device_create_file(&cdev->device, &dev_attr_max_state);
	if (result)
		goto unregister;

	result = device_create_file(&cdev->device, &dev_attr_cur_state);
	if (result)
		goto unregister;

	mutex_lock(&thermal_list_lock);
	list_add(&cdev->node, &thermal_cdev_list);
	list_for_each_entry(pos, &thermal_tz_list, node) {
		if (!pos->ops->bind)
			continue;
		result = pos->ops->bind(pos, cdev);
		if (result)
			break;

	}
	mutex_unlock(&thermal_list_lock);

	if (!result)
		return cdev;

      unregister:
	release_idr(&thermal_cdev_idr, &thermal_idr_lock, cdev->id);
	device_unregister(&cdev->device);
	return ERR_PTR(result);
}

EXPORT_SYMBOL(thermal_cooling_device_register);

/**
 * thermal_cooling_device_unregister - removes the registered thermal cooling device
 * @cdev:	the thermal cooling device to remove.
 *
 * thermal_cooling_device_unregister() must be called when the device is no
 * longer needed.
 */
void thermal_cooling_device_unregister(struct
				       thermal_cooling_device
				       *cdev)
{
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *pos = NULL;

	if (!cdev)
		return;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(pos, &thermal_cdev_list, node)
	    if (pos == cdev)
		break;
	if (pos != cdev) {
		/* thermal cooling device not found */
		mutex_unlock(&thermal_list_lock);
		return;
	}
	list_del(&cdev->node);
	list_for_each_entry(tz, &thermal_tz_list, node) {
		if (!tz->ops->unbind)
			continue;
		tz->ops->unbind(tz, cdev);
	}
	mutex_unlock(&thermal_list_lock);
	if (cdev->type[0])
		device_remove_file(&cdev->device, &dev_attr_cdev_type);
	device_remove_file(&cdev->device, &dev_attr_max_state);
	device_remove_file(&cdev->device, &dev_attr_cur_state);

	release_idr(&thermal_cdev_idr, &thermal_idr_lock, cdev->id);
	device_unregister(&cdev->device);
	return;
}

EXPORT_SYMBOL(thermal_cooling_device_unregister);

/**
 * thermal_zone_device_register - register a new thermal zone device
 * @type:	the thermal zone device type
 * @trips:	the number of trip points the thermal zone support
 * @devdata:	private device data
 * @ops:	standard thermal zone device callbacks
 *
 * thermal_zone_device_unregister() must be called when the device is no
 * longer needed.
 */
struct thermal_zone_device *thermal_zone_device_register(char *type,
							 int trips,
							 void *devdata, struct
							 thermal_zone_device_ops
							 *ops)
{
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *pos;
	int result;
	int count;

	if (strlen(type) >= THERMAL_NAME_LENGTH)
		return ERR_PTR(-EINVAL);

	if (trips > THERMAL_MAX_TRIPS || trips < 0)
		return ERR_PTR(-EINVAL);

	if (!ops || !ops->get_temp)
		return ERR_PTR(-EINVAL);

	tz = kzalloc(sizeof(struct thermal_zone_device), GFP_KERNEL);
	if (!tz)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&tz->cooling_devices);
	idr_init(&tz->idr);
	mutex_init(&tz->lock);
	result = get_idr(&thermal_tz_idr, &thermal_idr_lock, &tz->id);
	if (result) {
		kfree(tz);
		return ERR_PTR(result);
	}

	strcpy(tz->type, type);
	tz->ops = ops;
	tz->device.class = &thermal_class;
	tz->devdata = devdata;
	tz->trips = trips;
	sprintf(tz->device.bus_id, "thermal_zone%d", tz->id);
	result = device_register(&tz->device);
	if (result) {
		release_idr(&thermal_tz_idr, &thermal_idr_lock, tz->id);
		kfree(tz);
		return ERR_PTR(result);
	}

	/* sys I/F */
	if (type) {
		result = device_create_file(&tz->device, &dev_attr_type);
		if (result)
			goto unregister;
	}

	result = device_create_file(&tz->device, &dev_attr_temp);
	if (result)
		goto unregister;

	if (ops->get_mode) {
		result = device_create_file(&tz->device, &dev_attr_mode);
		if (result)
			goto unregister;
	}

	for (count = 0; count < trips; count++) {
		TRIP_POINT_ATTR_ADD(&tz->device, count, result);
		if (result)
			goto unregister;
	}

	result = thermal_add_hwmon_sysfs(tz);
	if (result)
		goto unregister;

	mutex_lock(&thermal_list_lock);
	list_add_tail(&tz->node, &thermal_tz_list);
	if (ops->bind)
		list_for_each_entry(pos, &thermal_cdev_list, node) {
		result = ops->bind(tz, pos);
		if (result)
			break;
		}
	mutex_unlock(&thermal_list_lock);

	if (!result)
		return tz;

      unregister:
	release_idr(&thermal_tz_idr, &thermal_idr_lock, tz->id);
	device_unregister(&tz->device);
	return ERR_PTR(result);
}

EXPORT_SYMBOL(thermal_zone_device_register);

/**
 * thermal_device_unregister - removes the registered thermal zone device
 * @tz: the thermal zone device to remove
 */
void thermal_zone_device_unregister(struct thermal_zone_device *tz)
{
	struct thermal_cooling_device *cdev;
	struct thermal_zone_device *pos = NULL;
	int count;

	if (!tz)
		return;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(pos, &thermal_tz_list, node)
	    if (pos == tz)
		break;
	if (pos != tz) {
		/* thermal zone device not found */
		mutex_unlock(&thermal_list_lock);
		return;
	}
	list_del(&tz->node);
	if (tz->ops->unbind)
		list_for_each_entry(cdev, &thermal_cdev_list, node)
		    tz->ops->unbind(tz, cdev);
	mutex_unlock(&thermal_list_lock);

	if (tz->type[0])
		device_remove_file(&tz->device, &dev_attr_type);
	device_remove_file(&tz->device, &dev_attr_temp);
	if (tz->ops->get_mode)
		device_remove_file(&tz->device, &dev_attr_mode);

	for (count = 0; count < tz->trips; count++)
		TRIP_POINT_ATTR_REMOVE(&tz->device, count);

	thermal_remove_hwmon_sysfs(tz);
	release_idr(&thermal_tz_idr, &thermal_idr_lock, tz->id);
	idr_destroy(&tz->idr);
	mutex_destroy(&tz->lock);
	device_unregister(&tz->device);
	return;
}

EXPORT_SYMBOL(thermal_zone_device_unregister);

static int __init thermal_init(void)
{
	int result = 0;

	result = class_register(&thermal_class);
	if (result) {
		idr_destroy(&thermal_tz_idr);
		idr_destroy(&thermal_cdev_idr);
		mutex_destroy(&thermal_idr_lock);
		mutex_destroy(&thermal_list_lock);
	}
	return result;
}

static void __exit thermal_exit(void)
{
	class_unregister(&thermal_class);
	idr_destroy(&thermal_tz_idr);
	idr_destroy(&thermal_cdev_idr);
	mutex_destroy(&thermal_idr_lock);
	mutex_destroy(&thermal_list_lock);
}

subsys_initcall(thermal_init);
module_exit(thermal_exit);
