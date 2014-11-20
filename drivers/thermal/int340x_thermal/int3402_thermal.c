/*
 * INT3402 thermal driver for memory temperature reporting
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Aaron Lu <aaron.lu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/thermal.h>

#define ACPI_ACTIVE_COOLING_MAX_NR 10

struct active_trip {
	unsigned long temp;
	int id;
	bool valid;
};

struct int3402_thermal_data {
	unsigned long *aux_trips;
	int aux_trip_nr;
	unsigned long psv_temp;
	int psv_trip_id;
	unsigned long crt_temp;
	int crt_trip_id;
	unsigned long hot_temp;
	int hot_trip_id;
	struct active_trip act_trips[ACPI_ACTIVE_COOLING_MAX_NR];
	acpi_handle *handle;
};

static int int3402_thermal_get_zone_temp(struct thermal_zone_device *zone,
					 unsigned long *temp)
{
	struct int3402_thermal_data *d = zone->devdata;
	unsigned long long tmp;
	acpi_status status;

	status = acpi_evaluate_integer(d->handle, "_TMP", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	/* _TMP returns the temperature in tenths of degrees Kelvin */
	*temp = DECI_KELVIN_TO_MILLICELSIUS(tmp);

	return 0;
}

static int int3402_thermal_get_trip_temp(struct thermal_zone_device *zone,
					 int trip, unsigned long *temp)
{
	struct int3402_thermal_data *d = zone->devdata;
	int i;

	if (trip < d->aux_trip_nr)
		*temp = d->aux_trips[trip];
	else if (trip == d->crt_trip_id)
		*temp = d->crt_temp;
	else if (trip == d->psv_trip_id)
		*temp = d->psv_temp;
	else if (trip == d->hot_trip_id)
		*temp = d->hot_temp;
	else {
		for (i = 0; i < ACPI_ACTIVE_COOLING_MAX_NR; i++) {
			if (d->act_trips[i].valid &&
			    d->act_trips[i].id == trip) {
				*temp = d->act_trips[i].temp;
				break;
			}
		}
		if (i == ACPI_ACTIVE_COOLING_MAX_NR)
			return -EINVAL;
	}
	return 0;
}

static int int3402_thermal_get_trip_type(struct thermal_zone_device *zone,
					 int trip, enum thermal_trip_type *type)
{
	struct int3402_thermal_data *d = zone->devdata;
	int i;

	if (trip < d->aux_trip_nr)
		*type = THERMAL_TRIP_PASSIVE;
	else if (trip == d->crt_trip_id)
		*type = THERMAL_TRIP_CRITICAL;
	else if (trip == d->hot_trip_id)
		*type = THERMAL_TRIP_HOT;
	else if (trip == d->psv_trip_id)
		*type = THERMAL_TRIP_PASSIVE;
	else {
		for (i = 0; i < ACPI_ACTIVE_COOLING_MAX_NR; i++) {
			if (d->act_trips[i].valid &&
			    d->act_trips[i].id == trip) {
				*type = THERMAL_TRIP_ACTIVE;
				break;
			}
		}
		if (i == ACPI_ACTIVE_COOLING_MAX_NR)
			return -EINVAL;
	}
	return 0;
}

static int int3402_thermal_set_trip_temp(struct thermal_zone_device *zone, int trip,
				  unsigned long temp)
{
	struct int3402_thermal_data *d = zone->devdata;
	acpi_status status;
	char name[10];

	snprintf(name, sizeof(name), "PAT%d", trip);
	status = acpi_execute_simple_method(d->handle, name,
			MILLICELSIUS_TO_DECI_KELVIN(temp));
	if (ACPI_FAILURE(status))
		return -EIO;

	d->aux_trips[trip] = temp;
	return 0;
}

static struct thermal_zone_device_ops int3402_thermal_zone_ops = {
	.get_temp       = int3402_thermal_get_zone_temp,
	.get_trip_temp	= int3402_thermal_get_trip_temp,
	.get_trip_type	= int3402_thermal_get_trip_type,
	.set_trip_temp	= int3402_thermal_set_trip_temp,
};

static struct thermal_zone_params int3402_thermal_params = {
	.governor_name = "user_space",
	.no_hwmon = true,
};

static int int3402_thermal_get_temp(acpi_handle handle, char *name,
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

static int int3402_thermal_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct int3402_thermal_data *d;
	struct thermal_zone_device *zone;
	acpi_status status;
	unsigned long long trip_cnt;
	int trip_mask = 0, i;

	if (!acpi_has_method(adev->handle, "_TMP"))
		return -ENODEV;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	status = acpi_evaluate_integer(adev->handle, "PATC", NULL, &trip_cnt);
	if (ACPI_FAILURE(status))
		trip_cnt = 0;
	else {
		d->aux_trips = devm_kzalloc(&pdev->dev,
				sizeof(*d->aux_trips) * trip_cnt, GFP_KERNEL);
		if (!d->aux_trips)
			return -ENOMEM;
		trip_mask = trip_cnt - 1;
		d->handle = adev->handle;
		d->aux_trip_nr = trip_cnt;
	}

	d->crt_trip_id = -1;
	if (!int3402_thermal_get_temp(adev->handle, "_CRT", &d->crt_temp))
		d->crt_trip_id = trip_cnt++;
	d->hot_trip_id = -1;
	if (!int3402_thermal_get_temp(adev->handle, "_HOT", &d->hot_temp))
		d->hot_trip_id = trip_cnt++;
	d->psv_trip_id = -1;
	if (!int3402_thermal_get_temp(adev->handle, "_PSV", &d->psv_temp))
		d->psv_trip_id = trip_cnt++;
	for (i = 0; i < ACPI_ACTIVE_COOLING_MAX_NR; i++) {
		char name[5] = { '_', 'A', 'C', '0' + i, '\0' };
		if (int3402_thermal_get_temp(adev->handle, name,
					     &d->act_trips[i].temp))
			break;
		d->act_trips[i].id = trip_cnt++;
		d->act_trips[i].valid = true;
	}

	zone = thermal_zone_device_register(acpi_device_bid(adev), trip_cnt,
					    trip_mask, d,
					    &int3402_thermal_zone_ops,
					    &int3402_thermal_params,
					    0, 0);
	if (IS_ERR(zone))
		return PTR_ERR(zone);
	platform_set_drvdata(pdev, zone);

	return 0;
}

static int int3402_thermal_remove(struct platform_device *pdev)
{
	struct thermal_zone_device *zone = platform_get_drvdata(pdev);

	thermal_zone_device_unregister(zone);
	return 0;
}

static const struct acpi_device_id int3402_thermal_match[] = {
	{"INT3402", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, int3402_thermal_match);

static struct platform_driver int3402_thermal_driver = {
	.probe = int3402_thermal_probe,
	.remove = int3402_thermal_remove,
	.driver = {
		   .name = "int3402 thermal",
		   .owner = THIS_MODULE,
		   .acpi_match_table = int3402_thermal_match,
		   },
};

module_platform_driver(int3402_thermal_driver);

MODULE_DESCRIPTION("INT3402 Thermal driver");
MODULE_LICENSE("GPL");
