// SPDX-License-Identifier: GPL-2.0
/*
 *  thermal.c - Generic Thermal Management Sysfs support.
 *
 *  Copyright (C) 2008 Intel Corp
 *  Copyright (C) 2008 Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008 Sujith Thomas <sujith.thomas@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/thermal.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/suspend.h>

#define CREATE_TRACE_POINTS
#include "thermal_trace.h"

#include "thermal_core.h"
#include "thermal_hwmon.h"

static DEFINE_IDA(thermal_tz_ida);
static DEFINE_IDA(thermal_cdev_ida);

static LIST_HEAD(thermal_tz_list);
static LIST_HEAD(thermal_cdev_list);
static LIST_HEAD(thermal_governor_list);

static DEFINE_MUTEX(thermal_list_lock);
static DEFINE_MUTEX(thermal_governor_lock);

static struct thermal_governor *def_governor;

static bool thermal_pm_suspended;

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

	guard(mutex)(&thermal_governor_lock);

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

	guard(mutex)(&thermal_list_lock);

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

	return err;
}

void thermal_unregister_governor(struct thermal_governor *governor)
{
	struct thermal_zone_device *pos;

	if (!governor)
		return;

	guard(mutex)(&thermal_governor_lock);

	if (!__find_governor(governor->name))
		return;

	list_del(&governor->governor_list);

	guard(mutex)(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, node) {
		if (!strncasecmp(pos->governor->name, governor->name,
				 THERMAL_NAME_LENGTH))
			thermal_set_governor(pos, NULL);
	}
}

int thermal_zone_device_set_policy(struct thermal_zone_device *tz,
				   char *policy)
{
	struct thermal_governor *gov;
	int ret = -EINVAL;

	guard(mutex)(&thermal_governor_lock);
	guard(thermal_zone)(tz);

	gov = __find_governor(strim(policy));
	if (gov)
		ret = thermal_set_governor(tz, gov);

	thermal_notify_tz_gov_change(tz, policy);

	return ret;
}

int thermal_build_list_of_policies(char *buf)
{
	struct thermal_governor *pos;
	ssize_t count = 0;

	guard(mutex)(&thermal_governor_lock);

	list_for_each_entry(pos, &thermal_governor_list, governor_list) {
		count += sysfs_emit_at(buf, count, "%s ", pos->name);
	}
	count += sysfs_emit_at(buf, count, "\n");

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

static int __thermal_zone_device_set_mode(struct thermal_zone_device *tz,
					  enum thermal_device_mode mode)
{
	if (tz->ops.change_mode) {
		int ret;

		ret = tz->ops.change_mode(tz, mode);
		if (ret)
			return ret;
	}

	tz->mode = mode;

	return 0;
}

static void thermal_zone_broken_disable(struct thermal_zone_device *tz)
{
	struct thermal_trip_desc *td;

	dev_err(&tz->device, "Unable to get temperature, disabling!\n");
	/*
	 * This function only runs for enabled thermal zones, so no need to
	 * check for the current mode.
	 */
	__thermal_zone_device_set_mode(tz, THERMAL_DEVICE_DISABLED);
	thermal_notify_tz_disable(tz);

	for_each_trip_desc(tz, td) {
		if (td->trip.type == THERMAL_TRIP_CRITICAL &&
		    td->trip.temperature > THERMAL_TEMP_INVALID) {
			dev_crit(&tz->device,
				 "Disabled thermal zone with critical trip point\n");
			return;
		}
	}
}

/*
 * Zone update section: main control loop applied to each zone while monitoring
 * in polling mode. The monitoring is done using a workqueue.
 * Same update may be done on a zone by calling thermal_zone_device_update().
 *
 * An update means:
 * - Non-critical trips will invoke the governor responsible for that zone;
 * - Hot trips will produce a notification to userspace;
 * - Critical trip point will cause a system shutdown.
 */
static void thermal_zone_device_set_polling(struct thermal_zone_device *tz,
					    unsigned long delay)
{
	if (delay > HZ)
		delay = round_jiffies_relative(delay);

	mod_delayed_work(system_freezable_power_efficient_wq, &tz->poll_queue, delay);
}

static void thermal_zone_recheck(struct thermal_zone_device *tz, int error)
{
	if (error == -EAGAIN) {
		thermal_zone_device_set_polling(tz, THERMAL_RECHECK_DELAY);
		return;
	}

	/*
	 * Print the message once to reduce log noise.  It will be followed by
	 * another one if the temperature cannot be determined after multiple
	 * attempts.
	 */
	if (tz->recheck_delay_jiffies == THERMAL_RECHECK_DELAY)
		dev_info(&tz->device, "Temperature check failed (%d)\n", error);

	thermal_zone_device_set_polling(tz, tz->recheck_delay_jiffies);

	tz->recheck_delay_jiffies += max(tz->recheck_delay_jiffies >> 1, 1ULL);
	if (tz->recheck_delay_jiffies > THERMAL_MAX_RECHECK_DELAY) {
		thermal_zone_broken_disable(tz);
		/*
		 * Restore the original recheck delay value to allow the thermal
		 * zone to try to recover when it is reenabled by user space.
		 */
		tz->recheck_delay_jiffies = THERMAL_RECHECK_DELAY;
	}
}

static void monitor_thermal_zone(struct thermal_zone_device *tz)
{
	if (tz->passive > 0 && tz->passive_delay_jiffies)
		thermal_zone_device_set_polling(tz, tz->passive_delay_jiffies);
	else if (tz->polling_delay_jiffies)
		thermal_zone_device_set_polling(tz, tz->polling_delay_jiffies);
}

static struct thermal_governor *thermal_get_tz_governor(struct thermal_zone_device *tz)
{
	if (tz->governor)
		return tz->governor;

	return def_governor;
}

void thermal_governor_update_tz(struct thermal_zone_device *tz,
				enum thermal_notify_event reason)
{
	if (!tz->governor || !tz->governor->update_tz)
		return;

