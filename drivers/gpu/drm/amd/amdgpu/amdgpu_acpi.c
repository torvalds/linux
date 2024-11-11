// SPDX-License-Identifier: MIT
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
#include <linux/backlight.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <linux/power_supply.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <acpi/video.h>
#include <acpi/actbl.h>

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_display.h"
#include "amd_acpi.h"
#include "atom.h"

/* Declare GUID for AMD _DSM method for XCCs */
static const guid_t amd_xcc_dsm_guid = GUID_INIT(0x8267f5d5, 0xa556, 0x44f2,
						 0xb8, 0xb4, 0x45, 0x56, 0x2e,
						 0x8c, 0x5b, 0xec);

#define AMD_XCC_HID_START 3000
#define AMD_XCC_DSM_GET_NUM_FUNCS 0
#define AMD_XCC_DSM_GET_SUPP_MODE 1
#define AMD_XCC_DSM_GET_XCP_MODE 2
#define AMD_XCC_DSM_GET_VF_XCC_MAPPING 4
#define AMD_XCC_DSM_GET_TMR_INFO 5
#define AMD_XCC_DSM_NUM_FUNCS 5

#define AMD_XCC_MAX_HID 24

struct xarray numa_info_xa;

/* Encapsulates the XCD acpi object information */
struct amdgpu_acpi_xcc_info {
	struct list_head list;
	struct amdgpu_numa_info *numa_info;
	uint8_t xcp_node;
	uint8_t phy_id;
	acpi_handle handle;
};

struct amdgpu_acpi_dev_info {
	struct list_head list;
	struct list_head xcc_list;
	uint32_t sbdf;
	uint16_t supp_xcp_mode;
	uint16_t xcp_mode;
	uint16_t mem_mode;
	uint64_t tmr_base;
	uint64_t tmr_size;
};

struct list_head amdgpu_acpi_dev_list;

struct amdgpu_atif_notification_cfg {
	bool enabled;
	int command_code;
};

struct amdgpu_atif_notifications {
	bool thermal_state;
	bool forced_power_state;
	bool system_power_state;
	bool brightness_change;
	bool dgpu_display_event;
	bool gpu_package_power_limit;
};

struct amdgpu_atif_functions {
	bool system_params;
	bool sbios_requests;
	bool temperature_change;
	bool query_backlight_transfer_characteristics;
	bool ready_to_undock;
	bool external_gpu_information;
};

struct amdgpu_atif {
	acpi_handle handle;

	struct amdgpu_atif_notifications notifications;
	struct amdgpu_atif_functions functions;
	struct amdgpu_atif_notification_cfg notification_cfg;
	struct backlight_device *bd;
	struct amdgpu_dm_backlight_caps backlight_caps;
};

struct amdgpu_atcs_functions {
	bool get_ext_state;
	bool pcie_perf_req;
	bool pcie_dev_rdy;
	bool pcie_bus_width;
	bool power_shift_control;
};

struct amdgpu_atcs {
	acpi_handle handle;

	struct amdgpu_atcs_functions functions;
};

static struct amdgpu_acpi_priv {
	struct amdgpu_atif atif;
	struct amdgpu_atcs atcs;
} amdgpu_acpi_priv;

/* Call the ATIF method
 */
/**
 * amdgpu_atif_call - call an ATIF method
 *
 * @atif: atif structure
 * @function: the ATIF function to execute
 * @params: ATIF function params
 *
 * Executes the requested ATIF function (all asics).
 * Returns a pointer to the acpi output buffer.
 */
static union acpi_object *amdgpu_atif_call(struct amdgpu_atif *atif,
					   int function,
					   struct acpi_buffer *params)
{
	acpi_status status;
	union acpi_object *obj;
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

	status = acpi_evaluate_object(atif->handle, NULL, &atif_arg,
				      &buffer);
	obj = (union acpi_object *)buffer.pointer;

	/* Fail if calling the method fails */
	if (ACPI_FAILURE(status)) {
		DRM_DEBUG_DRIVER("failed to evaluate ATIF got %s\n",
				 acpi_format_exception(status));
		kfree(obj);
		return NULL;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		DRM_DEBUG_DRIVER("bad object returned from ATIF: %d\n",
				 obj->type);
		kfree(obj);
		return NULL;
	}

	return obj;
}

/**
 * amdgpu_atif_parse_notification - parse supported notifications
 *
 * @n: supported notifications struct
 * @mask: supported notifications mask from ATIF
 *
 * Use the supported notifications mask from ATIF function
 * ATIF_FUNCTION_VERIFY_INTERFACE to determine what notifications
 * are supported (all asics).
 */
static void amdgpu_atif_parse_notification(struct amdgpu_atif_notifications *n, u32 mask)
{
	n->thermal_state = mask & ATIF_THERMAL_STATE_CHANGE_REQUEST_SUPPORTED;
	n->forced_power_state = mask & ATIF_FORCED_POWER_STATE_CHANGE_REQUEST_SUPPORTED;
	n->system_power_state = mask & ATIF_SYSTEM_POWER_SOURCE_CHANGE_REQUEST_SUPPORTED;
	n->brightness_change = mask & ATIF_PANEL_BRIGHTNESS_CHANGE_REQUEST_SUPPORTED;
	n->dgpu_display_event = mask & ATIF_DGPU_DISPLAY_EVENT_SUPPORTED;
	n->gpu_package_power_limit = mask & ATIF_GPU_PACKAGE_POWER_LIMIT_REQUEST_SUPPORTED;
}

/**
 * amdgpu_atif_parse_functions - parse supported functions
 *
 * @f: supported functions struct
 * @mask: supported functions mask from ATIF
 *
 * Use the supported functions mask from ATIF function
 * ATIF_FUNCTION_VERIFY_INTERFACE to determine what functions
 * are supported (all asics).
 */
static void amdgpu_atif_parse_functions(struct amdgpu_atif_functions *f, u32 mask)
{
	f->system_params = mask & ATIF_GET_SYSTEM_PARAMETERS_SUPPORTED;
	f->sbios_requests = mask & ATIF_GET_SYSTEM_BIOS_REQUESTS_SUPPORTED;
	f->temperature_change = mask & ATIF_TEMPERATURE_CHANGE_NOTIFICATION_SUPPORTED;
	f->query_backlight_transfer_characteristics =
		mask & ATIF_QUERY_BACKLIGHT_TRANSFER_CHARACTERISTICS_SUPPORTED;
	f->ready_to_undock = mask & ATIF_READY_TO_UNDOCK_NOTIFICATION_SUPPORTED;
	f->external_gpu_information = mask & ATIF_GET_EXTERNAL_GPU_INFORMATION_SUPPORTED;
}

