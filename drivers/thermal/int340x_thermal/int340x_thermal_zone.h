/*
 * int340x_thermal_zone.h
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

#ifndef __INT340X_THERMAL_ZONE_H__
#define __INT340X_THERMAL_ZONE_H__

#include <acpi/acpi_lpat.h>

#define INT340X_THERMAL_MAX_ACT_TRIP_COUNT	10

struct active_trip {
	int temp;
	int id;
	bool valid;
};

struct int34x_thermal_zone {
	struct acpi_device *adev;
	struct active_trip act_trips[INT340X_THERMAL_MAX_ACT_TRIP_COUNT];
	unsigned long *aux_trips;
	int aux_trip_nr;
	int psv_temp;
	int psv_trip_id;
	int crt_temp;
	int crt_trip_id;
	int hot_temp;
	int hot_trip_id;
	struct thermal_zone_device *zone;
	struct thermal_zone_device_ops *override_ops;
	void *priv_data;
	struct acpi_lpat_conversion_table *lpat_table;
};

struct int34x_thermal_zone *int340x_thermal_zone_add(struct acpi_device *,
				struct thermal_zone_device_ops *override_ops);
void int340x_thermal_zone_remove(struct int34x_thermal_zone *);
int int340x_thermal_read_trips(struct int34x_thermal_zone *int34x_zone);

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
