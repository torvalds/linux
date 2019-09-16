// SPDX-License-Identifier: GPL-2.0
/*
 *  thermal.c - Generic Thermal Management Sysfs support.
 *
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/thermal.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/of.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/suspend.h>

#define CREATE_TRACE_POINTS
#include <trace/events/thermal.h>

#include "thermal_core.h"
#include "thermal_hwmon.h"

MODULE_AUTHOR("Zhang Rui");
MODULE_DESCRIPTION("Generic thermal management sysfs support");
MODULE_LICENSE("GPL v2");

static DEFINE_IDA(thermal_tz_ida);
static DEFINE_IDA(thermal_cdev_ida);

static LIST_HEAD(thermal_tz_list);
static LIST_HEAD(thermal_cdev_list);
static LIST_HEAD(thermal_governor_list);

static DEFINE_MUTEX(thermal_list_lock);
static DEFINE_MUTEX(thermal_governor_lock);
static DEFINE_MUTEX(poweroff_lock);

static atomic_t in_suspend;
static bool power_off_triggered;

static struct thermal_governor *def_governor;

/*
 * Governor section: set of functions to handle thermal governors
 *
 * Functions to help in the life cycle of thermal governors within
 * the thermal core and by the thermal governor code.
 */

static struct thermal_governor *__find_governor(const char *name)
{
	struct thermal_governor *pos;

	if (!name || !name[0])
		return def_governor;

	list_for_each_entry(pos, &thermal_governor_list, governor_list)
		if (!strncasecmp(name, pos->name, THERMAL_NAME_LENGTH))
			return pos;

	return NULL;
}

/**
 * bind_previous_governor() - bind the previous governor of the thermal zone
 * @tz:		a valid pointer to a struct thermal_zone_device
 * @failed_gov_name:	the name of the governor that failed to register
 *
 * Register the previous governor of the thermal zone after a new
 * governor has failed to be bound.
 */
static void bind_previous_governor(struct thermal_zone_device *tz,
				   const char *failed_gov_name)
{
	if (tz->governor && tz->governor->bind_to_tz) {
		if (tz->governor->bind_to_tz(tz)) {
			dev_err(&tz->device,
				"governor %s failed to bind and the previous one (%s) failed to bind again, thermal zone %s has no governor\n",
				failed_gov_name, tz->governor->name, tz->type);
			tz->governor = NULL;
		}
	}
}

/**
 * thermal_set_governor() - Switch to another governor
 * @tz:		a valid pointer to a struct thermal_zone_device
 * @new_gov:	pointer to the new governor
 *
 * Change the governor of thermal zone @tz.
 *
 * Return: 0 on success, an error if the new governor's bind_to_tz() failed.
 */
static int thermal_set_governor(struct thermal_zone_device *tz,
				struct thermal_governor *new_gov)
{
	int ret = 0;

	if (tz->governor && tz->governor->unbind_from_tz)
		tz->governor->unbind_from_tz(tz);

	if (new_gov && new_gov->bind_to_tz) {
		ret = new_gov->bind_to_tz(tz);
		if (ret) {
			bind_previous_governor(tz, new_gov->name);

			return ret;
		}
	}

	tz->governor = new_gov;

	return ret;
}

int thermal_register_governor(struct thermal_governor *governor)
{
	int err;
	const char *name;
	struct thermal_zone_device *pos;

	if (!governor)
		return -EINVAL;

	mutex_lock(&thermal_governor_lock);

	err = -EBUSY;
	if (!__find_governor(governor->name)) {
		bool match_default;

		err = 0;
		list_add(&governor->governor_list, &thermal_governor_list);
		match_default = !strncmp(governor->name,
					 DEFAULT_THERMAL_GOVERNOR,
					 THERMAL_NAME_LENGTH);

		if (!def_governor && match_default)
			def_governor = governor;
	}

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, node) {
		/*
		 * only thermal zones with specified tz->tzp->governor_name
		 * may run with tz->govenor unset
		 */
		if (pos->governor)
			continue;

		name = pos->tzp->governor_name;

		if (!strncasecmp(name, governor->name, THERMAL_NAME_LENGTH)) {
			int ret;

			ret = thermal_set_governor(pos, governor);
			if (ret)
				dev_err(&pos->device,
					"Failed to set governor %s for thermal zone %s: %d\n",
					governor->name, pos->type, ret);
		}
	}

	mutex_unlock(&thermal_list_lock);
	mutex_unlock(&thermal_governor_lock);

	return err;
}

void thermal_unregister_governor(struct thermal_governor *governor)
{
	struct thermal_zone_device *pos;

	if (!governor)
		return;

	mutex_lock(&thermal_governor_lock);

	if (!__find_governor(governor->name))
		goto exit;

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, node) {
		if (!strncasecmp(pos->governor->name, governor->name,
				 THERMAL_NAME_LENGTH))
			thermal_set_governor(pos, NULL);
	}

	mutex_unlock(&thermal_list_lock);
	list_del(&governor->governor_list);
exit:
	mutex_unlock(&thermal_governor_lock);
}

int thermal_zone_device_set_policy(struct thermal_zone_device *tz,
				   char *policy)
{
	struct thermal_governor *gov;
	int ret = -EINVAL;

	mutex_lock(&thermal_governor_lock);
	mutex_lock(&tz->lock);

	gov = __find_governor(strim(policy));
	if (!gov)
		goto exit;

	ret = thermal_set_governor(tz, gov);

exit:
	mutex_unlock(&tz->lock);
	mutex_unlock(&thermal_governor_lock);

	return ret;
}

int thermal_build_list_of_policies(char *buf)
{
	struct thermal_governor *pos;
	ssize_t count = 0;
	ssize_t size = PAGE_SIZE;

	mutex_lock(&thermal_governor_lock);

	list_for_each_entry(pos, &thermal_governor_list, governor_list) {
		size = PAGE_SIZE - count;
		count += scnprintf(buf + count, size, "%s ", pos->name);
	}
	count += scnprintf(buf + count, size, "\n");

	mutex_unlock(&thermal_governor_lock);

	return count;
}