	tz->governor->update_tz(tz, reason);
}

static void thermal_zone_device_halt(struct thermal_zone_device *tz, bool shutdown)
{
	/*
	 * poweroff_delay_ms must be a carefully profiled positive value.
	 * Its a must for forced_emergency_poweroff_work to be scheduled.
	 */
	int poweroff_delay_ms = CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS;
	const char *msg = "Temperature too high";

	dev_emerg(&tz->device, "%s: critical temperature reached\n", tz->type);

	if (shutdown)
		hw_protection_shutdown(msg, poweroff_delay_ms);
	else
		hw_protection_reboot(msg, poweroff_delay_ms);
}

void thermal_zone_device_critical(struct thermal_zone_device *tz)
{
	thermal_zone_device_halt(tz, true);
}
EXPORT_SYMBOL(thermal_zone_device_critical);

void thermal_zone_device_critical_reboot(struct thermal_zone_device *tz)
{
	thermal_zone_device_halt(tz, false);
}

static void handle_critical_trips(struct thermal_zone_device *tz,
				  const struct thermal_trip *trip)
{
	trace_thermal_zone_trip(tz, thermal_zone_trip_id(tz, trip), trip->type);

	if (trip->type == THERMAL_TRIP_CRITICAL)
		tz->ops.critical(tz);
	else if (tz->ops.hot)
		tz->ops.hot(tz);
}

static void move_trip_to_sorted_list(struct thermal_trip_desc *td,
				     struct list_head *list)
{
	struct thermal_trip_desc *entry;

	/*
	 * Delete upfront and then add to make relocation within the same list
	 * work.
	 */
	list_del(&td->list_node);

	/* Assume that the new entry is likely to be the last one. */
	list_for_each_entry_reverse(entry, list, list_node) {
		if (entry->threshold <= td->threshold) {
			list_add(&td->list_node, &entry->list_node);
			return;
		}
	}
	list_add(&td->list_node, list);
}

static void move_to_trips_high(struct thermal_zone_device *tz,
			       struct thermal_trip_desc *td)
{
	td->threshold = td->trip.temperature;
	move_trip_to_sorted_list(td, &tz->trips_high);
}

static void move_to_trips_reached(struct thermal_zone_device *tz,
				  struct thermal_trip_desc *td)
{
	td->threshold = td->trip.temperature - td->trip.hysteresis;
	move_trip_to_sorted_list(td, &tz->trips_reached);
}

static void move_to_trips_invalid(struct thermal_zone_device *tz,
				  struct thermal_trip_desc *td)
{
	td->threshold = INT_MAX;
	list_move(&td->list_node, &tz->trips_invalid);
}

static void thermal_governor_trip_crossed(struct thermal_governor *governor,
					  struct thermal_zone_device *tz,
					  const struct thermal_trip *trip,
					  bool upward)
{
	if (trip->type == THERMAL_TRIP_HOT || trip->type == THERMAL_TRIP_CRITICAL)
		return;

	if (governor->trip_crossed)
		governor->trip_crossed(tz, trip, upward);
}

static void thermal_trip_crossed(struct thermal_zone_device *tz,
				 struct thermal_trip_desc *td,
				 struct thermal_governor *governor,
				 bool upward)
{
	const struct thermal_trip *trip = &td->trip;

	if (upward) {
		if (trip->type == THERMAL_TRIP_PASSIVE)
			tz->passive++;
		else if (trip->type == THERMAL_TRIP_CRITICAL ||
			 trip->type == THERMAL_TRIP_HOT)
			handle_critical_trips(tz, trip);

		thermal_notify_tz_trip_up(tz, trip);
		thermal_debug_tz_trip_up(tz, trip);
	} else {
		if (trip->type == THERMAL_TRIP_PASSIVE) {
			tz->passive--;
			WARN_ON(tz->passive < 0);
		}
		thermal_notify_tz_trip_down(tz, trip);
		thermal_debug_tz_trip_down(tz, trip);
	}
	thermal_governor_trip_crossed(governor, tz, trip, upward);
}

void thermal_zone_set_trip_hyst(struct thermal_zone_device *tz,
				struct thermal_trip *trip, int hyst)
{
	struct thermal_trip_desc *td = trip_to_trip_desc(trip);

	WRITE_ONCE(trip->hysteresis, hyst);
	thermal_notify_tz_trip_change(tz, trip);
	/*
	 * If the zone temperature is above or at the trip tmperature, the trip
	 * is in the trips_reached list and its threshold is equal to its low
	 * temperature.  It needs to stay in that list, but its threshold needs
	 * to be updated and the list ordering may need to be restored.
	 */
	if (tz->temperature >= td->threshold)
		move_to_trips_reached(tz, td);
}

void thermal_zone_set_trip_temp(struct thermal_zone_device *tz,
				struct thermal_trip *trip, int temp)
{
	struct thermal_trip_desc *td = trip_to_trip_desc(trip);
	int old_temp = trip->temperature;

	if (old_temp == temp)
		return;

	WRITE_ONCE(trip->temperature, temp);
	thermal_notify_tz_trip_change(tz, trip);

	if (old_temp == THERMAL_TEMP_INVALID) {
		/*
		 * The trip was invalid before the change, so move it to the
		 * trips_high list regardless of the new temperature value
		 * because there is no mitigation under way for it.  If a
		 * mitigation needs to be started, the trip will be moved to the
		 * trips_reached list later.
		 */
		move_to_trips_high(tz, td);
		return;
	}

	if (temp == THERMAL_TEMP_INVALID) {
		/*
		 * If the trip is in the trips_reached list, mitigation is under
		 * way for it and it needs to be stopped because the trip is
		 * effectively going away.
		 */
		if (tz->temperature >= td->threshold)
			thermal_trip_crossed(tz, td, thermal_get_tz_governor(tz), false);

		move_to_trips_invalid(tz, td);
		return;
	}

