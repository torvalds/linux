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
 * enum wmi_brightness_method - WMI method IDs
 * @WMI_BRIGHTNESS_METHOD_LEVEL:  Get/Set EC brightness level status
 * @WMI_BRIGHTNESS_METHOD_SOURCE: Get/Set EC Brightness Source
 */
enum wmi_brightness_method {
	WMI_BRIGHTNESS_METHOD_LEVEL = 1,
	WMI_BRIGHTNESS_METHOD_SOURCE = 2,
	WMI_BRIGHTNESS_METHOD_MAX
};

/**
 * enum wmi_brightness_mode - Operation mode for WMI-wrapped method
 * @WMI_BRIGHTNESS_MODE_GET:            Get the current brightness level/source.
 * @WMI_BRIGHTNESS_MODE_SET:            Set the brightness level.
 * @WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL:  Get the maximum brightness level. This
 *                                      is only valid when the WMI method is
 *                                      %WMI_BRIGHTNESS_METHOD_LEVEL.
 */
enum wmi_brightness_mode {
	WMI_BRIGHTNESS_MODE_GET = 0,
	WMI_BRIGHTNESS_MODE_SET = 1,
	WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL = 2,
	WMI_BRIGHTNESS_MODE_MAX
};

/**
 * enum wmi_brightness_source - Backlight brightness control source selection
 * @WMI_BRIGHTNESS_SOURCE_GPU: Backlight brightness is controlled by the GPU.
 * @WMI_BRIGHTNESS_SOURCE_EC:  Backlight brightness is controlled by the
 *                             system's Embedded Controller (EC).
 * @WMI_BRIGHTNESS_SOURCE_AUX: Backlight brightness is controlled over the
 *                             DisplayPort AUX channel.
 */
enum wmi_brightness_source {
	WMI_BRIGHTNESS_SOURCE_GPU = 1,
	WMI_BRIGHTNESS_SOURCE_EC = 2,
	WMI_BRIGHTNESS_SOURCE_AUX = 3,
	WMI_BRIGHTNESS_SOURCE_MAX
};

/**
 * struct wmi_brightness_args - arguments for the WMI-wrapped ACPI method
 * @mode:    Pass in an &enum wmi_brightness_mode value to select between
 *           getting or setting a value.
 * @val:     In parameter for value to set when using %WMI_BRIGHTNESS_MODE_SET
 *           mode. Not used in conjunction with %WMI_BRIGHTNESS_MODE_GET or
 *           %WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL mode.
 * @ret:     Out parameter returning retrieved value when operating in
 *           %WMI_BRIGHTNESS_MODE_GET or %WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL
 *           mode. Not used in %WMI_BRIGHTNESS_MODE_SET mode.
 * @ignored: Padding; not used. The ACPI method expects a 24 byte params struct.
 *
 * This is the parameters structure for the WmiBrightnessNotify ACPI method as
 * wrapped by WMI. The value passed in to @val or returned by @ret will be a
 * brightness value when the WMI method ID is %WMI_BRIGHTNESS_METHOD_LEVEL, or
 * an &enum wmi_brightness_source value with %WMI_BRIGHTNESS_METHOD_SOURCE.
 */
struct wmi_brightness_args {
	u32 mode;
	u32 val;
	u32 ret;
	u32 ignored[3];
};

/**
 * wmi_brightness_notify() - helper function for calling WMI-wrapped ACPI method
 * @w:    Pointer to the struct wmi_device identified by %WMI_BRIGHTNESS_GUID
 * @id:   The WMI method ID to call (e.g. %WMI_BRIGHTNESS_METHOD_LEVEL or
 *        %WMI_BRIGHTNESS_METHOD_SOURCE)
 * @mode: The operation to perform on the method (e.g. %WMI_BRIGHTNESS_MODE_SET
 *        or %WMI_BRIGHTNESS_MODE_GET)
 * @val:  Pointer to a value passed in by the caller when @mode is
 *        %WMI_BRIGHTNESS_MODE_SET, or a value passed out to caller when @mode
 *        is %WMI_BRIGHTNESS_MODE_GET or %WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL.
 *
 * Returns 0 on success, or a negative error number on failure.
 */