static void __init thermal_unregister_governors(void)
{
	struct thermal_governor **governor;

	for_each_governor_table(governor)
		thermal_unregister_governor(*governor);
}

static int __init thermal_register_governors(void)
{
	int ret = 0;
	struct thermal_governor **governor;

	for_each_governor_table(governor) {
		ret = thermal_register_governor(*governor);
		if (ret) {
			pr_err("Failed to register governor: '%s'",
			       (*governor)->name);
			break;
		}

		pr_info("Registered thermal governor '%s'",
			(*governor)->name);
	}

	if (ret) {
		struct thermal_governor **gov;

		for_each_governor_table(gov) {
			if (gov == governor)
				break;
			thermal_unregister_governor(*gov);
		}
	}

	return ret;
}

/*
 * Zone update section: main control loop applied to each zone while monitoring
 *
 * in polling mode. The monitoring is done using a workqueue.
 * Same update may be done on a zone by calling thermal_zone_device_update().
 *
 * An update means:
 * - Non-critical trips will invoke the governor responsible for that zone;
 * - Hot trips will produce a notification to userspace;
 * - Critical trip point will cause a system shutdown.
 */
static void thermal_zone_device_set_polling(struct thermal_zone_device *tz,
					    int delay)
{
	if (delay > 1000)
		mod_delayed_work(system_freezable_power_efficient_wq,
				 &tz->poll_queue,
				 round_jiffies(msecs_to_jiffies(delay)));
	else if (delay)
		mod_delayed_work(system_freezable_power_efficient_wq,
				 &tz->poll_queue,
				 msecs_to_jiffies(delay));
	else
		cancel_delayed_work(&tz->poll_queue);
}

static void monitor_thermal_zone(struct thermal_zone_device *tz)
{
	mutex_lock(&tz->lock);

	if (tz->passive)
		thermal_zone_device_set_polling(tz, tz->passive_delay);
	else if (tz->polling_delay)
		thermal_zone_device_set_polling(tz, tz->polling_delay);
	else
		thermal_zone_device_set_polling(tz, 0);

	mutex_unlock(&tz->lock);
}

static void handle_non_critical_trips(struct thermal_zone_device *tz, int trip)
{
	tz->governor ? tz->governor->throttle(tz, trip) :
		       def_governor->throttle(tz, trip);
}

/**
 * thermal_emergency_poweroff_func - emergency poweroff work after a known delay
 * @work: work_struct associated with the emergency poweroff function
 *
 * This function is called in very critical situations to force
 * a kernel poweroff after a configurable timeout value.
 */
static void thermal_emergency_poweroff_func(struct work_struct *work)
{
	/*
	 * We have reached here after the emergency thermal shutdown
	 * Waiting period has expired. This means orderly_poweroff has
	 * not been able to shut off the system for some reason.
	 * Try to shut down the system immediately using kernel_power_off
	 * if populated
	 */
	WARN(1, "Attempting kernel_power_off: Temperature too high\n");
	kernel_power_off();

	/*
	 * Worst of the worst case trigger emergency restart
	 */
	WARN(1, "Attempting emergency_restart: Temperature too high\n");
	emergency_restart();
}

static DECLARE_DELAYED_WORK(thermal_emergency_poweroff_work,
			    thermal_emergency_poweroff_func);

/**
 * thermal_emergency_poweroff - Trigger an emergency system poweroff
 *
 * This may be called from any critical situation to trigger a system shutdown
 * after a known period of time. By default this is not scheduled.
 */
static void thermal_emergency_poweroff(void)
{
	int poweroff_delay_ms = CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS;
	/*
	 * poweroff_delay_ms must be a carefully profiled positive value.
	 * Its a must for thermal_emergency_poweroff_work to be scheduled
	 */
	if (poweroff_delay_ms <= 0)
		return;
	schedule_delayed_work(&thermal_emergency_poweroff_work,
			      msecs_to_jiffies(poweroff_delay_ms));
}

static void handle_critical_trips(struct thermal_zone_device *tz,
				  int trip, enum thermal_trip_type trip_type)
{
	int trip_temp;

	tz->ops->get_trip_temp(tz, trip, &trip_temp);

	/* If we have not crossed the trip_temp, we do not care. */
	if (trip_temp <= 0 || tz->temperature < trip_temp)
		return;

	trace_thermal_zone_trip(tz, trip, trip_type);

	if (tz->ops->notify)
		tz->ops->notify(tz, trip, trip_type);

	if (trip_type == THERMAL_TRIP_CRITICAL) {
		dev_emerg(&tz->device,
			  "critical temperature reached (%d C), shutting down\n",
			  tz->temperature / 1000);
		mutex_lock(&poweroff_lock);
		if (!power_off_triggered) {
			/*
			 * Queue a backup emergency shutdown in the event of
			 * orderly_poweroff failure
			 */
			thermal_emergency_poweroff();
			orderly_poweroff(true);
			power_off_triggered = true;
		}
		mutex_unlock(&poweroff_lock);
	}
}

static void handle_thermal_trip(struct thermal_zone_device *tz, int trip)
{
	enum thermal_trip_type type;

	/* Ignore disabled trip points */
	if (test_bit(trip, &tz->trips_disabled))
		return;

	tz->ops->get_trip_type(tz, trip, &type);

	if (type == THERMAL_TRIP_CRITICAL || type == THERMAL_TRIP_HOT)
		handle_critical_trips(tz, trip, type);
	else
		handle_non_critical_trips(tz, trip);
	/*
	 * Alright, we handled this trip successfully.
	 * So, start monitoring again.
	 */
	monitor_thermal_zone(tz);
}