/**
 * amdgpu_atif_verify_interface - verify ATIF
 *
 * @atif: amdgpu atif struct
 *
 * Execute the ATIF_FUNCTION_VERIFY_INTERFACE ATIF function
 * to initialize ATIF and determine what features are supported
 * (all asics).
 * returns 0 on success, error on failure.
 */
static int amdgpu_atif_verify_interface(struct amdgpu_atif *atif)
{
	union acpi_object *info;
	struct atif_verify_interface output;
	size_t size;
	int err = 0;

	info = amdgpu_atif_call(atif, ATIF_FUNCTION_VERIFY_INTERFACE, NULL);
	if (!info)
		return -EIO;

	memset(&output, 0, sizeof(output));

	size = *(u16 *) info->buffer.pointer;
	if (size < 12) {
		DRM_INFO("ATIF buffer is too small: %zu\n", size);
		err = -EINVAL;
		goto out;
	}
	size = min(sizeof(output), size);

	memcpy(&output, info->buffer.pointer, size);

	/* TODO: check version? */
	DRM_DEBUG_DRIVER("ATIF version %u\n", output.version);

	amdgpu_atif_parse_notification(&atif->notifications, output.notification_mask);
	amdgpu_atif_parse_functions(&atif->functions, output.function_bits);

out:
	kfree(info);
	return err;
}

/**
 * amdgpu_atif_get_notification_params - determine notify configuration
 *
 * @atif: acpi handle
 *
 * Execute the ATIF_FUNCTION_GET_SYSTEM_PARAMETERS ATIF function
 * to determine if a notifier is used and if so which one
 * (all asics).  This is either Notify(VGA, 0x81) or Notify(VGA, n)
 * where n is specified in the result if a notifier is used.
 * Returns 0 on success, error on failure.
 */
static int amdgpu_atif_get_notification_params(struct amdgpu_atif *atif)
{
	union acpi_object *info;
	struct amdgpu_atif_notification_cfg *n = &atif->notification_cfg;
	struct atif_system_params params;
	size_t size;
	int err = 0;

	info = amdgpu_atif_call(atif, ATIF_FUNCTION_GET_SYSTEM_PARAMETERS,
				NULL);
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

	DRM_DEBUG_DRIVER("SYSTEM_PARAMS: mask = %#x, flags = %#x\n",
			params.flags, params.valid_mask);
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
	DRM_DEBUG_DRIVER("Notification %s, command code = %#x\n",
			(n->enabled ? "enabled" : "disabled"),
			n->command_code);
	kfree(info);
	return err;
}

/**
 * amdgpu_atif_query_backlight_caps - get min and max backlight input signal
 *
 * @atif: acpi handle
 *
 * Execute the QUERY_BRIGHTNESS_TRANSFER_CHARACTERISTICS ATIF function
 * to determine the acceptable range of backlight values
 *
 * Backlight_caps.caps_valid will be set to true if the query is successful
 *
 * The input signals are in range 0-255
 *
 * This function assumes the display with backlight is the first LCD
 *
 * Returns 0 on success, error on failure.
 */
static int amdgpu_atif_query_backlight_caps(struct amdgpu_atif *atif)
{
	union acpi_object *info;
	struct atif_qbtc_output characteristics;
	struct atif_qbtc_arguments arguments;
	struct acpi_buffer params;
	size_t size;
	int err = 0;

	arguments.size = sizeof(arguments);
	arguments.requested_display = ATIF_QBTC_REQUEST_LCD1;

	params.length = sizeof(arguments);
	params.pointer = (void *)&arguments;

	info = amdgpu_atif_call(atif,
		ATIF_FUNCTION_QUERY_BRIGHTNESS_TRANSFER_CHARACTERISTICS,
		&params);
	if (!info) {
		err = -EIO;
		goto out;
	}

	size = *(u16 *) info->buffer.pointer;
	if (size < 10) {
		err = -EINVAL;
		goto out;
	}

	memset(&characteristics, 0, sizeof(characteristics));
	size = min(sizeof(characteristics), size);
	memcpy(&characteristics, info->buffer.pointer, size);

	atif->backlight_caps.caps_valid = true;
	atif->backlight_caps.min_input_signal =
			characteristics.min_input_signal;
	atif->backlight_caps.max_input_signal =
			characteristics.max_input_signal;
	atif->backlight_caps.ac_level = characteristics.ac_level;
	atif->backlight_caps.dc_level = characteristics.dc_level;
out:
	kfree(info);
	return err;
}

/**
 * amdgpu_atif_get_sbios_requests - get requested sbios event
 *
 * @atif: acpi handle
 * @req: atif sbios request struct
 *
 * Execute the ATIF_FUNCTION_GET_SYSTEM_BIOS_REQUESTS ATIF function
 * to determine what requests the sbios is making to the driver
 * (all asics).
 * Returns 0 on success, error on failure.
 */
static int amdgpu_atif_get_sbios_requests(struct amdgpu_atif *atif,
					  struct atif_sbios_requests *req)
{
	union acpi_object *info;
	size_t size;
	int count = 0;

	info = amdgpu_atif_call(atif, ATIF_FUNCTION_GET_SYSTEM_BIOS_REQUESTS,
				NULL);
	if (!info)
		return -EIO;

	size = *(u16 *)info->buffer.pointer;
	if (size < 0xd) {
		count = -EINVAL;
		goto out;
	}
	memset(req, 0, sizeof(*req));

	size = min(sizeof(*req), size);
	memcpy(req, info->buffer.pointer, size);
	DRM_DEBUG_DRIVER("SBIOS pending requests: %#x\n", req->pending);

	count = hweight32(req->pending);

out:
	kfree(info);
	return count;
}

/**
 * amdgpu_atif_handler - handle ATIF notify requests
 *
 * @adev: amdgpu_device pointer
 * @event: atif sbios request struct
 *
 * Checks the acpi event and if it matches an atif event,
 * handles it.
 *
 * Returns:
 * NOTIFY_BAD or NOTIFY_DONE, depending on the event.
 */
