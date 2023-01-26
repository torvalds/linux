// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 Linaro Limited
 * Copyright 2023 Intel Corporation
 *
 * Library routines for populating a generic thermal trip point structure
 * with data obtained by evaluating a specific object in the ACPI Namespace.
 */
#include <linux/acpi.h>
#include <linux/units.h>

#include "thermal_core.h"

/*
 * Minimum temperature for full military grade is 218°K (-55°C) and
 * max temperature is 448°K (175°C). We can consider those values as
 * the boundaries for the [trips] temperature returned by the
 * firmware. Any values out of these boundaries may be considered
 * bogus and we can assume the firmware has no data to provide.
 */
#define TEMP_MIN_DECIK	2180
#define TEMP_MAX_DECIK	4480

static int thermal_acpi_trip_init(struct acpi_device *adev,
				  enum thermal_trip_type type, int id,
				  struct thermal_trip *trip)
{
	unsigned long long temp;
	acpi_status status;
	char obj_name[5];

	switch (type) {
	case THERMAL_TRIP_ACTIVE:
		if (id < 0 || id > 9)
			return -EINVAL;

		obj_name[1] = 'A';
		obj_name[2] = 'C';
		obj_name[3] = '0' + id;
		break;
	case THERMAL_TRIP_PASSIVE:
		obj_name[1] = 'P';
		obj_name[2] = 'S';
		obj_name[3] = 'V';
		break;
	case THERMAL_TRIP_HOT:
		obj_name[1] = 'H';
		obj_name[2] = 'O';
		obj_name[3] = 'T';
		break;
	case THERMAL_TRIP_CRITICAL:
		obj_name[1] = 'C';
		obj_name[2] = 'R';
		obj_name[3] = 'T';
		break;
	}

	obj_name[0] = '_';
	obj_name[4] = '\0';

	status = acpi_evaluate_integer(adev->handle, obj_name, NULL, &temp);
	if (ACPI_FAILURE(status)) {
		acpi_handle_debug(adev->handle, "%s evaluation failed\n", obj_name);
		return -ENODATA;
	}

	if (temp >= TEMP_MIN_DECIK && temp <= TEMP_MAX_DECIK) {
		trip->temperature = deci_kelvin_to_millicelsius(temp);
	} else {
		acpi_handle_debug(adev->handle, "%s result %llu out of range\n",
				  obj_name, temp);
		trip->temperature = THERMAL_TEMP_INVALID;
	}

	trip->hysteresis = 0;
	trip->type = type;

	return 0;
}

/**
 * thermal_acpi_trip_active - Get the specified active trip point
 * @adev: Thermal zone ACPI device object to get the description from.
 * @id: Active cooling level (0 - 9).
 * @trip: Trip point structure to be populated on success.
 *
 * Evaluate the _ACx object for the thermal zone represented by @adev to obtain
 * the temperature of the active cooling trip point corresponding to the active
 * cooling level given by @id and initialize @trip as an active trip point using
 * that temperature value.
 *
 * Return 0 on success or a negative error value on failure.
 */
int thermal_acpi_trip_active(struct acpi_device *adev, int id,
			     struct thermal_trip *trip)
{
	return thermal_acpi_trip_init(adev, THERMAL_TRIP_ACTIVE, id, trip);
}
EXPORT_SYMBOL_GPL(thermal_acpi_trip_active);

/**
 * thermal_acpi_trip_passive - Get the passive trip point
 * @adev: Thermal zone ACPI device object to get the description from.
 * @trip: Trip point structure to be populated on success.
 *
 * Evaluate the _PSV object for the thermal zone represented by @adev to obtain
 * the temperature of the passive cooling trip point and initialize @trip as a
 * passive trip point using that temperature value.
 *
 * Return 0 on success or -ENODATA on failure.
 */
int thermal_acpi_trip_passive(struct acpi_device *adev, struct thermal_trip *trip)
{
	return thermal_acpi_trip_init(adev, THERMAL_TRIP_PASSIVE, INT_MAX, trip);
}
EXPORT_SYMBOL_GPL(thermal_acpi_trip_passive);

/**
 * thermal_acpi_trip_hot - Get the near critical trip point
 * @adev: the ACPI device to get the description from.
 * @trip: a &struct thermal_trip to be filled if the function succeed.
 *
 * Evaluate the _HOT object for the thermal zone represented by @adev to obtain
 * the temperature of the trip point at which the system is expected to be put
 * into the S4 sleep state and initialize @trip as a hot trip point using that
 * temperature value.
 *
 * Return 0 on success or -ENODATA on failure.
 */
int thermal_acpi_trip_hot(struct acpi_device *adev, struct thermal_trip *trip)
{
	return thermal_acpi_trip_init(adev, THERMAL_TRIP_HOT, INT_MAX, trip);
}
EXPORT_SYMBOL_GPL(thermal_acpi_trip_hot);

/**
 * thermal_acpi_trip_critical - Get the critical trip point
 * @adev: the ACPI device to get the description from.
 * @trip: a &struct thermal_trip to be filled if the function succeed.
 *
 * Evaluate the _CRT object for the thermal zone represented by @adev to obtain
 * the temperature of the critical cooling trip point and initialize @trip as a
 * critical trip point using that temperature value.
 *
 * Return 0 on success or -ENODATA on failure.
 */
int thermal_acpi_trip_critical(struct acpi_device *adev, struct thermal_trip *trip)
{
	return thermal_acpi_trip_init(adev, THERMAL_TRIP_CRITICAL, INT_MAX, trip);
}
EXPORT_SYMBOL_GPL(thermal_acpi_trip_critical);