static void update_temperature(struct thermal_zone_device *tz)
{
	int temp, ret;

	ret = thermal_zone_get_temp(tz, &temp);
	if (ret) {
		if (ret != -EAGAIN)
			dev_warn(&tz->device,
				 "failed to read out thermal zone (%d)\n",
				 ret);
		return;
	}

	mutex_lock(&tz->lock);
	tz->last_temperature = tz->temperature;
	tz->temperature = temp;
	mutex_unlock(&tz->lock);

	trace_thermal_temperature(tz);
	if (tz->last_temperature == THERMAL_TEMP_INVALID)
		dev_dbg(&tz->device, "last_temperature N/A, current_temperature=%d\n",
			tz->temperature);
	else
		dev_dbg(&tz->device, "last_temperature=%d, current_temperature=%d\n",
			tz->last_temperature, tz->temperature);
}

static void thermal_zone_device_init(struct thermal_zone_device *tz)
{
	struct thermal_instance *pos;
	tz->temperature = THERMAL_TEMP_INVALID;
	list_for_each_entry(pos, &tz->thermal_instances, tz_node)
		pos->initialized = false;
}

static void thermal_zone_device_reset(struct thermal_zone_device *tz)
{
	tz->passive = 0;
	thermal_zone_device_init(tz);
}

void thermal_zone_device_update(struct thermal_zone_device *tz,
				enum thermal_notify_event event)
{
	int count;

	if (atomic_read(&in_suspend))
		return;

	if (!tz->ops->get_temp)
		return;

	update_temperature(tz);

	thermal_zone_set_trips(tz);

	tz->notify_event = event;

	for (count = 0; count < tz->trips; count++)
		handle_thermal_trip(tz, count);
}
EXPORT_SYMBOL_GPL(thermal_zone_device_update);

/**
 * thermal_notify_framework - Sensor drivers use this API to notify framework
 * @tz:		thermal zone device
 * @trip:	indicates which trip point has been crossed
 *
 * This function handles the trip events from sensor drivers. It starts
 * throttling the cooling devices according to the policy configured.
 * For CRITICAL and HOT trip points, this notifies the respective drivers,
 * and does actual throttling for other trip points i.e ACTIVE and PASSIVE.
 * The throttling policy is based on the configured platform data; if no
 * platform data is provided, this uses the step_wise throttling policy.
 */
void thermal_notify_framework(struct thermal_zone_device *tz, int trip)
{
	handle_thermal_trip(tz, trip);
}
EXPORT_SYMBOL_GPL(thermal_notify_framework);

static void thermal_zone_device_check(struct work_struct *work)
{
	struct thermal_zone_device *tz = container_of(work, struct
						      thermal_zone_device,
						      poll_queue.work);
	thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);
}

/*
 * Power actor section: interface to power actors to estimate power
 *
 * Set of functions used to interact to cooling devices that know
 * how to estimate their devices power consumption.
 */

/**
 * power_actor_get_max_power() - get the maximum power that a cdev can consume
 * @cdev:	pointer to &thermal_cooling_device
 * @tz:		a valid thermal zone device pointer
 * @max_power:	pointer in which to store the maximum power
 *
 * Calculate the maximum power consumption in milliwats that the
 * cooling device can currently consume and store it in @max_power.
 *
 * Return: 0 on success, -EINVAL if @cdev doesn't support the
 * power_actor API or -E* on other error.
 */
int power_actor_get_max_power(struct thermal_cooling_device *cdev,
			      struct thermal_zone_device *tz, u32 *max_power)
{
	if (!cdev_is_power_actor(cdev))
		return -EINVAL;

	return cdev->ops->state2power(cdev, tz, 0, max_power);
}

/**
 * power_actor_get_min_power() - get the mainimum power that a cdev can consume
 * @cdev:	pointer to &thermal_cooling_device
 * @tz:		a valid thermal zone device pointer
 * @min_power:	pointer in which to store the minimum power
 *
 * Calculate the minimum power consumption in milliwatts that the
 * cooling device can currently consume and store it in @min_power.
 *
 * Return: 0 on success, -EINVAL if @cdev doesn't support the
 * power_actor API or -E* on other error.
 */
int power_actor_get_min_power(struct thermal_cooling_device *cdev,
			      struct thermal_zone_device *tz, u32 *min_power)
{
	unsigned long max_state;
	int ret;

	if (!cdev_is_power_actor(cdev))
		return -EINVAL;

	ret = cdev->ops->get_max_state(cdev, &max_state);
	if (ret)
		return ret;

	return cdev->ops->state2power(cdev, tz, max_state, min_power);
}

/**
 * power_actor_set_power() - limit the maximum power a cooling device consumes
 * @cdev:	pointer to &thermal_cooling_device
 * @instance:	thermal instance to update
 * @power:	the power in milliwatts
 *
 * Set the cooling device to consume at most @power milliwatts. The limit is
 * expected to be a cap at the maximum power consumption.
 *
 * Return: 0 on success, -EINVAL if the cooling device does not
 * implement the power actor API or -E* for other failures.
 */
int power_actor_set_power(struct thermal_cooling_device *cdev,
			  struct thermal_instance *instance, u32 power)
{
	unsigned long state;
	int ret;

	if (!cdev_is_power_actor(cdev))
		return -EINVAL;

	ret = cdev->ops->power2state(cdev, instance->tz, power, &state);
	if (ret)
		return ret;

	instance->target = state;
	mutex_lock(&cdev->lock);
	cdev->updated = false;
	mutex_unlock(&cdev->lock);
	thermal_cdev_update(cdev);

	return 0;
}

