/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_soc_dts_iosf.h
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _INTEL_SOC_DTS_IOSF_CORE_H
#define _INTEL_SOC_DTS_IOSF_CORE_H

#include <linux/thermal.h>

/* DTS0 and DTS 1 */
#define SOC_MAX_DTS_SENSORS	2

/* Only 2 out of 4 is allowed for OSPM */
#define SOC_MAX_DTS_TRIPS	2

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
	u32 store_status;
	u32 trip_mask;
	struct thermal_trip trips[SOC_MAX_DTS_TRIPS];
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


struct intel_soc_dts_sensors *
intel_soc_dts_iosf_init(enum intel_soc_dts_interrupt_type intr_type,
			bool critical_trip, int crit_offset);
void intel_soc_dts_iosf_exit(struct intel_soc_dts_sensors *sensors);
void intel_soc_dts_iosf_interrupt_handler(
				struct intel_soc_dts_sensors *sensors);
#endif
