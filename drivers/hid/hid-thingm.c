/*
 * ThingM blink(1) USB RGB LED driver
 *
 * Copyright 2013-2014 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
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

#define REPORT_ID	1
#define REPORT_SIZE	9

/* Firmware major number of supported devices */
#define THINGM_MAJOR_MK1	'1'
#define THINGM_MAJOR_MK2	'2'

struct thingm_fwinfo {
	char major;
	unsigned numrgb;
	unsigned first;
};

static const struct thingm_fwinfo thingm_fwinfo[] = {
	{
		.major = THINGM_MAJOR_MK1,
		.numrgb = 1,
		.first = 0,
	}, {
		.major = THINGM_MAJOR_MK2,
		.numrgb = 2,
		.first = 1,
	}
};

/* A red, green or blue channel, part of an RGB chip */
struct thingm_led {
	struct thingm_rgb *rgb;
	struct led_classdev ldev;
	char name[32];
};

/* Basically a WS2812 5050 RGB LED chip */
struct thingm_rgb {
	struct thingm_device *tdev;
	struct thingm_led red;
	struct thingm_led green;
	struct thingm_led blue;
	u8 num;
};

struct thingm_device {
	struct hid_device *hdev;
	struct {
		char major;
		char minor;
	} version;
	const struct thingm_fwinfo *fwinfo;
	struct mutex lock;
	struct thingm_rgb *rgb;
};