void thermal_zone_device_rebind_exception(struct thermal_zone_device *tz,
					  const char *cdev_type, size_t size)
{
	struct thermal_cooling_device *cdev = NULL;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(cdev, &thermal_cdev_list, node) {
		/* skip non matching cdevs */
		if (strncmp(cdev_type, cdev->type, size))
			continue;

		/* re binding the exception matching the type pattern */
		thermal_zone_bind_cooling_device(tz, THERMAL_TRIPS_NONE, cdev,
						 THERMAL_NO_LIMIT,
						 THERMAL_NO_LIMIT,
						 THERMAL_WEIGHT_DEFAULT);
	}
	mutex_unlock(&thermal_list_lock);
}

void thermal_zone_device_unbind_exception(struct thermal_zone_device *tz,
					  const char *cdev_type, size_t size)
{
	struct thermal_cooling_device *cdev = NULL;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(cdev, &thermal_cdev_list, node) {
		/* skip non matching cdevs */
		if (strncmp(cdev_type, cdev->type, size))
			continue;
		/* unbinding the exception matching the type pattern */
		thermal_zone_unbind_cooling_device(tz, THERMAL_TRIPS_NONE,
						   cdev);
	}
	mutex_unlock(&thermal_list_lock);
}

/*
 * Device management section: cooling devices, zones devices, and binding
 *
 * Set of functions provided by the thermal core for:
 * - cooling devices lifecycle: registration, unregistration,
 *				binding, and unbinding.
 * - thermal zone devices lifecycle: registration, unregistration,
 *				     binding, and unbinding.
 */

/**
 * thermal_zone_bind_cooling_device() - bind a cooling device to a thermal zone
 * @tz:		pointer to struct thermal_zone_device
 * @trip:	indicates which trip point the cooling devices is
 *		associated with in this thermal zone.
 * @cdev:	pointer to struct thermal_cooling_device
 * @upper:	the Maximum cooling state for this trip point.
 *		THERMAL_NO_LIMIT means no upper limit,
 *		and the cooling device can be in max_state.
 * @lower:	the Minimum cooling state can be used for this trip point.
 *		THERMAL_NO_LIMIT means no lower limit,
 *		and the cooling device can be in cooling state 0.
 * @weight:	The weight of the cooling device to be bound to the
 *		thermal zone. Use THERMAL_WEIGHT_DEFAULT for the
 *		default value
 *
 * This interface function bind a thermal cooling device to the certain trip
 * point of a thermal zone device.
 * This function is usually called in the thermal zone device .bind callback.
 *
 * Return: 0 on success, the proper error value otherwise.
 */
int thermal_zone_bind_cooling_device(struct thermal_zone_device *tz,
				     int trip,
				     struct thermal_cooling_device *cdev,
				     unsigned long upper, unsigned long lower,
				     unsigned int weight)
{
	struct thermal_instance *dev;
	struct thermal_instance *pos;
	struct thermal_zone_device *pos1;
	struct thermal_cooling_device *pos2;
	unsigned long max_state;
	int result, ret;

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

	ret = cdev->ops->get_max_state(cdev, &max_state);
	if (ret)
		return ret;

	/* lower default 0, upper default max_state */
	lower = lower == THERMAL_NO_LIMIT ? 0 : lower;
	upper = upper == THERMAL_NO_LIMIT ? max_state : upper;

	if (lower > upper || upper > max_state)
		return -EINVAL;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->tz = tz;
	dev->cdev = cdev;
	dev->trip = trip;
	dev->upper = upper;
	dev->lower = lower;
	dev->target = THERMAL_NO_TARGET;
	dev->weight = weight;

	result = ida_simple_get(&tz->ida, 0, 0, GFP_KERNEL);
	if (result < 0)
		goto free_mem;

	dev->id = result;
	sprintf(dev->name, "cdev%d", dev->id);
	result =
	    sysfs_create_link(&tz->device.kobj, &cdev->device.kobj, dev->name);
	if (result)
		goto release_ida;

	sprintf(dev->attr_name, "cdev%d_trip_point", dev->id);
	sysfs_attr_init(&dev->attr.attr);
	dev->attr.attr.name = dev->attr_name;
	dev->attr.attr.mode = 0444;
	dev->attr.show = trip_point_show;
	result = device_create_file(&tz->device, &dev->attr);
	if (result)
		goto remove_symbol_link;

	sprintf(dev->weight_attr_name, "cdev%d_weight", dev->id);
	sysfs_attr_init(&dev->weight_attr.attr);
	dev->weight_attr.attr.name = dev->weight_attr_name;
	dev->weight_attr.attr.mode = S_IWUSR | S_IRUGO;
	dev->weight_attr.show = weight_show;
	dev->weight_attr.store = weight_store;
	result = device_create_file(&tz->device, &dev->weight_attr);
	if (result)
		goto remove_trip_file;

	mutex_lock(&tz->lock);
	mutex_lock(&cdev->lock);
	list_for_each_entry(pos, &tz->thermal_instances, tz_node)
		if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
			result = -EEXIST;
			break;
		}
	if (!result) {
		list_add_tail(&dev->tz_node, &tz->thermal_instances);
		list_add_tail(&dev->cdev_node, &cdev->thermal_instances);
		atomic_set(&tz->need_update, 1);
	}
	mutex_unlock(&cdev->lock);
	mutex_unlock(&tz->lock);

	if (!result)
		return 0;

	device_remove_file(&tz->device, &dev->weight_attr);
remove_trip_file:
	device_remove_file(&tz->device, &dev->attr);
remove_symbol_link:
	sysfs_remove_link(&tz->device.kobj, dev->name);
release_ida:
	ida_simple_remove(&tz->ida, dev->id);
free_mem:
	kfree(dev);
	return result;
}
EXPORT_SYMBOL_GPL(thermal_zone_bind_cooling_device);

/**
 * thermal_zone_unbind_cooling_device() - unbind a cooling device from a
 *					  thermal zone.
 * @tz:		pointer to a struct thermal_zone_device.
 * @trip:	indicates which trip point the cooling devices is
 *		associated with in this thermal zone.
 * @cdev:	pointer to a struct thermal_cooling_device.
 *
 * This interface function unbind a thermal cooling device from the certain
 * trip point of a thermal zone device.
 * This function is usually called in the thermal zone device .unbind callback.
 *
 * Return: 0 on success, the proper error value otherwise.
 */
