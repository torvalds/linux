/*
 * MSI GT683R led driver
 *
 * Copyright (c) 2014 Janne Kanniainen <janne.kanniainen@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>

#include "hid-ids.h"

#define GT683R_BUFFER_SIZE			8

/*
 * GT683R_LED_OFF: all LEDs are off
 * GT683R_LED_AUDIO: LEDs brightness depends on sound level
 * GT683R_LED_BREATHING: LEDs brightness varies at human breathing rate
 * GT683R_LED_NORMAL: LEDs are fully on when enabled
 */
enum gt683r_led_mode {
	GT683R_LED_OFF = 0,
	GT683R_LED_AUDIO = 2,
	GT683R_LED_BREATHING = 3,
	GT683R_LED_NORMAL = 5
};

enum gt683r_panels {
	GT683R_LED_BACK = 0,
	GT683R_LED_SIDE = 1,
	GT683R_LED_FRONT = 2,
	GT683R_LED_COUNT,
};

static const char * const gt683r_panel_names[] = {
	"back",
	"side",
	"front",
};

struct gt683r_led {
	struct hid_device *hdev;
	struct led_classdev led_devs[GT683R_LED_COUNT];
	struct mutex lock;
	struct work_struct work;
	enum led_brightness brightnesses[GT683R_LED_COUNT];
	enum gt683r_led_mode mode;
};

static const struct hid_device_id gt683r_led_id[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MSI, USB_DEVICE_ID_MSI_GT683R_LED_PANEL) },
	{ }
};

static void gt683r_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	int i;
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct gt683r_led *led = hid_get_drvdata(hdev);

	for (i = 0; i < GT683R_LED_COUNT; i++) {
		if (led_cdev == &led->led_devs[i])
			break;
	}

	if (i < GT683R_LED_COUNT) {
		led->brightnesses[i] = brightness;
		schedule_work(&led->work);
	}
}

static ssize_t leds_mode_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	u8 sysfs_mode;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct gt683r_led *led = hid_get_drvdata(hdev);

	if (led->mode == GT683R_LED_NORMAL)
		sysfs_mode = 0;
	else if (led->mode == GT683R_LED_AUDIO)
		sysfs_mode = 1;
	else
		sysfs_mode = 2;

	return scnprintf(buf, PAGE_SIZE, "%u\n", sysfs_mode);
}

static ssize_t leds_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 sysfs_mode;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct gt683r_led *led = hid_get_drvdata(hdev);


	if (kstrtou8(buf, 10, &sysfs_mode) || sysfs_mode > 2)
		return -EINVAL;

	mutex_lock(&led->lock);

	if (sysfs_mode == 0)
		led->mode = GT683R_LED_NORMAL;
	else if (sysfs_mode == 1)
		led->mode = GT683R_LED_AUDIO;
	else
		led->mode = GT683R_LED_BREATHING;

	mutex_unlock(&led->lock);
	schedule_work(&led->work);

	return count;
}

static int gt683r_led_snd_msg(struct gt683r_led *led, u8 *msg)
{
	int ret;

	ret = hid_hw_raw_request(led->hdev, msg[0], msg, GT683R_BUFFER_SIZE,
				HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret != GT683R_BUFFER_SIZE) {
		hid_err(led->hdev,
			"failed to send set report request: %i\n", ret);
		if (ret < 0)
			return ret;
		return -EIO;
	}

	return 0;
}

static int gt683r_leds_set(struct gt683r_led *led, u8 leds)
{
	int ret;
	u8 *buffer;

	buffer = kzalloc(GT683R_BUFFER_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = 0x01;
	buffer[1] = 0x02;
	buffer[2] = 0x30;
	buffer[3] = leds;
	ret = gt683r_led_snd_msg(led, buffer);

	kfree(buffer);
	return ret;
}

static int gt683r_mode_set(struct gt683r_led *led, u8 mode)
{
	int ret;
	u8 *buffer;

	buffer = kzalloc(GT683R_BUFFER_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = 0x01;
	buffer[1] = 0x02;
	buffer[2] = 0x20;
	buffer[3] = mode;
	buffer[4] = 0x01;
	ret = gt683r_led_snd_msg(led, buffer);

	kfree(buffer);
	return ret;
}

static void gt683r_led_work(struct work_struct *work)
{
	int i;
	u8 leds = 0;
	u8 mode;
	struct gt683r_led *led = container_of(work, struct gt683r_led, work);

	mutex_lock(&led->lock);

	for (i = 0; i < GT683R_LED_COUNT; i++) {
		if (led->brightnesses[i])
			leds |= BIT(i);
	}

	if (gt683r_leds_set(led, leds))
		goto fail;

	if (leds)
		mode = led->mode;
	else
		mode = GT683R_LED_OFF;

	gt683r_mode_set(led, mode);
fail:
	mutex_unlock(&led->lock);
}

static DEVICE_ATTR_RW(leds_mode);

static int gt683r_led_probe(struct hid_device *hdev,
			const struct hid_device_id *id)
{
	int i;
	int ret;
	int name_sz;
	char *name;
	struct gt683r_led *led;

	led = devm_kzalloc(&hdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->mode = GT683R_LED_NORMAL;
	led->hdev = hdev;
	hid_set_drvdata(hdev, led);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid parsing failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	for (i = 0; i < GT683R_LED_COUNT; i++) {
		name_sz = strlen(dev_name(&hdev->dev)) +
				strlen(gt683r_panel_names[i]) + 3;

		name = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
		if (!name) {
			ret = -ENOMEM;
			goto fail;
		}

		snprintf(name, name_sz, "%s::%s",
				dev_name(&hdev->dev), gt683r_panel_names[i]);
		led->led_devs[i].name = name;
		led->led_devs[i].max_brightness = 1;
		led->led_devs[i].brightness_set = gt683r_brightness_set;
		ret = led_classdev_register(&hdev->dev, &led->led_devs[i]);
		if (ret) {
			hid_err(hdev, "could not register led device\n");
			goto fail;
		}
	}

	ret = device_create_file(&led->hdev->dev, &dev_attr_leds_mode);
	if (ret) {
		hid_err(hdev, "could not make mode attribute file\n");
		goto fail;
	}

	mutex_init(&led->lock);
	INIT_WORK(&led->work, gt683r_led_work);

	return 0;

fail:
	for (i = i - 1; i >= 0; i--)
		led_classdev_unregister(&led->led_devs[i]);
	hid_hw_stop(hdev);
	return ret;
}

static void gt683r_led_remove(struct hid_device *hdev)
{
	int i;
	struct gt683r_led *led = hid_get_drvdata(hdev);

	device_remove_file(&hdev->dev, &dev_attr_leds_mode);
	for (i = 0; i < GT683R_LED_COUNT; i++)
		led_classdev_unregister(&led->led_devs[i]);
	flush_work(&led->work);
	hid_hw_stop(hdev);
}

static struct hid_driver gt683r_led_driver = {
	.probe = gt683r_led_probe,
	.remove = gt683r_led_remove,
	.name = "gt683r_led",
	.id_table = gt683r_led_id,
};

module_hid_driver(gt683r_led_driver);

MODULE_AUTHOR("Janne Kanniainen");
MODULE_DESCRIPTION("MSI GT683R led driver");
MODULE_LICENSE("GPL");
