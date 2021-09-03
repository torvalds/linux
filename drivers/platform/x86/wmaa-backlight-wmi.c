// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/wmi.h>

/**
 * enum wmaa_method - WMI method IDs for ACPI WMAA
 * @WMAA_METHOD_LEVEL:  Get or set the brightness level,
 *                      or get the maximum brightness level.
 * @WMAA_METHOD_SOURCE: Get the source for backlight control.
 */
enum wmaa_method {
	WMAA_METHOD_LEVEL = 1,
	WMAA_METHOD_SOURCE = 2,
	WMAA_METHOD_MAX
};

/**
 * enum wmaa_mode - Operation mode for ACPI WMAA method
 * @WMAA_MODE_GET:           Get the current brightness level or source.
 * @WMAA_MODE_SET:           Set the brightness level.
 * @WMAA_MODE_GET_MAX_LEVEL: Get the maximum brightness level. This is only
 *                           valid when the WMI method is %WMAA_METHOD_LEVEL.
 */
enum wmaa_mode {
	WMAA_MODE_GET = 0,
	WMAA_MODE_SET = 1,
	WMAA_MODE_GET_MAX_LEVEL = 2,
	WMAA_MODE_MAX
};

/**
 * enum wmaa_source - Backlight brightness control source identification
 * @WMAA_SOURCE_GPU:   Backlight brightness is controlled by the GPU.
 * @WMAA_SOURCE_EC:    Backlight brightness is controlled by the system's
 *                     Embedded Controller (EC).
 * @WMAA_SOURCE_AUX:   Backlight brightness is controlled over the DisplayPort
 *                     AUX channel.
 */
enum wmaa_source {
	WMAA_SOURCE_GPU = 1,
	WMAA_SOURCE_EC = 2,
	WMAA_SOURCE_AUX = 3,
	WMAA_SOURCE_MAX
};

/**
 * struct wmaa_args - arguments for the ACPI WMAA method
 * @mode:    Pass in an &enum wmaa_mode value to select between getting or
 *           setting a value.
 * @val:     In parameter for value to set when operating in %WMAA_MODE_SET
 *           mode. Not used in %WMAA_MODE_GET or %WMAA_MODE_GET_MAX_LEVEL mode.
 * @ret:     Out parameter returning retrieved value when operating in
 *           %WMAA_MODE_GET or %WMAA_MODE_GET_MAX_LEVEL mode. Not used in
 *           %WMAA_MODE_SET mode.
 * @ignored: Padding; not used. The ACPI method expects a 24 byte params struct.
 *
 * This is the parameters structure for the ACPI WMAA method as wrapped by WMI.
 * The value passed in to @val or returned by @ret will be a brightness value
 * when the WMI method ID is %WMAA_METHOD_LEVEL, or an &enum wmaa_source value
 * when the WMI method ID is %WMAA_METHOD_SOURCE.
 */
struct wmaa_args {
	u32 mode;
	u32 val;
	u32 ret;
	u32 ignored[3];
};

/**
 * wmi_call_wmaa() - helper function for calling ACPI WMAA via its WMI wrapper
 * @w:    Pointer to the struct wmi_device identified by %WMAA_WMI_GUID
 * @id:   The method ID for the ACPI WMAA method (e.g. %WMAA_METHOD_LEVEL or
 *        %WMAA_METHOD_SOURCE)
 * @mode: The operation to perform on the ACPI WMAA method (e.g. %WMAA_MODE_SET
 *        or %WMAA_MODE_GET)
 * @val:  Pointer to a value passed in by the caller when @mode is
 *        %WMAA_MODE_SET, or a value passed out to the caller when @mode is
 *        %WMAA_MODE_GET or %WMAA_MODE_GET_MAX_LEVEL.
 *
 * Returns 0 on success, or a negative error number on failure.
 */
