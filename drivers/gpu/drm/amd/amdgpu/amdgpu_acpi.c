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
#include <linux/power_supply.h>
#include <linux/pm_runtime.h>
#include <acpi/video.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amd_acpi.h"
#include "atom.h"

struct amdgpu_atif_notification_cfg {
	bool enabled;
	int command_code;
};

struct amdgpu_atif_notifications {
	bool display_switch;
	bool expansion_mode_change;
	bool thermal_state;
	bool forced_power_state;
	bool system_power_state;
	bool display_conf_change;
	bool px_gfx_switch;
	bool brightness_change;
	bool dgpu_display_event;
};

struct amdgpu_atif_functions {
	bool system_params;
	bool sbios_requests;
	bool select_active_disp;
	bool lid_state;
	bool get_tv_standard;
	bool set_tv_standard;
	bool get_panel_expansion_mode;
	bool set_panel_expansion_mode;
	bool temperature_change;
	bool graphics_device_types;
};

struct amdgpu_atif {
	acpi_handle handle;

	struct amdgpu_atif_notifications notifications;
	struct amdgpu_atif_functions functions;
	struct amdgpu_atif_notification_cfg notification_cfg;
	struct amdgpu_encoder *encoder_for_bl;
};

/* Call the ATIF method
 */
/**
 * amdgpu_atif_call - call an ATIF method
 *
 * @handle: acpi handle
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

	/* Fail only if calling the method fails and ATIF is supported */
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND) {
		DRM_DEBUG_DRIVER("failed to evaluate ATIF got %s\n",
				 acpi_format_exception(status));
		kfree(buffer.pointer);
		return NULL;
	}

	return buffer.pointer;
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
	f->select_active_disp = mask & ATIF_SELECT_ACTIVE_DISPLAYS_SUPPORTED;
	f->lid_state = mask & ATIF_GET_LID_STATE_SUPPORTED;
	f->get_tv_standard = mask & ATIF_GET_TV_STANDARD_FROM_CMOS_SUPPORTED;
	f->set_tv_standard = mask & ATIF_SET_TV_STANDARD_IN_CMOS_SUPPORTED;
	f->get_panel_expansion_mode = mask & ATIF_GET_PANEL_EXPANSION_MODE_FROM_CMOS_SUPPORTED;
	f->set_panel_expansion_mode = mask & ATIF_SET_PANEL_EXPANSION_MODE_IN_CMOS_SUPPORTED;
	f->temperature_change = mask & ATIF_TEMPERATURE_CHANGE_NOTIFICATION_SUPPORTED;
	f->graphics_device_types = mask & ATIF_GET_GRAPHICS_DEVICE_TYPES_SUPPORTED;
}

/**
 * amdgpu_atif_verify_interface - verify ATIF
 *
 * @handle: acpi handle
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

static acpi_handle amdgpu_atif_probe_handle(acpi_handle dhandle)
{
	acpi_handle handle = NULL;
	char acpi_method_name[255] = { 0 };
	struct acpi_buffer buffer = { sizeof(acpi_method_name), acpi_method_name };
	acpi_status status;

	/* For PX/HG systems, ATIF and ATPX are in the iGPU's namespace, on dGPU only
	 * systems, ATIF is in the dGPU's namespace.
	 */
	status = acpi_get_handle(dhandle, "ATIF", &handle);
	if (ACPI_SUCCESS(status))
		goto out;

	if (amdgpu_has_atpx()) {
		status = acpi_get_handle(amdgpu_atpx_get_dhandle(), "ATIF",
					 &handle);
		if (ACPI_SUCCESS(status))
			goto out;
	}

	DRM_DEBUG_DRIVER("No ATIF handle found\n");
	return NULL;
out:
	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	DRM_DEBUG_DRIVER("Found ATIF handle %s\n", acpi_method_name);
	return handle;
}

