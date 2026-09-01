// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Halo Box RGB LED Driver
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 * This driver provides RGB LED control for AMD Halo Box devices through
 * the LED multicolor subsystem. The Halo Box light bar can be controlled
 * via sysfs to display any RGB color combination.
 */

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/compiler_attributes.h>
#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/device/devres.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wmi.h>

#include <linux/byteorder/generic.h>

#define AMD_HALO_GUID "081E747B-E028-4232-AF24-EAAEAB2B1E86"

/* WMI method IDs from MOF */
enum {
	AMD_HALO_WMI_TURN_OFF	= 0x04,
	AMD_HALO_WMI_RGB	= 0x07,
};

/* Arg0 of the AMD_HALO_WMI_RGB Method */
enum {
	AMD_HALO_RGB_CMD_GET	= 0x0,
	AMD_HALO_RGB_CMD_SET	= 0x1,
};

/* Status codes from spec */
#define AMD_HALO_STATUS_SUCCESS		0x0000
#define AMD_HALO_STATUS_INVALID_PARAM	0xFFFD

/* Brightness uses 0-100 range */
#define AMD_HALO_MAX_HW_BRIGHTNESS	100

/**
 * struct amd_halo_led_data - Driver private data
 * @wdev: WMI device pointer
 * @led_mc: LED multicolor class device
 * @subled_info: RGB channel information
 * @lock: Mutex to protect WMI calls
 */
struct amd_halo_led_data {
	struct wmi_device *wdev;
	struct led_classdev_mc led_mc;
	struct mc_subled subled_info[3];
	struct mutex lock;
};

struct amd_halo_wmi_args {
	__le32 arg0;
	__le32 arg1;
};

struct amd_halo_wmi_args_rgb {
	__le32 cmd;
	__le32 red;
	__le32 green;
	__le32 blue;
};

struct amd_halo_wmi_output_rgb {
	__le16 status;
	u8 red;
	u8 green;
	u8 blue;
} __packed;

static inline int wmi_status_to_err(u16 status)
{
	switch (status) {
	case AMD_HALO_STATUS_SUCCESS:
		return 0;
	case AMD_HALO_STATUS_INVALID_PARAM:
		return -EINVAL;
	default:
		return -EIO;
	}
}

static int __amd_halo_wmi_call(struct wmi_device *wdev,
			       u32 method_id, void *data, size_t length)
{
	struct wmi_buffer input = {
		.length = length,
		.data = data,
	};
	struct wmi_buffer output = { };
	int ret;

	/* Return buffer per spec: Bytes[0:1] = Status (little-endian) */
	ret = wmidev_invoke_method(wdev, 0, method_id,
				   &input, &output, sizeof(__le16));
	if (ret)
		return ret;

	__le16 *result_status __free(kfree) = output.data;

	return wmi_status_to_err(le16_to_cpu(*result_status));
}

/**
 * amd_halo_wmi_turn_off - Turn off all LED channels
 * @wdev: WMI device pointer
 *
 * Return: 0 on success, negative error code on failure
 */
static int amd_halo_wmi_turn_off(struct wmi_device *wdev)
{
	struct amd_halo_wmi_args args = { };

	return __amd_halo_wmi_call(wdev, AMD_HALO_WMI_TURN_OFF, &args, sizeof(args));
}

/**
 * amd_halo_wmi_set_rgb - Set all RGB channels atomically
 * @wdev: WMI device pointer
 * @r: brightness for red channel (0 - 100)
 * @g: brightness for green channel (0 - 100)
 * @b: brightness for blue channel (0 - 100)
 *
 * Return: 0 on success, negative error code on failure
 */
static int amd_halo_wmi_set_rgb(struct wmi_device *wdev, u32 r, u32 g, u32 b)
{
	struct amd_halo_wmi_args_rgb args = {
		.cmd = cpu_to_le32(AMD_HALO_RGB_CMD_SET),
		.red = cpu_to_le32(r),
		.green = cpu_to_le32(g),
		.blue = cpu_to_le32(b),
	};

	if (r > AMD_HALO_MAX_HW_BRIGHTNESS ||
	    g > AMD_HALO_MAX_HW_BRIGHTNESS ||
	    b > AMD_HALO_MAX_HW_BRIGHTNESS) {
		return -EINVAL;
	}

	return __amd_halo_wmi_call(wdev, AMD_HALO_WMI_RGB, &args, sizeof(args));
}

/**
 * amd_halo_wmi_get_rgb - Get RGB values
 * @wdev: WMI device pointer
 * @r: output buffer for red value
 * @g: output buffer for green value
 * @b: output buffer for blue value
 *
 * Return: 0 on success, negative error code on failure
 */