static int amdgpu_atif_handler(struct amdgpu_device *adev,
			       struct acpi_bus_event *event)
{
	struct amdgpu_atif *atif = &amdgpu_acpi_priv.atif;
	int count;

	DRM_DEBUG_DRIVER("event, device_class = %s, type = %#x\n",
			event->device_class, event->type);

	if (strcmp(event->device_class, ACPI_VIDEO_CLASS) != 0)
		return NOTIFY_DONE;

	/* Is this actually our event? */
	if (!atif->notification_cfg.enabled ||
	    event->type != atif->notification_cfg.command_code) {
		/* These events will generate keypresses otherwise */
		if (event->type == ACPI_VIDEO_NOTIFY_PROBE)
			return NOTIFY_BAD;
		else
			return NOTIFY_DONE;
	}

	if (atif->functions.sbios_requests) {
		struct atif_sbios_requests req;

		/* Check pending SBIOS requests */
		count = amdgpu_atif_get_sbios_requests(atif, &req);

		if (count <= 0)
			return NOTIFY_BAD;

		DRM_DEBUG_DRIVER("ATIF: %d pending SBIOS requests\n", count);

		if (req.pending & ATIF_PANEL_BRIGHTNESS_CHANGE_REQUEST) {
			if (atif->bd) {
				DRM_DEBUG_DRIVER("Changing brightness to %d\n",
						 req.backlight_level);
				/*
				 * XXX backlight_device_set_brightness() is
				 * hardwired to post BACKLIGHT_UPDATE_SYSFS.
				 * It probably should accept 'reason' parameter.
				 */
				backlight_device_set_brightness(atif->bd, req.backlight_level);
			}
		}

		if (req.pending & ATIF_DGPU_DISPLAY_EVENT) {
			if (adev->flags & AMD_IS_PX) {
				pm_runtime_get_sync(adev_to_drm(adev)->dev);
				/* Just fire off a uevent and let userspace tell us what to do */
				drm_helper_hpd_irq_event(adev_to_drm(adev));
				pm_runtime_mark_last_busy(adev_to_drm(adev)->dev);
				pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
			}
		}
		/* TODO: check other events */
	}

	/* We've handled the event, stop the notifier chain. The ACPI interface
	 * overloads ACPI_VIDEO_NOTIFY_PROBE, we don't want to send that to
	 * userspace if the event was generated only to signal a SBIOS
	 * request.
	 */
	return NOTIFY_BAD;
}

/* Call the ATCS method
 */
/**
 * amdgpu_atcs_call - call an ATCS method
 *
 * @atcs: atcs structure
 * @function: the ATCS function to execute
 * @params: ATCS function params
 *
 * Executes the requested ATCS function (all asics).
 * Returns a pointer to the acpi output buffer.
 */
static union acpi_object *amdgpu_atcs_call(struct amdgpu_atcs *atcs,
					   int function,
					   struct acpi_buffer *params)
{
	acpi_status status;
	union acpi_object atcs_arg_elements[2];
	struct acpi_object_list atcs_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	atcs_arg.count = 2;
	atcs_arg.pointer = &atcs_arg_elements[0];

	atcs_arg_elements[0].type = ACPI_TYPE_INTEGER;
	atcs_arg_elements[0].integer.value = function;

	if (params) {
		atcs_arg_elements[1].type = ACPI_TYPE_BUFFER;
		atcs_arg_elements[1].buffer.length = params->length;
		atcs_arg_elements[1].buffer.pointer = params->pointer;
	} else {
		/* We need a second fake parameter */
		atcs_arg_elements[1].type = ACPI_TYPE_INTEGER;
		atcs_arg_elements[1].integer.value = 0;
	}

	status = acpi_evaluate_object(atcs->handle, NULL, &atcs_arg, &buffer);

	/* Fail only if calling the method fails and ATIF is supported */
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		DRM_DEBUG_DRIVER("failed to evaluate ATCS got %s\n",
				 acpi_format_exception(status));
		kfree(buffer.pointer);
		return NULL;
	}

	return buffer.pointer;
}

/**
 * amdgpu_atcs_parse_functions - parse supported functions
 *
 * @f: supported functions struct
 * @mask: supported functions mask from ATCS
 *
 * Use the supported functions mask from ATCS function
 * ATCS_FUNCTION_VERIFY_INTERFACE to determine what functions
 * are supported (all asics).
 */
static void amdgpu_atcs_parse_functions(struct amdgpu_atcs_functions *f, u32 mask)
{
	f->get_ext_state = mask & ATCS_GET_EXTERNAL_STATE_SUPPORTED;
	f->pcie_perf_req = mask & ATCS_PCIE_PERFORMANCE_REQUEST_SUPPORTED;
	f->pcie_dev_rdy = mask & ATCS_PCIE_DEVICE_READY_NOTIFICATION_SUPPORTED;
	f->pcie_bus_width = mask & ATCS_SET_PCIE_BUS_WIDTH_SUPPORTED;
	f->power_shift_control = mask & ATCS_SET_POWER_SHIFT_CONTROL_SUPPORTED;
}

/**
 * amdgpu_atcs_verify_interface - verify ATCS
 *
 * @atcs: amdgpu atcs struct
 *
 * Execute the ATCS_FUNCTION_VERIFY_INTERFACE ATCS function
 * to initialize ATCS and determine what features are supported
 * (all asics).
 * returns 0 on success, error on failure.
 */
static int amdgpu_atcs_verify_interface(struct amdgpu_atcs *atcs)
{
	union acpi_object *info;
	struct atcs_verify_interface output;
	size_t size;
	int err = 0;

	info = amdgpu_atcs_call(atcs, ATCS_FUNCTION_VERIFY_INTERFACE, NULL);
	if (!info)
		return -EIO;

	memset(&output, 0, sizeof(output));

	size = *(u16 *) info->buffer.pointer;
	if (size < 8) {
		DRM_INFO("ATCS buffer is too small: %zu\n", size);
		err = -EINVAL;
		goto out;
	}
	size = min(sizeof(output), size);

	memcpy(&output, info->buffer.pointer, size);

	/* TODO: check version? */
	DRM_DEBUG_DRIVER("ATCS version %u\n", output.version);

	amdgpu_atcs_parse_functions(&atcs->functions, output.function_bits);

out:
	kfree(info);
	return err;
}