	/*
	 * The trip stays on its current list, but its threshold needs to be
	 * updated due to the temperature change and the list ordering may need
	 * to be restored.
	 */
	if (tz->temperature >= td->threshold)
		move_to_trips_reached(tz, td);
	else
		move_to_trips_high(tz, td);
}
EXPORT_SYMBOL_GPL(thermal_zone_set_trip_temp);

static void thermal_zone_handle_trips(struct thermal_zone_device *tz,
				      struct thermal_governor *governor,
				      int *low, int *high)
{
	struct thermal_trip_desc *td, *next;
	LIST_HEAD(way_down_list);

	/* Check the trips that were below or at the zone temperature. */
	list_for_each_entry_safe_reverse(td, next, &tz->trips_reached, list_node) {
		if (td->threshold <= tz->temperature)
			break;

		thermal_trip_crossed(tz, td, governor, false);
		/*
		 * The current trips_high list needs to be processed before
		 * adding new entries to it, so put them on a temporary list.
		 */
		list_move(&td->list_node, &way_down_list);
	}
	/* Check the trips that were previously above the zone temperature. */
	list_for_each_entry_safe(td, next, &tz->trips_high, list_node) {
		if (td->threshold > tz->temperature)
			break;

		thermal_trip_crossed(tz, td, governor, true);
		move_to_trips_reached(tz, td);
	}
	/* Move all of the trips from the temporary list to trips_high. */
	list_for_each_entry_safe(td, next, &way_down_list, list_node)
		move_to_trips_high(tz, td);

	if (!list_empty(&tz->trips_reached)) {
		td = list_last_entry(&tz->trips_reached,
				     struct thermal_trip_desc, list_node);
		/*
		 * Set the "low" value below the current trip threshold in case
		 * the zone temperature is at that threshold and stays there,
		 * which would trigger a new interrupt immediately in vain.
		 */
		*low = td->threshold - 1;
	}
	if (!list_empty(&tz->trips_high)) {
		td = list_first_entry(&tz->trips_high,
				      struct thermal_trip_desc, list_node);
		*high = td->threshold;
	}
}

void __thermal_zone_device_update(struct thermal_zone_device *tz,
				  enum thermal_notify_event event)
{
	struct thermal_governor *governor = thermal_get_tz_governor(tz);
	int low = -INT_MAX, high = INT_MAX;
	int temp, ret;

	if (tz->state != TZ_STATE_READY || tz->mode != THERMAL_DEVICE_ENABLED)
		return;

	ret = __thermal_zone_get_temp(tz, &temp);
	if (ret) {
		thermal_zone_recheck(tz, ret);
		return;
	} else if (temp <= THERMAL_TEMP_INVALID) {
		/*
		 * Special case: No valid temperature value is available, but
		 * the zone owner does not want the core to do anything about
		 * it.  Continue regular zone polling if needed, so that this
		 * function can be called again, but skip everything else.
		 */
		goto monitor;
	}

	tz->recheck_delay_jiffies = THERMAL_RECHECK_DELAY;

	tz->last_temperature = tz->temperature;
	tz->temperature = temp;

	trace_thermal_temperature(tz);

	thermal_genl_sampling_temp(tz->id, temp);

	tz->notify_event = event;

	thermal_zone_handle_trips(tz, governor, &low, &high);

	thermal_thresholds_handle(tz, &low, &high);

	thermal_zone_set_trips(tz, low, high);

	if (governor->manage)
		governor->manage(tz);

	thermal_debug_update_trip_stats(tz);

monitor:
	monitor_thermal_zone(tz);
}

static int thermal_zone_device_set_mode(struct thermal_zone_device *tz,
					enum thermal_device_mode mode)
{
	int ret;

	guard(thermal_zone)(tz);

	/* do nothing if mode isn't changing */
	if (mode == tz->mode)
		return 0;

	ret = __thermal_zone_device_set_mode(tz, mode);
	if (ret)
		return ret;

	__thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);

	if (mode == THERMAL_DEVICE_ENABLED)
		thermal_notify_tz_enable(tz);
	else
		thermal_notify_tz_disable(tz);

	return 0;
}

int thermal_zone_device_enable(struct thermal_zone_device *tz)
{
	return thermal_zone_device_set_mode(tz, THERMAL_DEVICE_ENABLED);
}
EXPORT_SYMBOL_GPL(thermal_zone_device_enable);

int thermal_zone_device_disable(struct thermal_zone_device *tz)
{
	return thermal_zone_device_set_mode(tz, THERMAL_DEVICE_DISABLED);
}
EXPORT_SYMBOL_GPL(thermal_zone_device_disable);

static bool thermal_zone_is_present(struct thermal_zone_device *tz)
{
	return !list_empty(&tz->node);
}

void thermal_zone_device_update(struct thermal_zone_device *tz,
				enum thermal_notify_event event)
{
	guard(thermal_zone)(tz);

	if (thermal_zone_is_present(tz))
		__thermal_zone_device_update(tz, event);
}
EXPORT_SYMBOL_GPL(thermal_zone_device_update);

int for_each_thermal_governor(int (*cb)(struct thermal_governor *, void *),
			      void *data)
{
	struct thermal_governor *gov;

	guard(mutex)(&thermal_governor_lock);

	list_for_each_entry(gov, &thermal_governor_list, governor_list) {
		int ret;

		ret = cb(gov, data);
		if (ret)
			return ret;
	}

	return 0;
}

int for_each_thermal_cooling_device(int (*cb)(struct thermal_cooling_device *,
					      void *), void *data)
{
	struct thermal_cooling_device *cdev;

	guard(mutex)(&thermal_list_lock);

	list_for_each_entry(cdev, &thermal_cdev_list, node) {
		int ret;

		ret = cb(cdev, data);
		if (ret)
			return ret;
	}

	return 0;
}