static int wmi_brightness_notify(struct wmi_device *w, enum wmi_brightness_method id, enum wmi_brightness_mode mode, u32 *val)
{
	struct wmi_brightness_args args = {
		.mode = mode,
		.val = 0,
		.ret = 0,
	};
	struct acpi_buffer buf = { (acpi_size)sizeof(args), &args };
	acpi_status status;

	if (id < WMI_BRIGHTNESS_METHOD_LEVEL ||
	    id >= WMI_BRIGHTNESS_METHOD_MAX ||
	    mode < WMI_BRIGHTNESS_MODE_GET || mode >= WMI_BRIGHTNESS_MODE_MAX)
		return -EINVAL;

	if (mode == WMI_BRIGHTNESS_MODE_SET)
		args.val = *val;

	status = wmidev_evaluate_method(w, 0, id, &buf, &buf);
	if (ACPI_FAILURE(status)) {
		dev_err(&w->dev, "EC backlight control failed: %s\n",
			acpi_format_exception(status));
		return -EIO;
	}

	if (mode != WMI_BRIGHTNESS_MODE_SET)
		*val = args.ret;

	return 0;
}

static int nvidia_wmi_ec_backlight_update_status(struct backlight_device *bd)
{
	struct wmi_device *wdev = bl_get_data(bd);

	return wmi_brightness_notify(wdev, WMI_BRIGHTNESS_METHOD_LEVEL,
	                             WMI_BRIGHTNESS_MODE_SET,
			             &bd->props.brightness);
}

static int nvidia_wmi_ec_backlight_get_brightness(struct backlight_device *bd)
{
	struct wmi_device *wdev = bl_get_data(bd);
	u32 level;
	int ret;

	ret = wmi_brightness_notify(wdev, WMI_BRIGHTNESS_METHOD_LEVEL,
	                            WMI_BRIGHTNESS_MODE_GET, &level);
	if (ret < 0)
		return ret;

	return level;
}

static const struct backlight_ops nvidia_wmi_ec_backlight_ops = {
	.update_status = nvidia_wmi_ec_backlight_update_status,
	.get_brightness = nvidia_wmi_ec_backlight_get_brightness,
};

static int nvidia_wmi_ec_backlight_probe(struct wmi_device *wdev, const void *ctx)
{
	struct backlight_properties props = {};
	struct backlight_device *bdev;
	u32 source;
	int ret;

	ret = wmi_brightness_notify(wdev, WMI_BRIGHTNESS_METHOD_SOURCE,
	                           WMI_BRIGHTNESS_MODE_GET, &source);
	if (ret)
		return ret;

	/*
	 * This driver is only to be used when brightness control is handled
	 * by the EC; otherwise, the GPU driver(s) should control brightness.
	 */
	if (source != WMI_BRIGHTNESS_SOURCE_EC)
		return -ENODEV;

	/*
	 * Identify this backlight device as a firmware device so that it can
	 * be prioritized over any exposed GPU-driven raw device(s).
	 */
	props.type = BACKLIGHT_FIRMWARE;

	ret = wmi_brightness_notify(wdev, WMI_BRIGHTNESS_METHOD_LEVEL,
	                           WMI_BRIGHTNESS_MODE_GET_MAX_LEVEL,
	                           &props.max_brightness);
	if (ret)
		return ret;

	ret = wmi_brightness_notify(wdev, WMI_BRIGHTNESS_METHOD_LEVEL,
	                           WMI_BRIGHTNESS_MODE_GET, &props.brightness);
	if (ret)
		return ret;

	bdev = devm_backlight_device_register(&wdev->dev,
	                                      "nvidia_wmi_ec_backlight",
					      &wdev->dev, wdev,
					      &nvidia_wmi_ec_backlight_ops,
					      &props);
	return PTR_ERR_OR_ZERO(bdev);
}

#define WMI_BRIGHTNESS_GUID "603E9613-EF25-4338-A3D0-C46177516DB7"

static const struct wmi_device_id nvidia_wmi_ec_backlight_id_table[] = {
	{ .guid_string = WMI_BRIGHTNESS_GUID },
	{ }
};
MODULE_DEVICE_TABLE(wmi, nvidia_wmi_ec_backlight_id_table);

static struct wmi_driver nvidia_wmi_ec_backlight_driver = {
	.driver = {
		.name = "nvidia-wmi-ec-backlight",
	},
	.probe = nvidia_wmi_ec_backlight_probe,
	.id_table = nvidia_wmi_ec_backlight_id_table,
};
module_wmi_driver(nvidia_wmi_ec_backlight_driver);

MODULE_AUTHOR("Daniel Dadap <ddadap@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA WMI EC Backlight driver");
MODULE_LICENSE("GPL");
