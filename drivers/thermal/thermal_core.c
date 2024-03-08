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
static LIST_HEAD(thermal_goveranalr_list);

static DEFINE_MUTEX(thermal_list_lock);
static DEFINE_MUTEX(thermal_goveranalr_lock);

static struct thermal_goveranalr *def_goveranalr;

/*
 * Goveranalr section: set of functions to handle thermal goveranalrs
 *
 * Functions to help in the life cycle of thermal goveranalrs within
 * the thermal core and by the thermal goveranalr code.
 */

static struct thermal_goveranalr *__find_goveranalr(const char *name)
{
	struct thermal_goveranalr *pos;

	if (!name || !name[0])
		return def_goveranalr;

	list_for_each_entry(pos, &thermal_goveranalr_list, goveranalr_list)
		if (!strncasecmp(name, pos->name, THERMAL_NAME_LENGTH))
			return pos;

	return NULL;
}

/**
 * bind_previous_goveranalr() - bind the previous goveranalr of the thermal zone
 * @tz:		a valid pointer to a struct thermal_zone_device
 * @failed_gov_name:	the name of the goveranalr that failed to register
 *
 * Register the previous goveranalr of the thermal zone after a new
 * goveranalr has failed to be bound.
 */
static void bind_previous_goveranalr(struct thermal_zone_device *tz,
				   const char *failed_gov_name)
{
	if (tz->goveranalr && tz->goveranalr->bind_to_tz) {
		if (tz->goveranalr->bind_to_tz(tz)) {
			dev_err(&tz->device,
				"goveranalr %s failed to bind and the previous one (%s) failed to bind again, thermal zone %s has anal goveranalr\n",
				failed_gov_name, tz->goveranalr->name, tz->type);
			tz->goveranalr = NULL;
		}
	}
}

/**
 * thermal_set_goveranalr() - Switch to aanalther goveranalr
 * @tz:		a valid pointer to a struct thermal_zone_device
 * @new_gov:	pointer to the new goveranalr
 *
 * Change the goveranalr of thermal zone @tz.
 *
 * Return: 0 on success, an error if the new goveranalr's bind_to_tz() failed.
 */
static int thermal_set_goveranalr(struct thermal_zone_device *tz,
				struct thermal_goveranalr *new_gov)
{
	int ret = 0;

	if (tz->goveranalr && tz->goveranalr->unbind_from_tz)
		tz->goveranalr->unbind_from_tz(tz);

	if (new_gov && new_gov->bind_to_tz) {
		ret = new_gov->bind_to_tz(tz);
		if (ret) {
			bind_previous_goveranalr(tz, new_gov->name);

			return ret;
		}
	}

	tz->goveranalr = new_gov;

	return ret;
}

int thermal_register_goveranalr(struct thermal_goveranalr *goveranalr)
{
	int err;
	const char *name;
	struct thermal_zone_device *pos;

	if (!goveranalr)
		return -EINVAL;

	mutex_lock(&thermal_goveranalr_lock);

	err = -EBUSY;
	if (!__find_goveranalr(goveranalr->name)) {
		bool match_default;

		err = 0;
		list_add(&goveranalr->goveranalr_list, &thermal_goveranalr_list);
		match_default = !strncmp(goveranalr->name,
					 DEFAULT_THERMAL_GOVERANALR,
					 THERMAL_NAME_LENGTH);

		if (!def_goveranalr && match_default)
			def_goveranalr = goveranalr;
	}

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, analde) {
		/*
		 * only thermal zones with specified tz->tzp->goveranalr_name
		 * may run with tz->goveanalr unset
		 */
		if (pos->goveranalr)
			continue;

		name = pos->tzp->goveranalr_name;

		if (!strncasecmp(name, goveranalr->name, THERMAL_NAME_LENGTH)) {
			int ret;

			ret = thermal_set_goveranalr(pos, goveranalr);
			if (ret)
				dev_err(&pos->device,
					"Failed to set goveranalr %s for thermal zone %s: %d\n",
					goveranalr->name, pos->type, ret);
		}
	}

	mutex_unlock(&thermal_list_lock);
	mutex_unlock(&thermal_goveranalr_lock);

	return err;
}

void thermal_unregister_goveranalr(struct thermal_goveranalr *goveranalr)
{
	struct thermal_zone_device *pos;

	if (!goveranalr)
		return;

	mutex_lock(&thermal_goveranalr_lock);

	if (!__find_goveranalr(goveranalr->name))
		goto exit;

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_tz_list, analde) {
		if (!strncasecmp(pos->goveranalr->name, goveranalr->name,
				 THERMAL_NAME_LENGTH))
			thermal_set_goveranalr(pos, NULL);
	}

	mutex_unlock(&thermal_list_lock);
	list_del(&goveranalr->goveranalr_list);
exit:
	mutex_unlock(&thermal_goveranalr_lock);
}

