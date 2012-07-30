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

struct atif_verify_interface {
	u16 size;		/* structure size in bytes (includes size field) */
	u16 version;		/* version */
	u32 notification_mask;	/* supported notifications mask */
	u32 function_bits;	/* supported functions bit vector */
} __packed;

struct atif_system_params {
	u16 size;
	u32 valid_mask;
	u32 flags;
	u8 command_code;
} __packed;

#define ATIF_NOTIFY_MASK	0x3
#define ATIF_NOTIFY_NONE	0
#define ATIF_NOTIFY_81		1
#define ATIF_NOTIFY_N		2

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

static void radeon_atif_parse_notification(struct radeon_atif_notifications *n, u32 mask)
{
	n->display_switch = mask & ATIF_DISPLAY_SWITCH_REQUEST_SUPPORTED;
	n->expansion_mode_change = mask & ATIF_EXPANSION_MODE_CHANGE_REQUEST_SUPPORTED;
	n->thermal_state = mask & ATIF_THERMAL_STATE_CHANGE_REQUEST_SUPPORTED;
	n->forced_power_state = mask & ATIF_FORCED_POWER_STATE_CHANGE_REQUEST_SUPPORTED;
	n->system_power_state = mask & ATIF_SYSTEM_POWER_SOURCE_CHANGE_REQUEST_SUPPORTED;
	n->display_conf_change = mask & ATIF_DISPLAY_CONF_CHANGE_REQUEST_SUPPORTED;
	n->px_gfx_switch = mask & ATIF_PX_GFX_SWITCH_REQUEST_SUPPORTED;
	n->brightness_change = mask & ATIF_PANEL_BRIGHTNESS_CHANGE_REQUEST_SUPPORTED;
	n->dgpu_display_event = mask & ATIF_DGPU_DISPLAY_EVENT_SUPPORTED;
}

static void radeon_atif_parse_functions(struct radeon_atif_functions *f, u32 mask)
{
	f->system_params = mask & ATIF_GET_SYSTEM_PARAMETERS_SUPPORTED;
	f->sbios_requests = mask & ATIF_GET_SYSTEM_BIOS_REQUESTS_SUPPORTED;
	f->select_active_disp = mask & ATIF_SELECT_ACTIVE_DISPLAYS_SUPPORTED;
	f->lid_state = mask & ATIF_GET_LID_STATE_SUPPORTED;
	f->get_tv_standard = mask & ATIF_GET_TV_STANDARD_FROM_CMOS_SUPPORTED;
	f->set_tv_standard = mask & ATIF_SET_TV_STANDARD_IN_CMOS_SUPPORTED;
	f->get_panel_expansion_mode = mask & ATIF_GET_PANEL_EXPANSION_MODE_FROM_CMOS_SUPPORTED;
	f->set_panel_expansion_mode = mask & ATIF_SET_PANEL_EXPANSION_MODE_IN_CMOS_SUPPORTED;
	f->temperature_change = mask & ATIF_TEMPERATURE_CHANGE_NOTIFICATION_SUPPORTED;
	f->graphics_device_types = mask & ATIF_GET_GRAPHICS_DEVICE_TYPES_SUPPORTED;
}

static int radeon_atif_verify_interface(acpi_handle handle,
		struct radeon_atif *atif)
{
	union acpi_object *info;
	struct atif_verify_interface output;
	size_t size;
	int err = 0;

	info = radeon_atif_call(handle, ATIF_FUNCTION_VERIFY_INTERFACE, NULL);
	if (!info)
		return -EIO;

	memset(&output, 0, sizeof(output));

	size = *(u16 *) info->buffer.pointer;
	if (size < 12) {
		DRM_INFO("ATIF buffer is too small: %lu\n", size);
		err = -EINVAL;
		goto out;
	}
	size = min(sizeof(output), size);

	memcpy(&output, info->buffer.pointer, size);

	/* TODO: check version? */
	DRM_DEBUG_DRIVER("ATIF version %u\n", output.version);

	radeon_atif_parse_notification(&atif->notifications, output.notification_mask);
	radeon_atif_parse_functions(&atif->functions, output.function_bits);

out:
	kfree(info);
	return err;
}

static int radeon_atif_get_notification_params(acpi_handle handle,
		struct radeon_atif_notification_cfg *n)
{
	union acpi_object *info;
	struct atif_system_params params;
	size_t size;
	int err = 0;

	info = radeon_atif_call(handle, ATIF_FUNCTION_GET_SYSTEM_PARAMETERS, NULL);
	if (!info) {
		err = -EIO;
		goto out;
	}

	size = *(u16 *) info->buffer.pointer;
	if (size < 10) {
		err = -EINVAL;
		goto out;
	}

	memset(&params, 0, sizeof(params));
	size = min(sizeof(params), size);
	memcpy(&params, info->buffer.pointer, size);

	params.flags = params.flags & params.valid_mask;

	if ((params.flags & ATIF_NOTIFY_MASK) == ATIF_NOTIFY_NONE) {
		n->enabled = false;
		n->command_code = 0;
	} else if ((params.flags & ATIF_NOTIFY_MASK) == ATIF_NOTIFY_81) {
		n->enabled = true;
		n->command_code = 0x81;
	} else {
		if (size < 11) {
			err = -EINVAL;
			goto out;
		}
		n->enabled = true;
		n->command_code = params.command_code;
	}

out:
	kfree(info);
	return err;
}

/* Call all ACPI methods here */
int radeon_acpi_init(struct radeon_device *rdev)
{
	acpi_handle handle;
	struct radeon_atif *atif = &rdev->atif;
	int ret;

	/* Get the device handle */
	handle = DEVICE_ACPI_HANDLE(&rdev->pdev->dev);

	/* No need to proceed if we're sure that ATIF is not supported */
	if (!ASIC_IS_AVIVO(rdev) || !rdev->bios || !handle)
		return 0;

	/* Call the ATIF method */
	ret = radeon_atif_verify_interface(handle, atif);
	if (ret) {
		DRM_DEBUG_DRIVER("Call to verify_interface failed: %d\n", ret);
		goto out;
	}

	if (atif->functions.sbios_requests && !atif->functions.system_params) {
		/* XXX check this workraround, if sbios request function is
		 * present we have to see how it's configured in the system
		 * params
		 */
		atif->functions.system_params = true;
	}

	if (atif->functions.system_params) {
		ret = radeon_atif_get_notification_params(handle,
				&atif->notification_cfg);
		if (ret) {
			DRM_DEBUG_DRIVER("Call to GET_SYSTEM_PARAMS failed: %d\n",
					ret);
			/* Disable notification */
			atif->notification_cfg.enabled = false;
		}
	}

out:
	return ret;
}