static int amd_halo_wmi_get_rgb(struct wmi_device *wdev, u8 *r, u8 *g, u8 *b)
{
	struct amd_halo_wmi_args_rgb args = {
		.cmd = cpu_to_le32(AMD_HALO_RGB_CMD_GET),
	};
	struct wmi_buffer input = {
		.length = sizeof(args),
		.data = &args,
	};
	struct wmi_buffer output = { };
	int ret;

	ret = wmidev_invoke_method(wdev, 0, AMD_HALO_WMI_RGB,
				   &input, &output, sizeof(struct amd_halo_wmi_output_rgb));
	if (ret)
		return ret;

	struct amd_halo_wmi_output_rgb *data __free(kfree) = output.data;

	ret = wmi_status_to_err(le16_to_cpu(data->status));
	if (ret)
		return ret;

	if (data->red > AMD_HALO_MAX_HW_BRIGHTNESS ||
	    data->green > AMD_HALO_MAX_HW_BRIGHTNESS ||
	    data->blue > AMD_HALO_MAX_HW_BRIGHTNESS) {
		return -EPROTO;
	}

	*r = data->red;
	*g = data->green;
	*b = data->blue;

	return 0;
}

/**
 * amd_halo_brightness_set - Set LED brightness
 * @cdev: LED class device
 * @brightness: Brightness value
 *
 * Return: 0 on success, negative error code on failure
 */
static int amd_halo_brightness_set(struct led_classdev *cdev,
				   enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct amd_halo_led_data *data = container_of(mc_cdev,
						      struct amd_halo_led_data,
						      led_mc);
	u32 red_hw, green_hw, blue_hw;
	int ret;

	guard(mutex)(&data->lock);

	led_mc_calc_color_components(mc_cdev, brightness);

	if (brightness == 0)
		return amd_halo_wmi_turn_off(data->wdev);

	red_hw = mc_cdev->subled_info[0].brightness;
	green_hw = mc_cdev->subled_info[1].brightness;
	blue_hw = mc_cdev->subled_info[2].brightness;

	ret = amd_halo_wmi_set_rgb(data->wdev, red_hw, green_hw, blue_hw);
	if (ret)
		goto out;

	return 0;

out:
	/*
	 * Consider the light bar non-functional if AMD_HALO_WMI_RGB failed.
	 * Attempt to turn the LED off completely as clean-up.
	 */
	if (amd_halo_wmi_turn_off(data->wdev))
		dev_warn_ratelimited(&data->wdev->dev, "Failed to turn LED off on cleanup\n");

	return ret;
}

static int amd_halo_probe(struct wmi_device *wdev, const void *context)
{
	struct led_init_data led_init_data = {
		.devicename = "amd_halo",
		.default_label = "multicolor:" LED_FUNCTION_STATUS,
		.devname_mandatory = true,
	};
	struct amd_halo_led_data *data;
	u8 r, g, b;
	int ret;

	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = devm_mutex_init(&wdev->dev, &data->lock);
	if (ret)
		return ret;

	data->wdev = wdev;
	dev_set_drvdata(&wdev->dev, data);

	data->subled_info[0].color_index = LED_COLOR_ID_RED;
	data->subled_info[1].color_index = LED_COLOR_ID_GREEN;
	data->subled_info[2].color_index = LED_COLOR_ID_BLUE;

	data->led_mc.led_cdev.brightness = AMD_HALO_MAX_HW_BRIGHTNESS;
	data->led_mc.led_cdev.max_brightness = AMD_HALO_MAX_HW_BRIGHTNESS;
	data->led_mc.led_cdev.brightness_set_blocking = amd_halo_brightness_set;
	data->led_mc.led_cdev.flags = LED_CORE_SUSPENDRESUME | LED_RETAIN_AT_SHUTDOWN;
	data->led_mc.num_colors = ARRAY_SIZE(data->subled_info);
	data->led_mc.subled_info = data->subled_info;

	ret = amd_halo_wmi_get_rgb(wdev, &r, &g, &b);
	if (ret)
		return ret;

	data->subled_info[0].intensity = r;
	data->subled_info[1].intensity = g;
	data->subled_info[2].intensity = b;

	ret = devm_led_classdev_multicolor_register_ext(&wdev->dev, &data->led_mc,
							&led_init_data);
	if (ret)
		return dev_err_probe(&wdev->dev, ret,
				     "Failed to register multicolor LED\n");
	return 0;
}

static const struct wmi_device_id amd_halo_id_table[] = {
	{ .guid_string = AMD_HALO_GUID },
	{ }
};
MODULE_DEVICE_TABLE(wmi, amd_halo_id_table);

static struct wmi_driver amd_halo_driver = {
	.driver = {
		.name = "amd_halo_led",
	},
	.id_table = amd_halo_id_table,
	.probe = amd_halo_probe,
	.no_singleton = true,
};

module_wmi_driver(amd_halo_driver);

MODULE_AUTHOR("Mario Limonciello (AMD) <superm1@kernel.org>");
MODULE_AUTHOR("Yo-Jung Leo Lin (AMD) <Leo.Lin@amd.com>");
MODULE_DESCRIPTION("AMD Halo Box RGB LED Control Driver");
MODULE_LICENSE("GPL");
