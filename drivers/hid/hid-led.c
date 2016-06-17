/*
 * Simple USB RGB LED driver
 *
 * Copyright 2016 Heiner Kallweit <hkallweit1@gmail.com>
 * Based on drivers/hid/hid-thingm.c and
 * drivers/usb/misc/usbled.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include "hid-ids.h"

enum hidled_report_type {
	RAW_REQUEST,
	OUTPUT_REPORT
};

enum hidled_type {
	RISO_KAGAKU,
	DREAM_CHEEKY,
};

static unsigned const char riso_kagaku_tbl[] = {
/* R+2G+4B -> riso kagaku color index */
	[0] = 0, /* black   */
	[1] = 2, /* red     */
	[2] = 1, /* green   */
	[3] = 5, /* yellow  */
	[4] = 3, /* blue    */
	[5] = 6, /* magenta */
	[6] = 4, /* cyan    */
	[7] = 7  /* white   */
};

#define RISO_KAGAKU_IX(r, g, b) riso_kagaku_tbl[((r)?1:0)+((g)?2:0)+((b)?4:0)]

struct hidled_device;

struct hidled_config {
	enum hidled_type	type;
	const char		*name;
	const char		*short_name;
	enum led_brightness	max_brightness;
	size_t			report_size;
	enum hidled_report_type	report_type;
	u8			report_id;
	int (*init)(struct hidled_device *ldev);
	int (*write)(struct led_classdev *cdev, enum led_brightness br);
};

struct hidled_led {
	struct led_classdev	cdev;
	struct hidled_device	*ldev;
	char			name[32];
};

struct hidled_device {
	const struct hidled_config *config;
	struct hidled_led	red;
	struct hidled_led	green;
	struct hidled_led	blue;
	struct hid_device       *hdev;
	struct mutex		lock;
};

#define MAX_REPORT_SIZE		16

#define to_hidled_led(arg) container_of(arg, struct hidled_led, cdev)