/**
 * amdgpu_atif_get_notification_params - determine notify configuration
 *
 * @handle: acpi handle
 * @n: atif notification configuration struct
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
 * amdgpu_atif_get_sbios_requests - get requested sbios event
 *
 * @handle: acpi handle
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
 * Returns NOTIFY code
 */
static int amdgpu_atif_handler(struct amdgpu_device *adev,
			       struct acpi_bus_event *event)
{
	struct amdgpu_atif *atif = adev->atif;
	int count;

	DRM_DEBUG_DRIVER("event, device_class = %s, type = %#x\n",
			event->device_class, event->type);

	if (strcmp(event->device_class, ACPI_VIDEO_CLASS) != 0)
		return NOTIFY_DONE;

	if (!atif ||
	    !atif->notification_cfg.enabled ||
	    event->type != atif->notification_cfg.command_code)
		/* Not our event */
		return NOTIFY_DONE;

	if (atif->functions.sbios_requests) {
		struct atif_sbios_requests req;

		/* Check pending SBIOS requests */
		count = amdgpu_atif_get_sbios_requests(atif, &req);

		if (count <= 0)
			return NOTIFY_DONE;

		DRM_DEBUG_DRIVER("ATIF: %d pending SBIOS requests\n", count);

		if (req.pending & ATIF_PANEL_BRIGHTNESS_CHANGE_REQUEST) {
			struct amdgpu_encoder *enc = atif->encoder_for_bl;

			if (enc) {
				struct amdgpu_encoder_atom_dig *dig = enc->enc_priv;

				DRM_DEBUG_DRIVER("Changing brightness to %d\n",
						 req.backlight_level);

				amdgpu_display_backlight_set_level(adev, enc, req.backlight_level);

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) || defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)
				backlight_force_update(dig->bl_dev,
						       BACKLIGHT_UPDATE_HOTKEY);
#endif
			}
		}
		if (req.pending & ATIF_DGPU_DISPLAY_EVENT) {
			if ((adev->flags & AMD_IS_PX) &&
			    amdgpu_atpx_dgpu_req_power_for_displays()) {
				pm_runtime_get_sync(adev->ddev->dev);
				/* Just fire off a uevent and let userspace tell us what to do */
				drm_helper_hpd_irq_event(adev->ddev);
				pm_runtime_mark_last_busy(adev->ddev->dev);
				pm_runtime_put_autosuspend(adev->ddev->dev);
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
 * @handle: acpi handle
 * @function: the ATCS function to execute
 * @params: ATCS function params
 *
 * Executes the requested ATCS function (all asics).
 * Returns a pointer to the acpi output buffer.
 */
static union acpi_object *amdgpu_atcs_call(acpi_handle handle, int function,
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

	status = acpi_evaluate_object(handle, "ATCS", &atcs_arg, &buffer);

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
}

/**
 * amdgpu_atcs_verify_interface - verify ATCS
 *
 * @handle: acpi handle
 * @atcs: amdgpu atcs struct
 *
 * Execute the ATCS_FUNCTION_VERIFY_INTERFACE ATCS function
 * to initialize ATCS and determine what features are supported
 * (all asics).
 * returns 0 on success, error on failure.
 */
static int amdgpu_atcs_verify_interface(acpi_handle handle,
					struct amdgpu_atcs *atcs)
{
	union acpi_object *info;
	struct atcs_verify_interface output;
	size_t size;
	int err = 0;

	info = amdgpu_atcs_call(handle, ATCS_FUNCTION_VERIFY_INTERFACE, NULL);
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
	struct amdgpu_atcs *atcs = &adev->atcs;

	if (atcs->functions.pcie_perf_req && atcs->functions.pcie_dev_rdy)
		return true;

	return false;
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
	acpi_handle handle;
	union acpi_object *info;
	struct amdgpu_atcs *atcs = &adev->atcs;

	/* Get the device handle */
	handle = ACPI_HANDLE(&adev->pdev->dev);
	if (!handle)
		return -EINVAL;

	if (!atcs->functions.pcie_dev_rdy)
		return -EINVAL;

	info = amdgpu_atcs_call(handle, ATCS_FUNCTION_PCIE_DEVICE_READY_NOTIFICATION, NULL);
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
	acpi_handle handle;
	union acpi_object *info;
	struct amdgpu_atcs *atcs = &adev->atcs;
	struct atcs_pref_req_input atcs_input;
	struct atcs_pref_req_output atcs_output;
	struct acpi_buffer params;
	size_t size;
	u32 retry = 3;

	if (amdgpu_acpi_pcie_notify_device_ready(adev))
		return -EINVAL;

	/* Get the device handle */
	handle = ACPI_HANDLE(&adev->pdev->dev);
	if (!handle)
		return -EINVAL;

	if (!atcs->functions.pcie_perf_req)
		return -EINVAL;

	atcs_input.size = sizeof(struct atcs_pref_req_input);
	/* client id (bit 2-0: func num, 7-3: dev num, 15-8: bus num) */
	atcs_input.client_id = adev->pdev->devfn | (adev->pdev->bus->number << 8);
	atcs_input.valid_flags_mask = ATCS_VALID_FLAGS_MASK;
	atcs_input.flags = ATCS_WAIT_FOR_COMPLETION;
	if (advertise)
		atcs_input.flags |= ATCS_ADVERTISE_CAPS;
	atcs_input.req_type = ATCS_PCIE_LINK_SPEED;
	atcs_input.perf_req = perf_req;

	params.length = sizeof(struct atcs_pref_req_input);
	params.pointer = &atcs_input;

	while (retry--) {
		info = amdgpu_atcs_call(handle, ATCS_FUNCTION_PCIE_PERFORMANCE_REQUEST, &params);
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
	acpi_handle handle, atif_handle;
	struct amdgpu_atif *atif;
	struct amdgpu_atcs *atcs = &adev->atcs;
	int ret;

	/* Get the device handle */
	handle = ACPI_HANDLE(&adev->pdev->dev);

	if (!adev->bios || !handle)
		return 0;

	/* Call the ATCS method */
	ret = amdgpu_atcs_verify_interface(handle, atcs);
	if (ret) {
		DRM_DEBUG_DRIVER("Call to ATCS verify_interface failed: %d\n", ret);
	}

	/* Probe for ATIF, and initialize it if found */
	atif_handle = amdgpu_atif_probe_handle(handle);
	if (!atif_handle)
		goto out;

	atif = kzalloc(sizeof(*atif), GFP_KERNEL);
	if (!atif) {
		DRM_WARN("Not enough memory to initialize ATIF\n");
		goto out;
	}
	atif->handle = atif_handle;

	/* Call the ATIF method */
	ret = amdgpu_atif_verify_interface(atif);
	if (ret) {
		DRM_DEBUG_DRIVER("Call to ATIF verify_interface failed: %d\n", ret);
		kfree(atif);
		goto out;
	}
	adev->atif = atif;

	if (atif->notifications.brightness_change) {
		struct drm_encoder *tmp;

		/* Find the encoder controlling the brightness */
		list_for_each_entry(tmp, &adev->ddev->mode_config.encoder_list,
				head) {
			struct amdgpu_encoder *enc = to_amdgpu_encoder(tmp);

			if ((enc->devices & (ATOM_DEVICE_LCD_SUPPORT)) &&
			    enc->enc_priv) {
				struct amdgpu_encoder_atom_dig *dig = enc->enc_priv;
				if (dig->bl_dev) {
					atif->encoder_for_bl = enc;
					break;
				}
			}
		}
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

out:
	adev->acpi_nb.notifier_call = amdgpu_acpi_event;
	register_acpi_notifier(&adev->acpi_nb);

	return ret;
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
	if (adev->atif)
		kfree(adev->atif);
}
