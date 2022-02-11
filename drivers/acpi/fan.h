/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * ACPI fan device IDs are shared between the fan driver and the device power
 * management code.
 *
 * Add new device IDs before the generic ACPI fan one.
 */

#ifndef _ACPI_FAN_H_
#define _ACPI_FAN_H_

#define ACPI_FAN_DEVICE_IDS	\
	{"INT3404", }, /* Fan */ \
	{"INTC1044", }, /* Fan for Tiger Lake generation */ \
	{"INTC1048", }, /* Fan for Alder Lake generation */ \
	{"INTC10A2", }, /* Fan for Raptor Lake generation */ \
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
	u64 revision;
	u64 fine_grain_ctrl;
	u64 step_size;
	u64 low_speed_notification;
};

struct acpi_fan {
	bool acpi4;
	struct acpi_fan_fif fif;
	struct acpi_fan_fps *fps;
	int fps_count;
	struct thermal_cooling_device *cdev;
};

int acpi_fan_create_attributes(struct acpi_device *device);
void acpi_fan_delete_attributes(struct acpi_device *device);
#endif
