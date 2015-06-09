/*
 * int340x_thermal_zone.c
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include "int340x_thermal_zone.h"

static int int340x_thermal_get_zone_temp(struct thermal_zone_device *zone,
					 unsigned long *temp)
{
	struct int34x_thermal_zone *d = zone->devdata;
	unsigned long long tmp;
	acpi_status status;

	if (d->override_ops && d->override_ops->get_temp)
		return d->override_ops->get_temp(zone, temp);

	status = acpi_evaluate_integer(d->adev->handle, "_TMP", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (d->lpat_table) {
		int conv_temp;

		conv_temp = acpi_lpat_raw_to_temp(d->lpat_table, (int)tmp);
		if (conv_temp < 0)
			return conv_temp;

		*temp = (unsigned long)conv_temp * 10;
	} else
		/* _TMP returns the temperature in tenths of degrees Kelvin */
		*temp = DECI_KELVIN_TO_MILLICELSIUS(tmp);

	return 0;
}

static int int340x_thermal_get_trip_temp(struct thermal_zone_device *zone,
					 int trip, unsigned long *temp)
{
	struct int34x_thermal_zone *d = zone->devdata;
	int i;

	if (d->override_ops && d->override_ops->get_trip_temp)
		return d->override_ops->get_trip_temp(zone, trip, temp);

	if (trip < d->aux_trip_nr)
		*temp = d->aux_trips[trip];
	else if (trip == d->crt_trip_id)
		*temp = d->crt_temp;
	else if (trip == d->psv_trip_id)
		*temp = d->psv_temp;
	else if (trip == d->hot_trip_id)
		*temp = d->hot_temp;
	else {
		for (i = 0; i < INT340X_THERMAL_MAX_ACT_TRIP_COUNT; i++) {
			if (d->act_trips[i].valid &&
			    d->act_trips[i].id == trip) {
				*temp = d->act_trips[i].temp;
				break;
			}
		}
		if (i == INT340X_THERMAL_MAX_ACT_TRIP_COUNT)
			return -EINVAL;
	}

	return 0;
}

static int int340x_thermal_get_trip_type(struct thermal_zone_device *zone,
					 int trip,
					 enum thermal_trip_type *type)
{
	struct int34x_thermal_zone *d = zone->devdata;
	int i;

	if (d->override_ops && d->override_ops->get_trip_type)
		return d->override_ops->get_trip_type(zone, trip, type);

	if (trip < d->aux_trip_nr)
		*type = THERMAL_TRIP_PASSIVE;
	else if (trip == d->crt_trip_id)
		*type = THERMAL_TRIP_CRITICAL;
	else if (trip == d->hot_trip_id)
		*type = THERMAL_TRIP_HOT;
	else if (trip == d->psv_trip_id)
		*type = THERMAL_TRIP_PASSIVE;
	else {
		for (i = 0; i < INT340X_THERMAL_MAX_ACT_TRIP_COUNT; i++) {
			if (d->act_trips[i].valid &&
			    d->act_trips[i].id == trip) {
				*type = THERMAL_TRIP_ACTIVE;
				break;
			}
		}
		if (i == INT340X_THERMAL_MAX_ACT_TRIP_COUNT)
			return -EINVAL;
	}

	return 0;
}

static int int340x_thermal_set_trip_temp(struct thermal_zone_device *zone,
				      int trip, unsigned long temp)
{
	struct int34x_thermal_zone *d = zone->devdata;
	acpi_status status;
	char name[10];

	if (d->override_ops && d->override_ops->set_trip_temp)
		return d->override_ops->set_trip_temp(zone, trip, temp);

	snprintf(name, sizeof(name), "PAT%d", trip);
	status = acpi_execute_simple_method(d->adev->handle, name,
			MILLICELSIUS_TO_DECI_KELVIN(temp));
	if (ACPI_FAILURE(status))
		return -EIO;

	d->aux_trips[trip] = temp;

	return 0;
}


static int int340x_thermal_get_trip_hyst(struct thermal_zone_device *zone,
		int trip, unsigned long *temp)
{
	struct int34x_thermal_zone *d = zone->devdata;
	acpi_status status;
	unsigned long long hyst;

	if (d->override_ops && d->override_ops->get_trip_hyst)
		return d->override_ops->get_trip_hyst(zone, trip, temp);

	status = acpi_evaluate_integer(d->adev->handle, "GTSH", NULL, &hyst);
	if (ACPI_FAILURE(status))
		return -EIO;

	*temp = hyst * 100;

	return 0;
}

static struct thermal_zone_device_ops int340x_thermal_zone_ops = {
	.get_temp       = int340x_thermal_get_zone_temp,
	.get_trip_temp	= int340x_thermal_get_trip_temp,
	.get_trip_type	= int340x_thermal_get_trip_type,
	.set_trip_temp	= int340x_thermal_set_trip_temp,
	.get_trip_hyst =  int340x_thermal_get_trip_hyst,
};