/**
 * amdgpu_acpi_is_pcie_performance_request_supported
 *
 * @adev: amdgpu_device pointer
 *
 * Check if the ATCS pcie_perf_req and pcie_dev_rdy methods
 * are supported (all asics).
 * returns true if supported, false if not.
 */
bool amdgpu_acpi_is_pcie_performance_request_supported(struct amdgpu_device *adev)
{
	struct amdgpu_atcs *atcs = &amdgpu_acpi_priv.atcs;

	if (atcs->functions.pcie_perf_req && atcs->functions.pcie_dev_rdy)
		return true;

	return false;
}

/**
 * amdgpu_acpi_is_power_shift_control_supported
 *
 * Check if the ATCS power shift control method
 * is supported.
 * returns true if supported, false if not.
 */
bool amdgpu_acpi_is_power_shift_control_supported(void)
{
	return amdgpu_acpi_priv.atcs.functions.power_shift_control;
}

/**
 * amdgpu_acpi_pcie_notify_device_ready
 *
 * @adev: amdgpu_device pointer
 *
 * Executes the PCIE_DEVICE_READY_NOTIFICATION method
 * (all asics).
 * returns 0 on success, error on failure.
 */
int amdgpu_acpi_pcie_notify_device_ready(struct amdgpu_device *adev)
{
	union acpi_object *info;
	struct amdgpu_atcs *atcs = &amdgpu_acpi_priv.atcs;

	if (!atcs->functions.pcie_dev_rdy)
		return -EINVAL;

	info = amdgpu_atcs_call(atcs, ATCS_FUNCTION_PCIE_DEVICE_READY_NOTIFICATION, NULL);
	if (!info)
		return -EIO;

	kfree(info);

	return 0;
}

/**
 * amdgpu_acpi_pcie_performance_request
 *
 * @adev: amdgpu_device pointer
 * @perf_req: requested perf level (pcie gen speed)
 * @advertise: set advertise caps flag if set
 *
 * Executes the PCIE_PERFORMANCE_REQUEST method to
 * change the pcie gen speed (all asics).
 * returns 0 on success, error on failure.
 */
int amdgpu_acpi_pcie_performance_request(struct amdgpu_device *adev,
					 u8 perf_req, bool advertise)
{
	union acpi_object *info;
	struct amdgpu_atcs *atcs = &amdgpu_acpi_priv.atcs;
	struct atcs_pref_req_input atcs_input;
	struct atcs_pref_req_output atcs_output;
	struct acpi_buffer params;
	size_t size;
	u32 retry = 3;

	if (amdgpu_acpi_pcie_notify_device_ready(adev))
		return -EINVAL;

	if (!atcs->functions.pcie_perf_req)
		return -EINVAL;

	atcs_input.size = sizeof(struct atcs_pref_req_input);
	/* client id (bit 2-0: func num, 7-3: dev num, 15-8: bus num) */
	atcs_input.client_id = pci_dev_id(adev->pdev);
	atcs_input.valid_flags_mask = ATCS_VALID_FLAGS_MASK;
	atcs_input.flags = ATCS_WAIT_FOR_COMPLETION;
	if (advertise)
		atcs_input.flags |= ATCS_ADVERTISE_CAPS;
	atcs_input.req_type = ATCS_PCIE_LINK_SPEED;
	atcs_input.perf_req = perf_req;

	params.length = sizeof(struct atcs_pref_req_input);
	params.pointer = &atcs_input;

	while (retry--) {
		info = amdgpu_atcs_call(atcs, ATCS_FUNCTION_PCIE_PERFORMANCE_REQUEST, &params);
		if (!info)
			return -EIO;

		memset(&atcs_output, 0, sizeof(atcs_output));

		size = *(u16 *) info->buffer.pointer;
		if (size < 3) {
			DRM_INFO("ATCS buffer is too small: %zu\n", size);
			kfree(info);
			return -EINVAL;
		}
		size = min(sizeof(atcs_output), size);

		memcpy(&atcs_output, info->buffer.pointer, size);

		kfree(info);

		switch (atcs_output.ret_val) {
		case ATCS_REQUEST_REFUSED:
		default:
			return -EINVAL;
		case ATCS_REQUEST_COMPLETE:
			return 0;
		case ATCS_REQUEST_IN_PROGRESS:
			udelay(10);
			break;
		}
	}

	return 0;
}

/**
 * amdgpu_acpi_power_shift_control
 *
 * @adev: amdgpu_device pointer
 * @dev_state: device acpi state
 * @drv_state: driver state
 *
 * Executes the POWER_SHIFT_CONTROL method to
 * communicate current dGPU device state and
 * driver state to APU/SBIOS.
 * returns 0 on success, error on failure.
 */
int amdgpu_acpi_power_shift_control(struct amdgpu_device *adev,
				    u8 dev_state, bool drv_state)
{
	union acpi_object *info;
	struct amdgpu_atcs *atcs = &amdgpu_acpi_priv.atcs;
	struct atcs_pwr_shift_input atcs_input;
	struct acpi_buffer params;

	if (!amdgpu_acpi_is_power_shift_control_supported())
		return -EINVAL;

	atcs_input.size = sizeof(struct atcs_pwr_shift_input);
	/* dGPU id (bit 2-0: func num, 7-3: dev num, 15-8: bus num) */
	atcs_input.dgpu_id = pci_dev_id(adev->pdev);
	atcs_input.dev_acpi_state = dev_state;
	atcs_input.drv_state = drv_state;

	params.length = sizeof(struct atcs_pwr_shift_input);
	params.pointer = &atcs_input;

	info = amdgpu_atcs_call(atcs, ATCS_FUNCTION_POWER_SHIFT_CONTROL, &params);
	if (!info) {
		DRM_ERROR("ATCS PSC update failed\n");
		return -EIO;
	}

	return 0;
}

/**
 * amdgpu_acpi_smart_shift_update - update dGPU device state to SBIOS
 *
 * @dev: drm_device pointer
 * @ss_state: current smart shift event
 *
 * returns 0 on success,
 * otherwise return error number.
 */
