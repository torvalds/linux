/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _I2C_HID_ACPI_H
#define _I2C_HID_ACPI_H

#include <linux/acpi.h>
#include <linux/uuid.h>

static inline int i2c_hid_acpi_get_descriptor(struct acpi_device *adev)
{
	/* HID I²C Device: 3cdff6f7-4267-4555-ad05-b30a3d8938de */
	static const guid_t i2c_hid_guid =
		GUID_INIT(0x3CDFF6F7, 0x4267, 0x4555,
			  0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE);

	acpi_handle handle = acpi_device_handle(adev);
	union acpi_object *obj;
	u16 addr;

	obj = acpi_evaluate_dsm_typed(handle, &i2c_hid_guid,
				      1, 1, NULL, ACPI_TYPE_INTEGER);
	if (!obj) {
		acpi_handle_err(handle,
				"Error _DSM call to get HID descriptor address failed\n");
		return -ENODEV;
	}

	addr = obj->integer.value;
	ACPI_FREE(obj);
	return addr;
}

#endif