int for_each_thermal_zone(int (*cb)(struct thermal_zone_device *, void *),
			  void *data)
{
	struct thermal_zone_device *tz;

	guard(mutex)(&thermal_list_lock);

	list_for_each_entry(tz, &thermal_tz_list, node) {
		int ret;

		ret = cb(tz, data);
		if (ret)
			return ret;
	}

	return 0;
}

struct thermal_zone_device *thermal_zone_get_by_id(int id)
{
	struct thermal_zone_device *tz;

	guard(mutex)(&thermal_list_lock);

	list_for_each_entry(tz, &thermal_tz_list, node) {
		if (tz->id == id) {
			get_device(&tz->device);
			return tz;
		}
	}

	return NULL;
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

static int thermal_instance_add(struct thermal_instance *new_instance,
				struct thermal_cooling_device *cdev,
				struct thermal_trip_desc *td)
{
	struct thermal_instance *instance;

	list_for_each_entry(instance, &td->thermal_instances, trip_node) {
		if (instance->cdev == cdev)
			return -EEXIST;
	}

	list_add_tail(&new_instance->trip_node, &td->thermal_instances);

	guard(cooling_dev)(cdev);

	list_add_tail(&new_instance->cdev_node, &cdev->thermal_instances);

	return 0;
}

/**
 * thermal_bind_cdev_to_trip - bind a cooling device to a thermal zone
 * @tz:		pointer to struct thermal_zone_device
 * @td:		descriptor of the trip point to bind @cdev to
 * @cdev:	pointer to struct thermal_cooling_device
 * @cool_spec:	cooling specification for the trip point and @cdev
 *
 * This interface function bind a thermal cooling device to the certain trip
 * point of a thermal zone device.
 * This function is usually called in the thermal zone device .bind callback.
 *
 * Return: 0 on success, the proper error value otherwise.
 */
static int thermal_bind_cdev_to_trip(struct thermal_zone_device *tz,
				     struct thermal_trip_desc *td,
				     struct thermal_cooling_device *cdev,
				     struct cooling_spec *cool_spec)
{
	struct thermal_instance *dev;
	bool upper_no_limit;
	int result;

	/* lower default 0, upper default max_state */
	if (cool_spec->lower == THERMAL_NO_LIMIT)
		cool_spec->lower = 0;

	if (cool_spec->upper == THERMAL_NO_LIMIT) {
		cool_spec->upper = cdev->max_state;
		upper_no_limit = true;
	} else {
		upper_no_limit = false;
	}

	if (cool_spec->lower > cool_spec->upper || cool_spec->upper > cdev->max_state)
		return -EINVAL;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->cdev = cdev;
	dev->trip = &td->trip;
	dev->upper = cool_spec->upper;
	dev->upper_no_limit = upper_no_limit;
	dev->lower = cool_spec->lower;
	dev->target = THERMAL_NO_TARGET;
	dev->weight = cool_spec->weight;

	result = ida_alloc(&tz->ida, GFP_KERNEL);
	if (result < 0)
		goto free_mem;

	dev->id = result;
	sprintf(dev->name, "cdev%d", dev->id);
	result =
	    sysfs_create_link(&tz->device.kobj, &cdev->device.kobj, dev->name);
	if (result)
		goto release_ida;

	snprintf(dev->attr_name, sizeof(dev->attr_name), "cdev%d_trip_point",
		 dev->id);
	sysfs_attr_init(&dev->attr.attr);
	dev->attr.attr.name = dev->attr_name;
	dev->attr.attr.mode = 0444;
	dev->attr.show = trip_point_show;
	result = device_create_file(&tz->device, &dev->attr);
	if (result)
		goto remove_symbol_link;

	snprintf(dev->weight_attr_name, sizeof(dev->weight_attr_name),
		 "cdev%d_weight", dev->id);
	sysfs_attr_init(&dev->weight_attr.attr);
	dev->weight_attr.attr.name = dev->weight_attr_name;
	dev->weight_attr.attr.mode = S_IWUSR | S_IRUGO;
	dev->weight_attr.show = weight_show;
	dev->weight_attr.store = weight_store;
	result = device_create_file(&tz->device, &dev->weight_attr);
	if (result)
		goto remove_trip_file;

	result = thermal_instance_add(dev, cdev, td);
	if (result)
		goto remove_weight_file;

	thermal_governor_update_tz(tz, THERMAL_TZ_BIND_CDEV);

	return 0;

remove_weight_file:
	device_remove_file(&tz->device, &dev->weight_attr);
remove_trip_file:
	device_remove_file(&tz->device, &dev->attr);
remove_symbol_link:
	sysfs_remove_link(&tz->device.kobj, dev->name);
release_ida:
	ida_free(&tz->ida, dev->id);
free_mem:
	kfree(dev);
	return result;
}

static void thermal_instance_delete(struct thermal_instance *instance)
{
	list_del(&instance->trip_node);

	guard(cooling_dev)(instance->cdev);

	list_del(&instance->cdev_node);
}

/**
 * thermal_unbind_cdev_from_trip - unbind a cooling device from a thermal zone.
 * @tz:		pointer to a struct thermal_zone_device.
 * @td:		descriptor of the trip point to unbind @cdev from
 * @cdev:	pointer to a struct thermal_cooling_device.
 *
 * This interface function unbind a thermal cooling device from the certain
 * trip point of a thermal zone device.
 * This function is usually called in the thermal zone device .unbind callback.
 */
static void thermal_unbind_cdev_from_trip(struct thermal_zone_device *tz,
					  struct thermal_trip_desc *td,
					  struct thermal_cooling_device *cdev)
{
	struct thermal_instance *pos, *next;

	list_for_each_entry_safe(pos, next, &td->thermal_instances, trip_node) {
		if (pos->cdev == cdev) {
			thermal_instance_delete(pos);
			goto unbind;
		}
	}

	return;

unbind:
	thermal_governor_update_tz(tz, THERMAL_TZ_UNBIND_CDEV);

