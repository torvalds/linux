// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024, Intel Corporation
 *
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * Thermal zone tempalates handling for thermal core testing.
 */

#define pr_fmt(fmt) "thermal-testing: " fmt

#include <linux/debugfs.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#include "thermal_testing.h"

#define TT_MAX_FILE_NAME_LENGTH		16

/**
 * struct tt_thermal_zone - Testing thermal zone template
 *
 * Represents a template of a thermal zone that can be used for registering
 * a test thermal zone with the thermal core.
 *
 * @list_node: Node in the list of all testing thermal zone templates.
 * @trips: List of trip point templates for this thermal zone template.
 * @d_tt_zone: Directory in debugfs representing this template.
 * @tz: Test thermal zone based on this template, if present.
 * @lock: Mutex for synchronizing changes of this template.
 * @ida: IDA for trip point IDs.
 * @id: The ID of this template for the debugfs interface.
 * @temp: Temperature value.
 * @tz_temp: Current thermal zone temperature (after registration).
 * @num_trips: Number of trip points in the @trips list.
 * @refcount: Reference counter for usage and removal synchronization.
 */
struct tt_thermal_zone {
	struct list_head list_node;
	struct list_head trips;
	struct dentry *d_tt_zone;
	struct thermal_zone_device *tz;
	struct mutex lock;
	struct ida ida;
	int id;
	int temp;
	int tz_temp;
	unsigned int num_trips;
	unsigned int refcount;
};

DEFINE_GUARD(tt_zone, struct tt_thermal_zone *, mutex_lock(&_T->lock), mutex_unlock(&_T->lock))

/**
 * struct tt_trip - Testing trip point template
 *
 * Represents a template of a trip point to be used for populating a trip point
 * during the registration of a thermal zone based on a given zone template.
 *
 * @list_node: Node in the list of all trip templates in the zone template.
 * @trip: Trip point data to use for thernal zone registration.
 * @id: The ID of this trip template for the debugfs interface.
 */
struct tt_trip {
	struct list_head list_node;
	struct thermal_trip trip;
	int id;
};

/*
 * It is both questionable and potentially problematic from the sychnronization
 * perspective to attempt to manipulate debugfs from within a debugfs file
 * "write" operation, so auxiliary work items are used for that.  The majority
 * of zone-related command functions have a part that runs from a workqueue and
 * make changes in debugs, among other things.
 */
struct tt_work {
	struct work_struct work;
	struct tt_thermal_zone *tt_zone;
	struct tt_trip *tt_trip;
};

static inline struct tt_work *tt_work_of_work(struct work_struct *work)
{
	return container_of(work, struct tt_work, work);
}

static LIST_HEAD(tt_thermal_zones);
static DEFINE_IDA(tt_thermal_zones_ida);
static DEFINE_MUTEX(tt_thermal_zones_lock);