int amdgpu_acpi_smart_shift_update(struct drm_device *dev, enum amdgpu_ss ss_state)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	int r;

	if (!amdgpu_device_supports_smart_shift(dev))
		return 0;

	switch (ss_state) {
	/* SBIOS trigger “stop”, “enable” and “start” at D0, Driver Operational.
	 * SBIOS trigger “stop” at D3, Driver Not Operational.
	 * SBIOS trigger “stop” and “disable” at D0, Driver NOT operational.
	 */
	case AMDGPU_SS_DRV_LOAD:
		r = amdgpu_acpi_power_shift_control(adev,
						    AMDGPU_ATCS_PSC_DEV_STATE_D0,
						    AMDGPU_ATCS_PSC_DRV_STATE_OPR);
		break;
	case AMDGPU_SS_DEV_D0:
		r = amdgpu_acpi_power_shift_control(adev,
						    AMDGPU_ATCS_PSC_DEV_STATE_D0,
						    AMDGPU_ATCS_PSC_DRV_STATE_OPR);
		break;
	case AMDGPU_SS_DEV_D3:
		r = amdgpu_acpi_power_shift_control(adev,
						    AMDGPU_ATCS_PSC_DEV_STATE_D3_HOT,
						    AMDGPU_ATCS_PSC_DRV_STATE_NOT_OPR);
		break;
	case AMDGPU_SS_DRV_UNLOAD:
		r = amdgpu_acpi_power_shift_control(adev,
						    AMDGPU_ATCS_PSC_DEV_STATE_D0,
						    AMDGPU_ATCS_PSC_DRV_STATE_NOT_OPR);
		break;
	default:
		return -EINVAL;
	}

	return r;
}

#ifdef CONFIG_ACPI_NUMA
static inline uint64_t amdgpu_acpi_get_numa_size(int nid)
{
	/* This is directly using si_meminfo_node implementation as the
	 * function is not exported.
	 */
	int zone_type;
	uint64_t managed_pages = 0;

	pg_data_t *pgdat = NODE_DATA(nid);

	for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++)
		managed_pages +=
			zone_managed_pages(&pgdat->node_zones[zone_type]);
	return managed_pages * PAGE_SIZE;
}

static struct amdgpu_numa_info *amdgpu_acpi_get_numa_info(uint32_t pxm)
{
	struct amdgpu_numa_info *numa_info;
	int nid;

	numa_info = xa_load(&numa_info_xa, pxm);

	if (!numa_info) {
		struct sysinfo info;

		numa_info = kzalloc(sizeof(*numa_info), GFP_KERNEL);
		if (!numa_info)
			return NULL;

		nid = pxm_to_node(pxm);
		numa_info->pxm = pxm;
		numa_info->nid = nid;

		if (numa_info->nid == NUMA_NO_NODE) {
			si_meminfo(&info);
			numa_info->size = info.totalram * info.mem_unit;
		} else {
			numa_info->size = amdgpu_acpi_get_numa_size(nid);
		}
		xa_store(&numa_info_xa, numa_info->pxm, numa_info, GFP_KERNEL);
	}

	return numa_info;
}
#endif

/**
 * amdgpu_acpi_get_node_id - obtain the NUMA node id for corresponding amdgpu
 * acpi device handle
 *
 * @handle: acpi handle
 * @numa_info: amdgpu_numa_info structure holding numa information
 *
 * Queries the ACPI interface to fetch the corresponding NUMA Node ID for a
 * given amdgpu acpi device.
 *
 * Returns ACPI STATUS OK with Node ID on success or the corresponding failure reason
 */
static acpi_status amdgpu_acpi_get_node_id(acpi_handle handle,
				    struct amdgpu_numa_info **numa_info)
{
#ifdef CONFIG_ACPI_NUMA
	u64 pxm;
	acpi_status status;

	if (!numa_info)
		return_ACPI_STATUS(AE_ERROR);

	status = acpi_evaluate_integer(handle, "_PXM", NULL, &pxm);

	if (ACPI_FAILURE(status))
		return status;

	*numa_info = amdgpu_acpi_get_numa_info(pxm);

	if (!*numa_info)
		return_ACPI_STATUS(AE_ERROR);

	return_ACPI_STATUS(AE_OK);
#else
	return_ACPI_STATUS(AE_NOT_EXIST);
#endif
}

static struct amdgpu_acpi_dev_info *amdgpu_acpi_get_dev(u32 sbdf)
{
	struct amdgpu_acpi_dev_info *acpi_dev;

	if (list_empty(&amdgpu_acpi_dev_list))
		return NULL;

	list_for_each_entry(acpi_dev, &amdgpu_acpi_dev_list, list)
		if (acpi_dev->sbdf == sbdf)
			return acpi_dev;

	return NULL;
}

static int amdgpu_acpi_dev_init(struct amdgpu_acpi_dev_info **dev_info,
				struct amdgpu_acpi_xcc_info *xcc_info, u32 sbdf)
{
	struct amdgpu_acpi_dev_info *tmp;
	union acpi_object *obj;
	int ret = -ENOENT;

