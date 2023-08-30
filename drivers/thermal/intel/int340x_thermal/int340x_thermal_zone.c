// SPDX-License-Identifier: GPL-2.0-only
/*
 * int340x_thermal_zone.c
 * Copyright (c) 2015, Intel Corporation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/units.h>
#include "int340x_thermal_zone.h"

static int int340x_thermal_get_zone_temp(struct thermal_zone_device *zone,
					 int *temp)
{
	struct int34x_thermal_zone *d = thermal_zone_device_priv(zone);
	unsigned long long tmp;
	acpi_status status;

	status = acpi_evaluate_integer(d->adev->handle, "_TMP", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (d->lpat_table) {
		int conv_temp;

		conv_temp = acpi_lpat_raw_to_temp(d->lpat_table, (int)tmp);
		if (conv_temp < 0)
			return conv_temp;

		*temp = conv_temp * 10;
	} else {
		/* _TMP returns the temperature in tenths of degrees Kelvin */
		*temp = deci_kelvin_to_millicelsius(tmp);
	}

	return 0;
}

static int int340x_thermal_set_trip_temp(struct thermal_zone_device *zone,
					 int trip, int temp)
{
	struct int34x_thermal_zone *d = thermal_zone_device_priv(zone);
	char name[] = {'P', 'A', 'T', '0' + trip, '\0'};
	acpi_status status;

	if (trip > 9)
		return -EINVAL;

	status = acpi_execute_simple_method(d->adev->handle, name,
					    millicelsius_to_deci_kelvin(temp));
	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

static void int340x_thermal_critical(struct thermal_zone_device *zone)
{
	dev_dbg(&zone->device, "%s: critical temperature reached\n", zone->type);
}

static struct thermal_zone_device_ops int340x_thermal_zone_ops = {
	.get_temp       = int340x_thermal_get_zone_temp,
	.set_trip_temp	= int340x_thermal_set_trip_temp,
	.critical	= int340x_thermal_critical,
};

static int int340x_thermal_read_trips(struct acpi_device *zone_adev,
				      struct thermal_trip *zone_trips,
				      int trip_cnt)
{
	int i, ret;

	ret = thermal_acpi_critical_trip_temp(zone_adev,
					      &zone_trips[trip_cnt].temperature);
	if (!ret) {
		zone_trips[trip_cnt].type = THERMAL_TRIP_CRITICAL;
		trip_cnt++;
	}

	ret = thermal_acpi_hot_trip_temp(zone_adev,
					 &zone_trips[trip_cnt].temperature);
	if (!ret) {
		zone_trips[trip_cnt].type = THERMAL_TRIP_HOT;
		trip_cnt++;
	}

	ret = thermal_acpi_passive_trip_temp(zone_adev,
					     &zone_trips[trip_cnt].temperature);
	if (!ret) {
		zone_trips[trip_cnt].type = THERMAL_TRIP_PASSIVE;
		trip_cnt++;
	}

	for (i = 0; i < INT340X_THERMAL_MAX_ACT_TRIP_COUNT; i++) {
		ret = thermal_acpi_active_trip_temp(zone_adev, i,
						    &zone_trips[trip_cnt].temperature);
		if (ret)
			break;

		zone_trips[trip_cnt].type = THERMAL_TRIP_ACTIVE;
		trip_cnt++;
	}

	return trip_cnt;
}

static struct thermal_zone_params int340x_thermal_params = {
	.governor_name = "user_space",
	.no_hwmon = true,
};

struct int34x_thermal_zone *int340x_thermal_zone_add(struct acpi_device *adev,
						     int (*get_temp) (struct thermal_zone_device *, int *))
{
	struct int34x_thermal_zone *int34x_zone;
	struct thermal_trip *zone_trips;
	unsigned long long trip_cnt = 0;
	unsigned long long hyst;
	int trip_mask = 0;
	acpi_status status;
	int i, ret;

	int34x_zone = kzalloc(sizeof(*int34x_zone), GFP_KERNEL);
	if (!int34x_zone)
		return ERR_PTR(-ENOMEM);

	int34x_zone->adev = adev;

	int34x_zone->ops = kmemdup(&int340x_thermal_zone_ops,
				   sizeof(int340x_thermal_zone_ops), GFP_KERNEL);
	if (!int34x_zone->ops) {
		ret = -ENOMEM;
		goto err_ops_alloc;
	}

	if (get_temp)
		int34x_zone->ops->get_temp = get_temp;

	status = acpi_evaluate_integer(adev->handle, "PATC", NULL, &trip_cnt);
	if (ACPI_SUCCESS(status)) {
		int34x_zone->aux_trip_nr = trip_cnt;
		trip_mask = BIT(trip_cnt) - 1;
	}

	zone_trips = kzalloc(sizeof(*zone_trips) * (trip_cnt + INT340X_THERMAL_MAX_TRIP_COUNT),
			     GFP_KERNEL);
	if (!zone_trips) {
		ret = -ENOMEM;
		goto err_trips_alloc;
	}

	for (i = 0; i < trip_cnt; i++) {
		zone_trips[i].type = THERMAL_TRIP_PASSIVE;
		zone_trips[i].temperature = THERMAL_TEMP_INVALID;
	}

	trip_cnt = int340x_thermal_read_trips(adev, zone_trips, trip_cnt);

	status = acpi_evaluate_integer(adev->handle, "GTSH", NULL, &hyst);
	if (ACPI_SUCCESS(status))
		hyst *= 100;
	else
		hyst = 0;

	for (i = 0; i < trip_cnt; ++i)
		zone_trips[i].hysteresis = hyst;

	int34x_zone->trips = zone_trips;

	int34x_zone->lpat_table = acpi_lpat_get_conversion_table(adev->handle);

	int34x_zone->zone = thermal_zone_device_register_with_trips(
							acpi_device_bid(adev),
							zone_trips, trip_cnt,
							trip_mask, int34x_zone,
							int34x_zone->ops,
							&int340x_thermal_params,
							0, 0);
	if (IS_ERR(int34x_zone->zone)) {
		ret = PTR_ERR(int34x_zone->zone);
		goto err_thermal_zone;
	}
	ret = thermal_zone_device_enable(int34x_zone->zone);
	if (ret)
		goto err_enable;

	return int34x_zone;

err_enable:
	thermal_zone_device_unregister(int34x_zone->zone);
err_thermal_zone:
	kfree(int34x_zone->trips);
	acpi_lpat_free_conversion_table(int34x_zone->lpat_table);
err_trips_alloc:
	kfree(int34x_zone->ops);
err_ops_alloc:
	kfree(int34x_zone);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(int340x_thermal_zone_add);

void int340x_thermal_zone_remove(struct int34x_thermal_zone *int34x_zone)
{
	thermal_zone_device_unregister(int34x_zone->zone);
	acpi_lpat_free_conversion_table(int34x_zone->lpat_table);
	kfree(int34x_zone->trips);
	kfree(int34x_zone->ops);
	kfree(int34x_zone);
}
EXPORT_SYMBOL_GPL(int340x_thermal_zone_remove);

void int340x_thermal_update_trips(struct int34x_thermal_zone *int34x_zone)
{
	struct acpi_device *zone_adev = int34x_zone->adev;
	struct thermal_trip *zone_trips = int34x_zone->trips;
	int trip_cnt = int34x_zone->zone->num_trips;
	int act_trip_nr = 0;
	int i;

	mutex_lock(&int34x_zone->zone->lock);

	for (i = int34x_zone->aux_trip_nr; i < trip_cnt; i++) {
		int temp, err;

		switch (zone_trips[i].type) {
		case THERMAL_TRIP_CRITICAL:
			err = thermal_acpi_critical_trip_temp(zone_adev, &temp);
			break;
		case THERMAL_TRIP_HOT:
			err = thermal_acpi_hot_trip_temp(zone_adev, &temp);
			break;
		case THERMAL_TRIP_PASSIVE:
			err = thermal_acpi_passive_trip_temp(zone_adev, &temp);
			break;
		case THERMAL_TRIP_ACTIVE:
			err = thermal_acpi_active_trip_temp(zone_adev, act_trip_nr++,
							    &temp);
			break;
		default:
			err = -ENODEV;
		}
		if (err) {
			zone_trips[i].temperature = THERMAL_TEMP_INVALID;
			continue;
		}

		zone_trips[i].temperature = temp;
	}

	mutex_unlock(&int34x_zone->zone->lock);
}
EXPORT_SYMBOL_GPL(int340x_thermal_update_trips);

MODULE_AUTHOR("Aaron Lu <aaron.lu@intel.com>");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Intel INT340x common thermal zone handler");
MODULE_LICENSE("GPL v2");
