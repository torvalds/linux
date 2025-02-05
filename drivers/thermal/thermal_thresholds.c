// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * Thermal thresholds
 */
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/slab.h>

#include "thermal_core.h"
#include "thermal_thresholds.h"

int thermal_thresholds_init(struct thermal_zone_device *tz)
{
	INIT_LIST_HEAD(&tz->user_thresholds);

	return 0;
}

static void __thermal_thresholds_flush(struct thermal_zone_device *tz)
{
	struct list_head *thresholds = &tz->user_thresholds;
	struct user_threshold *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, thresholds, list_node) {
		list_del(&entry->list_node);
		kfree(entry);
	}
}

void thermal_thresholds_flush(struct thermal_zone_device *tz)
{
	lockdep_assert_held(&tz->lock);

	__thermal_thresholds_flush(tz);

	thermal_notify_threshold_flush(tz);

	__thermal_zone_device_update(tz, THERMAL_TZ_FLUSH_THRESHOLDS);
}

void thermal_thresholds_exit(struct thermal_zone_device *tz)
{
	__thermal_thresholds_flush(tz);
}

static int __thermal_thresholds_cmp(void *data,
				    const struct list_head *l1,
				    const struct list_head *l2)
{
	struct user_threshold *t1 = container_of(l1, struct user_threshold, list_node);
	struct user_threshold *t2 = container_of(l2, struct user_threshold, list_node);

	return t1->temperature - t2->temperature;
}

static struct user_threshold *__thermal_thresholds_find(const struct list_head *thresholds,
							int temperature)
{
	struct user_threshold *t;

	list_for_each_entry(t, thresholds, list_node)
		if (t->temperature == temperature)
			return t;

	return NULL;
}

static bool thermal_thresholds_handle_raising(struct list_head *thresholds, int temperature,
					      int last_temperature)
{
	struct user_threshold *t;

	list_for_each_entry(t, thresholds, list_node) {

		if (!(t->direction & THERMAL_THRESHOLD_WAY_UP))
		    continue;

		if (temperature >= t->temperature &&
		    last_temperature < t->temperature)
			return true;
	}

	return false;
}

static bool thermal_thresholds_handle_dropping(struct list_head *thresholds, int temperature,
					       int last_temperature)
{
	struct user_threshold *t;

	list_for_each_entry_reverse(t, thresholds, list_node) {

		if (!(t->direction & THERMAL_THRESHOLD_WAY_DOWN))
		    continue;

		if (temperature <= t->temperature &&
		    last_temperature > t->temperature)
			return true;
	}

	return false;
}

static void thermal_threshold_find_boundaries(struct list_head *thresholds, int temperature,
					      int *low, int *high)
{
	struct user_threshold *t;

	list_for_each_entry(t, thresholds, list_node) {
		if (temperature < t->temperature &&
		    (t->direction & THERMAL_THRESHOLD_WAY_UP) &&
		    *high > t->temperature)
			*high = t->temperature;
	}

	list_for_each_entry_reverse(t, thresholds, list_node) {
		if (temperature > t->temperature &&
		    (t->direction & THERMAL_THRESHOLD_WAY_DOWN) &&
		    *low < t->temperature)
			*low = t->temperature;
	}
}

void thermal_thresholds_handle(struct thermal_zone_device *tz, int *low, int *high)
{
	struct list_head *thresholds = &tz->user_thresholds;

	int temperature = tz->temperature;
	int last_temperature = tz->last_temperature;

	lockdep_assert_held(&tz->lock);

	thermal_threshold_find_boundaries(thresholds, temperature, low, high);

	/*
	 * We need a second update in order to detect a threshold being crossed
	 */
	if (last_temperature == THERMAL_TEMP_INVALID)
		return;

	/*
	 * The temperature is stable, so obviously we can not have
	 * crossed a threshold.
	 */
	if (last_temperature == temperature)
		return;

	/*
	 * Since last update the temperature:
	 * - increased : thresholds are crossed the way up
	 * - decreased : thresholds are crossed the way down
	 */
	if (temperature > last_temperature) {
		if (thermal_thresholds_handle_raising(thresholds,
						      temperature, last_temperature))
			thermal_notify_threshold_up(tz);
	} else {
		if (thermal_thresholds_handle_dropping(thresholds,
						       temperature, last_temperature))
			thermal_notify_threshold_down(tz);
	}
}

int thermal_thresholds_add(struct thermal_zone_device *tz,
			   int temperature, int direction)
{
	struct list_head *thresholds = &tz->user_thresholds;
	struct user_threshold *t;

	lockdep_assert_held(&tz->lock);

	t = __thermal_thresholds_find(thresholds, temperature);
	if (t) {
		if (t->direction == direction)
			return -EEXIST;

		t->direction |= direction;
	} else {

		t = kmalloc(sizeof(*t), GFP_KERNEL);
		if (!t)
			return -ENOMEM;

		INIT_LIST_HEAD(&t->list_node);
		t->temperature = temperature;
		t->direction = direction;
		list_add(&t->list_node, thresholds);
		list_sort(NULL, thresholds, __thermal_thresholds_cmp);
	}

	thermal_notify_threshold_add(tz, temperature, direction);

	__thermal_zone_device_update(tz, THERMAL_TZ_ADD_THRESHOLD);

	return 0;
}

int thermal_thresholds_delete(struct thermal_zone_device *tz,
			      int temperature, int direction)
{
	struct list_head *thresholds = &tz->user_thresholds;
	struct user_threshold *t;

	lockdep_assert_held(&tz->lock);

	t = __thermal_thresholds_find(thresholds, temperature);
	if (!t)
		return -ENOENT;

	if (t->direction == direction) {
		list_del(&t->list_node);
		kfree(t);
	} else {
		t->direction &= ~direction;
	}

	thermal_notify_threshold_delete(tz, temperature, direction);

	__thermal_zone_device_update(tz, THERMAL_TZ_DEL_THRESHOLD);

	return 0;
}

int thermal_thresholds_for_each(struct thermal_zone_device *tz,
				int (*cb)(struct user_threshold *, void *arg), void *arg)
{
	struct list_head *thresholds = &tz->user_thresholds;
	struct user_threshold *entry;
	int ret;

	guard(thermal_zone)(tz);

	list_for_each_entry(entry, thresholds, list_node) {
		ret = cb(entry, arg);
		if (ret)
			return ret;
	}

	return 0;
}
