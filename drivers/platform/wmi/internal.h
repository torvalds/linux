/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal interfaces used by the WMI core.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#ifndef _WMI_INTERNAL_H_
#define _WMI_INTERNAL_H_

union acpi_object;
struct wmi_buffer;

int wmi_unmarshal_acpi_object(const union acpi_object *obj, struct wmi_buffer *buffer);
int wmi_marshal_string(const struct wmi_buffer *buffer, struct acpi_buffer *out);

#endif /* _WMI_INTERNAL_H_ */