int thermal_zone_unbind_cooling_device(struct thermal_zone_device *tz,
				       int trip,
				       struct thermal_cooling_device *cdev)
{
	struct thermal_instance *pos, *next;

	mutex_lock(&tz->lock);
	mutex_lock(&cdev->lock);
	list_for_each_entry_safe(pos, next, &tz->thermal_instances, tz_node) {
		if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
			list_del(&pos->tz_node);
			list_del(&pos->cdev_node);
			mutex_unlock(&cdev->lock);
			mutex_unlock(&tz->lock);
			goto unbind;
		}
	}
	mutex_unlock(&cdev->lock);
	mutex_unlock(&tz->lock);

	return -ENODEV;

unbind:
	device_remove_file(&tz->device, &pos->weight_attr);
	device_remove_file(&tz->device, &pos->attr);
	sysfs_remove_link(&tz->device.kobj, pos->name);
	ida_simple_remove(&tz->ida, pos->id);
	kfree(pos);
	return 0;
}
EXPORT_SYMBOL_GPL(thermal_zone_unbind_cooling_device);

static void thermal_release(struct device *dev)
{
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev;

	if (!strncmp(dev_name(dev), "thermal_zone",
		     sizeof("thermal_zone") - 1)) {
		tz = to_thermal_zone(dev);
		thermal_zone_destroy_device_groups(tz);
		kfree(tz);
	} else if (!strncmp(dev_name(dev), "cooling_device",
			    sizeof("cooling_device") - 1)) {
		cdev = to_cooling_device(dev);
		kfree(cdev);
	}
}

static struct class thermal_class = {
	.name = "thermal",
	.dev_release = thermal_release,
};

static inline
void print_bind_err_msg(struct thermal_zone_device *tz,
			struct thermal_cooling_device *cdev, int ret)
{
	dev_err(&tz->device, "binding zone %s with cdev %s failed:%d\n",
		tz->type, cdev->type, ret);
}

static void __bind(struct thermal_zone_device *tz, int mask,
		   struct thermal_cooling_device *cdev,
		   unsigned long *limits,
		   unsigned int weight)
{
	int i, ret;

	for (i = 0; i < tz->trips; i++) {
		if (mask & (1 << i)) {
			unsigned long upper, lower;

			upper = THERMAL_NO_LIMIT;
			lower = THERMAL_NO_LIMIT;
			if (limits) {
				lower = limits[i * 2];
				upper = limits[i * 2 + 1];
			}
			ret = thermal_zone_bind_cooling_device(tz, i, cdev,
							       upper, lower,
							       weight);
			if (ret)
				print_bind_err_msg(tz, cdev, ret);
		}
	}
}

static void bind_cdev(struct thermal_cooling_device *cdev)
{
	int i, ret;
	const struct thermal_zone_params *tzp;
	struct thermal_zone_device *pos = NULL;

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, node) {
		if (!pos->tzp && !pos->ops->bind)
			continue;

		if (pos->ops->bind) {
			ret = pos->ops->bind(pos, cdev);
			if (ret)
				print_bind_err_msg(pos, cdev, ret);
			continue;
		}

		tzp = pos->tzp;
		if (!tzp || !tzp->tbp)
			continue;

		for (i = 0; i < tzp->num_tbps; i++) {
			if (tzp->tbp[i].cdev || !tzp->tbp[i].match)
				continue;
			if (tzp->tbp[i].match(pos, cdev))
				continue;
			tzp->tbp[i].cdev = cdev;
			__bind(pos, tzp->tbp[i].trip_mask, cdev,
			       tzp->tbp[i].binding_limits,
			       tzp->tbp[i].weight);
		}
	}

	mutex_unlock(&thermal_list_lock);
}

/**
 * __thermal_cooling_device_register() - register a new thermal cooling device
 * @np:		a pointer to a device tree node.
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:		standard thermal cooling devices callbacks.
 *
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 * It also gives the opportunity to link the cooling device to a device tree
 * node, so that it can be bound to a thermal zone created out of device tree.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
static struct thermal_cooling_device *
__thermal_cooling_device_register(struct device_node *np,
				  const char *type, void *devdata,
				  const struct thermal_cooling_device_ops *ops)
{
	struct thermal_cooling_device *cdev;
	struct thermal_zone_device *pos = NULL;
	int result;

	if (type && strlen(type) >= THERMAL_NAME_LENGTH)
		return ERR_PTR(-EINVAL);

	if (!ops || !ops->get_max_state || !ops->get_cur_state ||
	    !ops->set_cur_state)
		return ERR_PTR(-EINVAL);

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	result = ida_simple_get(&thermal_cdev_ida, 0, 0, GFP_KERNEL);
	if (result < 0) {
		kfree(cdev);
		return ERR_PTR(result);
	}

	cdev->id = result;
	strlcpy(cdev->type, type ? : "", sizeof(cdev->type));
	mutex_init(&cdev->lock);
	INIT_LIST_HEAD(&cdev->thermal_instances);
	cdev->np = np;
	cdev->ops = ops;
	cdev->updated = false;
	cdev->device.class = &thermal_class;
	cdev->devdata = devdata;
	thermal_cooling_device_setup_sysfs(cdev);
	dev_set_name(&cdev->device, "cooling_device%d", cdev->id);
	result = device_register(&cdev->device);
	if (result) {
		ida_simple_remove(&thermal_cdev_ida, cdev->id);
		kfree(cdev);
		return ERR_PTR(result);
	}

	/* Add 'this' new cdev to the global cdev list */
	mutex_lock(&thermal_list_lock);
	list_add(&cdev->node, &thermal_cdev_list);
	mutex_unlock(&thermal_list_lock);

	/* Update binding information for 'this' new cdev */
	bind_cdev(cdev);

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(pos, &thermal_tz_list, node)
		if (atomic_cmpxchg(&pos->need_update, 1, 0))
			thermal_zone_device_update(pos,
						   THERMAL_EVENT_UNSPECIFIED);
	mutex_unlock(&thermal_list_lock);

	return cdev;
}