	device_remove_file(&tz->device, &pos->weight_attr);
	device_remove_file(&tz->device, &pos->attr);
	sysfs_remove_link(&tz->device.kobj, pos->name);
	ida_free(&tz->ida, pos->id);
	kfree(pos);
}

static void thermal_release(struct device *dev)
{
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev;

	if (!strncmp(dev_name(dev), "thermal_zone",
		     sizeof("thermal_zone") - 1)) {
		tz = to_thermal_zone(dev);
		thermal_zone_destroy_device_groups(tz);
		mutex_destroy(&tz->lock);
		complete(&tz->removal);
	} else if (!strncmp(dev_name(dev), "cooling_device",
			    sizeof("cooling_device") - 1)) {
		cdev = to_cooling_device(dev);
		thermal_cooling_device_destroy_sysfs(cdev);
		kfree_const(cdev->type);
		ida_free(&thermal_cdev_ida, cdev->id);
		kfree(cdev);
	}
}

static struct class *thermal_class;

static inline
void print_bind_err_msg(struct thermal_zone_device *tz,
			const struct thermal_trip_desc *td,
			struct thermal_cooling_device *cdev, int ret)
{
	dev_err(&tz->device, "binding cdev %s to trip %d failed: %d\n",
		cdev->type, thermal_zone_trip_id(tz, &td->trip), ret);
}

static bool __thermal_zone_cdev_bind(struct thermal_zone_device *tz,
				     struct thermal_cooling_device *cdev)
{
	struct thermal_trip_desc *td;
	bool update_tz = false;

	if (!tz->ops.should_bind)
		return false;

	for_each_trip_desc(tz, td) {
		struct cooling_spec c = {
			.upper = THERMAL_NO_LIMIT,
			.lower = THERMAL_NO_LIMIT,
			.weight = THERMAL_WEIGHT_DEFAULT
		};
		int ret;

		if (!tz->ops.should_bind(tz, &td->trip, cdev, &c))
			continue;

		ret = thermal_bind_cdev_to_trip(tz, td, cdev, &c);
		if (ret) {
			print_bind_err_msg(tz, td, cdev, ret);
			continue;
		}

		update_tz = true;
	}

	return update_tz;
}

static void thermal_zone_cdev_bind(struct thermal_zone_device *tz,
				   struct thermal_cooling_device *cdev)
{
	guard(thermal_zone)(tz);

	if (__thermal_zone_cdev_bind(tz, cdev))
		__thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);
}

static void thermal_cooling_device_init_complete(struct thermal_cooling_device *cdev)
{
	struct thermal_zone_device *tz;

	guard(mutex)(&thermal_list_lock);

	list_add(&cdev->node, &thermal_cdev_list);

	list_for_each_entry(tz, &thermal_tz_list, node)
		thermal_zone_cdev_bind(tz, cdev);
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
	unsigned long current_state;
	int id, ret;

	if (!ops || !ops->get_max_state || !ops->get_cur_state ||
	    !ops->set_cur_state)
		return ERR_PTR(-EINVAL);

	if (!thermal_class)
		return ERR_PTR(-ENODEV);

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	ret = ida_alloc(&thermal_cdev_ida, GFP_KERNEL);
	if (ret < 0)
		goto out_kfree_cdev;
	cdev->id = ret;
	id = ret;

	cdev->type = kstrdup_const(type ? type : "", GFP_KERNEL);
	if (!cdev->type) {
		ret = -ENOMEM;
		goto out_ida_remove;
	}

	mutex_init(&cdev->lock);
	INIT_LIST_HEAD(&cdev->thermal_instances);
	cdev->np = np;
	cdev->ops = ops;
	cdev->updated = false;
	cdev->device.class = thermal_class;
	cdev->devdata = devdata;

	ret = cdev->ops->get_max_state(cdev, &cdev->max_state);
	if (ret)
		goto out_cdev_type;

	/*
	 * The cooling device's current state is only needed for debug
	 * initialization below, so a failure to get it does not cause
	 * the entire cooling device initialization to fail.  However,
	 * the debug will not work for the device if its initial state
	 * cannot be determined and drivers are responsible for ensuring
	 * that this will not happen.
	 */
	ret = cdev->ops->get_cur_state(cdev, &current_state);
	if (ret)
		current_state = ULONG_MAX;

	thermal_cooling_device_setup_sysfs(cdev);

	ret = dev_set_name(&cdev->device, "cooling_device%d", cdev->id);
	if (ret)
		goto out_cooling_dev;

	ret = device_register(&cdev->device);
	if (ret) {
		/* thermal_release() handles rest of the cleanup */
		put_device(&cdev->device);
		return ERR_PTR(ret);
	}

	if (current_state <= cdev->max_state)
		thermal_debug_cdev_add(cdev, current_state);

	thermal_cooling_device_init_complete(cdev);

	return cdev;

out_cooling_dev:
	thermal_cooling_device_destroy_sysfs(cdev);
out_cdev_type:
	kfree_const(cdev->type);
out_ida_remove:
	ida_free(&thermal_cdev_ida, id);
out_kfree_cdev:
	kfree(cdev);
	return ERR_PTR(ret);
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
				const char *type, void *devdata,
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

static bool thermal_cooling_device_present(struct thermal_cooling_device *cdev)
{
	struct thermal_cooling_device *pos = NULL;

	list_for_each_entry(pos, &thermal_cdev_list, node) {
		if (pos == cdev)
			return true;
	}

	return false;
}

/**
 * thermal_cooling_device_update - Update a cooling device object
 * @cdev: Target cooling device.
 *
 * Update @cdev to reflect a change of the underlying hardware or platform.
 *
 * Must be called when the maximum cooling state of @cdev becomes invalid and so
 * its .get_max_state() callback needs to be run to produce the new maximum
 * cooling state value.
 */
void thermal_cooling_device_update(struct thermal_cooling_device *cdev)
{
	struct thermal_instance *ti;
	unsigned long state;

	if (IS_ERR_OR_NULL(cdev))
		return;

	/*
	 * Hold thermal_list_lock throughout the update to prevent the device
	 * from going away while being updated.
	 */
	guard(mutex)(&thermal_list_lock);

	if (!thermal_cooling_device_present(cdev))
		return;

	/*
	 * Update under the cdev lock to prevent the state from being set beyond
	 * the new limit concurrently.
	 */
	guard(cooling_dev)(cdev);

	if (cdev->ops->get_max_state(cdev, &cdev->max_state))
		return;

	thermal_cooling_device_stats_reinit(cdev);

	list_for_each_entry(ti, &cdev->thermal_instances, cdev_node) {
		if (ti->upper == cdev->max_state)
			continue;

		if (ti->upper < cdev->max_state) {
			if (ti->upper_no_limit)
				ti->upper = cdev->max_state;

			continue;
		}

		ti->upper = cdev->max_state;
		if (ti->lower > ti->upper)
			ti->lower = ti->upper;

		if (ti->target == THERMAL_NO_TARGET)
			continue;

		if (ti->target > ti->upper)
			ti->target = ti->upper;
	}

	if (cdev->ops->get_cur_state(cdev, &state) || state > cdev->max_state)
		return;

	thermal_cooling_device_stats_update(cdev, state);
}
EXPORT_SYMBOL_GPL(thermal_cooling_device_update);

static void __thermal_zone_cdev_unbind(struct thermal_zone_device *tz,
				       struct thermal_cooling_device *cdev)
{
	struct thermal_trip_desc *td;

	for_each_trip_desc(tz, td)
		thermal_unbind_cdev_from_trip(tz, td, cdev);
}

static void thermal_zone_cdev_unbind(struct thermal_zone_device *tz,
				     struct thermal_cooling_device *cdev)
{
	guard(thermal_zone)(tz);

	__thermal_zone_cdev_unbind(tz, cdev);
}

static bool thermal_cooling_device_exit(struct thermal_cooling_device *cdev)
{
	struct thermal_zone_device *tz;

	guard(mutex)(&thermal_list_lock);

	if (!thermal_cooling_device_present(cdev))
		return false;

	list_del(&cdev->node);

	list_for_each_entry(tz, &thermal_tz_list, node)
		thermal_zone_cdev_unbind(tz, cdev);

	return true;
}

/**
 * thermal_cooling_device_unregister() - removes a thermal cooling device
 * @cdev: Thermal cooling device to remove.
 */
void thermal_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	if (!cdev)
		return;

	thermal_debug_cdev_remove(cdev);

	if (thermal_cooling_device_exit(cdev))
		device_unregister(&cdev->device);
}
EXPORT_SYMBOL_GPL(thermal_cooling_device_unregister);

