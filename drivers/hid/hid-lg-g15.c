// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for gaming keys on Logitech gaming keyboards (such as the G15)
 *
 *  Copyright (c) 2019 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/usb.h>
#include <linux/wait.h>

#include "hid-ids.h"

#define LG_G15_TRANSFER_BUF_SIZE	20

#define LG_G15_FEATURE_REPORT		0x02

enum lg_g15_model {
	LG_G15,
	LG_G15_V2,
};

enum lg_g15_led_type {
	LG_G15_KBD_BRIGHTNESS,
	LG_G15_LCD_BRIGHTNESS,
	LG_G15_BRIGHTNESS_MAX,
};

struct lg_g15_led {
	struct led_classdev cdev;
	enum led_brightness brightness;
	enum lg_g15_led_type led;
};

struct lg_g15_data {
	/* Must be first for proper dma alignment */
	u8 transfer_buf[LG_G15_TRANSFER_BUF_SIZE];
	/* Protects the transfer_buf and led brightness */
	struct mutex mutex;
	struct work_struct work;
	struct input_dev *input;
	struct hid_device *hdev;
	enum lg_g15_model model;
	struct lg_g15_led leds[LG_G15_BRIGHTNESS_MAX];
};

static int lg_g15_update_led_brightness(struct lg_g15_data *g15)
{
	int ret;

	ret = hid_hw_raw_request(g15->hdev, LG_G15_FEATURE_REPORT,
				 g15->transfer_buf, 4,
				 HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret != 4) {
		hid_err(g15->hdev, "Error getting LED brightness: %d\n", ret);
		return (ret < 0) ? ret : -EIO;
	}

	g15->leds[LG_G15_KBD_BRIGHTNESS].brightness = g15->transfer_buf[1];
	g15->leds[LG_G15_LCD_BRIGHTNESS].brightness = g15->transfer_buf[2];
	return 0;
}

static enum led_brightness lg_g15_led_get(struct led_classdev *led_cdev)
{
	struct lg_g15_led *g15_led =
		container_of(led_cdev, struct lg_g15_led, cdev);
	struct lg_g15_data *g15 = dev_get_drvdata(led_cdev->dev->parent);
	enum led_brightness brightness;

	mutex_lock(&g15->mutex);
	lg_g15_update_led_brightness(g15);
	brightness = g15->leds[g15_led->led].brightness;
	mutex_unlock(&g15->mutex);

	return brightness;
}

static int lg_g15_led_set(struct led_classdev *led_cdev,
			  enum led_brightness brightness)
{
	struct lg_g15_led *g15_led =
		container_of(led_cdev, struct lg_g15_led, cdev);
	struct lg_g15_data *g15 = dev_get_drvdata(led_cdev->dev->parent);
	int ret;

	/* Ignore LED off on unregister / keyboard unplug */
	if (led_cdev->flags & LED_UNREGISTERING)
		return 0;

	mutex_lock(&g15->mutex);

	g15->transfer_buf[0] = LG_G15_FEATURE_REPORT;
	g15->transfer_buf[1] = g15_led->led + 1;
	g15->transfer_buf[2] = brightness << (g15_led->led * 4);
	g15->transfer_buf[3] = 0;

	ret = hid_hw_raw_request(g15->hdev, LG_G15_FEATURE_REPORT,
				 g15->transfer_buf, 4,
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret == 4) {
		/* Success */
		g15_led->brightness = brightness;
		ret = 0;
	} else {
		hid_err(g15->hdev, "Error setting LED brightness: %d\n", ret);
		ret = (ret < 0) ? ret : -EIO;
	}

	mutex_unlock(&g15->mutex);

	return ret;
}

static void lg_g15_leds_changed_work(struct work_struct *work)
{
	struct lg_g15_data *g15 = container_of(work, struct lg_g15_data, work);
	enum led_brightness old_brightness[LG_G15_BRIGHTNESS_MAX];
	enum led_brightness brightness[LG_G15_BRIGHTNESS_MAX];
	int i, ret;

	mutex_lock(&g15->mutex);
	for (i = 0; i < LG_G15_BRIGHTNESS_MAX; i++)
		old_brightness[i] = g15->leds[i].brightness;

	ret = lg_g15_update_led_brightness(g15);

	for (i = 0; i < LG_G15_BRIGHTNESS_MAX; i++)
		brightness[i] = g15->leds[i].brightness;
	mutex_unlock(&g15->mutex);

	if (ret)
		return;

	for (i = 0; i < LG_G15_BRIGHTNESS_MAX; i++) {
		if (brightness[i] == old_brightness[i])
			continue;

		led_classdev_notify_brightness_hw_changed(&g15->leds[i].cdev,
							  brightness[i]);
	}
}

