/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * int340x_thermal_zone.h
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __INT340X_THERMAL_ZONE_H__
#define __INT340X_THERMAL_ZONE_H__

#include <acpi/acpi_lpat.h>

#define INT340X_THERMAL_MAX_ACT_TRIP_COUNT	10
#define INT340X_THERMAL_MAX_TRIP_COUNT INT340X_THERMAL_MAX_ACT_TRIP_COUNT + 3

struct active_trip {
	int temp;
	int id;
	bool valid;
};

struct int34x_thermal_zone {
	struct acpi_device *adev;
	struct thermal_trip *trips;
	int aux_trip_nr;
	struct thermal_zone_device *zone;
	struct thermal_zone_device_ops *ops;
	void *priv_data;
	struct acpi_lpat_conversion_table *lpat_table;
};

struct int34x_thermal_zone *int340x_thermal_zone_add(struct acpi_device *,
				int (*get_temp) (struct thermal_zone_device *, int *));
void int340x_thermal_zone_remove(struct int34x_thermal_zone *);
void int340x_thermal_update_trips(struct int34x_thermal_zone *int34x_zone);

static inline void int340x_thermal_zone_set_priv_data(
			struct int34x_thermal_zone *tzone, void *priv_data)
{
	tzone->priv_data = priv_data;
}

static inline void *int340x_thermal_zone_get_priv_data(
			struct int34x_thermal_zone *tzone)
{
	return tzone->priv_data;
}

static inline void int340x_thermal_zone_device_update(
					struct int34x_thermal_zone *tzone,
					enum thermal_notify_event event)
{
	thermal_zone_device_update(tzone->zone, event);
}

#endif
