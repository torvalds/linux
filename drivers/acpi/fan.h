/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * ACPI fan device IDs are shared between the fan driver and the device power
 * management code.
 *
 * Add new device IDs before the generic ACPI fan one.
 */
#define ACPI_FAN_DEVICE_IDS	\
	{"INT3404", }, /* Fan */ \
	{"INTC1044", }, /* Fan for Tiger Lake generation */ \
	{"INTC1048", }, /* Fan for Alder Lake generation */ \
	{"PNP0C0B", } /* Generic ACPI fan */