	*dev_info = NULL;
	tmp = kzalloc(sizeof(struct amdgpu_acpi_dev_info), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	INIT_LIST_HEAD(&tmp->xcc_list);
	INIT_LIST_HEAD(&tmp->list);
	tmp->sbdf = sbdf;

	obj = acpi_evaluate_dsm_typed(xcc_info->handle, &amd_xcc_dsm_guid, 0,
				      AMD_XCC_DSM_GET_SUPP_MODE, NULL,
				      ACPI_TYPE_INTEGER);

	if (!obj) {
		acpi_handle_debug(xcc_info->handle,
				  "_DSM function %d evaluation failed",
				  AMD_XCC_DSM_GET_SUPP_MODE);
		ret = -ENOENT;
		goto out;
	}

	tmp->supp_xcp_mode = obj->integer.value & 0xFFFF;
	ACPI_FREE(obj);

	obj = acpi_evaluate_dsm_typed(xcc_info->handle, &amd_xcc_dsm_guid, 0,
				      AMD_XCC_DSM_GET_XCP_MODE, NULL,
				      ACPI_TYPE_INTEGER);

	if (!obj) {
		acpi_handle_debug(xcc_info->handle,
				  "_DSM function %d evaluation failed",
				  AMD_XCC_DSM_GET_XCP_MODE);
		ret = -ENOENT;
		goto out;
	}

	tmp->xcp_mode = obj->integer.value & 0xFFFF;
	tmp->mem_mode = (obj->integer.value >> 32) & 0xFFFF;
	ACPI_FREE(obj);

	/* Evaluate DSMs and fill XCC information */
	obj = acpi_evaluate_dsm_typed(xcc_info->handle, &amd_xcc_dsm_guid, 0,
				      AMD_XCC_DSM_GET_TMR_INFO, NULL,
				      ACPI_TYPE_PACKAGE);

	if (!obj || obj->package.count < 2) {
		acpi_handle_debug(xcc_info->handle,
				  "_DSM function %d evaluation failed",
				  AMD_XCC_DSM_GET_TMR_INFO);
		ret = -ENOENT;
		goto out;
	}

	tmp->tmr_base = obj->package.elements[0].integer.value;
	tmp->tmr_size = obj->package.elements[1].integer.value;
	ACPI_FREE(obj);

	DRM_DEBUG_DRIVER(
		"New dev(%x): Supported xcp mode: %x curr xcp_mode : %x mem mode : %x, tmr base: %llx tmr size: %llx  ",
		tmp->sbdf, tmp->supp_xcp_mode, tmp->xcp_mode, tmp->mem_mode,
		tmp->tmr_base, tmp->tmr_size);
	list_add_tail(&tmp->list, &amdgpu_acpi_dev_list);
	*dev_info = tmp;

	return 0;

out:
	if (obj)
		ACPI_FREE(obj);
	kfree(tmp);

	return ret;
}

static int amdgpu_acpi_get_xcc_info(struct amdgpu_acpi_xcc_info *xcc_info,
				    u32 *sbdf)
{
	union acpi_object *obj;
	acpi_status status;
	int ret = -ENOENT;

	obj = acpi_evaluate_dsm_typed(xcc_info->handle, &amd_xcc_dsm_guid, 0,
				      AMD_XCC_DSM_GET_NUM_FUNCS, NULL,
				      ACPI_TYPE_INTEGER);

	if (!obj || obj->integer.value != AMD_XCC_DSM_NUM_FUNCS)
		goto out;
	ACPI_FREE(obj);

	/* Evaluate DSMs and fill XCC information */
	obj = acpi_evaluate_dsm_typed(xcc_info->handle, &amd_xcc_dsm_guid, 0,
				      AMD_XCC_DSM_GET_VF_XCC_MAPPING, NULL,
				      ACPI_TYPE_INTEGER);

	if (!obj) {
		acpi_handle_debug(xcc_info->handle,
				  "_DSM function %d evaluation failed",
				  AMD_XCC_DSM_GET_VF_XCC_MAPPING);
		ret = -EINVAL;
		goto out;
	}

	/* PF xcc id [39:32] */
	xcc_info->phy_id = (obj->integer.value >> 32) & 0xFF;
	/* xcp node of this xcc [47:40] */
	xcc_info->xcp_node = (obj->integer.value >> 40) & 0xFF;
	/* PF domain of this xcc [31:16] */
	*sbdf = (obj->integer.value) & 0xFFFF0000;
	/* PF bus/dev/fn of this xcc [63:48] */
	*sbdf |= (obj->integer.value >> 48) & 0xFFFF;
	ACPI_FREE(obj);
	obj = NULL;

	status =
		amdgpu_acpi_get_node_id(xcc_info->handle, &xcc_info->numa_info);

	/* TODO: check if this check is required */
	if (ACPI_SUCCESS(status))
		ret = 0;
out:
	if (obj)
		ACPI_FREE(obj);

	return ret;
}

static int amdgpu_acpi_enumerate_xcc(void)
{
	struct amdgpu_acpi_dev_info *dev_info = NULL;
	struct amdgpu_acpi_xcc_info *xcc_info;
	struct acpi_device *acpi_dev;
	char hid[ACPI_ID_LEN];
	int ret, id;
	u32 sbdf;

	INIT_LIST_HEAD(&amdgpu_acpi_dev_list);
	xa_init(&numa_info_xa);

	for (id = 0; id < AMD_XCC_MAX_HID; id++) {
		sprintf(hid, "%s%d", "AMD", AMD_XCC_HID_START + id);
		acpi_dev = acpi_dev_get_first_match_dev(hid, NULL, -1);
		/* These ACPI objects are expected to be in sequential order. If
		 * one is not found, no need to check the rest.
		 */
		if (!acpi_dev) {
			DRM_DEBUG_DRIVER("No matching acpi device found for %s",
					 hid);
			break;
		}

		xcc_info = kzalloc(sizeof(struct amdgpu_acpi_xcc_info),
				   GFP_KERNEL);
		if (!xcc_info) {
			DRM_ERROR("Failed to allocate memory for xcc info\n");
			return -ENOMEM;
		}

		INIT_LIST_HEAD(&xcc_info->list);
		xcc_info->handle = acpi_device_handle(acpi_dev);
		acpi_dev_put(acpi_dev);

		ret = amdgpu_acpi_get_xcc_info(xcc_info, &sbdf);
		if (ret) {
			kfree(xcc_info);
			continue;
		}

		dev_info = amdgpu_acpi_get_dev(sbdf);

		if (!dev_info)
			ret = amdgpu_acpi_dev_init(&dev_info, xcc_info, sbdf);

		if (ret == -ENOMEM)
			return ret;

		if (!dev_info) {
			kfree(xcc_info);
			continue;
		}

		list_add_tail(&xcc_info->list, &dev_info->xcc_list);
	}

	return 0;
}

int amdgpu_acpi_get_tmr_info(struct amdgpu_device *adev, u64 *tmr_offset,
			     u64 *tmr_size)
{
	struct amdgpu_acpi_dev_info *dev_info;
	u32 sbdf;

	if (!tmr_offset || !tmr_size)
		return -EINVAL;

	sbdf = (pci_domain_nr(adev->pdev->bus) << 16);
	sbdf |= pci_dev_id(adev->pdev);
	dev_info = amdgpu_acpi_get_dev(sbdf);
	if (!dev_info)
		return -ENOENT;

	*tmr_offset = dev_info->tmr_base;
	*tmr_size = dev_info->tmr_size;

	return 0;
}

int amdgpu_acpi_get_mem_info(struct amdgpu_device *adev, int xcc_id,
			     struct amdgpu_numa_info *numa_info)
{
	struct amdgpu_acpi_dev_info *dev_info;
	struct amdgpu_acpi_xcc_info *xcc_info;
	u32 sbdf;

