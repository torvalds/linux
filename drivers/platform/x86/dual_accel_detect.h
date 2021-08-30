/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Helper code to detect 360 degree hinges (yoga) style 2-in-1 devices using 2 accelerometers
 * to allow the OS to determine the angle between the display and the base of the device.
 *
 * On Windows these are read by a special HingeAngleService process which calls undocumented
 * ACPI methods, to let the firmware know if the 2-in-1 is in tablet- or laptop-mode.
 * The firmware may use this to disable the kbd and touchpad to avoid spurious input in
 * tablet-mode as well as to report SW_TABLET_MODE info to the OS.
 *
 * Since Linux does not call these undocumented methods, the SW_TABLET_MODE info reported
 * by various drivers/platform/x86 drivers is incorrect. These drivers use the detection
 * code in this file to disable SW_TABLET_MODE reporting to avoid reporting broken info
 * (instead userspace can derive the status itself by directly reading the 2 accels).
 */

#include <linux/acpi.h>
#include <linux/i2c.h>

static int dual_accel_i2c_resource_count(struct acpi_resource *ares, void *data)
{
	struct acpi_resource_i2c_serialbus *sb;
	int *count = data;

	if (i2c_acpi_get_i2c_resource(ares, &sb))
		*count = *count + 1;

	return 1;
}

static int dual_accel_i2c_client_count(struct acpi_device *adev)
{
	int ret, count = 0;
	LIST_HEAD(r);

	ret = acpi_dev_get_resources(adev, &r, dual_accel_i2c_resource_count, &count);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&r);
	return count;
}

static bool dual_accel_detect_bosc0200(void)
{
	struct acpi_device *adev;
	int count;

	adev = acpi_dev_get_first_match_dev("BOSC0200", NULL, -1);
	if (!adev)
		return false;

	count = dual_accel_i2c_client_count(adev);

	acpi_dev_put(adev);

	return count == 2;
}

static bool dual_accel_detect(void)
{
	/* Systems which use a pair of accels with KIOX010A / KIOX020A ACPI ids */
	if (acpi_dev_present("KIOX010A", NULL, -1) &&
	    acpi_dev_present("KIOX020A", NULL, -1))
		return true;

	/* Systems which use a single DUAL250E ACPI device to model 2 accels */
	if (acpi_dev_present("DUAL250E", NULL, -1))
		return true;

	/* Systems which use a single BOSC0200 ACPI device to model 2 accels */
	if (dual_accel_detect_bosc0200())
		return true;

	return false;
}
