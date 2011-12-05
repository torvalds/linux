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

#include <linux/vga_switcheroo.h>

/* Call the ATIF method
 *
 * Note: currently we discard the output
 */
static int radeon_atif_call(acpi_handle handle)
{
	acpi_status status;
	union acpi_object atif_arg_elements[2];
	struct acpi_object_list atif_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};

	atif_arg.count = 2;
	atif_arg.pointer = &atif_arg_elements[0];

	atif_arg_elements[0].type = ACPI_TYPE_INTEGER;
	atif_arg_elements[0].integer.value = 0;
	atif_arg_elements[1].type = ACPI_TYPE_INTEGER;
	atif_arg_elements[1].integer.value = 0;

	status = acpi_evaluate_object(handle, "ATIF", &atif_arg, &buffer);

	/* Fail only if calling the method fails and ATIF is supported */
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		DRM_DEBUG_DRIVER("failed to evaluate ATIF got %s\n",
				 acpi_format_exception(status));
		kfree(buffer.pointer);
		return 1;
	}

	kfree(buffer.pointer);
	return 0;
}

/* Call all ACPI methods here */
int radeon_acpi_init(struct radeon_device *rdev)
{
	acpi_handle handle;
	int ret;

	/* Get the device handle */
	handle = DEVICE_ACPI_HANDLE(&rdev->pdev->dev);

	/* No need to proceed if we're sure that ATIF is not supported */
	if (!ASIC_IS_AVIVO(rdev) || !rdev->bios || !handle)
		return 0;

	/* Call the ATIF method */
	ret = radeon_atif_call(handle);
	if (ret)
		return ret;

	return 0;
}

