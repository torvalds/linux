/*
 * intel_soc_dts_iosf.h
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

#ifndef _INTEL_SOC_DTS_IOSF_CORE_H
#define _INTEL_SOC_DTS_IOSF_CORE_H

#include <linux/thermal.h>

/* DTS0 and DTS 1 */
#define SOC_MAX_DTS_SENSORS	2

enum intel_soc_dts_interrupt_type {
	INTEL_SOC_DTS_INTERRUPT_NONE,
	INTEL_SOC_DTS_INTERRUPT_APIC,
	INTEL_SOC_DTS_INTERRUPT_MSI,
	INTEL_SOC_DTS_INTERRUPT_SCI,
	INTEL_SOC_DTS_INTERRUPT_SMI,
};

struct intel_soc_dts_sensors;

struct intel_soc_dts_sensor_entry {
	int id;
	u32 temp_mask;
	u32 temp_shift;
	u32 store_status;
	u32 trip_mask;
	u32 trip_count;
	enum thermal_trip_type trip_types[2];
	struct thermal_zone_device *tzone;
	struct intel_soc_dts_sensors *sensors;
};

struct intel_soc_dts_sensors {
	u32 tj_max;
	spinlock_t intr_notify_lock;
	struct mutex dts_update_lock;
	enum intel_soc_dts_interrupt_type intr_type;
	struct intel_soc_dts_sensor_entry soc_dts[SOC_MAX_DTS_SENSORS];
};

struct intel_soc_dts_sensors *intel_soc_dts_iosf_init(
	enum intel_soc_dts_interrupt_type intr_type, int trip_count,
	int read_only_trip_count);
void intel_soc_dts_iosf_exit(struct intel_soc_dts_sensors *sensors);
void intel_soc_dts_iosf_interrupt_handler(
				struct intel_soc_dts_sensors *sensors);
int intel_soc_dts_iosf_add_read_only_critical_trip(
	struct intel_soc_dts_sensors *sensors, int critical_offset);
#endif
