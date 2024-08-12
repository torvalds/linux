// SPDX-License-Identifier: GPL-2.0-only
/* IIO ACPI helper functions */

#include <linux/acpi.h>
#include <linux/dev_printk.h>
#include <linux/iio/iio.h>
#include <linux/sprintf.h>

/**
 * iio_read_acpi_mount_matrix() - Read accelerometer mount matrix info from ACPI
 * @dev:		Device structure
 * @orientation:	iio_mount_matrix struct to fill
 * @acpi_method:	ACPI method name to read the matrix from, usually "ROTM"
 *
 * Try to read the mount-matrix by calling the specified method on the device's
 * ACPI firmware-node. If the device has no ACPI firmware-node; or the method
 * does not exist then this will fail silently. This expects the method to
 * return data in the ACPI "ROTM" format defined by Microsoft:
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/sensors/sensors-acpi-entries
 * This is a Microsoft extension and not part of the official ACPI spec.
 * The method name is configurable because some dual-accel setups define 2 mount
 * matrices in a single ACPI device using separate "ROMK" and "ROMS" methods.
 *
 * Returns: true if the matrix was successfully, false otherwise.
 */
bool iio_read_acpi_mount_matrix(struct device *dev,
				struct iio_mount_matrix *orientation,
				char *acpi_method)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_device *adev = ACPI_COMPANION(dev);
	char *str;
	union acpi_object *obj, *elements;
	acpi_status status;
	int i, j, val[3];
	bool ret = false;

	if (!adev || !acpi_has_method(adev->handle, acpi_method))
		return false;

	status = acpi_evaluate_object(adev->handle, acpi_method, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Failed to get ACPI mount matrix: %d\n", status);
		return false;
	}

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_PACKAGE || obj->package.count != 3) {
		dev_err(dev, "Unknown ACPI mount matrix package format\n");
		goto out_free_buffer;
	}

	elements = obj->package.elements;
	for (i = 0; i < 3; i++) {
		if (elements[i].type != ACPI_TYPE_STRING) {
			dev_err(dev, "Unknown ACPI mount matrix element format\n");
			goto out_free_buffer;
		}

		str = elements[i].string.pointer;
		if (sscanf(str, "%d %d %d", &val[0], &val[1], &val[2]) != 3) {
			dev_err(dev, "Incorrect ACPI mount matrix string format\n");
			goto out_free_buffer;
		}

		for (j = 0; j < 3; j++) {
			switch (val[j]) {
			case -1: str = "-1"; break;
			case 0:  str = "0";  break;
			case 1:  str = "1";  break;
			default:
				dev_err(dev, "Invalid value in ACPI mount matrix: %d\n", val[j]);
				goto out_free_buffer;
			}
			orientation->rotation[i * 3 + j] = str;
		}
	}

	ret = true;

out_free_buffer:
	kfree(buffer.pointer);
	return ret;
}
EXPORT_SYMBOL_GPL(iio_read_acpi_mount_matrix);
