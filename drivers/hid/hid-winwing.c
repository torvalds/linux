// SPDX-License-Identifier: GPL-2.0

/*
 * HID driver for WinWing Orion 2 throttle
 *
 * Copyright (c) 2023 Ivan Gorinov
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>

#define MAX_REPORT 16

struct winwing_led {
	struct led_classdev cdev;
	struct hid_device *hdev;
	int number;
};

struct winwing_led_info {
	int number;
	int max_brightness;
	const char *led_name;
};

static const struct winwing_led_info led_info[3] = {
	{ 0, 255, "backlight" },
	{ 1, 1, "a-a" },
	{ 2, 1, "a-g" },
};

struct winwing_drv_data {
	struct hid_device *hdev;
	__u8 *report_buf;
	struct mutex lock;
	unsigned int num_leds;
	struct winwing_led leds[];
};

static int winwing_led_write(struct led_classdev *cdev,
		enum led_brightness br)
{
	struct winwing_led *led = (struct winwing_led *) cdev;
	struct winwing_drv_data *data = hid_get_drvdata(led->hdev);
	__u8 *buf = data->report_buf;
	int ret;

	mutex_lock(&data->lock);

	buf[0] = 0x02;
	buf[1] = 0x60;
	buf[2] = 0xbe;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x03;
	buf[6] = 0x49;
	buf[7] = led->number;
	buf[8] = br;
	buf[9] = 0x00;
	buf[10] = 0;
	buf[11] = 0;
	buf[12] = 0;
	buf[13] = 0;

	ret = hid_hw_output_report(led->hdev, buf, 14);

	mutex_unlock(&data->lock);

	return ret;
}

static int winwing_init_led(struct hid_device *hdev,
		struct input_dev *input)
{
	struct winwing_drv_data *data;
	struct winwing_led *led;
	int ret;
	int i;

	size_t data_size = struct_size(data, leds, 3);

	data = devm_kzalloc(&hdev->dev, data_size, GFP_KERNEL);

	if (!data)
		return -ENOMEM;

	data->report_buf = devm_kmalloc(&hdev->dev, MAX_REPORT, GFP_KERNEL);

	if (!data->report_buf)
		return -ENOMEM;

	for (i = 0; i < 3; i += 1) {
		const struct winwing_led_info *info = &led_info[i];

		led = &data->leds[i];
		led->hdev = hdev;
		led->number = info->number;
		led->cdev.max_brightness = info->max_brightness;
		led->cdev.brightness_set_blocking = winwing_led_write;
		led->cdev.flags = LED_HW_PLUGGABLE;
		led->cdev.name = devm_kasprintf(&hdev->dev, GFP_KERNEL,
						"%s::%s",
						dev_name(&input->dev),
						info->led_name);

		ret = devm_led_classdev_register(&hdev->dev, &led->cdev);
		if (ret)
			return ret;
	}

	hid_set_drvdata(hdev, data);

	return ret;
}

static int winwing_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	return 0;
}

static int winwing_input_configured(struct hid_device *hdev,
		struct hid_input *hidinput)
{
	int ret;

	ret = winwing_init_led(hdev, hidinput->input);

	if (ret)
		hid_err(hdev, "led init failed\n");

	return ret;
}

static const __u8 original_rdesc_buttons[] = {
	0x05, 0x09, 0x19, 0x01, 0x29, 0x6F,
	0x15, 0x00, 0x25, 0x01, 0x35, 0x00,
	0x45, 0x01, 0x75, 0x01, 0x95, 0x6F,
	0x81, 0x02, 0x75, 0x01, 0x95, 0x01,
	0x81, 0x01
};

/*
 * HID report descriptor shows 111 buttons, which exceeds maximum
 * number of buttons (80) supported by Linux kernel HID subsystem.
 *
 * This module skips numbers 32-63, unused on some throttle grips.
 */

static const __u8 *winwing_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	int sig_length = sizeof(original_rdesc_buttons);
	int unused_button_numbers = 32;

	if (*rsize < 34)
		return rdesc;

	if (memcmp(rdesc + 8, original_rdesc_buttons, sig_length) == 0) {

		/* Usage Maximum */
		rdesc[13] -= unused_button_numbers;

		/*  Report Count for buttons */
		rdesc[25] -= unused_button_numbers;

		/*  Report Count for padding [HID1_11, 6.2.2.9] */
		rdesc[31] += unused_button_numbers;

		hid_info(hdev, "winwing descriptor fixed\n");
	}

	return rdesc;
}

static int winwing_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *raw_data, int size)
{
	if (size >= 15) {
		/* Skip buttons 32 .. 63 */
		memmove(raw_data + 5, raw_data + 9, 6);

		/* Clear the padding */
		memset(raw_data + 11, 0, 4);
	}

	return 0;
}

static const struct hid_device_id winwing_devices[] = {
	{ HID_USB_DEVICE(0x4098, 0xbe62) },  /* TGRIP-18 */
	{ HID_USB_DEVICE(0x4098, 0xbe68) },  /* TGRIP-16EX */
	{}
};

MODULE_DEVICE_TABLE(hid, winwing_devices);

static struct hid_driver winwing_driver = {
	.name = "winwing",
	.id_table = winwing_devices,
	.probe = winwing_probe,
	.input_configured = winwing_input_configured,
	.report_fixup = winwing_report_fixup,
	.raw_event = winwing_raw_event,
};
module_hid_driver(winwing_driver);

MODULE_DESCRIPTION("HID driver for WinWing Orion 2 throttle");
MODULE_LICENSE("GPL");