static int wmi_call_wmaa(struct wmi_device *w, enum wmaa_method id, enum wmaa_mode mode, u32 *val)
{
	struct wmaa_args args = {
		.mode = mode,
		.val = 0,
		.ret = 0,
	};
	struct acpi_buffer buf = { (acpi_size)sizeof(args), &args };
	acpi_status status;

	if (id < WMAA_METHOD_LEVEL || id >= WMAA_METHOD_MAX ||
	    mode < WMAA_MODE_GET || mode >= WMAA_MODE_MAX)
		return -EINVAL;

	if (mode == WMAA_MODE_SET)
		args.val = *val;

	status = wmidev_evaluate_method(w, 0, id, &buf, &buf);
	if (ACPI_FAILURE(status)) {
		dev_err(&w->dev, "ACPI WMAA failed: %s\n",
			acpi_format_exception(status));
		return -EIO;
	}

	if (mode != WMAA_MODE_SET)
		*val = args.ret;

	return 0;
}

static int wmaa_backlight_update_status(struct backlight_device *bd)
{
	struct wmi_device *wdev = bl_get_data(bd);

	return wmi_call_wmaa(wdev, WMAA_METHOD_LEVEL, WMAA_MODE_SET,
			     &bd->props.brightness);
}

static int wmaa_backlight_get_brightness(struct backlight_device *bd)
{
	struct wmi_device *wdev = bl_get_data(bd);
	u32 level;
	int ret;

	ret = wmi_call_wmaa(wdev, WMAA_METHOD_LEVEL, WMAA_MODE_GET, &level);
	if (ret < 0)
		return ret;

	return level;
}

static const struct backlight_ops wmaa_backlight_ops = {
	.update_status = wmaa_backlight_update_status,
	.get_brightness = wmaa_backlight_get_brightness,
};

static int wmaa_backlight_wmi_probe(struct wmi_device *wdev, const void *ctx)
{
	struct backlight_properties props = {};
	struct backlight_device *bdev;
	u32 source;
	int ret;

	ret = wmi_call_wmaa(wdev, WMAA_METHOD_SOURCE, WMAA_MODE_GET, &source);
	if (ret)
		return ret;

	/*
	 * This driver is only to be used when brightness control is handled
	 * by the EC; otherwise, the GPU driver(s) should control brightness.
	 */
	if (source != WMAA_SOURCE_EC)
		return -ENODEV;

	/*
	 * Identify this backlight device as a firmware device so that it can
	 * be prioritized over any exposed GPU-driven raw device(s).
	 */
	props.type = BACKLIGHT_FIRMWARE;

	ret = wmi_call_wmaa(wdev, WMAA_METHOD_LEVEL, WMAA_MODE_GET_MAX_LEVEL,
			    &props.max_brightness);
	if (ret)
		return ret;

	ret = wmi_call_wmaa(wdev, WMAA_METHOD_LEVEL, WMAA_MODE_GET,
			    &props.brightness);
	if (ret)
		return ret;

	bdev = devm_backlight_device_register(&wdev->dev, "wmaa_backlight",
					      &wdev->dev, wdev,
					      &wmaa_backlight_ops, &props);
	return PTR_ERR_OR_ZERO(bdev);
}

#define WMAA_WMI_GUID "603E9613-EF25-4338-A3D0-C46177516DB7"

static const struct wmi_device_id wmaa_backlight_wmi_id_table[] = {
	{ .guid_string = WMAA_WMI_GUID },
	{ }
};
MODULE_DEVICE_TABLE(wmi, wmaa_backlight_wmi_id_table);

static struct wmi_driver wmaa_backlight_wmi_driver = {
	.driver = {
		.name = "wmaa-backlight",
	},
	.probe = wmaa_backlight_wmi_probe,
	.id_table = wmaa_backlight_wmi_id_table,
};
module_wmi_driver(wmaa_backlight_wmi_driver);

MODULE_AUTHOR("Daniel Dadap <ddadap@nvidia.com>");
MODULE_DESCRIPTION("WMAA Backlight WMI driver");
MODULE_LICENSE("GPL");