static bool riso_kagaku_switch_green_blue;
module_param(riso_kagaku_switch_green_blue, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(riso_kagaku_switch_green_blue,
	"switch green and blue RGB component for Riso Kagaku devices");

static int hidled_send(struct hidled_device *ldev, __u8 *buf)
{
	int ret;

	buf[0] = ldev->config->report_id;

	mutex_lock(&ldev->lock);

	if (ldev->config->report_type == RAW_REQUEST)
		ret = hid_hw_raw_request(ldev->hdev, buf[0], buf,
					 ldev->config->report_size,
					 HID_FEATURE_REPORT,
					 HID_REQ_SET_REPORT);
	else if (ldev->config->report_type == OUTPUT_REPORT)
		ret = hid_hw_output_report(ldev->hdev, buf,
					   ldev->config->report_size);
	else
		ret = -EINVAL;

	mutex_unlock(&ldev->lock);

	if (ret < 0)
		return ret;

	return ret == ldev->config->report_size ? 0 : -EMSGSIZE;
}

static u8 riso_kagaku_index(struct hidled_device *ldev)
{
	enum led_brightness r, g, b;

	r = ldev->red.cdev.brightness;
	g = ldev->green.cdev.brightness;
	b = ldev->blue.cdev.brightness;

	if (riso_kagaku_switch_green_blue)
		return RISO_KAGAKU_IX(r, b, g);
	else
		return RISO_KAGAKU_IX(r, g, b);
}

static int riso_kagaku_write(struct led_classdev *cdev, enum led_brightness br)
{
	struct hidled_led *led = to_hidled_led(cdev);
	struct hidled_device *ldev = led->ldev;
	__u8 buf[MAX_REPORT_SIZE] = {};

	buf[1] = riso_kagaku_index(ldev);

	return hidled_send(ldev, buf);
}

static int dream_cheeky_write(struct led_classdev *cdev, enum led_brightness br)
{
	struct hidled_led *led = to_hidled_led(cdev);
	struct hidled_device *ldev = led->ldev;
	__u8 buf[MAX_REPORT_SIZE] = {};

	buf[1] = ldev->red.cdev.brightness;
	buf[2] = ldev->green.cdev.brightness;
	buf[3] = ldev->blue.cdev.brightness;
	buf[7] = 0x1a;
	buf[8] = 0x05;

	return hidled_send(ldev, buf);
}

static int dream_cheeky_init(struct hidled_device *ldev)
{
	__u8 buf[MAX_REPORT_SIZE] = {};

	/* Dream Cheeky magic */
	buf[1] = 0x1f;
	buf[2] = 0x02;
	buf[4] = 0x5f;
	buf[7] = 0x1a;
	buf[8] = 0x03;

	return hidled_send(ldev, buf);
}

static const struct hidled_config hidled_configs[] = {
	{
		.type = RISO_KAGAKU,
		.name = "Riso Kagaku Webmail Notifier",
		.short_name = "riso_kagaku",
		.max_brightness = 1,
		.report_size = 6,
		.report_type = OUTPUT_REPORT,
		.report_id = 0,
		.write = riso_kagaku_write,
	},
	{
		.type = DREAM_CHEEKY,
		.name = "Dream Cheeky Webmail Notifier",
		.short_name = "dream_cheeky",
		.max_brightness = 31,
		.report_size = 9,
		.report_type = RAW_REQUEST,
		.report_id = 0,
		.init = dream_cheeky_init,
		.write = dream_cheeky_write,
	},
};

static int hidled_init_led(struct hidled_led *led, const char *color_name,
			   struct hidled_device *ldev, unsigned int minor)
{
	snprintf(led->name, sizeof(led->name), "%s%u:%s",
		 ldev->config->short_name, minor, color_name);
	led->cdev.name = led->name;
	led->cdev.max_brightness = ldev->config->max_brightness;
	led->cdev.brightness_set_blocking = ldev->config->write;
	led->cdev.flags = LED_HW_PLUGGABLE;
	led->ldev = ldev;

	return devm_led_classdev_register(&ldev->hdev->dev, &led->cdev);
}

static int hidled_init_rgb(struct hidled_device *ldev, unsigned int minor)
{
	int ret;

	/* Register the red diode */
	ret = hidled_init_led(&ldev->red, "red", ldev, minor);
	if (ret)
		return ret;

	/* Register the green diode */
	ret = hidled_init_led(&ldev->green, "green", ldev, minor);
	if (ret)
		return ret;

	/* Register the blue diode */
	return hidled_init_led(&ldev->blue, "blue", ldev, minor);
}

static int hidled_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct hidled_device *ldev;
	unsigned int minor;
	int ret, i;

	ldev = devm_kzalloc(&hdev->dev, sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ldev->hdev = hdev;
	mutex_init(&ldev->lock);

	for (i = 0; !ldev->config && i < ARRAY_SIZE(hidled_configs); i++)
		if (hidled_configs[i].type == id->driver_data)
			ldev->config = &hidled_configs[i];

	if (!ldev->config)
		return -EINVAL;

	if (ldev->config->init) {
		ret = ldev->config->init(ldev);
		if (ret)
			return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;

	minor = ((struct hidraw *) hdev->hidraw)->minor;

	ret = hidled_init_rgb(ldev, minor);
	if (ret) {
		hid_hw_stop(hdev);
		return ret;
	}

	hid_info(hdev, "%s initialized\n", ldev->config->name);

	return 0;
}

static const struct hid_device_id hidled_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_RISO_KAGAKU,
	  USB_DEVICE_ID_RI_KA_WEBMAIL), .driver_data = RISO_KAGAKU },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DREAM_CHEEKY,
	  USB_DEVICE_ID_DREAM_CHEEKY_WN), .driver_data = DREAM_CHEEKY },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DREAM_CHEEKY,
	  USB_DEVICE_ID_DREAM_CHEEKY_FA), .driver_data = DREAM_CHEEKY },
	{ }
};
MODULE_DEVICE_TABLE(hid, hidled_table);

static struct hid_driver hidled_driver = {
	.name = "hid-led",
	.probe = hidled_probe,
	.id_table = hidled_table,
};

module_hid_driver(hidled_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Heiner Kallweit <hkallweit1@gmail.com>");
MODULE_DESCRIPTION("Simple USB RGB LED driver");