/**
 * thermal_cooling_device_register() - register a new thermal cooling device
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:		standard thermal cooling devices callbacks.
 *
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
struct thermal_cooling_device *
thermal_cooling_device_register(const char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops)
{
	return __thermal_cooling_device_register(NULL, type, devdata, ops);
}
EXPORT_SYMBOL_GPL(thermal_cooling_device_register);

/**
 * thermal_of_cooling_device_register() - register an OF thermal cooling device
 * @np:		a pointer to a device tree node.
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:		standard thermal cooling devices callbacks.
 *
 * This function will register a cooling device with device tree node reference.
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
struct thermal_cooling_device *
thermal_of_cooling_device_register(struct device_node *np,
				   const char *type, void *devdata,
				   const struct thermal_cooling_device_ops *ops)
{
	return __thermal_cooling_device_register(np, type, devdata, ops);
}
EXPORT_SYMBOL_GPL(thermal_of_cooling_device_register);

static void thermal_cooling_device_release(struct device *dev, void *res)
{
	thermal_cooling_device_unregister(
				*(struct thermal_cooling_device **)res);
}

/**
 * devm_thermal_of_cooling_device_register() - register an OF thermal cooling
 *					       device
 * @dev:	a valid struct device pointer of a sensor device.
 * @np:		a pointer to a device tree node.
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:	standard thermal cooling devices callbacks.
 *
 * This function will register a cooling device with device tree node reference.
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
struct thermal_cooling_device *
devm_thermal_of_cooling_device_register(struct device *dev,
				struct device_node *np,
				char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops)
{
	struct thermal_cooling_device **ptr, *tcd;

	ptr = devres_alloc(thermal_cooling_device_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	tcd = __thermal_cooling_device_register(np, type, devdata, ops);
	if (IS_ERR(tcd)) {
		devres_free(ptr);
		return tcd;
	}

	*ptr = tcd;
	devres_add(dev, ptr);

	return tcd;
}
EXPORT_SYMBOL_GPL(devm_thermal_of_cooling_device_register);

static void __unbind(struct thermal_zone_device *tz, int mask,
		     struct thermal_cooling_device *cdev)
{
	int i;

	for (i = 0; i < tz->trips; i++)
		if (mask & (1 << i))
			thermal_zone_unbind_cooling_device(tz, i, cdev);
}

/**
 * thermal_cooling_device_unregister - removes a thermal cooling device
 * @cdev:	the thermal cooling device to remove.
 *
 * thermal_cooling_device_unregister() must be called when a registered
 * thermal cooling device is no longer needed.
 */
void thermal_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	int i;
	const struct thermal_zone_params *tzp;
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

	/* Unbind all thermal zones associated with 'this' cdev */
	list_for_each_entry(tz, &thermal_tz_list, node) {
		if (tz->ops->unbind) {
			tz->ops->unbind(tz, cdev);
			continue;
		}

		if (!tz->tzp || !tz->tzp->tbp)
			continue;

		tzp = tz->tzp;
		for (i = 0; i < tzp->num_tbps; i++) {
			if (tzp->tbp[i].cdev == cdev) {
				__unbind(tz, tzp->tbp[i].trip_mask, cdev);
				tzp->tbp[i].cdev = NULL;
			}
		}
	}

	mutex_unlock(&thermal_list_lock);

	ida_simple_remove(&thermal_cdev_ida, cdev->id);
	device_del(&cdev->device);
	thermal_cooling_device_destroy_sysfs(cdev);
	put_device(&cdev->device);
}
EXPORT_SYMBOL_GPL(thermal_cooling_device_unregister);

static void bind_tz(struct thermal_zone_device *tz)
{
	int i, ret;
	struct thermal_cooling_device *pos = NULL;
	const struct thermal_zone_params *tzp = tz->tzp;

	if (!tzp && !tz->ops->bind)
		return;

	mutex_lock(&thermal_list_lock);

	/* If there is ops->bind, try to use ops->bind */
	if (tz->ops->bind) {
		list_for_each_entry(pos, &thermal_cdev_list, node) {
			ret = tz->ops->bind(tz, pos);
			if (ret)
				print_bind_err_msg(tz, pos, ret);
		}
		goto exit;
	}

	if (!tzp || !tzp->tbp)
		goto exit;

	list_for_each_entry(pos, &thermal_cdev_list, node) {
		for (i = 0; i < tzp->num_tbps; i++) {
			if (tzp->tbp[i].cdev || !tzp->tbp[i].match)
				continue;
			if (tzp->tbp[i].match(tz, pos))
				continue;
			tzp->tbp[i].cdev = pos;
			__bind(tz, tzp->tbp[i].trip_mask, pos,
			       tzp->tbp[i].binding_limits,
			       tzp->tbp[i].weight);
		}
	}
exit:
	mutex_unlock(&thermal_list_lock);
}

/**
 * thermal_zone_device_register() - register a new thermal zone device
 * @type:	the thermal zone device type
 * @trips:	the number of trip points the thermal zone support
 * @mask:	a bit string indicating the writeablility of trip points
 * @devdata:	private device data
 * @ops:	standard thermal zone device callbacks
 * @tzp:	thermal zone platform parameters
 * @passive_delay: number of milliseconds to wait between polls when
 *		   performing passive cooling
 * @polling_delay: number of milliseconds to wait between polls when checking
 *		   whether trip points have been crossed (0 for interrupt
 *		   driven systems)
 *
 * This interface function adds a new thermal zone device (sensor) to
 * /sys/class/thermal folder as thermal_zone[0-*]. It tries to bind all the
 * thermal cooling devices registered at the same time.
 * thermal_zone_device_unregister() must be called when the device is no
 * longer needed. The passive cooling depends on the .get_trend() return value.
 *
 * Return: a pointer to the created struct thermal_zone_device or an
 * in case of error, an ERR_PTR. Caller must check return value with
 * IS_ERR*() helpers.
 */