/* On the G15 Mark I Logitech has been quite creative with which bit is what */
static int lg_g15_event(struct lg_g15_data *g15, u8 *data, int size)
{
	int i, val;

	/* G1 - G6 */
	for (i = 0; i < 6; i++) {
		val = data[i + 1] & (1 << i);
		input_report_key(g15->input, KEY_MACRO1 + i, val);
	}
	/* G7 - G12 */
	for (i = 0; i < 6; i++) {
		val = data[i + 2] & (1 << i);
		input_report_key(g15->input, KEY_MACRO7 + i, val);
	}
	/* G13 - G17 */
	for (i = 0; i < 5; i++) {
		val = data[i + 1] & (4 << i);
		input_report_key(g15->input, KEY_MACRO13 + i, val);
	}
	/* G18 */
	input_report_key(g15->input, KEY_MACRO18, data[8] & 0x40);

	/* M1 - M3 */
	for (i = 0; i < 3; i++) {
		val = data[i + 6] & (1 << i);
		input_report_key(g15->input, KEY_MACRO_PRESET1 + i, val);
	}
	/* MR */
	input_report_key(g15->input, KEY_MACRO_RECORD_START, data[7] & 0x40);

	/* Most left (round) button below the LCD */
	input_report_key(g15->input, KEY_KBD_LCD_MENU1, data[8] & 0x80);
	/* 4 other buttons below the LCD */
	for (i = 0; i < 4; i++) {
		val = data[i + 2] & 0x80;
		input_report_key(g15->input, KEY_KBD_LCD_MENU2 + i, val);
	}

	/* Backlight cycle button pressed? */
	if (data[1] & 0x80)
		schedule_work(&g15->work);

	input_sync(g15->input);
	return 0;
}

static int lg_g15_v2_event(struct lg_g15_data *g15, u8 *data, int size)
{
	int i, val;

	/* G1 - G6 */
	for (i = 0; i < 6; i++) {
		val = data[1] & (1 << i);
		input_report_key(g15->input, KEY_MACRO1 + i, val);
	}

	/* M1 - M3 + MR */
	input_report_key(g15->input, KEY_MACRO_PRESET1, data[1] & 0x40);
	input_report_key(g15->input, KEY_MACRO_PRESET2, data[1] & 0x80);
	input_report_key(g15->input, KEY_MACRO_PRESET3, data[2] & 0x20);
	input_report_key(g15->input, KEY_MACRO_RECORD_START, data[2] & 0x40);

	/* Round button to the left of the LCD */
	input_report_key(g15->input, KEY_KBD_LCD_MENU1, data[2] & 0x80);
	/* 4 buttons below the LCD */
	for (i = 0; i < 4; i++) {
		val = data[2] & (2 << i);
		input_report_key(g15->input, KEY_KBD_LCD_MENU2 + i, val);
	}

	/* Backlight cycle button pressed? */
	if (data[2] & 0x01)
		schedule_work(&g15->work);

	input_sync(g15->input);
	return 0;
}

static int lg_g15_raw_event(struct hid_device *hdev, struct hid_report *report,
			    u8 *data, int size)
{
	struct lg_g15_data *g15 = hid_get_drvdata(hdev);

	if (g15->model == LG_G15 && data[0] == 0x02 && size == 9)
		return lg_g15_event(g15, data, size);

	if (g15->model == LG_G15_V2 && data[0] == 0x02 && size == 5)
		return lg_g15_v2_event(g15, data, size);

	return 0;
}

static int lg_g15_input_open(struct input_dev *dev)
{
	struct hid_device *hdev = input_get_drvdata(dev);

	return hid_hw_open(hdev);
}

static void lg_g15_input_close(struct input_dev *dev)
{
	struct hid_device *hdev = input_get_drvdata(dev);

	hid_hw_close(hdev);
}

static int lg_g15_register_led(struct lg_g15_data *g15, int i)
{
	const char * const led_names[] = {
		"g15::kbd_backlight",
		"g15::lcd_backlight",
	};

	g15->leds[i].led = i;
	g15->leds[i].cdev.name = led_names[i];
	g15->leds[i].cdev.brightness_set_blocking = lg_g15_led_set;
	g15->leds[i].cdev.brightness_get = lg_g15_led_get;
	g15->leds[i].cdev.flags = LED_BRIGHT_HW_CHANGED;
	g15->leds[i].cdev.max_brightness = 2;

	return devm_led_classdev_register(&g15->hdev->dev, &g15->leds[i].cdev);
}

