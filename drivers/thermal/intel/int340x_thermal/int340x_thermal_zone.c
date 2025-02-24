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
					 const struct thermal_trip *trip, int temp)
{
	struct int34x_thermal_zone *d = thermal_zone_device_priv(zone);
	unsigned int trip_index = THERMAL_TRIP_PRIV_TO_INT(trip->priv);
	char name[] = {'P', 'A', 'T', '0' + trip_index, '\0'};
	acpi_status status;

	if (trip_index > 9)
		return -EINVAL;

	status = acpi_execute_simple_method(d->adev->handle, name,
					    millicelsius_to_deci_kelvin(temp));
	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

static void int340x_thermal_critical(struct thermal_zone_device *zone)
{
	dev_dbg(thermal_zone_device(zone), "%s: critical temperature reached\n",
		thermal_zone_device_type(zone));
}

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
		zone_trips[trip_cnt].priv = THERMAL_INT_TO_TRIP_PRIV(i);
		trip_cnt++;
	}

	return trip_cnt;
}

static struct thermal_zone_params int340x_thermal_params = {
	.no_hwmon = true,
};

struct int34x_thermal_zone *int340x_thermal_zone_add(struct acpi_device *adev,
						     int (*get_temp) (struct thermal_zone_device *, int *))
{
	const struct thermal_zone_device_ops zone_ops = {
		.set_trip_temp = int340x_thermal_set_trip_temp,
		.critical = int340x_thermal_critical,
		.get_temp = get_temp ? get_temp : int340x_thermal_get_zone_temp,
	};
	struct int34x_thermal_zone *int34x_zone;
	struct thermal_trip *zone_trips;
	unsigned long long trip_cnt = 0;
	unsigned long long hyst;
	acpi_status status;
	int i, ret;

	int34x_zone = kzalloc(sizeof(*int34x_zone), GFP_KERNEL);
	if (!int34x_zone)
		return ERR_PTR(-ENOMEM);

	int34x_zone->adev = adev;

	status = acpi_evaluate_integer(adev->handle, "PATC", NULL, &trip_cnt);
	if (ACPI_SUCCESS(status))
		int34x_zone->aux_trip_nr = trip_cnt;

	zone_trips = kcalloc(trip_cnt + INT340X_THERMAL_MAX_TRIP_COUNT,
			     sizeof(*zone_trips), GFP_KERNEL);
	if (!zone_trips) {
		ret = -ENOMEM;
		goto err_trips_alloc;
	}

	for (i = 0; i < trip_cnt; i++) {
		zone_trips[i].type = THERMAL_TRIP_PASSIVE;
		zone_trips[i].temperature = THERMAL_TEMP_INVALID;
		zone_trips[i].flags = THERMAL_TRIP_FLAG_RW_TEMP;
		zone_trips[i].priv = THERMAL_INT_TO_TRIP_PRIV(i);
	}

	trip_cnt = int340x_thermal_read_trips(adev, zone_trips, trip_cnt);

	status = acpi_evaluate_integer(adev->handle, "GTSH", NULL, &hyst);
	if (ACPI_SUCCESS(status))
		hyst *= 100;
	else
		hyst = 0;

	for (i = 0; i < trip_cnt; ++i)
		zone_trips[i].hysteresis = hyst;

	int34x_zone->lpat_table = acpi_lpat_get_conversion_table(adev->handle);

	int34x_zone->zone = thermal_zone_device_register_with_trips(
							acpi_device_bid(adev),
							zone_trips, trip_cnt,
							int34x_zone,
							&zone_ops,
							&int340x_thermal_params,
							0, 0);
	kfree(zone_trips);

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
	acpi_lpat_free_conversion_table(int34x_zone->lpat_table);
err_trips_alloc:
	kfree(int34x_zone);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(int340x_thermal_zone_add);

void int340x_thermal_zone_remove(struct int34x_thermal_zone *int34x_zone)
{
	thermal_zone_device_unregister(int34x_zone->zone);
	acpi_lpat_free_conversion_table(int34x_zone->lpat_table);
	kfree(int34x_zone);
}
EXPORT_SYMBOL_GPL(int340x_thermal_zone_remove);

static int int340x_update_one_trip(struct thermal_trip *trip, void *arg)
{
	struct int34x_thermal_zone *int34x_zone = arg;
	struct acpi_device *zone_adev = int34x_zone->adev;
	int temp, err;

	switch (trip->type) {
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
		err = thermal_acpi_active_trip_temp(zone_adev,
						    THERMAL_TRIP_PRIV_TO_INT(trip->priv),
						    &temp);
		break;
	default:
		err = -ENODEV;
	}
	if (err)
		temp = THERMAL_TEMP_INVALID;

	thermal_zone_set_trip_temp(int34x_zone->zone, trip, temp);

	return 0;
}

void int340x_thermal_update_trips(struct int34x_thermal_zone *int34x_zone)
{
	thermal_zone_for_each_trip(int34x_zone->zone, int340x_update_one_trip,
				   int34x_zone);
}
EXPORT_SYMBOL_GPL(int340x_thermal_update_trips);

MODULE_AUTHOR("Aaron Lu <aaron.lu@intel.com>");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Intel INT340x common thermal zone handler");
MODULE_LICENSE("GPL v2");