struct thermal_zone_device *
thermal_zone_device_register(const char *type, int trips, int mask,
			     void *devdata, struct thermal_zone_device_ops *ops,
			     struct thermal_zone_params *tzp, int passive_delay,
			     int polling_delay)
{
	struct thermal_zone_device *tz;
	enum thermal_trip_type trip_type;
	int trip_temp;
	int result;
	int count;
	struct thermal_governor *governor;

	if (!type || strlen(type) == 0)
		return ERR_PTR(-EINVAL);

	if (type && strlen(type) >= THERMAL_NAME_LENGTH)
		return ERR_PTR(-EINVAL);

	if (trips > THERMAL_MAX_TRIPS || trips < 0 || mask >> trips)
		return ERR_PTR(-EINVAL);

	if (!ops)
		return ERR_PTR(-EINVAL);

	if (trips > 0 && (!ops->get_trip_type || !ops->get_trip_temp))
		return ERR_PTR(-EINVAL);

	tz = kzalloc(sizeof(*tz), GFP_KERNEL);
	if (!tz)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&tz->thermal_instances);
	ida_init(&tz->ida);
	mutex_init(&tz->lock);
	result = ida_simple_get(&thermal_tz_ida, 0, 0, GFP_KERNEL);
	if (result < 0)
		goto free_tz;

	tz->id = result;
	strlcpy(tz->type, type, sizeof(tz->type));
	tz->ops = ops;
	tz->tzp = tzp;
	tz->device.class = &thermal_class;
	tz->devdata = devdata;
	tz->trips = trips;
	tz->passive_delay = passive_delay;
	tz->polling_delay = polling_delay;

	/* sys I/F */
	/* Add nodes that are always present via .groups */
	result = thermal_zone_create_device_groups(tz, mask);
	if (result)
		goto remove_id;

	/* A new thermal zone needs to be updated anyway. */
	atomic_set(&tz->need_update, 1);

	dev_set_name(&tz->device, "thermal_zone%d", tz->id);
	result = device_register(&tz->device);
	if (result)
		goto remove_device_groups;

	for (count = 0; count < trips; count++) {
		if (tz->ops->get_trip_type(tz, count, &trip_type))
			set_bit(count, &tz->trips_disabled);
		if (tz->ops->get_trip_temp(tz, count, &trip_temp))
			set_bit(count, &tz->trips_disabled);
		/* Check for bogus trip points */
		if (trip_temp == 0)
			set_bit(count, &tz->trips_disabled);
	}

	/* Update 'this' zone's governor information */
	mutex_lock(&thermal_governor_lock);

	if (tz->tzp)
		governor = __find_governor(tz->tzp->governor_name);
	else
		governor = def_governor;

	result = thermal_set_governor(tz, governor);
	if (result) {
		mutex_unlock(&thermal_governor_lock);
		goto unregister;
	}

	mutex_unlock(&thermal_governor_lock);

	if (!tz->tzp || !tz->tzp->no_hwmon) {
		result = thermal_add_hwmon_sysfs(tz);
		if (result)
			goto unregister;
	}

	mutex_lock(&thermal_list_lock);
	list_add_tail(&tz->node, &thermal_tz_list);
	mutex_unlock(&thermal_list_lock);

	/* Bind cooling devices for this zone */
	bind_tz(tz);

	INIT_DELAYED_WORK(&tz->poll_queue, thermal_zone_device_check);

	thermal_zone_device_reset(tz);
	/* Update the new thermal zone and mark it as already updated. */
	if (atomic_cmpxchg(&tz->need_update, 1, 0))
		thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);

	return tz;

unregister:
	ida_simple_remove(&thermal_tz_ida, tz->id);
	device_unregister(&tz->device);
	return ERR_PTR(result);

remove_device_groups:
	thermal_zone_destroy_device_groups(tz);
remove_id:
	ida_simple_remove(&thermal_tz_ida, tz->id);
free_tz:
	kfree(tz);
	return ERR_PTR(result);
}
EXPORT_SYMBOL_GPL(thermal_zone_device_register);

/**
 * thermal_device_unregister - removes the registered thermal zone device
 * @tz: the thermal zone device to remove
 */
void thermal_zone_device_unregister(struct thermal_zone_device *tz)
{
	int i;
	const struct thermal_zone_params *tzp;
	struct thermal_cooling_device *cdev;
	struct thermal_zone_device *pos = NULL;

	if (!tz)
		return;

	tzp = tz->tzp;

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

	/* Unbind all cdevs associated with 'this' thermal zone */
	list_for_each_entry(cdev, &thermal_cdev_list, node) {
		if (tz->ops->unbind) {
			tz->ops->unbind(tz, cdev);
			continue;
		}

		if (!tzp || !tzp->tbp)
			break;

		for (i = 0; i < tzp->num_tbps; i++) {
			if (tzp->tbp[i].cdev == cdev) {
				__unbind(tz, tzp->tbp[i].trip_mask, cdev);
				tzp->tbp[i].cdev = NULL;
			}
		}
	}

	mutex_unlock(&thermal_list_lock);

	thermal_zone_device_set_polling(tz, 0);

	thermal_set_governor(tz, NULL);

	thermal_remove_hwmon_sysfs(tz);
	ida_simple_remove(&thermal_tz_ida, tz->id);
	ida_destroy(&tz->ida);
	mutex_destroy(&tz->lock);
	device_unregister(&tz->device);
}
EXPORT_SYMBOL_GPL(thermal_zone_device_unregister);