static int tt_int_get(void *data, u64 *val)
{
	*val = *(int *)data;
	return 0;
}
static int tt_int_set(void *data, u64 val)
{
	if ((int)val < THERMAL_TEMP_INVALID)
		return -EINVAL;

	*(int *)data = val;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(tt_int_attr, tt_int_get, tt_int_set, "%lld\n");
DEFINE_DEBUGFS_ATTRIBUTE(tt_unsigned_int_attr, tt_int_get, tt_int_set, "%llu\n");

static int tt_zone_tz_temp_get(void *data, u64 *val)
{
	struct tt_thermal_zone *tt_zone = data;

	guard(tt_zone)(tt_zone);

	if (!tt_zone->tz)
		return -EBUSY;

	*val = tt_zone->tz_temp;

	return 0;
}
static int tt_zone_tz_temp_set(void *data, u64 val)
{
	struct tt_thermal_zone *tt_zone = data;

	guard(tt_zone)(tt_zone);

	if (!tt_zone->tz)
		return -EBUSY;

	WRITE_ONCE(tt_zone->tz_temp, val);
	thermal_zone_device_update(tt_zone->tz, THERMAL_EVENT_TEMP_SAMPLE);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE_SIGNED(tt_zone_tz_temp_attr, tt_zone_tz_temp_get,
				tt_zone_tz_temp_set, "%lld\n");

static void tt_zone_free_trips(struct tt_thermal_zone *tt_zone)
{
	struct tt_trip *tt_trip, *aux;

	list_for_each_entry_safe(tt_trip, aux, &tt_zone->trips, list_node) {
		list_del(&tt_trip->list_node);
		ida_free(&tt_zone->ida, tt_trip->id);
		kfree(tt_trip);
	}
}

static void tt_zone_free(struct tt_thermal_zone *tt_zone)
{
	tt_zone_free_trips(tt_zone);
	ida_free(&tt_thermal_zones_ida, tt_zone->id);
	ida_destroy(&tt_zone->ida);
	kfree(tt_zone);
}

static void tt_add_tz_work_fn(struct work_struct *work)
{
	struct tt_work *tt_work = tt_work_of_work(work);
	struct tt_thermal_zone *tt_zone = tt_work->tt_zone;
	char f_name[TT_MAX_FILE_NAME_LENGTH];

	kfree(tt_work);

	snprintf(f_name, TT_MAX_FILE_NAME_LENGTH, "tz%d", tt_zone->id);
	tt_zone->d_tt_zone = debugfs_create_dir(f_name, d_testing);
	if (IS_ERR(tt_zone->d_tt_zone)) {
		tt_zone_free(tt_zone);
		return;
	}

	debugfs_create_file_unsafe("temp", 0600, tt_zone->d_tt_zone, tt_zone,
				   &tt_zone_tz_temp_attr);

	debugfs_create_file_unsafe("init_temp", 0600, tt_zone->d_tt_zone,
				   &tt_zone->temp, &tt_int_attr);

	guard(mutex)(&tt_thermal_zones_lock);

	list_add_tail(&tt_zone->list_node, &tt_thermal_zones);
}

int tt_add_tz(void)
{
	struct tt_thermal_zone *tt_zone __free(kfree);
	struct tt_work *tt_work __free(kfree) = NULL;
	int ret;

	tt_zone = kzalloc(sizeof(*tt_zone), GFP_KERNEL);
	if (!tt_zone)
		return -ENOMEM;

	tt_work = kzalloc(sizeof(*tt_work), GFP_KERNEL);
	if (!tt_work)
		return -ENOMEM;

	INIT_LIST_HEAD(&tt_zone->trips);
	mutex_init(&tt_zone->lock);
	ida_init(&tt_zone->ida);
	tt_zone->temp = THERMAL_TEMP_INVALID;

	ret = ida_alloc(&tt_thermal_zones_ida, GFP_KERNEL);
	if (ret < 0)
		return ret;

	tt_zone->id = ret;

	INIT_WORK(&tt_work->work, tt_add_tz_work_fn);
	tt_work->tt_zone = no_free_ptr(tt_zone);
	schedule_work(&(no_free_ptr(tt_work)->work));

	return 0;
}

static void tt_del_tz_work_fn(struct work_struct *work)
{
	struct tt_work *tt_work = tt_work_of_work(work);
	struct tt_thermal_zone *tt_zone = tt_work->tt_zone;

	kfree(tt_work);

	debugfs_remove(tt_zone->d_tt_zone);
	tt_zone_free(tt_zone);
}

static void tt_zone_unregister_tz(struct tt_thermal_zone *tt_zone)
{
	guard(tt_zone)(tt_zone);

	if (tt_zone->tz) {
		thermal_zone_device_unregister(tt_zone->tz);
		tt_zone->tz = NULL;
	}
}

int tt_del_tz(const char *arg)
{
	struct tt_work *tt_work __free(kfree) = NULL;
	struct tt_thermal_zone *tt_zone, *aux;
	int ret;
	int id;

	ret = sscanf(arg, "%d", &id);
	if (ret != 1)
		return -EINVAL;

	tt_work = kzalloc(sizeof(*tt_work), GFP_KERNEL);
	if (!tt_work)
		return -ENOMEM;

	guard(mutex)(&tt_thermal_zones_lock);

	ret = -EINVAL;
	list_for_each_entry_safe(tt_zone, aux, &tt_thermal_zones, list_node) {
		if (tt_zone->id == id) {
			if (tt_zone->refcount) {
				ret = -EBUSY;
			} else {
				list_del(&tt_zone->list_node);
				ret = 0;
			}
			break;
		}
	}

	if (ret)
		return ret;

	tt_zone_unregister_tz(tt_zone);

	INIT_WORK(&tt_work->work, tt_del_tz_work_fn);
	tt_work->tt_zone = tt_zone;
	schedule_work(&(no_free_ptr(tt_work)->work));

	return 0;
}

static struct tt_thermal_zone *tt_get_tt_zone(const char *arg)
{
	struct tt_thermal_zone *tt_zone;
	int ret, id;

	ret = sscanf(arg, "%d", &id);
	if (ret != 1)
		return ERR_PTR(-EINVAL);

	guard(mutex)(&tt_thermal_zones_lock);

	list_for_each_entry(tt_zone, &tt_thermal_zones, list_node) {
		if (tt_zone->id == id) {
			tt_zone->refcount++;
			return tt_zone;
		}
	}

	return ERR_PTR(-EINVAL);
}

static void tt_put_tt_zone(struct tt_thermal_zone *tt_zone)
{
	guard(mutex)(&tt_thermal_zones_lock);

	tt_zone->refcount--;
}

DEFINE_FREE(put_tt_zone, struct tt_thermal_zone *,
	    if (!IS_ERR_OR_NULL(_T)) tt_put_tt_zone(_T))

static void tt_zone_add_trip_work_fn(struct work_struct *work)
{
	struct tt_work *tt_work = tt_work_of_work(work);
	struct tt_thermal_zone *tt_zone = tt_work->tt_zone;
	struct tt_trip *tt_trip = tt_work->tt_trip;
	char d_name[TT_MAX_FILE_NAME_LENGTH];

	kfree(tt_work);

	snprintf(d_name, TT_MAX_FILE_NAME_LENGTH, "trip_%d_temp", tt_trip->id);
	debugfs_create_file_unsafe(d_name, 0600, tt_zone->d_tt_zone,
				   &tt_trip->trip.temperature, &tt_int_attr);

	snprintf(d_name, TT_MAX_FILE_NAME_LENGTH, "trip_%d_hyst", tt_trip->id);
	debugfs_create_file_unsafe(d_name, 0600, tt_zone->d_tt_zone,
				   &tt_trip->trip.hysteresis, &tt_unsigned_int_attr);

	tt_put_tt_zone(tt_zone);
}

int tt_zone_add_trip(const char *arg)
{
	struct tt_thermal_zone *tt_zone __free(put_tt_zone) = NULL;
	struct tt_trip *tt_trip __free(kfree) = NULL;
	struct tt_work *tt_work __free(kfree);
	int id;

	tt_work = kzalloc(sizeof(*tt_work), GFP_KERNEL);
	if (!tt_work)
		return -ENOMEM;

	tt_trip = kzalloc(sizeof(*tt_trip), GFP_KERNEL);
	if (!tt_trip)
		return -ENOMEM;

	tt_zone = tt_get_tt_zone(arg);
	if (IS_ERR(tt_zone))
		return PTR_ERR(tt_zone);

	id = ida_alloc(&tt_zone->ida, GFP_KERNEL);
	if (id < 0)
		return id;

	tt_trip->trip.type = THERMAL_TRIP_ACTIVE;
	tt_trip->trip.temperature = THERMAL_TEMP_INVALID;
	tt_trip->trip.flags = THERMAL_TRIP_FLAG_RW;
	tt_trip->id = id;

	guard(tt_zone)(tt_zone);

	list_add_tail(&tt_trip->list_node, &tt_zone->trips);
	tt_zone->num_trips++;

	INIT_WORK(&tt_work->work, tt_zone_add_trip_work_fn);
	tt_work->tt_zone = no_free_ptr(tt_zone);
	tt_work->tt_trip = no_free_ptr(tt_trip);
	schedule_work(&(no_free_ptr(tt_work)->work));

	return 0;
}

static int tt_zone_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct tt_thermal_zone *tt_zone = thermal_zone_device_priv(tz);

	*temp = READ_ONCE(tt_zone->tz_temp);

	if (*temp < THERMAL_TEMP_INVALID)
		return -ENODATA;

	return 0;
}

static struct thermal_zone_device_ops tt_zone_ops = {
	.get_temp = tt_zone_get_temp,
};

static int tt_zone_register_tz(struct tt_thermal_zone *tt_zone)
{
	struct thermal_trip *trips __free(kfree) = NULL;
	struct thermal_zone_device *tz;
	struct tt_trip *tt_trip;
	int i;

	guard(tt_zone)(tt_zone);

	if (tt_zone->tz)
		return -EINVAL;

	trips = kcalloc(tt_zone->num_trips, sizeof(*trips), GFP_KERNEL);
	if (!trips)
		return -ENOMEM;

	i = 0;
	list_for_each_entry(tt_trip, &tt_zone->trips, list_node)
		trips[i++] = tt_trip->trip;

	tt_zone->tz_temp = tt_zone->temp;

	tz = thermal_zone_device_register_with_trips("test_tz", trips, i, tt_zone,
						     &tt_zone_ops, NULL, 0, 0);
	if (IS_ERR(tz))
		return PTR_ERR(tz);

	tt_zone->tz = tz;

	thermal_zone_device_enable(tz);

	return 0;
}

int tt_zone_reg(const char *arg)
{
	struct tt_thermal_zone *tt_zone __free(put_tt_zone);

	tt_zone = tt_get_tt_zone(arg);
	if (IS_ERR(tt_zone))
		return PTR_ERR(tt_zone);

	return tt_zone_register_tz(tt_zone);
}

int tt_zone_unreg(const char *arg)
{
	struct tt_thermal_zone *tt_zone __free(put_tt_zone);

	tt_zone = tt_get_tt_zone(arg);
	if (IS_ERR(tt_zone))
		return PTR_ERR(tt_zone);

	tt_zone_unregister_tz(tt_zone);

	return 0;
}

void tt_zone_cleanup(void)
{
	struct tt_thermal_zone *tt_zone, *aux;

	list_for_each_entry_safe(tt_zone, aux, &tt_thermal_zones, list_node) {
		tt_zone_unregister_tz(tt_zone);

		list_del(&tt_zone->list_node);

		tt_zone_free(tt_zone);
	}
}
