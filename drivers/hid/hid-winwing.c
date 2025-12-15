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
	int map_more_buttons;
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

	data = hid_get_drvdata(hdev);

	if (!data)
		return -EINVAL;

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

		if (!led->cdev.name)
			return -ENOMEM;

		ret = devm_led_classdev_register(&hdev->dev, &led->cdev);
		if (ret)
			return ret;
	}

	return ret;
}

static int winwing_map_button(int button, int map_more_buttons)
{
	if (button < 1)
		return KEY_RESERVED;

	if (button > 112)
		return KEY_RESERVED;

	if (button <= 16) {
		/*
		 * Grip buttons [1 .. 16] are mapped to
		 * key codes BTN_TRIGGER .. BTN_DEAD
		 */
		return (button - 1) + BTN_JOYSTICK;
	}

	if (button >= 65) {
		/*
		 * Base buttons [65 .. 112] are mapped to
		 * key codes BTN_TRIGGER_HAPPY17 .. KEY_MAX
		 */
		return (button - 65) + BTN_TRIGGER_HAPPY17;
	}

	if (!map_more_buttons) {
		/*
		 * Not mapping numbers [33 .. 64] which
		 * are not assigned to any real buttons
		 */
		if (button >= 33)
			return KEY_RESERVED;
		/*
		 * Grip buttons [17 .. 32] are mapped to
		 * BTN_TRIGGER_HAPPY1 .. BTN_TRIGGER_HAPPY16
		 */
		return (button - 17) + BTN_TRIGGER_HAPPY1;
	}

	if (button >= 49) {
		/*
		 * Grip buttons [49 .. 64] are mapped to
		 * BTN_TRIGGER_HAPPY1 .. BTN_TRIGGER_HAPPY16
		 */
		return (button - 49) + BTN_TRIGGER_HAPPY1;
	}

	/*
	 * Grip buttons [17 .. 44] are mapped to
	 * key codes KEY_MACRO1 .. KEY_MACRO28;
	 * also mapping numbers [45 .. 48] which
	 * are not assigned to any real buttons.
	 */
	return (button - 17) + KEY_MACRO1;
}

static int winwing_input_mapping(struct hid_device *hdev,
	struct hid_input *hi, struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	struct winwing_drv_data *data;
	int code = KEY_RESERVED;
	int button = 0;

	data = hid_get_drvdata(hdev);

	if (!data)
		return -EINVAL;

	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
		return 0;

	if (field->application != HID_GD_JOYSTICK)
		return 0;

	/* Button numbers start with 1 */
	button = usage->hid & HID_USAGE;

	code = winwing_map_button(button, data->map_more_buttons);

	hid_map_usage(hi, usage, bit, max, EV_KEY, code);

	return 1;
}

static int winwing_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	struct winwing_drv_data *data;
	size_t data_size = struct_size(data, leds, 3);
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	data = devm_kzalloc(&hdev->dev, data_size, GFP_KERNEL);

	if (!data)
		return -ENOMEM;

	data->map_more_buttons = id->driver_data;

	hid_set_drvdata(hdev, data);

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

static const struct hid_device_id winwing_devices[] = {
	{ HID_USB_DEVICE(0x4098, 0xbd65), .driver_data = 1 },  /* TGRIP-15E  */
	{ HID_USB_DEVICE(0x4098, 0xbd64), .driver_data = 1 },  /* TGRIP-15EX */
	{ HID_USB_DEVICE(0x4098, 0xbe68), .driver_data = 0 },  /* TGRIP-16EX */
	{ HID_USB_DEVICE(0x4098, 0xbe62), .driver_data = 0 },  /* TGRIP-18   */
	{}
};

MODULE_DEVICE_TABLE(hid, winwing_devices);

static struct hid_driver winwing_driver = {
	.name = "winwing",
	.id_table = winwing_devices,
	.input_configured = winwing_input_configured,
	.input_mapping = winwing_input_mapping,
	.probe = winwing_probe,
};
module_hid_driver(winwing_driver);

MODULE_DESCRIPTION("HID driver for WinWing Orion 2 throttle");
MODULE_LICENSE("GPL");
