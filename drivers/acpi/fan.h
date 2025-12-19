/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * ACPI fan device IDs are shared between the fan driver and the device power
 * management code.
 *
 * Add new device IDs before the generic ACPI fan one.
 */

#ifndef _ACPI_FAN_H_
#define _ACPI_FAN_H_

#include <linux/kconfig.h>
#include <linux/limits.h>

#define ACPI_FAN_DEVICE_IDS	\
	{"INT3404", }, /* Fan */ \
	{"INTC1044", }, /* Fan for Tiger Lake generation */ \
	{"INTC1048", }, /* Fan for Alder Lake generation */ \
	{"INTC1063", }, /* Fan for Meteor Lake generation */ \
	{"INTC106A", }, /* Fan for Lunar Lake generation */ \
	{"INTC10A2", }, /* Fan for Raptor Lake generation */ \
	{"INTC10D6", }, /* Fan for Panther Lake generation */ \
	{"INTC10FE", }, /* Fan for Wildcat Lake generation */ \
	{"INTC10F5", }, /* Fan for Nova Lake generation */ \
	{"PNP0C0B", } /* Generic ACPI fan */

#define ACPI_FPS_NAME_LEN	20

struct acpi_fan_fps {
	u64 control;
	u64 trip_point;
	u64 speed;
	u64 noise_level;
	u64 power;
	char name[ACPI_FPS_NAME_LEN];
	struct device_attribute dev_attr;
};

struct acpi_fan_fif {
	u8 revision;
	u8 fine_grain_ctrl;
	u8 step_size;
	u8 low_speed_notification;
};

struct acpi_fan_fst {
	u64 revision;
	u64 control;
	u64 speed;
};

struct acpi_fan {
	acpi_handle handle;
	bool acpi4;
	bool has_fst;
	struct acpi_fan_fif fif;
	struct acpi_fan_fps *fps;
	int fps_count;
	/* A value of 0 means that trippoint-related functions are not supported */
	u32 fan_trip_granularity;
#if IS_REACHABLE(CONFIG_HWMON)
	struct device *hdev;
#endif
	struct thermal_cooling_device *cdev;
	struct device_attribute fst_speed;
	struct device_attribute fine_grain_control;
};

/**
 * acpi_fan_speed_valid - Check if fan speed value is valid
 * @speeed: Speed value returned by the ACPI firmware
 *
 * Check if the fan speed value returned by the ACPI firmware is valid. This function is
 * necessary as ACPI firmware implementations can return 0xFFFFFFFF to signal that the
 * ACPI fan does not support speed reporting. Additionally, some buggy ACPI firmware
 * implementations return a value larger than the 32-bit integer value defined by
 * the ACPI specification when using placeholder values. Such invalid values are also
 * detected by this function.
 *
 * Returns: True if the fan speed value is valid, false otherwise.
 */
static inline bool acpi_fan_speed_valid(u64 speed)
{
	return speed < U32_MAX;
}

/**
 * acpi_fan_power_valid - Check if fan power value is valid
 * @power: Power value returned by the ACPI firmware
 *
 * Check if the fan power value returned by the ACPI firmware is valid.
 * See acpi_fan_speed_valid() for details.
 *
 * Returns: True if the fan power value is valid, false otherwise.
 */
static inline bool acpi_fan_power_valid(u64 power)
{
	return power < U32_MAX;
}

int acpi_fan_get_fst(acpi_handle handle, struct acpi_fan_fst *fst);
int acpi_fan_create_attributes(struct acpi_device *device);
void acpi_fan_delete_attributes(struct acpi_device *device);

#if IS_REACHABLE(CONFIG_HWMON)
int devm_acpi_fan_create_hwmon(struct device *dev);
void acpi_fan_notify_hwmon(struct device *dev);
#else
static inline int devm_acpi_fan_create_hwmon(struct device *dev) { return 0; };
static inline void acpi_fan_notify_hwmon(struct device *dev) { };
#endif

#endif