int thermal_zone_get_crit_temp(struct thermal_zone_device *tz, int *temp)
{
	const struct thermal_trip_desc *td;
	int ret = -EINVAL;

	if (tz->ops.get_crit_temp)
		return tz->ops.get_crit_temp(tz, temp);

	guard(thermal_zone)(tz);

	for_each_trip_desc(tz, td) {
		const struct thermal_trip *trip = &td->trip;

		if (trip->type == THERMAL_TRIP_CRITICAL) {
			*temp = trip->temperature;
			ret = 0;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_crit_temp);

static void thermal_zone_device_check(struct work_struct *work)
{
	struct thermal_zone_device *tz = container_of(work, struct
						      thermal_zone_device,
						      poll_queue.work);
	thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);
}

static void thermal_zone_device_init(struct thermal_zone_device *tz)
{
	struct thermal_trip_desc *td, *next;

	INIT_DELAYED_WORK(&tz->poll_queue, thermal_zone_device_check);

	tz->temperature = THERMAL_TEMP_INIT;
	tz->passive = 0;
	tz->prev_low_trip = -INT_MAX;
	tz->prev_high_trip = INT_MAX;
	for_each_trip_desc(tz, td) {
		struct thermal_instance *instance;

		list_for_each_entry(instance, &td->thermal_instances, trip_node)
			instance->initialized = false;
	}
	/*
	 * At this point, all valid trips need to be moved to trips_high so that
	 * mitigation can be started if the zone temperature is above them.
	 */
	list_for_each_entry_safe(td, next, &tz->trips_invalid, list_node) {
		if (td->trip.temperature != THERMAL_TEMP_INVALID)
			move_to_trips_high(tz, td);
	}
	/* The trips_reached list may not be empty during system resume. */
	list_for_each_entry_safe(td, next, &tz->trips_reached, list_node) {
		if (td->trip.temperature == THERMAL_TEMP_INVALID)
			move_to_trips_invalid(tz, td);
		else
			move_to_trips_high(tz, td);
	}
}

static int thermal_zone_init_governor(struct thermal_zone_device *tz)
{
	struct thermal_governor *governor;

	guard(mutex)(&thermal_governor_lock);

	if (tz->tzp)
		governor = __find_governor(tz->tzp->governor_name);
	else
		governor = def_governor;

	return thermal_set_governor(tz, governor);
}

static void thermal_zone_init_complete(struct thermal_zone_device *tz)
{
	struct thermal_cooling_device *cdev;

	guard(mutex)(&thermal_list_lock);

	list_add_tail(&tz->node, &thermal_tz_list);

	guard(thermal_zone)(tz);

	/* Bind cooling devices for this zone. */
	list_for_each_entry(cdev, &thermal_cdev_list, node)
		__thermal_zone_cdev_bind(tz, cdev);

	tz->state &= ~TZ_STATE_FLAG_INIT;
	/*
	 * If system suspend or resume is in progress at this point, the
	 * new thermal zone needs to be marked as suspended because
	 * thermal_pm_notify() has run already.
	 */
	if (thermal_pm_suspended)
		tz->state |= TZ_STATE_FLAG_SUSPENDED;

	__thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);
}