	if (!numa_info)
		return -EINVAL;

	sbdf = (pci_domain_nr(adev->pdev->bus) << 16);
	sbdf |= pci_dev_id(adev->pdev);
	dev_info = amdgpu_acpi_get_dev(sbdf);
	if (!dev_info)
		return -ENOENT;

	list_for_each_entry(xcc_info, &dev_info->xcc_list, list) {
		if (xcc_info->phy_id == xcc_id) {
			memcpy(numa_info, xcc_info->numa_info,
			       sizeof(*numa_info));
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * amdgpu_acpi_event - handle notify events
 *
 * @nb: notifier block
 * @val: val
 * @data: acpi event
 *
 * Calls relevant amdgpu functions in response to various
 * acpi events.
 * Returns NOTIFY code
 */
static int amdgpu_acpi_event(struct notifier_block *nb,
			     unsigned long val,
			     void *data)
{
	struct amdgpu_device *adev = container_of(nb, struct amdgpu_device, acpi_nb);
	struct acpi_bus_event *entry = (struct acpi_bus_event *)data;

	if (strcmp(entry->device_class, ACPI_AC_CLASS) == 0) {
		if (power_supply_is_system_supplied() > 0)
			DRM_DEBUG_DRIVER("pm: AC\n");
		else
			DRM_DEBUG_DRIVER("pm: DC\n");

		amdgpu_pm_acpi_event_handler(adev);
	}

	/* Check for pending SBIOS requests */
	return amdgpu_atif_handler(adev, entry);
}

/* Call all ACPI methods here */
/**
 * amdgpu_acpi_init - init driver acpi support
 *
 * @adev: amdgpu_device pointer
 *
 * Verifies the AMD ACPI interfaces and registers with the acpi
 * notifier chain (all asics).
 * Returns 0 on success, error on failure.
 */
int amdgpu_acpi_init(struct amdgpu_device *adev)
{
	struct amdgpu_atif *atif = &amdgpu_acpi_priv.atif;

	if (atif->notifications.brightness_change) {
		if (adev->dc_enabled) {
#if defined(CONFIG_DRM_AMD_DC)
			struct amdgpu_display_manager *dm = &adev->dm;

			if (dm->backlight_dev[0])
				atif->bd = dm->backlight_dev[0];
#endif
		} else {
			struct drm_encoder *tmp;

			/* Find the encoder controlling the brightness */
			list_for_each_entry(tmp, &adev_to_drm(adev)->mode_config.encoder_list,
					    head) {
				struct amdgpu_encoder *enc = to_amdgpu_encoder(tmp);

				if ((enc->devices & (ATOM_DEVICE_LCD_SUPPORT)) &&
				    enc->enc_priv) {
					struct amdgpu_encoder_atom_dig *dig = enc->enc_priv;

					if (dig->bl_dev) {
						atif->bd = dig->bl_dev;
						break;
					}
				}
			}
		}
	}
	adev->acpi_nb.notifier_call = amdgpu_acpi_event;
	register_acpi_notifier(&adev->acpi_nb);

	return 0;
}

void amdgpu_acpi_get_backlight_caps(struct amdgpu_dm_backlight_caps *caps)
{
	struct amdgpu_atif *atif = &amdgpu_acpi_priv.atif;

	caps->caps_valid = atif->backlight_caps.caps_valid;
	caps->min_input_signal = atif->backlight_caps.min_input_signal;
	caps->max_input_signal = atif->backlight_caps.max_input_signal;
	caps->ac_level = atif->backlight_caps.ac_level;
	caps->dc_level = atif->backlight_caps.dc_level;
}

/**
 * amdgpu_acpi_fini - tear down driver acpi support
 *
 * @adev: amdgpu_device pointer
 *
 * Unregisters with the acpi notifier chain (all asics).
 */
void amdgpu_acpi_fini(struct amdgpu_device *adev)
{
	unregister_acpi_notifier(&adev->acpi_nb);
}

/**
 * amdgpu_atif_pci_probe_handle - look up the ATIF handle
 *
 * @pdev: pci device
 *
 * Look up the ATIF handles (all asics).
 * Returns true if the handle is found, false if not.
 */
static bool amdgpu_atif_pci_probe_handle(struct pci_dev *pdev)
{
	char acpi_method_name[255] = { 0 };
	struct acpi_buffer buffer = {sizeof(acpi_method_name), acpi_method_name};
	acpi_handle dhandle, atif_handle;
	acpi_status status;
	int ret;

	dhandle = ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return false;

	status = acpi_get_handle(dhandle, "ATIF", &atif_handle);
	if (ACPI_FAILURE(status))
		return false;

	amdgpu_acpi_priv.atif.handle = atif_handle;
	acpi_get_name(amdgpu_acpi_priv.atif.handle, ACPI_FULL_PATHNAME, &buffer);
	DRM_DEBUG_DRIVER("Found ATIF handle %s\n", acpi_method_name);
	ret = amdgpu_atif_verify_interface(&amdgpu_acpi_priv.atif);
	if (ret) {
		amdgpu_acpi_priv.atif.handle = 0;
		return false;
	}
	return true;
}

/**
 * amdgpu_atcs_pci_probe_handle - look up the ATCS handle
 *
 * @pdev: pci device
 *
 * Look up the ATCS handles (all asics).
 * Returns true if the handle is found, false if not.
 */
static bool amdgpu_atcs_pci_probe_handle(struct pci_dev *pdev)
{
	char acpi_method_name[255] = { 0 };
	struct acpi_buffer buffer = { sizeof(acpi_method_name), acpi_method_name };
	acpi_handle dhandle, atcs_handle;
	acpi_status status;
	int ret;

	dhandle = ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return false;

	status = acpi_get_handle(dhandle, "ATCS", &atcs_handle);
	if (ACPI_FAILURE(status))
		return false;

	amdgpu_acpi_priv.atcs.handle = atcs_handle;
	acpi_get_name(amdgpu_acpi_priv.atcs.handle, ACPI_FULL_PATHNAME, &buffer);
	DRM_DEBUG_DRIVER("Found ATCS handle %s\n", acpi_method_name);
	ret = amdgpu_atcs_verify_interface(&amdgpu_acpi_priv.atcs);
	if (ret) {
		amdgpu_acpi_priv.atcs.handle = 0;
		return false;
	}
	return true;
}


/**
 * amdgpu_acpi_should_gpu_reset
 *
 * @adev: amdgpu_device_pointer
 *
 * returns true if should reset GPU, false if not
 */
bool amdgpu_acpi_should_gpu_reset(struct amdgpu_device *adev)
{
	if ((adev->flags & AMD_IS_APU) &&
	    adev->gfx.imu.funcs) /* Not need to do mode2 reset for IMU enabled APUs */
		return false;

	if ((adev->flags & AMD_IS_APU) &&
	    amdgpu_acpi_is_s3_active(adev))
		return false;

	if (amdgpu_sriov_vf(adev))
		return false;

#if IS_ENABLED(CONFIG_SUSPEND)
	return pm_suspend_target_state != PM_SUSPEND_TO_IDLE;
#else
	return true;
#endif
}

/*
 * amdgpu_acpi_detect - detect ACPI ATIF/ATCS methods
 *
 * Check if we have the ATIF/ATCS methods and populate
 * the structures in the driver.
 */
void amdgpu_acpi_detect(void)
{
	struct amdgpu_atif *atif = &amdgpu_acpi_priv.atif;
	struct amdgpu_atcs *atcs = &amdgpu_acpi_priv.atcs;
	struct pci_dev *pdev = NULL;
	int ret;

	while ((pdev = pci_get_base_class(PCI_BASE_CLASS_DISPLAY, pdev))) {
		if ((pdev->class != PCI_CLASS_DISPLAY_VGA << 8) &&
		    (pdev->class != PCI_CLASS_DISPLAY_OTHER << 8))
			continue;

		if (!atif->handle)
			amdgpu_atif_pci_probe_handle(pdev);
		if (!atcs->handle)
			amdgpu_atcs_pci_probe_handle(pdev);
	}

	if (atif->functions.sbios_requests && !atif->functions.system_params) {
		/* XXX check this workraround, if sbios request function is
		 * present we have to see how it's configured in the system
		 * params
		 */
		atif->functions.system_params = true;
	}

	if (atif->functions.system_params) {
		ret = amdgpu_atif_get_notification_params(atif);
		if (ret) {
			DRM_DEBUG_DRIVER("Call to GET_SYSTEM_PARAMS failed: %d\n",
					ret);
			/* Disable notification */
			atif->notification_cfg.enabled = false;
		}
	}

	if (atif->functions.query_backlight_transfer_characteristics) {
		ret = amdgpu_atif_query_backlight_caps(atif);
		if (ret) {
			DRM_DEBUG_DRIVER("Call to QUERY_BACKLIGHT_TRANSFER_CHARACTERISTICS failed: %d\n",
					ret);
			atif->backlight_caps.caps_valid = false;
		}
	} else {
		atif->backlight_caps.caps_valid = false;
	}

	amdgpu_acpi_enumerate_xcc();
}

void amdgpu_acpi_release(void)
{
	struct amdgpu_acpi_dev_info *dev_info, *dev_tmp;
	struct amdgpu_acpi_xcc_info *xcc_info, *xcc_tmp;
	struct amdgpu_numa_info *numa_info;
	unsigned long index;

	xa_for_each(&numa_info_xa, index, numa_info) {
		kfree(numa_info);
		xa_erase(&numa_info_xa, index);
	}

	if (list_empty(&amdgpu_acpi_dev_list))
		return;

	list_for_each_entry_safe(dev_info, dev_tmp, &amdgpu_acpi_dev_list,
				 list) {
		list_for_each_entry_safe(xcc_info, xcc_tmp, &dev_info->xcc_list,
					 list) {
			list_del(&xcc_info->list);
			kfree(xcc_info);
		}

		list_del(&dev_info->list);
		kfree(dev_info);
	}
}

#if IS_ENABLED(CONFIG_SUSPEND)
/**
 * amdgpu_acpi_is_s3_active
 *
 * @adev: amdgpu_device_pointer
 *
 * returns true if supported, false if not.
 */
bool amdgpu_acpi_is_s3_active(struct amdgpu_device *adev)
{
	return !(adev->flags & AMD_IS_APU) ||
		(pm_suspend_target_state == PM_SUSPEND_MEM);
}

/**
 * amdgpu_acpi_is_s0ix_active
 *
 * @adev: amdgpu_device_pointer
 *
 * returns true if supported, false if not.
 */
bool amdgpu_acpi_is_s0ix_active(struct amdgpu_device *adev)
{
	if (!(adev->flags & AMD_IS_APU) ||
	    (pm_suspend_target_state != PM_SUSPEND_TO_IDLE))
		return false;

	if (adev->asic_type < CHIP_RAVEN)
		return false;

	if (!(adev->pm.pp_feature & PP_GFXOFF_MASK))
		return false;

	/*
	 * If ACPI_FADT_LOW_POWER_S0 is not set in the FADT, it is generally
	 * risky to do any special firmware-related preparations for entering
	 * S0ix even though the system is suspending to idle, so return false
	 * in that case.
	 */
	if (!(acpi_gbl_FADT.flags & ACPI_FADT_LOW_POWER_S0)) {
		dev_err_once(adev->dev,
			      "Power consumption will be higher as BIOS has not been configured for suspend-to-idle.\n"
			      "To use suspend-to-idle change the sleep mode in BIOS setup.\n");
		return false;
	}

#if !IS_ENABLED(CONFIG_AMD_PMC)
	dev_err_once(adev->dev,
		      "Power consumption will be higher as the kernel has not been compiled with CONFIG_AMD_PMC.\n");
	return false;
#else
	return true;
#endif /* CONFIG_AMD_PMC */
}

/**
 * amdgpu_choose_low_power_state
 *
 * @adev: amdgpu_device_pointer
 *
 * Choose the target low power state for the GPU
 */
void amdgpu_choose_low_power_state(struct amdgpu_device *adev)
{
	if (adev->in_runpm)
		return;

	if (amdgpu_acpi_is_s0ix_active(adev))
		adev->in_s0ix = true;
	else if (amdgpu_acpi_is_s3_active(adev))
		adev->in_s3 = true;
}

#endif /* CONFIG_SUSPEND */