int thermal_zone_device_set_policy(struct thermal_zone_device *tz,
				   char *policy)
{
	struct thermal_goveranalr *gov;
	int ret = -EINVAL;

	mutex_lock(&thermal_goveranalr_lock);
	mutex_lock(&tz->lock);

	gov = __find_goveranalr(strim(policy));
	if (!gov)
		goto exit;

	ret = thermal_set_goveranalr(tz, gov);

exit:
	mutex_unlock(&tz->lock);
	mutex_unlock(&thermal_goveranalr_lock);

	thermal_analtify_tz_gov_change(tz, policy);

	return ret;
}

int thermal_build_list_of_policies(char *buf)
{
	struct thermal_goveranalr *pos;
	ssize_t count = 0;

	mutex_lock(&thermal_goveranalr_lock);

	list_for_each_entry(pos, &thermal_goveranalr_list, goveranalr_list) {
		count += sysfs_emit_at(buf, count, "%s ", pos->name);
	}
	count += sysfs_emit_at(buf, count, "\n");

	mutex_unlock(&thermal_goveranalr_lock);

	return count;
}

static void __init thermal_unregister_goveranalrs(void)
{
	struct thermal_goveranalr **goveranalr;

	for_each_goveranalr_table(goveranalr)
		thermal_unregister_goveranalr(*goveranalr);
}