/**
 * thermal_zone_device_register_with_trips() - register a new thermal zone device
 * @type:	the thermal zone device type
 * @trips:	a pointer to an array of thermal trips
 * @num_trips:	the number of trip points the thermal zone support
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
thermal_zone_device_register_with_trips(const char *type,
					const struct thermal_trip *trips,
					int num_trips, void *devdata,
					const struct thermal_zone_device_ops *ops,
					const struct thermal_zone_params *tzp,
					unsigned int passive_delay,
					unsigned int polling_delay)
{
	const struct thermal_trip *trip = trips;
	struct thermal_zone_device *tz;
	struct thermal_trip_desc *td;
	int id;
	int result;

	if (!type || strlen(type) == 0) {
		pr_err("No thermal zone type defined\n");
		return ERR_PTR(-EINVAL);
	}

	if (strlen(type) >= THERMAL_NAME_LENGTH) {
		pr_err("Thermal zone name (%s) too long, should be under %d chars\n",
		       type, THERMAL_NAME_LENGTH);
		return ERR_PTR(-EINVAL);
	}

	if (num_trips < 0) {
		pr_err("Incorrect number of thermal trips\n");
		return ERR_PTR(-EINVAL);
	}

	if (!ops || !ops->get_temp) {
		pr_err("Thermal zone device ops not defined or invalid\n");
		return ERR_PTR(-EINVAL);
	}

	if (num_trips > 0 && !trips)
		return ERR_PTR(-EINVAL);

	if (polling_delay && passive_delay > polling_delay)
		return ERR_PTR(-EINVAL);

	if (!thermal_class)
		return ERR_PTR(-ENODEV);

	tz = kzalloc(struct_size(tz, trips, num_trips), GFP_KERNEL);
	if (!tz)
		return ERR_PTR(-ENOMEM);

	if (tzp) {
		tz->tzp = kmemdup(tzp, sizeof(*tzp), GFP_KERNEL);
		if (!tz->tzp) {
			result = -ENOMEM;
			goto free_tz;
		}
	}

	INIT_LIST_HEAD(&tz->node);
	INIT_LIST_HEAD(&tz->trips_high);
	INIT_LIST_HEAD(&tz->trips_reached);
	INIT_LIST_HEAD(&tz->trips_invalid);
	ida_init(&tz->ida);
	mutex_init(&tz->lock);
	init_completion(&tz->removal);
	init_completion(&tz->resume);
	id = ida_alloc(&thermal_tz_ida, GFP_KERNEL);
	if (id < 0) {
		result = id;
		goto free_tzp;
	}

	tz->id = id;
	strscpy(tz->type, type, sizeof(tz->type));

	tz->ops = *ops;
	if (!tz->ops.critical)
		tz->ops.critical = thermal_zone_device_critical;

	tz->device.class = thermal_class;
	tz->devdata = devdata;
	tz->num_trips = num_trips;
	for_each_trip_desc(tz, td) {
		td->trip = *trip++;
		INIT_LIST_HEAD(&td->thermal_instances);
		INIT_LIST_HEAD(&td->list_node);
		/*
		 * Mark all thresholds as invalid to start with even though
		 * this only matters for the trips that start as invalid and
		 * become valid later.
		 */
		move_to_trips_invalid(tz, td);
	}

	tz->polling_delay_jiffies = msecs_to_jiffies(polling_delay);
	tz->passive_delay_jiffies = msecs_to_jiffies(passive_delay);
	tz->recheck_delay_jiffies = THERMAL_RECHECK_DELAY;

	tz->state = TZ_STATE_FLAG_INIT;

	result = dev_set_name(&tz->device, "thermal_zone%d", tz->id);
	if (result)
		goto remove_id;

	thermal_zone_device_init(tz);

	result = thermal_zone_init_governor(tz);
	if (result)
		goto remove_id;

	/* sys I/F */
	/* Add nodes that are always present via .groups */
	result = thermal_zone_create_device_groups(tz);
	if (result)
		goto remove_id;

	result = device_register(&tz->device);
	if (result)
		goto release_device;

	if (!tz->tzp || !tz->tzp->no_hwmon) {
		result = thermal_add_hwmon_sysfs(tz);
		if (result)
			goto unregister;
	}

	result = thermal_thresholds_init(tz);
	if (result)
		goto remove_hwmon;

	thermal_zone_init_complete(tz);

	thermal_notify_tz_create(tz);

	thermal_debug_tz_add(tz);

	return tz;

remove_hwmon:
	thermal_remove_hwmon_sysfs(tz);
unregister:
	device_del(&tz->device);
release_device:
	put_device(&tz->device);
remove_id:
	ida_free(&thermal_tz_ida, id);
free_tzp:
	kfree(tz->tzp);
free_tz:
	kfree(tz);
	return ERR_PTR(result);
}
EXPORT_SYMBOL_GPL(thermal_zone_device_register_with_trips);

struct thermal_zone_device *thermal_tripless_zone_device_register(
					const char *type,
					void *devdata,
					const struct thermal_zone_device_ops *ops,
					const struct thermal_zone_params *tzp)
{
	return thermal_zone_device_register_with_trips(type, NULL, 0, devdata,
						       ops, tzp, 0, 0);
}
EXPORT_SYMBOL_GPL(thermal_tripless_zone_device_register);

void *thermal_zone_device_priv(struct thermal_zone_device *tzd)
{
	return tzd->devdata;
}
EXPORT_SYMBOL_GPL(thermal_zone_device_priv);

const char *thermal_zone_device_type(struct thermal_zone_device *tzd)
{
	return tzd->type;
}
EXPORT_SYMBOL_GPL(thermal_zone_device_type);

int thermal_zone_device_id(struct thermal_zone_device *tzd)
{
	return tzd->id;
}
EXPORT_SYMBOL_GPL(thermal_zone_device_id);

struct device *thermal_zone_device(struct thermal_zone_device *tzd)
{
	return &tzd->device;
}
EXPORT_SYMBOL_GPL(thermal_zone_device);