static int lg_g15_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	u8 gkeys_settings_output_report = 0;
	unsigned int connect_mask = 0;
	struct lg_g15_data *g15;
	struct input_dev *input;
	int ret, i, gkeys = 0;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	g15 = devm_kzalloc(&hdev->dev, sizeof(*g15), GFP_KERNEL);
	if (!g15)
		return -ENOMEM;

	mutex_init(&g15->mutex);

	input = devm_input_allocate_device(&hdev->dev);
	if (!input)
		return -ENOMEM;

	g15->hdev = hdev;
	g15->model = id->driver_data;
	INIT_WORK(&g15->work, lg_g15_leds_changed_work);
	hid_set_drvdata(hdev, (void *)g15);

	switch (g15->model) {
	case LG_G15:
		/*
		 * The G15 and G15 v2 use a separate usb-device (on a builtin
		 * hub) which emulates a keyboard for the F1 - F12 emulation
		 * on the G-keys, which we disable, rendering the emulated kbd
		 * non-functional, so we do not let hid-input connect.
		 */
		connect_mask = HID_CONNECT_HIDRAW;
		gkeys_settings_output_report = 0x02;
		gkeys = 18;
		break;
	case LG_G15_V2:
		connect_mask = HID_CONNECT_HIDRAW;
		gkeys_settings_output_report = 0x02;
		gkeys = 6;
		break;
	}

	ret = hid_hw_start(hdev, connect_mask);
	if (ret)
		return ret;

	/* Tell the keyboard to stop sending F1-F12 + 1-6 for G1 - G18 */
	if (gkeys_settings_output_report) {
		g15->transfer_buf[0] = gkeys_settings_output_report;
		memset(g15->transfer_buf + 1, 0, gkeys);
		/*
		 * The kbd ignores our output report if we do not queue
		 * an URB on the USB input endpoint first...
		 */
		ret = hid_hw_open(hdev);
		if (ret)
			goto error_hw_stop;
		ret = hid_hw_output_report(hdev, g15->transfer_buf, gkeys + 1);
		hid_hw_close(hdev);
	}

	if (ret < 0) {
		hid_err(hdev, "Error disabling keyboard emulation for the G-keys\n");
		goto error_hw_stop;
	}

	/* Get initial brightness levels */
	ret = lg_g15_update_led_brightness(g15);
	if (ret)
		goto error_hw_stop;

	/* Setup and register input device */
	input->name = "Logitech Gaming Keyboard Gaming Keys";
	input->phys = hdev->phys;
	input->uniq = hdev->uniq;
	input->id.bustype = hdev->bus;
	input->id.vendor  = hdev->vendor;
	input->id.product = hdev->product;
	input->id.version = hdev->version;
	input->dev.parent = &hdev->dev;
	input->open = lg_g15_input_open;
	input->close = lg_g15_input_close;

	/* G-keys */
	for (i = 0; i < gkeys; i++)
		input_set_capability(input, EV_KEY, KEY_MACRO1 + i);

	/* M1 - M3 and MR keys */
	for (i = 0; i < 3; i++)
		input_set_capability(input, EV_KEY, KEY_MACRO_PRESET1 + i);
	input_set_capability(input, EV_KEY, KEY_MACRO_RECORD_START);

	/* Keys below the LCD, intended for controlling a menu on the LCD */
	for (i = 0; i < 5; i++)
		input_set_capability(input, EV_KEY, KEY_KBD_LCD_MENU1 + i);

	g15->input = input;
	input_set_drvdata(input, hdev);

	ret = input_register_device(input);
	if (ret)
		goto error_hw_stop;

	/* Register LED devices */
	for (i = 0; i < LG_G15_BRIGHTNESS_MAX; i++) {
		ret = lg_g15_register_led(g15, i);
		if (ret)
			goto error_hw_stop;
	}

	return 0;

error_hw_stop:
	hid_hw_stop(hdev);
	return ret;
}

static const struct hid_device_id lg_g15_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
			 USB_DEVICE_ID_LOGITECH_G15_LCD),
		.driver_data = LG_G15 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH,
			 USB_DEVICE_ID_LOGITECH_G15_V2_LCD),
		.driver_data = LG_G15_V2 },
	{ }
};
MODULE_DEVICE_TABLE(hid, lg_g15_devices);

static struct hid_driver lg_g15_driver = {
	.name			= "lg-g15",
	.id_table		= lg_g15_devices,
	.raw_event		= lg_g15_raw_event,
	.probe			= lg_g15_probe,
};
module_hid_driver(lg_g15_driver);

MODULE_LICENSE("GPL");
