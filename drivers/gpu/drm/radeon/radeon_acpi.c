/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>

#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "drm_crtc_helper.h"
#include "radeon.h"
#include "radeon_acpi.h"

#include <linux/vga_switcheroo.h>

/* Call the ATIF method
 */
static union acpi_object *radeon_atif_call(acpi_handle handle, int function,
		struct acpi_buffer *params)
{
	acpi_status status;
	union acpi_object atif_arg_elements[2];
	struct acpi_object_list atif_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	atif_arg.count = 2;
	atif_arg.pointer = &atif_arg_elements[0];

	atif_arg_elements[0].type = ACPI_TYPE_INTEGER;
	atif_arg_elements[0].integer.value = function;

	if (params) {
		atif_arg_elements[1].type = ACPI_TYPE_BUFFER;
		atif_arg_elements[1].buffer.length = params->length;
		atif_arg_elements[1].buffer.pointer = params->pointer;
	} else {
		/* We need a second fake parameter */
		atif_arg_elements[1].type = ACPI_TYPE_INTEGER;
		atif_arg_elements[1].integer.value = 0;
	}

	status = acpi_evaluate_object(handle, "ATIF", &atif_arg, &buffer);

	/* Fail only if calling the method fails and ATIF is supported */
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		DRM_DEBUG_DRIVER("failed to evaluate ATIF got %s\n",
				 acpi_format_exception(status));
		kfree(buffer.pointer);
		return NULL;
	}

	return buffer.pointer;
}

/* Call all ACPI methods here */
int radeon_acpi_init(struct radeon_device *rdev)
{
	acpi_handle handle;
	union acpi_object *info;
	int ret = 0;

	/* Get the device handle */
	handle = DEVICE_ACPI_HANDLE(&rdev->pdev->dev);

	/* No need to proceed if we're sure that ATIF is not supported */
	if (!ASIC_IS_AVIVO(rdev) || !rdev->bios || !handle)
		return 0;

	/* Call the ATIF method */
	info = radeon_atif_call(handle, ATIF_FUNCTION_VERIFY_INTERFACE, NULL);
	if (!info)
		ret = -EIO;

	kfree(info);
	return ret;
}