static bool thermal_zone_exit(struct thermal_zone_device *tz)
{
	struct thermal_cooling_device *cdev;

	guard(mutex)(&thermal_list_lock);

	if (list_empty(&tz->node))
		return false;

	guard(thermal_zone)(tz);

	tz->state |= TZ_STATE_FLAG_EXIT;
	list_del_init(&tz->node);

	/* Unbind all cdevs associated with this thermal zone. */
	list_for_each_entry(cdev, &thermal_cdev_list, node)
		__thermal_zone_cdev_unbind(tz, cdev);

	return true;
}

/**
 * thermal_zone_device_unregister - removes the registered thermal zone device
 * @tz: the thermal zone device to remove
 */
void thermal_zone_device_unregister(struct thermal_zone_device *tz)
{
	if (!tz)
		return;

	thermal_debug_tz_remove(tz);

	if (!thermal_zone_exit(tz))
		return;

	cancel_delayed_work_sync(&tz->poll_queue);

	thermal_set_governor(tz, NULL);

	thermal_thresholds_exit(tz);
	thermal_remove_hwmon_sysfs(tz);
	ida_free(&thermal_tz_ida, tz->id);
	ida_destroy(&tz->ida);

	device_del(&tz->device);
	put_device(&tz->device);

	thermal_notify_tz_delete(tz);

	wait_for_completion(&tz->removal);
	kfree(tz->tzp);
	kfree(tz);
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
		return ERR_PTR(-EINVAL);

	guard(mutex)(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, node)
		if (!strncasecmp(name, pos->type, THERMAL_NAME_LENGTH)) {
			found++;
			ref = pos;
		}

	if (!found)
		return ERR_PTR(-ENODEV);

	/* Success only when one zone is found. */
	if (found > 1)
		return ERR_PTR(-EEXIST);

	return ref;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_zone_by_name);

static void thermal_zone_device_resume(struct work_struct *work)
{
	struct thermal_zone_device *tz;

	tz = container_of(work, struct thermal_zone_device, poll_queue.work);

	guard(thermal_zone)(tz);

	tz->state &= ~(TZ_STATE_FLAG_SUSPENDED | TZ_STATE_FLAG_RESUMING);

	thermal_debug_tz_resume(tz);
	thermal_zone_device_init(tz);
	thermal_governor_update_tz(tz, THERMAL_TZ_RESUME);
	__thermal_zone_device_update(tz, THERMAL_TZ_RESUME);

	complete(&tz->resume);
}

static void thermal_zone_pm_prepare(struct thermal_zone_device *tz)
{
	guard(thermal_zone)(tz);

	if (tz->state & TZ_STATE_FLAG_RESUMING) {
		/*
		 * thermal_zone_device_resume() queued up for this zone has not
		 * acquired the lock yet, so release it to let the function run
		 * and wait util it has done the work.
		 */
		scoped_guard(thermal_zone_reverse, tz) {
			wait_for_completion(&tz->resume);
		}
	}

	tz->state |= TZ_STATE_FLAG_SUSPENDED;
}

static void thermal_pm_notify_prepare(void)
{
	struct thermal_zone_device *tz;

	guard(mutex)(&thermal_list_lock);

	thermal_pm_suspended = true;

	list_for_each_entry(tz, &thermal_tz_list, node)
		thermal_zone_pm_prepare(tz);
}

static void thermal_zone_pm_complete(struct thermal_zone_device *tz)
{
	guard(thermal_zone)(tz);

	cancel_delayed_work(&tz->poll_queue);

	reinit_completion(&tz->resume);
	tz->state |= TZ_STATE_FLAG_RESUMING;

	/*
	 * Replace the work function with the resume one, which will restore the
	 * original work function and schedule the polling work if needed.
	 */
	INIT_DELAYED_WORK(&tz->poll_queue, thermal_zone_device_resume);
	/* Queue up the work without a delay. */
	mod_delayed_work(system_freezable_power_efficient_wq, &tz->poll_queue, 0);
}

static void thermal_pm_notify_complete(void)
{
	struct thermal_zone_device *tz;

	guard(mutex)(&thermal_list_lock);

	thermal_pm_suspended = false;

	list_for_each_entry(tz, &thermal_tz_list, node)
		thermal_zone_pm_complete(tz);
}

static int thermal_pm_notify(struct notifier_block *nb,
			     unsigned long mode, void *_unused)
{
	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		thermal_pm_notify_prepare();
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		thermal_pm_notify_complete();
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block thermal_pm_nb = {
	.notifier_call = thermal_pm_notify,
	/*
	 * Run at the lowest priority to avoid interference between the thermal
	 * zone resume work items spawned by thermal_pm_notify() and the other
	 * PM notifiers.
	 */
	.priority = INT_MIN,
};

static int __init thermal_init(void)
{
	int result;

	thermal_debug_init();

	result = thermal_netlink_init();
	if (result)
		goto error;

	result = thermal_register_governors();
	if (result)
		goto unregister_netlink;

	thermal_class = kzalloc(sizeof(*thermal_class), GFP_KERNEL);
	if (!thermal_class) {
		result = -ENOMEM;
		goto unregister_governors;
	}

	thermal_class->name = "thermal";
	thermal_class->dev_release = thermal_release;

	result = class_register(thermal_class);
	if (result) {
		kfree(thermal_class);
		thermal_class = NULL;
		goto unregister_governors;
	}

	result = register_pm_notifier(&thermal_pm_nb);
	if (result)
		pr_warn("Thermal: Can not register suspend notifier, return %d\n",
			result);

	return 0;

unregister_governors:
	thermal_unregister_governors();
unregister_netlink:
	thermal_netlink_exit();
error:
	mutex_destroy(&thermal_list_lock);
	mutex_destroy(&thermal_governor_lock);
	return result;
}
postcore_initcall(thermal_init);