/**
 * thermal_zone_get_zone_by_name() - search for a zone and returns its ref
 * @name: thermal zone name to fetch the temperature
 *
 * When only one zone is found with the passed name, returns a reference to it.
 *
 * Return: On success returns a reference to an unique thermal zone with
 * matching name equals to @name, an ERR_PTR otherwise (-EINVAL for invalid
 * paramenters, -ENODEV for not found and -EEXIST for multiple matches).
 */
struct thermal_zone_device *thermal_zone_get_zone_by_name(const char *name)
{
	struct thermal_zone_device *pos = NULL, *ref = ERR_PTR(-EINVAL);
	unsigned int found = 0;

	if (!name)
		goto exit;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(pos, &thermal_tz_list, node)
		if (!strncasecmp(name, pos->type, THERMAL_NAME_LENGTH)) {
			found++;
			ref = pos;
		}
	mutex_unlock(&thermal_list_lock);

	/* nothing has been found, thus an error code for it */
	if (found == 0)
		ref = ERR_PTR(-ENODEV);
	else if (found > 1)
	/* Success only when an unique zone is found */
		ref = ERR_PTR(-EEXIST);

exit:
	return ref;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_zone_by_name);

#ifdef CONFIG_NET
static const struct genl_multicast_group thermal_event_mcgrps[] = {
	{ .name = THERMAL_GENL_MCAST_GROUP_NAME, },
};

static struct genl_family thermal_event_genl_family __ro_after_init = {
	.module = THIS_MODULE,
	.name = THERMAL_GENL_FAMILY_NAME,
	.version = THERMAL_GENL_VERSION,
	.maxattr = THERMAL_GENL_ATTR_MAX,
	.mcgrps = thermal_event_mcgrps,
	.n_mcgrps = ARRAY_SIZE(thermal_event_mcgrps),
};

int thermal_generate_netlink_event(struct thermal_zone_device *tz,
				   enum events event)
{
	struct sk_buff *skb;
	struct nlattr *attr;
	struct thermal_genl_event *thermal_event;
	void *msg_header;
	int size;
	int result;
	static unsigned int thermal_event_seqnum;

	if (!tz)
		return -EINVAL;

	/* allocate memory */
	size = nla_total_size(sizeof(struct thermal_genl_event)) +
	       nla_total_size(0);

	skb = genlmsg_new(size, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	/* add the genetlink message header */
	msg_header = genlmsg_put(skb, 0, thermal_event_seqnum++,
				 &thermal_event_genl_family, 0,
				 THERMAL_GENL_CMD_EVENT);
	if (!msg_header) {
		nlmsg_free(skb);
		return -ENOMEM;
	}

	/* fill the data */
	attr = nla_reserve(skb, THERMAL_GENL_ATTR_EVENT,
			   sizeof(struct thermal_genl_event));

	if (!attr) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	thermal_event = nla_data(attr);
	if (!thermal_event) {
		nlmsg_free(skb);
		return -EINVAL;
	}

	memset(thermal_event, 0, sizeof(struct thermal_genl_event));

	thermal_event->orig = tz->id;
	thermal_event->event = event;

	/* send multicast genetlink message */
	genlmsg_end(skb, msg_header);

	result = genlmsg_multicast(&thermal_event_genl_family, skb, 0,
				   0, GFP_ATOMIC);
	if (result)
		dev_err(&tz->device, "Failed to send netlink event:%d", result);

	return result;
}
EXPORT_SYMBOL_GPL(thermal_generate_netlink_event);

static int __init genetlink_init(void)
{
	return genl_register_family(&thermal_event_genl_family);
}

static void genetlink_exit(void)
{
	genl_unregister_family(&thermal_event_genl_family);
}
#else /* !CONFIG_NET */
static inline int genetlink_init(void) { return 0; }
static inline void genetlink_exit(void) {}
#endif /* !CONFIG_NET */

static int thermal_pm_notify(struct notifier_block *nb,
			     unsigned long mode, void *_unused)
{
	struct thermal_zone_device *tz;
	enum thermal_device_mode tz_mode;

	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		atomic_set(&in_suspend, 1);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		atomic_set(&in_suspend, 0);
		list_for_each_entry(tz, &thermal_tz_list, node) {
			tz_mode = THERMAL_DEVICE_ENABLED;
			if (tz->ops->get_mode)
				tz->ops->get_mode(tz, &tz_mode);

			if (tz_mode == THERMAL_DEVICE_DISABLED)
				continue;

			thermal_zone_device_init(tz);
			thermal_zone_device_update(tz,
						   THERMAL_EVENT_UNSPECIFIED);
		}
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block thermal_pm_nb = {
	.notifier_call = thermal_pm_notify,
};

static int __init thermal_init(void)
{
	int result;

	mutex_init(&poweroff_lock);
	result = thermal_register_governors();
	if (result)
		goto error;

	result = class_register(&thermal_class);
	if (result)
		goto unregister_governors;

	result = genetlink_init();
	if (result)
		goto unregister_class;

	result = of_parse_thermal_zones();
	if (result)
		goto exit_netlink;

	result = register_pm_notifier(&thermal_pm_nb);
	if (result)
		pr_warn("Thermal: Can not register suspend notifier, return %d\n",
			result);

	return 0;

exit_netlink:
	genetlink_exit();
unregister_class:
	class_unregister(&thermal_class);
unregister_governors:
	thermal_unregister_governors();
error:
	ida_destroy(&thermal_tz_ida);
	ida_destroy(&thermal_cdev_ida);
	mutex_destroy(&thermal_list_lock);
	mutex_destroy(&thermal_governor_lock);
	mutex_destroy(&poweroff_lock);
	return result;
}
fs_initcall(thermal_init);