static int thingm_send(struct thingm_device *tdev, u8 buf[REPORT_SIZE])
{
	int ret;

	hid_dbg(tdev->hdev, "-> %d %c %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx\n",
			buf[0], buf[1], buf[2], buf[3], buf[4],
			buf[5], buf[6], buf[7], buf[8]);

	mutex_lock(&tdev->lock);

	ret = hid_hw_raw_request(tdev->hdev, buf[0], buf, REPORT_SIZE,
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	mutex_unlock(&tdev->lock);

	return ret < 0 ? ret : 0;
}

static int thingm_recv(struct thingm_device *tdev, u8 buf[REPORT_SIZE])
{
	int ret;

	/*
	 * A read consists of two operations: sending the read command
	 * and the actual read from the device. Use the mutex to protect
	 * the full sequence of both operations.
	 */
	mutex_lock(&tdev->lock);

	ret = hid_hw_raw_request(tdev->hdev, buf[0], buf, REPORT_SIZE,
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		goto err;

	ret = hid_hw_raw_request(tdev->hdev, buf[0], buf, REPORT_SIZE,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		goto err;

	ret = 0;

	hid_dbg(tdev->hdev, "<- %d %c %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx\n",
			buf[0], buf[1], buf[2], buf[3], buf[4],
			buf[5], buf[6], buf[7], buf[8]);
err:
	mutex_unlock(&tdev->lock);
	return ret;
}

static int thingm_version(struct thingm_device *tdev)
{
	u8 buf[REPORT_SIZE] = { REPORT_ID, 'v', 0, 0, 0, 0, 0, 0, 0 };
	int err;

	err = thingm_recv(tdev, buf);
	if (err)
		return err;

	tdev->version.major = buf[3];
	tdev->version.minor = buf[4];

	return 0;
}

static int thingm_write_color(struct thingm_rgb *rgb)
{
	u8 buf[REPORT_SIZE] = { REPORT_ID, 'c', 0, 0, 0, 0, 0, rgb->num, 0 };

	buf[2] = rgb->red.ldev.brightness;
	buf[3] = rgb->green.ldev.brightness;
	buf[4] = rgb->blue.ldev.brightness;

	return thingm_send(rgb->tdev, buf);
}

static int thingm_led_set(struct led_classdev *ldev,
			  enum led_brightness brightness)
{
	struct thingm_led *led = container_of(ldev, struct thingm_led, ldev);
	int ret;

	ret = thingm_write_color(led->rgb);
	if (ret)
		hid_err(led->rgb->tdev->hdev, "failed to write color\n");

	return ret;
}

static int thingm_init_rgb(struct thingm_rgb *rgb)
{
	const int minor = ((struct hidraw *) rgb->tdev->hdev->hidraw)->minor;
	int err;

	/* Register the red diode */
	snprintf(rgb->red.name, sizeof(rgb->red.name),
			"thingm%d:red:led%d", minor, rgb->num);
	rgb->red.ldev.name = rgb->red.name;
	rgb->red.ldev.max_brightness = 255;
	rgb->red.ldev.brightness_set_blocking = thingm_led_set;
	rgb->red.rgb = rgb;

	err = devm_led_classdev_register(&rgb->tdev->hdev->dev,
					 &rgb->red.ldev);
	if (err)
		return err;

	/* Register the green diode */
	snprintf(rgb->green.name, sizeof(rgb->green.name),
			"thingm%d:green:led%d", minor, rgb->num);
	rgb->green.ldev.name = rgb->green.name;
	rgb->green.ldev.max_brightness = 255;
	rgb->green.ldev.brightness_set_blocking = thingm_led_set;
	rgb->green.rgb = rgb;

	err = devm_led_classdev_register(&rgb->tdev->hdev->dev,
					 &rgb->green.ldev);
	if (err)
		return err;

	/* Register the blue diode */
	snprintf(rgb->blue.name, sizeof(rgb->blue.name),
			"thingm%d:blue:led%d", minor, rgb->num);
	rgb->blue.ldev.name = rgb->blue.name;
	rgb->blue.ldev.max_brightness = 255;
	rgb->blue.ldev.brightness_set_blocking = thingm_led_set;
	rgb->blue.rgb = rgb;

	err = devm_led_classdev_register(&rgb->tdev->hdev->dev,
					 &rgb->blue.ldev);
	return err;
}

static int thingm_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct thingm_device *tdev;
	int i, err;

	tdev = devm_kzalloc(&hdev->dev, sizeof(struct thingm_device),
			GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	tdev->hdev = hdev;
	hid_set_drvdata(hdev, tdev);

	err = hid_parse(hdev);
	if (err)
		return err;

	mutex_init(&tdev->lock);

	err = thingm_version(tdev);
	if (err)
		return err;

	hid_dbg(hdev, "firmware version: %c.%c\n",
			tdev->version.major, tdev->version.minor);

	for (i = 0; i < ARRAY_SIZE(thingm_fwinfo) && !tdev->fwinfo; ++i)
		if (thingm_fwinfo[i].major == tdev->version.major)
			tdev->fwinfo = &thingm_fwinfo[i];

	if (!tdev->fwinfo) {
		hid_err(hdev, "unsupported firmware %c\n", tdev->version.major);
		return -ENODEV;
	}

	tdev->rgb = devm_kzalloc(&hdev->dev,
			sizeof(struct thingm_rgb) * tdev->fwinfo->numrgb,
			GFP_KERNEL);
	if (!tdev->rgb)
		return -ENOMEM;

	err = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (err)
		return err;

	for (i = 0; i < tdev->fwinfo->numrgb; ++i) {
		struct thingm_rgb *rgb = tdev->rgb + i;

		rgb->tdev = tdev;
		rgb->num = tdev->fwinfo->first + i;
		err = thingm_init_rgb(rgb);
		if (err) {
			hid_hw_stop(hdev);
			return err;
		}
	}

	return 0;
}

static const struct hid_device_id thingm_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_THINGM, USB_DEVICE_ID_BLINK1) },
	{ }
};
MODULE_DEVICE_TABLE(hid, thingm_table);

static struct hid_driver thingm_driver = {
	.name = "thingm",
	.probe = thingm_probe,
	.id_table = thingm_table,
};

module_hid_driver(thingm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vivien Didelot <vivien.didelot@savoirfairelinux.com>");
MODULE_DESCRIPTION("ThingM blink(1) USB RGB LED driver");