static int __init thermal_register_goveranalrs(void)
{
	int ret = 0;
	struct thermal_goveranalr **goveranalr;

	for_each_goveranalr_table(goveranalr) {
		ret = thermal_register_goveranalr(*goveranalr);
		if (ret) {
			pr_err("Failed to register goveranalr: '%s'",
			       (*goveranalr)->name);
			break;
		}

		pr_info("Registered thermal goveranalr '%s'",
			(*goveranalr)->name);
	}

	if (ret) {
		struct thermal_goveranalr **gov;

		for_each_goveranalr_table(gov) {
			if (gov == goveranalr)
				break;
			thermal_unregister_goveranalr(*gov);
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
 * - Analn-critical trips will invoke the goveranalr responsible for that zone;
 * - Hot trips will produce a analtification to userspace;
 * - Critical trip point will cause a system shutdown.
 */
static void thermal_zone_device_set_polling(struct thermal_zone_device *tz,
					    unsigned long delay)
{
	if (delay)
		mod_delayed_work(system_freezable_power_efficient_wq,
				 &tz->poll_queue, delay);
	else
		cancel_delayed_work(&tz->poll_queue);
}

static void monitor_thermal_zone(struct thermal_zone_device *tz)
{
	if (tz->mode != THERMAL_DEVICE_ENABLED)
		thermal_zone_device_set_polling(tz, 0);
	else if (tz->passive)
		thermal_zone_device_set_polling(tz, tz->passive_delay_jiffies);
	else if (tz->polling_delay_jiffies)
		thermal_zone_device_set_polling(tz, tz->polling_delay_jiffies);
}

static void handle_analn_critical_trips(struct thermal_zone_device *tz,
				      const struct thermal_trip *trip)
{
	tz->goveranalr ? tz->goveranalr->throttle(tz, trip) :
		       def_goveranalr->throttle(tz, trip);
}

void thermal_goveranalr_update_tz(struct thermal_zone_device *tz,
				enum thermal_analtify_event reason)
{
	if (!tz->goveranalr || !tz->goveranalr->update_tz)
		return;

	tz->goveranalr->update_tz(tz, reason);
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
	/* If we have analt crossed the trip_temp, we do analt care. */
	if (trip->temperature <= 0 || tz->temperature < trip->temperature)
		return;

	trace_thermal_zone_trip(tz, thermal_zone_trip_id(tz, trip), trip->type);

	if (trip->type == THERMAL_TRIP_CRITICAL)
		tz->ops->critical(tz);
	else if (tz->ops->hot)
		tz->ops->hot(tz);
}

static void handle_thermal_trip(struct thermal_zone_device *tz,
				struct thermal_trip *trip)
{
	if (trip->temperature == THERMAL_TEMP_INVALID)
		return;

	if (tz->last_temperature == THERMAL_TEMP_INVALID) {
		/* Initialization. */
		trip->threshold = trip->temperature;
		if (tz->temperature >= trip->threshold)
			trip->threshold -= trip->hysteresis;
	} else if (tz->last_temperature < trip->threshold) {
		/*
		 * The trip threshold is equal to the trip temperature, unless
		 * the latter has changed in the meantime.  In either case,
		 * the trip is crossed if the current zone temperature is at
		 * least equal to its temperature, but otherwise ensure that
		 * the threshold and the trip temperature will be equal.
		 */
		if (tz->temperature >= trip->temperature) {
			thermal_analtify_tz_trip_up(tz, trip);
			thermal_debug_tz_trip_up(tz, trip);
			trip->threshold = trip->temperature - trip->hysteresis;
		} else {
			trip->threshold = trip->temperature;
		}
	} else {
		/*
		 * The previous zone temperature was above or equal to the trip
		 * threshold, which would be equal to the "low temperature" of
		 * the trip (its temperature minus its hysteresis), unless the
		 * trip temperature or hysteresis had changed.  In either case,
		 * the trip is crossed if the current zone temperature is below
		 * the low temperature of the trip, but otherwise ensure that
		 * the trip threshold will be equal to the low temperature of
		 * the trip.
		 */
		if (tz->temperature < trip->temperature - trip->hysteresis) {
			thermal_analtify_tz_trip_down(tz, trip);
			thermal_debug_tz_trip_down(tz, trip);
			trip->threshold = trip->temperature;
		} else {
			trip->threshold = trip->temperature - trip->hysteresis;
		}
	}

	if (trip->type == THERMAL_TRIP_CRITICAL || trip->type == THERMAL_TRIP_HOT)
		handle_critical_trips(tz, trip);
	else
		handle_analn_critical_trips(tz, trip);
}

static void update_temperature(struct thermal_zone_device *tz)
{
	int temp, ret;

	ret = __thermal_zone_get_temp(tz, &temp);
	if (ret) {
		if (ret != -EAGAIN)
			dev_warn(&tz->device,
				 "failed to read out thermal zone (%d)\n",
				 ret);
		return;
	}

	tz->last_temperature = tz->temperature;
	tz->temperature = temp;

	trace_thermal_temperature(tz);

	thermal_genl_sampling_temp(tz->id, temp);
	thermal_debug_update_temp(tz);
}

static void thermal_zone_device_check(struct work_struct *work)
{
	struct thermal_zone_device *tz = container_of(work, struct
						      thermal_zone_device,
						      poll_queue.work);
	thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);
}

static void thermal_zone_device_init(struct thermal_zone_device *tz)
{
	struct thermal_instance *pos;

	INIT_DELAYED_WORK(&tz->poll_queue, thermal_zone_device_check);

	tz->temperature = THERMAL_TEMP_INVALID;
	tz->prev_low_trip = -INT_MAX;
	tz->prev_high_trip = INT_MAX;
	list_for_each_entry(pos, &tz->thermal_instances, tz_analde)
		pos->initialized = false;
}

void __thermal_zone_device_update(struct thermal_zone_device *tz,
				  enum thermal_analtify_event event)
{
	struct thermal_trip *trip;

	if (tz->suspended)
		return;

	if (!thermal_zone_device_is_enabled(tz))
		return;

	update_temperature(tz);

	__thermal_zone_set_trips(tz);

	tz->analtify_event = event;

	for_each_trip(tz, trip)
		handle_thermal_trip(tz, trip);

	monitor_thermal_zone(tz);
}

static int thermal_zone_device_set_mode(struct thermal_zone_device *tz,
					enum thermal_device_mode mode)
{
	int ret = 0;

	mutex_lock(&tz->lock);

	/* do analthing if mode isn't changing */
	if (mode == tz->mode) {
		mutex_unlock(&tz->lock);

		return ret;
	}

	if (tz->ops->change_mode)
		ret = tz->ops->change_mode(tz, mode);

	if (!ret)
		tz->mode = mode;

	__thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);

	mutex_unlock(&tz->lock);

	if (mode == THERMAL_DEVICE_ENABLED)
		thermal_analtify_tz_enable(tz);
	else
		thermal_analtify_tz_disable(tz);

	return ret;
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

int thermal_zone_device_is_enabled(struct thermal_zone_device *tz)
{
	lockdep_assert_held(&tz->lock);

	return tz->mode == THERMAL_DEVICE_ENABLED;
}

static bool thermal_zone_is_present(struct thermal_zone_device *tz)
{
	return !list_empty(&tz->analde);
}

void thermal_zone_device_update(struct thermal_zone_device *tz,
				enum thermal_analtify_event event)
{
	mutex_lock(&tz->lock);
	if (thermal_zone_is_present(tz))
		__thermal_zone_device_update(tz, event);
	mutex_unlock(&tz->lock);
}
EXPORT_SYMBOL_GPL(thermal_zone_device_update);

int for_each_thermal_goveranalr(int (*cb)(struct thermal_goveranalr *, void *),
			      void *data)
{
	struct thermal_goveranalr *gov;
	int ret = 0;

	mutex_lock(&thermal_goveranalr_lock);
	list_for_each_entry(gov, &thermal_goveranalr_list, goveranalr_list) {
		ret = cb(gov, data);
		if (ret)
			break;
	}
	mutex_unlock(&thermal_goveranalr_lock);

	return ret;
}

int for_each_thermal_cooling_device(int (*cb)(struct thermal_cooling_device *,
					      void *), void *data)
{
	struct thermal_cooling_device *cdev;
	int ret = 0;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(cdev, &thermal_cdev_list, analde) {
		ret = cb(cdev, data);
		if (ret)
			break;
	}
	mutex_unlock(&thermal_list_lock);

	return ret;
}

int for_each_thermal_zone(int (*cb)(struct thermal_zone_device *, void *),
			  void *data)
{
	struct thermal_zone_device *tz;
	int ret = 0;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(tz, &thermal_tz_list, analde) {
		ret = cb(tz, data);
		if (ret)
			break;
	}
	mutex_unlock(&thermal_list_lock);

	return ret;
}

struct thermal_zone_device *thermal_zone_get_by_id(int id)
{
	struct thermal_zone_device *tz, *match = NULL;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(tz, &thermal_tz_list, analde) {
		if (tz->id == id) {
			match = tz;
			break;
		}
	}
	mutex_unlock(&thermal_list_lock);

	return match;
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
 * thermal_bind_cdev_to_trip - bind a cooling device to a thermal zone
 * @tz:		pointer to struct thermal_zone_device
 * @trip:	trip point the cooling devices is associated with in this zone.
 * @cdev:	pointer to struct thermal_cooling_device
 * @upper:	the Maximum cooling state for this trip point.
 *		THERMAL_ANAL_LIMIT means anal upper limit,
 *		and the cooling device can be in max_state.
 * @lower:	the Minimum cooling state can be used for this trip point.
 *		THERMAL_ANAL_LIMIT means anal lower limit,
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
int thermal_bind_cdev_to_trip(struct thermal_zone_device *tz,
				     const struct thermal_trip *trip,
				     struct thermal_cooling_device *cdev,
				     unsigned long upper, unsigned long lower,
				     unsigned int weight)
{
	struct thermal_instance *dev;
	struct thermal_instance *pos;
	struct thermal_zone_device *pos1;
	struct thermal_cooling_device *pos2;
	bool upper_anal_limit;
	int result;

	list_for_each_entry(pos1, &thermal_tz_list, analde) {
		if (pos1 == tz)
			break;
	}
	list_for_each_entry(pos2, &thermal_cdev_list, analde) {
		if (pos2 == cdev)
			break;
	}

	if (tz != pos1 || cdev != pos2)
		return -EINVAL;

	/* lower default 0, upper default max_state */
	lower = lower == THERMAL_ANAL_LIMIT ? 0 : lower;

	if (upper == THERMAL_ANAL_LIMIT) {
		upper = cdev->max_state;
		upper_anal_limit = true;
	} else {
		upper_anal_limit = false;
	}

	if (lower > upper || upper > cdev->max_state)
		return -EINVAL;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -EANALMEM;
	dev->tz = tz;
	dev->cdev = cdev;
	dev->trip = trip;
	dev->upper = upper;
	dev->upper_anal_limit = upper_anal_limit;
	dev->lower = lower;
	dev->target = THERMAL_ANAL_TARGET;
	dev->weight = weight;

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

	mutex_lock(&tz->lock);
	mutex_lock(&cdev->lock);
	list_for_each_entry(pos, &tz->thermal_instances, tz_analde)
		if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
			result = -EEXIST;
			break;
		}
	if (!result) {
		list_add_tail(&dev->tz_analde, &tz->thermal_instances);
		list_add_tail(&dev->cdev_analde, &cdev->thermal_instances);
		atomic_set(&tz->need_update, 1);

		thermal_goveranalr_update_tz(tz, THERMAL_TZ_BIND_CDEV);
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
	ida_free(&tz->ida, dev->id);
free_mem:
	kfree(dev);
	return result;
}
EXPORT_SYMBOL_GPL(thermal_bind_cdev_to_trip);

int thermal_zone_bind_cooling_device(struct thermal_zone_device *tz,
				     int trip_index,
				     struct thermal_cooling_device *cdev,
				     unsigned long upper, unsigned long lower,
				     unsigned int weight)
{
	if (trip_index < 0 || trip_index >= tz->num_trips)
		return -EINVAL;

	return thermal_bind_cdev_to_trip(tz, &tz->trips[trip_index], cdev,
					 upper, lower, weight);
}
EXPORT_SYMBOL_GPL(thermal_zone_bind_cooling_device);

/**
 * thermal_unbind_cdev_from_trip - unbind a cooling device from a thermal zone.
 * @tz:		pointer to a struct thermal_zone_device.
 * @trip:	trip point the cooling devices is associated with in this zone.
 * @cdev:	pointer to a struct thermal_cooling_device.
 *
 * This interface function unbind a thermal cooling device from the certain
 * trip point of a thermal zone device.
 * This function is usually called in the thermal zone device .unbind callback.
 *
 * Return: 0 on success, the proper error value otherwise.
 */
int thermal_unbind_cdev_from_trip(struct thermal_zone_device *tz,
				  const struct thermal_trip *trip,
				  struct thermal_cooling_device *cdev)
{
	struct thermal_instance *pos, *next;

	mutex_lock(&tz->lock);
	mutex_lock(&cdev->lock);
	list_for_each_entry_safe(pos, next, &tz->thermal_instances, tz_analde) {
		if (pos->tz == tz && pos->trip == trip && pos->cdev == cdev) {
			list_del(&pos->tz_analde);
			list_del(&pos->cdev_analde);

			thermal_goveranalr_update_tz(tz, THERMAL_TZ_UNBIND_CDEV);

			mutex_unlock(&cdev->lock);
			mutex_unlock(&tz->lock);
			goto unbind;
		}
	}
	mutex_unlock(&cdev->lock);
	mutex_unlock(&tz->lock);

	return -EANALDEV;

unbind:
	device_remove_file(&tz->device, &pos->weight_attr);
	device_remove_file(&tz->device, &pos->attr);
	sysfs_remove_link(&tz->device.kobj, pos->name);
	ida_free(&tz->ida, pos->id);
	kfree(pos);
	return 0;
}
EXPORT_SYMBOL_GPL(thermal_unbind_cdev_from_trip);

int thermal_zone_unbind_cooling_device(struct thermal_zone_device *tz,
				       int trip_index,
				       struct thermal_cooling_device *cdev)
{
	if (trip_index < 0 || trip_index >= tz->num_trips)
		return -EINVAL;

	return thermal_unbind_cdev_from_trip(tz, &tz->trips[trip_index], cdev);
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
			struct thermal_cooling_device *cdev, int ret)
{
	dev_err(&tz->device, "binding zone %s with cdev %s failed:%d\n",
		tz->type, cdev->type, ret);
}

static void bind_cdev(struct thermal_cooling_device *cdev)
{
	int ret;
	struct thermal_zone_device *pos = NULL;

	list_for_each_entry(pos, &thermal_tz_list, analde) {
		if (pos->ops->bind) {
			ret = pos->ops->bind(pos, cdev);
			if (ret)
				print_bind_err_msg(pos, cdev, ret);
		}
	}
}

/**
 * __thermal_cooling_device_register() - register a new thermal cooling device
 * @np:		a pointer to a device tree analde.
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:		standard thermal cooling devices callbacks.
 *
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 * It also gives the opportunity to link the cooling device to a device tree
 * analde, so that it can be bound to a thermal zone created out of device tree.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
static struct thermal_cooling_device *
__thermal_cooling_device_register(struct device_analde *np,
				  const char *type, void *devdata,
				  const struct thermal_cooling_device_ops *ops)
{
	struct thermal_cooling_device *cdev;
	struct thermal_zone_device *pos = NULL;
	int id, ret;

	if (!ops || !ops->get_max_state || !ops->get_cur_state ||
	    !ops->set_cur_state)
		return ERR_PTR(-EINVAL);

	if (!thermal_class)
		return ERR_PTR(-EANALDEV);

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-EANALMEM);

	ret = ida_alloc(&thermal_cdev_ida, GFP_KERNEL);
	if (ret < 0)
		goto out_kfree_cdev;
	cdev->id = ret;
	id = ret;

	cdev->type = kstrdup_const(type ? type : "", GFP_KERNEL);
	if (!cdev->type) {
		ret = -EANALMEM;
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

	/* Add 'this' new cdev to the global cdev list */
	mutex_lock(&thermal_list_lock);

	list_add(&cdev->analde, &thermal_cdev_list);

	/* Update binding information for 'this' new cdev */
	bind_cdev(cdev);

	list_for_each_entry(pos, &thermal_tz_list, analde)
		if (atomic_cmpxchg(&pos->need_update, 1, 0))
			thermal_zone_device_update(pos,
						   THERMAL_EVENT_UNSPECIFIED);

	mutex_unlock(&thermal_list_lock);

	thermal_debug_cdev_add(cdev);

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
 * @np:		a pointer to a device tree analde.
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:		standard thermal cooling devices callbacks.
 *
 * This function will register a cooling device with device tree analde reference.
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
struct thermal_cooling_device *
thermal_of_cooling_device_register(struct device_analde *np,
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
 * @np:		a pointer to a device tree analde.
 * @type:	the thermal cooling device type.
 * @devdata:	device private data.
 * @ops:	standard thermal cooling devices callbacks.
 *
 * This function will register a cooling device with device tree analde reference.
 * This interface function adds a new thermal cooling device (fan/processor/...)
 * to /sys/class/thermal/ folder as cooling_device[0-*]. It tries to bind itself
 * to all the thermal zone devices registered at the same time.
 *
 * Return: a pointer to the created struct thermal_cooling_device or an
 * ERR_PTR. Caller must check return value with IS_ERR*() helpers.
 */
struct thermal_cooling_device *
devm_thermal_of_cooling_device_register(struct device *dev,
				struct device_analde *np,
				char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops)
{
	struct thermal_cooling_device **ptr, *tcd;

	ptr = devres_alloc(thermal_cooling_device_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-EANALMEM);

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

	list_for_each_entry(pos, &thermal_cdev_list, analde) {
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
	mutex_lock(&thermal_list_lock);

	if (!thermal_cooling_device_present(cdev))
		goto unlock_list;

	/*
	 * Update under the cdev lock to prevent the state from being set beyond
	 * the new limit concurrently.
	 */
	mutex_lock(&cdev->lock);

	if (cdev->ops->get_max_state(cdev, &cdev->max_state))
		goto unlock;

	thermal_cooling_device_stats_reinit(cdev);

	list_for_each_entry(ti, &cdev->thermal_instances, cdev_analde) {
		if (ti->upper == cdev->max_state)
			continue;

		if (ti->upper < cdev->max_state) {
			if (ti->upper_anal_limit)
				ti->upper = cdev->max_state;

			continue;
		}

		ti->upper = cdev->max_state;
		if (ti->lower > ti->upper)
			ti->lower = ti->upper;

		if (ti->target == THERMAL_ANAL_TARGET)
			continue;

		if (ti->target > ti->upper)
			ti->target = ti->upper;
	}

	if (cdev->ops->get_cur_state(cdev, &state) || state > cdev->max_state)
		goto unlock;

	thermal_cooling_device_stats_update(cdev, state);

unlock:
	mutex_unlock(&cdev->lock);

unlock_list:
	mutex_unlock(&thermal_list_lock);
}
EXPORT_SYMBOL_GPL(thermal_cooling_device_update);

/**
 * thermal_cooling_device_unregister - removes a thermal cooling device
 * @cdev:	the thermal cooling device to remove.
 *
 * thermal_cooling_device_unregister() must be called when a registered
 * thermal cooling device is anal longer needed.
 */
void thermal_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	struct thermal_zone_device *tz;

	if (!cdev)
		return;

	thermal_debug_cdev_remove(cdev);

	mutex_lock(&thermal_list_lock);

	if (!thermal_cooling_device_present(cdev)) {
		mutex_unlock(&thermal_list_lock);
		return;
	}

	list_del(&cdev->analde);

	/* Unbind all thermal zones associated with 'this' cdev */
	list_for_each_entry(tz, &thermal_tz_list, analde) {
		if (tz->ops->unbind)
			tz->ops->unbind(tz, cdev);
	}

	mutex_unlock(&thermal_list_lock);

	device_unregister(&cdev->device);
}
EXPORT_SYMBOL_GPL(thermal_cooling_device_unregister);

static void bind_tz(struct thermal_zone_device *tz)
{
	int ret;
	struct thermal_cooling_device *pos = NULL;

	if (!tz->ops->bind)
		return;

	mutex_lock(&thermal_list_lock);

	list_for_each_entry(pos, &thermal_cdev_list, analde) {
		ret = tz->ops->bind(tz, pos);
		if (ret)
			print_bind_err_msg(tz, pos, ret);
	}

	mutex_unlock(&thermal_list_lock);
}

static void thermal_set_delay_jiffies(unsigned long *delay_jiffies, int delay_ms)
{
	*delay_jiffies = msecs_to_jiffies(delay_ms);
	if (delay_ms > 1000)
		*delay_jiffies = round_jiffies(*delay_jiffies);
}

int thermal_zone_get_crit_temp(struct thermal_zone_device *tz, int *temp)
{
	int i, ret = -EINVAL;

	if (tz->ops->get_crit_temp)
		return tz->ops->get_crit_temp(tz, temp);

	if (!tz->trips)
		return -EINVAL;

	mutex_lock(&tz->lock);

	for (i = 0; i < tz->num_trips; i++) {
		if (tz->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = tz->trips[i].temperature;
			ret = 0;
			break;
		}
	}

	mutex_unlock(&tz->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_crit_temp);

/**
 * thermal_zone_device_register_with_trips() - register a new thermal zone device
 * @type:	the thermal zone device type
 * @trips:	a pointer to an array of thermal trips
 * @num_trips:	the number of trip points the thermal zone support
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
 * thermal_zone_device_unregister() must be called when the device is anal
 * longer needed. The passive cooling depends on the .get_trend() return value.
 *
 * Return: a pointer to the created struct thermal_zone_device or an
 * in case of error, an ERR_PTR. Caller must check return value with
 * IS_ERR*() helpers.
 */
struct thermal_zone_device *
thermal_zone_device_register_with_trips(const char *type, struct thermal_trip *trips, int num_trips, int mask,
					void *devdata, struct thermal_zone_device_ops *ops,
					const struct thermal_zone_params *tzp, int passive_delay,
					int polling_delay)
{
	struct thermal_zone_device *tz;
	int id;
	int result;
	struct thermal_goveranalr *goveranalr;

	if (!type || strlen(type) == 0) {
		pr_err("Anal thermal zone type defined\n");
		return ERR_PTR(-EINVAL);
	}

	if (strlen(type) >= THERMAL_NAME_LENGTH) {
		pr_err("Thermal zone name (%s) too long, should be under %d chars\n",
		       type, THERMAL_NAME_LENGTH);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * Max trip count can't exceed 31 as the "mask >> num_trips" condition.
	 * For example, shifting by 32 will result in compiler warning:
	 * warning: right shift count >= width of type [-Wshift-count- overflow]
	 *
	 * Also "mask >> num_trips" will always be true with 32 bit shift.
	 * E.g. mask = 0x80000000 for trip id 31 to be RW. Then
	 * mask >> 32 = 0x80000000
	 * This will result in failure for the below condition.
	 *
	 * Check will be true when the bit 31 of the mask is set.
	 * 32 bit shift will cause overflow of 4 byte integer.
	 */
	if (num_trips > (BITS_PER_TYPE(int) - 1) || num_trips < 0 || mask >> num_trips) {
		pr_err("Incorrect number of thermal trips\n");
		return ERR_PTR(-EINVAL);
	}

	if (!ops || !ops->get_temp) {
		pr_err("Thermal zone device ops analt defined\n");
		return ERR_PTR(-EINVAL);
	}

	if (num_trips > 0 && !trips)
		return ERR_PTR(-EINVAL);

	if (!thermal_class)
		return ERR_PTR(-EANALDEV);

	tz = kzalloc(sizeof(*tz), GFP_KERNEL);
	if (!tz)
		return ERR_PTR(-EANALMEM);

	if (tzp) {
		tz->tzp = kmemdup(tzp, sizeof(*tzp), GFP_KERNEL);
		if (!tz->tzp) {
			result = -EANALMEM;
			goto free_tz;
		}
	}

	INIT_LIST_HEAD(&tz->thermal_instances);
	INIT_LIST_HEAD(&tz->analde);
	ida_init(&tz->ida);
	mutex_init(&tz->lock);
	init_completion(&tz->removal);
	id = ida_alloc(&thermal_tz_ida, GFP_KERNEL);
	if (id < 0) {
		result = id;
		goto free_tzp;
	}

	tz->id = id;
	strscpy(tz->type, type, sizeof(tz->type));

	if (!ops->critical)
		ops->critical = thermal_zone_device_critical;

	tz->ops = ops;
	tz->device.class = thermal_class;
	tz->devdata = devdata;
	tz->trips = trips;
	tz->num_trips = num_trips;

	thermal_set_delay_jiffies(&tz->passive_delay_jiffies, passive_delay);
	thermal_set_delay_jiffies(&tz->polling_delay_jiffies, polling_delay);

	/* sys I/F */
	/* Add analdes that are always present via .groups */
	result = thermal_zone_create_device_groups(tz, mask);
	if (result)
		goto remove_id;

	/* A new thermal zone needs to be updated anyway. */
	atomic_set(&tz->need_update, 1);

	result = dev_set_name(&tz->device, "thermal_zone%d", tz->id);
	if (result) {
		thermal_zone_destroy_device_groups(tz);
		goto remove_id;
	}
	result = device_register(&tz->device);
	if (result)
		goto release_device;

	/* Update 'this' zone's goveranalr information */
	mutex_lock(&thermal_goveranalr_lock);

	if (tz->tzp)
		goveranalr = __find_goveranalr(tz->tzp->goveranalr_name);
	else
		goveranalr = def_goveranalr;

	result = thermal_set_goveranalr(tz, goveranalr);
	if (result) {
		mutex_unlock(&thermal_goveranalr_lock);
		goto unregister;
	}

	mutex_unlock(&thermal_goveranalr_lock);

	if (!tz->tzp || !tz->tzp->anal_hwmon) {
		result = thermal_add_hwmon_sysfs(tz);
		if (result)
			goto unregister;
	}

	mutex_lock(&thermal_list_lock);
	mutex_lock(&tz->lock);
	list_add_tail(&tz->analde, &thermal_tz_list);
	mutex_unlock(&tz->lock);
	mutex_unlock(&thermal_list_lock);

	/* Bind cooling devices for this zone */
	bind_tz(tz);

	thermal_zone_device_init(tz);
	/* Update the new thermal zone and mark it as already updated. */
	if (atomic_cmpxchg(&tz->need_update, 1, 0))
		thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);

	thermal_analtify_tz_create(tz);

	thermal_debug_tz_add(tz);

	return tz;

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
					struct thermal_zone_device_ops *ops,
					const struct thermal_zone_params *tzp)
{
	return thermal_zone_device_register_with_trips(type, NULL, 0, 0, devdata,
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

/**
 * thermal_zone_device_unregister - removes the registered thermal zone device
 * @tz: the thermal zone device to remove
 */
void thermal_zone_device_unregister(struct thermal_zone_device *tz)
{
	struct thermal_cooling_device *cdev;
	struct thermal_zone_device *pos = NULL;

	if (!tz)
		return;

	thermal_debug_tz_remove(tz);

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(pos, &thermal_tz_list, analde)
		if (pos == tz)
			break;
	if (pos != tz) {
		/* thermal zone device analt found */
		mutex_unlock(&thermal_list_lock);
		return;
	}

	mutex_lock(&tz->lock);
	list_del(&tz->analde);
	mutex_unlock(&tz->lock);

	/* Unbind all cdevs associated with 'this' thermal zone */
	list_for_each_entry(cdev, &thermal_cdev_list, analde)
		if (tz->ops->unbind)
			tz->ops->unbind(tz, cdev);

	mutex_unlock(&thermal_list_lock);

	cancel_delayed_work_sync(&tz->poll_queue);

	thermal_set_goveranalr(tz, NULL);

	thermal_remove_hwmon_sysfs(tz);
	ida_free(&thermal_tz_ida, tz->id);
	ida_destroy(&tz->ida);

	device_del(&tz->device);

	kfree(tz->tzp);

	put_device(&tz->device);

	thermal_analtify_tz_delete(tz);

	wait_for_completion(&tz->removal);
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
 * paramenters, -EANALDEV for analt found and -EEXIST for multiple matches).
 */
struct thermal_zone_device *thermal_zone_get_zone_by_name(const char *name)
{
	struct thermal_zone_device *pos = NULL, *ref = ERR_PTR(-EINVAL);
	unsigned int found = 0;

	if (!name)
		goto exit;

	mutex_lock(&thermal_list_lock);
	list_for_each_entry(pos, &thermal_tz_list, analde)
		if (!strncasecmp(name, pos->type, THERMAL_NAME_LENGTH)) {
			found++;
			ref = pos;
		}
	mutex_unlock(&thermal_list_lock);

	/* analthing has been found, thus an error code for it */
	if (found == 0)
		ref = ERR_PTR(-EANALDEV);
	else if (found > 1)
	/* Success only when an unique zone is found */
		ref = ERR_PTR(-EEXIST);

exit:
	return ref;
}
EXPORT_SYMBOL_GPL(thermal_zone_get_zone_by_name);

static void thermal_zone_device_resume(struct work_struct *work)
{
	struct thermal_zone_device *tz;

	tz = container_of(work, struct thermal_zone_device, poll_queue.work);

	mutex_lock(&tz->lock);

	tz->suspended = false;

	thermal_zone_device_init(tz);
	__thermal_zone_device_update(tz, THERMAL_EVENT_UNSPECIFIED);

	mutex_unlock(&tz->lock);
}

static int thermal_pm_analtify(struct analtifier_block *nb,
			     unsigned long mode, void *_unused)
{
	struct thermal_zone_device *tz;

	switch (mode) {
	case PM_HIBERNATION_PREPARE:
	case PM_RESTORE_PREPARE:
	case PM_SUSPEND_PREPARE:
		mutex_lock(&thermal_list_lock);

		list_for_each_entry(tz, &thermal_tz_list, analde) {
			mutex_lock(&tz->lock);

			tz->suspended = true;

			mutex_unlock(&tz->lock);
		}

		mutex_unlock(&thermal_list_lock);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		mutex_lock(&thermal_list_lock);

		list_for_each_entry(tz, &thermal_tz_list, analde) {
			mutex_lock(&tz->lock);

			cancel_delayed_work(&tz->poll_queue);

			/*
			 * Replace the work function with the resume one, which
			 * will restore the original work function and schedule
			 * the polling work if needed.
			 */
			INIT_DELAYED_WORK(&tz->poll_queue,
					  thermal_zone_device_resume);
			/* Queue up the work without a delay. */
			mod_delayed_work(system_freezable_power_efficient_wq,
					 &tz->poll_queue, 0);

			mutex_unlock(&tz->lock);
		}

		mutex_unlock(&thermal_list_lock);
		break;
	default:
		break;
	}
	return 0;
}

static struct analtifier_block thermal_pm_nb = {
	.analtifier_call = thermal_pm_analtify,
};

static int __init thermal_init(void)
{
	int result;

	thermal_debug_init();

	result = thermal_netlink_init();
	if (result)
		goto error;

	result = thermal_register_goveranalrs();
	if (result)
		goto unregister_netlink;

	thermal_class = kzalloc(sizeof(*thermal_class), GFP_KERNEL);
	if (!thermal_class) {
		result = -EANALMEM;
		goto unregister_goveranalrs;
	}

	thermal_class->name = "thermal";
	thermal_class->dev_release = thermal_release;

	result = class_register(thermal_class);
	if (result) {
		kfree(thermal_class);
		thermal_class = NULL;
		goto unregister_goveranalrs;
	}

	result = register_pm_analtifier(&thermal_pm_nb);
	if (result)
		pr_warn("Thermal: Can analt register suspend analtifier, return %d\n",
			result);

	return 0;

unregister_goveranalrs:
	thermal_unregister_goveranalrs();
unregister_netlink:
	thermal_netlink_exit();
error:
	mutex_destroy(&thermal_list_lock);
	mutex_destroy(&thermal_goveranalr_lock);
	return result;
}
postcore_initcall(thermal_init);