static int int340x_thermal_get_trip_config(acpi_handle handle, char *name,
				      unsigned long *temp)
{
	unsigned long long r;
	acpi_status status;

	status = acpi_evaluate_integer(handle, name, NULL, &r);
	if (ACPI_FAILURE(status))
		return -EIO;

	*temp = DECI_KELVIN_TO_MILLICELSIUS(r);

	return 0;
}

static struct thermal_zone_params int340x_thermal_params = {
	.governor_name = "user_space",
	.no_hwmon = true,
};

struct int34x_thermal_zone *int340x_thermal_zone_add(struct acpi_device *adev,
				struct thermal_zone_device_ops *override_ops)
{
	struct int34x_thermal_zone *int34x_thermal_zone;
	acpi_status status;
	unsigned long long trip_cnt;
	int trip_mask = 0, i;
	int ret;

	int34x_thermal_zone = kzalloc(sizeof(*int34x_thermal_zone),
				      GFP_KERNEL);
	if (!int34x_thermal_zone)
		return ERR_PTR(-ENOMEM);

	int34x_thermal_zone->adev = adev;
	int34x_thermal_zone->override_ops = override_ops;

	status = acpi_evaluate_integer(adev->handle, "PATC", NULL, &trip_cnt);
	if (ACPI_FAILURE(status))
		trip_cnt = 0;
	else {
		int34x_thermal_zone->aux_trips = kzalloc(
				sizeof(*int34x_thermal_zone->aux_trips) *
				trip_cnt, GFP_KERNEL);
		if (!int34x_thermal_zone->aux_trips) {
			ret = -ENOMEM;
			goto err_trip_alloc;
		}
		trip_mask = BIT(trip_cnt) - 1;
		int34x_thermal_zone->aux_trip_nr = trip_cnt;
	}

	int34x_thermal_zone->crt_trip_id = -1;
	if (!int340x_thermal_get_trip_config(adev->handle, "_CRT",
					     &int34x_thermal_zone->crt_temp))
		int34x_thermal_zone->crt_trip_id = trip_cnt++;
	int34x_thermal_zone->hot_trip_id = -1;
	if (!int340x_thermal_get_trip_config(adev->handle, "_HOT",
					     &int34x_thermal_zone->hot_temp))
		int34x_thermal_zone->hot_trip_id = trip_cnt++;
	int34x_thermal_zone->psv_trip_id = -1;
	if (!int340x_thermal_get_trip_config(adev->handle, "_PSV",
					     &int34x_thermal_zone->psv_temp))
		int34x_thermal_zone->psv_trip_id = trip_cnt++;
	for (i = 0; i < INT340X_THERMAL_MAX_ACT_TRIP_COUNT; i++) {
		char name[5] = { '_', 'A', 'C', '0' + i, '\0' };

		if (int340x_thermal_get_trip_config(adev->handle, name,
				&int34x_thermal_zone->act_trips[i].temp))
			break;

		int34x_thermal_zone->act_trips[i].id = trip_cnt++;
		int34x_thermal_zone->act_trips[i].valid = true;
	}
	int34x_thermal_zone->lpat_table = acpi_lpat_get_conversion_table(
								adev->handle);

	int34x_thermal_zone->zone = thermal_zone_device_register(
						acpi_device_bid(adev),
						trip_cnt,
						trip_mask, int34x_thermal_zone,
						&int340x_thermal_zone_ops,
						&int340x_thermal_params,
						0, 0);
	if (IS_ERR(int34x_thermal_zone->zone)) {
		ret = PTR_ERR(int34x_thermal_zone->zone);
		goto err_thermal_zone;
	}

	return int34x_thermal_zone;

err_thermal_zone:
	acpi_lpat_free_conversion_table(int34x_thermal_zone->lpat_table);
	kfree(int34x_thermal_zone->aux_trips);
err_trip_alloc:
	kfree(int34x_thermal_zone);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(int340x_thermal_zone_add);

void int340x_thermal_zone_remove(struct int34x_thermal_zone
				 *int34x_thermal_zone)
{
	thermal_zone_device_unregister(int34x_thermal_zone->zone);
	acpi_lpat_free_conversion_table(int34x_thermal_zone->lpat_table);
	kfree(int34x_thermal_zone->aux_trips);
	kfree(int34x_thermal_zone);
}
EXPORT_SYMBOL_GPL(int340x_thermal_zone_remove);

MODULE_AUTHOR("Aaron Lu <aaron.lu@intel.com>");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Intel INT340x common thermal zone handler");
MODULE_LICENSE("GPL v2");
