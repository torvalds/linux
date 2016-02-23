/*
 * HID driver for Corsair devices
 *
 * Supported devices:
 *  - Vengeance K90 Keyboard
 *
 * Copyright (c) 2015 Clement Vuchener
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/leds.h>

#include "hid-ids.h"

#define CORSAIR_USE_K90_MACRO	(1<<0)
#define CORSAIR_USE_K90_BACKLIGHT	(1<<1)

struct k90_led {
	struct led_classdev cdev;
	int brightness;
	struct work_struct work;
	bool removed;
};

struct k90_drvdata {
	struct k90_led record_led;
};

struct corsair_drvdata {
	unsigned long quirks;
	struct k90_drvdata *k90;
	struct k90_led *backlight;
};

#define K90_GKEY_COUNT	18

static int corsair_usage_to_gkey(unsigned int usage)
{
	/* G1 (0xd0) to G16 (0xdf) */
	if (usage >= 0xd0 && usage <= 0xdf)
		return usage - 0xd0 + 1;
	/* G17 (0xe8) to G18 (0xe9) */
	if (usage >= 0xe8 && usage <= 0xe9)
		return usage - 0xe8 + 17;
	return 0;
}

static unsigned short corsair_gkey_map[K90_GKEY_COUNT] = {
	BTN_TRIGGER_HAPPY1,
	BTN_TRIGGER_HAPPY2,
	BTN_TRIGGER_HAPPY3,
	BTN_TRIGGER_HAPPY4,
	BTN_TRIGGER_HAPPY5,
	BTN_TRIGGER_HAPPY6,
	BTN_TRIGGER_HAPPY7,
	BTN_TRIGGER_HAPPY8,
	BTN_TRIGGER_HAPPY9,
	BTN_TRIGGER_HAPPY10,
	BTN_TRIGGER_HAPPY11,
	BTN_TRIGGER_HAPPY12,
	BTN_TRIGGER_HAPPY13,
	BTN_TRIGGER_HAPPY14,
	BTN_TRIGGER_HAPPY15,
	BTN_TRIGGER_HAPPY16,
	BTN_TRIGGER_HAPPY17,
	BTN_TRIGGER_HAPPY18,
};

module_param_array_named(gkey_codes, corsair_gkey_map, ushort, NULL, S_IRUGO);
MODULE_PARM_DESC(gkey_codes, "Key codes for the G-keys");

static unsigned short corsair_record_keycodes[2] = {
	BTN_TRIGGER_HAPPY19,
	BTN_TRIGGER_HAPPY20
};

module_param_array_named(recordkey_codes, corsair_record_keycodes, ushort,
			 NULL, S_IRUGO);
MODULE_PARM_DESC(recordkey_codes, "Key codes for the MR (start and stop record) button");

static unsigned short corsair_profile_keycodes[3] = {
	BTN_TRIGGER_HAPPY21,
	BTN_TRIGGER_HAPPY22,
	BTN_TRIGGER_HAPPY23
};

module_param_array_named(profilekey_codes, corsair_profile_keycodes, ushort,
			 NULL, S_IRUGO);
MODULE_PARM_DESC(profilekey_codes, "Key codes for the profile buttons");

#define CORSAIR_USAGE_SPECIAL_MIN 0xf0
#define CORSAIR_USAGE_SPECIAL_MAX 0xff

#define CORSAIR_USAGE_MACRO_RECORD_START 0xf6
#define CORSAIR_USAGE_MACRO_RECORD_STOP 0xf7

#define CORSAIR_USAGE_PROFILE 0xf1
#define CORSAIR_USAGE_M1 0xf1
#define CORSAIR_USAGE_M2 0xf2
#define CORSAIR_USAGE_M3 0xf3
#define CORSAIR_USAGE_PROFILE_MAX 0xf3

#define CORSAIR_USAGE_META_OFF 0xf4
#define CORSAIR_USAGE_META_ON  0xf5

#define CORSAIR_USAGE_LIGHT 0xfa
#define CORSAIR_USAGE_LIGHT_OFF 0xfa
#define CORSAIR_USAGE_LIGHT_DIM 0xfb
#define CORSAIR_USAGE_LIGHT_MEDIUM 0xfc
#define CORSAIR_USAGE_LIGHT_BRIGHT 0xfd
#define CORSAIR_USAGE_LIGHT_MAX 0xfd

/* USB control protocol */

#define K90_REQUEST_BRIGHTNESS 49
#define K90_REQUEST_MACRO_MODE 2
#define K90_REQUEST_STATUS 4
#define K90_REQUEST_GET_MODE 5
#define K90_REQUEST_PROFILE 20

#define K90_MACRO_MODE_SW 0x0030
#define K90_MACRO_MODE_HW 0x0001

#define K90_MACRO_LED_ON  0x0020
#define K90_MACRO_LED_OFF 0x0040

/*
 * LED class devices
 */

#define K90_BACKLIGHT_LED_SUFFIX "::backlight"
#define K90_RECORD_LED_SUFFIX "::record"

static enum led_brightness k90_backlight_get(struct led_classdev *led_cdev)
{
	int ret;
	struct k90_led *led = container_of(led_cdev, struct k90_led, cdev);
	struct device *dev = led->cdev.dev->parent;
	struct usb_interface *usbif = to_usb_interface(dev->parent);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	int brightness;
	char data[8];

	ret = usb_control_msg(usbdev, usb_rcvctrlpipe(usbdev, 0),
			      K90_REQUEST_STATUS,
			      USB_DIR_IN | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, 0, 0, data, 8,
			      USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_warn(dev, "Failed to get K90 initial state (error %d).\n",
			 ret);
		return -EIO;
	}
	brightness = data[4];
	if (brightness < 0 || brightness > 3) {
		dev_warn(dev,
			 "Read invalid backlight brightness: %02hhx.\n",
			 data[4]);
		return -EIO;
	}
	return brightness;
}

static enum led_brightness k90_record_led_get(struct led_classdev *led_cdev)
{
	struct k90_led *led = container_of(led_cdev, struct k90_led, cdev);

	return led->brightness;
}

static void k90_brightness_set(struct led_classdev *led_cdev,
			       enum led_brightness brightness)
{
	struct k90_led *led = container_of(led_cdev, struct k90_led, cdev);

	led->brightness = brightness;
	schedule_work(&led->work);
}

static void k90_backlight_work(struct work_struct *work)
{
	int ret;
	struct k90_led *led = container_of(work, struct k90_led, work);
	struct device *dev;
	struct usb_interface *usbif;
	struct usb_device *usbdev;

	if (led->removed)
		return;

	dev = led->cdev.dev->parent;
	usbif = to_usb_interface(dev->parent);
	usbdev = interface_to_usbdev(usbif);

	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0),
			      K90_REQUEST_BRIGHTNESS,
			      USB_DIR_OUT | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, led->brightness, 0,
			      NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (ret != 0)
		dev_warn(dev, "Failed to set backlight brightness (error: %d).\n",
			 ret);
}

static void k90_record_led_work(struct work_struct *work)
{
	int ret;
	struct k90_led *led = container_of(work, struct k90_led, work);
	struct device *dev;
	struct usb_interface *usbif;
	struct usb_device *usbdev;
	int value;

	if (led->removed)
		return;

	dev = led->cdev.dev->parent;
	usbif = to_usb_interface(dev->parent);
	usbdev = interface_to_usbdev(usbif);

	if (led->brightness > 0)
		value = K90_MACRO_LED_ON;
	else
		value = K90_MACRO_LED_OFF;

	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0),
			      K90_REQUEST_MACRO_MODE,
			      USB_DIR_OUT | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, value, 0, NULL, 0,
			      USB_CTRL_SET_TIMEOUT);
	if (ret != 0)
		dev_warn(dev, "Failed to set record LED state (error: %d).\n",
			 ret);
}

/*
 * Keyboard attributes
 */

static ssize_t k90_show_macro_mode(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret;
	struct usb_interface *usbif = to_usb_interface(dev->parent);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	const char *macro_mode;
	char data[8];

	ret = usb_control_msg(usbdev, usb_rcvctrlpipe(usbdev, 0),
			      K90_REQUEST_GET_MODE,
			      USB_DIR_IN | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, 0, 0, data, 2,
			      USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_warn(dev, "Failed to get K90 initial mode (error %d).\n",
			 ret);
		return -EIO;
	}

	switch (data[0]) {
	case K90_MACRO_MODE_HW:
		macro_mode = "HW";
		break;

	case K90_MACRO_MODE_SW:
		macro_mode = "SW";
		break;
	default:
		dev_warn(dev, "K90 in unknown mode: %02hhx.\n",
			 data[0]);
		return -EIO;
	}

	return snprintf(buf, PAGE_SIZE, "%s\n", macro_mode);
}

static ssize_t k90_store_macro_mode(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int ret;
	struct usb_interface *usbif = to_usb_interface(dev->parent);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	__u16 value;

	if (strncmp(buf, "SW", 2) == 0)
		value = K90_MACRO_MODE_SW;
	else if (strncmp(buf, "HW", 2) == 0)
		value = K90_MACRO_MODE_HW;
	else
		return -EINVAL;

	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0),
			      K90_REQUEST_MACRO_MODE,
			      USB_DIR_OUT | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, value, 0, NULL, 0,
			      USB_CTRL_SET_TIMEOUT);
	if (ret != 0) {
		dev_warn(dev, "Failed to set macro mode.\n");
		return ret;
	}

	return count;
}

static ssize_t k90_show_current_profile(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret;
	struct usb_interface *usbif = to_usb_interface(dev->parent);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	int current_profile;
	char data[8];

	ret = usb_control_msg(usbdev, usb_rcvctrlpipe(usbdev, 0),
			      K90_REQUEST_STATUS,
			      USB_DIR_IN | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, 0, 0, data, 8,
			      USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_warn(dev, "Failed to get K90 initial state (error %d).\n",
			 ret);
		return -EIO;
	}
	current_profile = data[7];
	if (current_profile < 1 || current_profile > 3) {
		dev_warn(dev, "Read invalid current profile: %02hhx.\n",
			 data[7]);
		return -EIO;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", current_profile);
}

static ssize_t k90_store_current_profile(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	int ret;
	struct usb_interface *usbif = to_usb_interface(dev->parent);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	int profile;

	if (kstrtoint(buf, 10, &profile))
		return -EINVAL;
	if (profile < 1 || profile > 3)
		return -EINVAL;

	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0),
			      K90_REQUEST_PROFILE,
			      USB_DIR_OUT | USB_TYPE_VENDOR |
			      USB_RECIP_DEVICE, profile, 0, NULL, 0,
			      USB_CTRL_SET_TIMEOUT);
	if (ret != 0) {
		dev_warn(dev, "Failed to change current profile (error %d).\n",
			 ret);
		return ret;
	}

	return count;
}

static DEVICE_ATTR(macro_mode, 0644, k90_show_macro_mode, k90_store_macro_mode);
static DEVICE_ATTR(current_profile, 0644, k90_show_current_profile,
		   k90_store_current_profile);

static struct attribute *k90_attrs[] = {
	&dev_attr_macro_mode.attr,
	&dev_attr_current_profile.attr,
	NULL
};

static const struct attribute_group k90_attr_group = {
	.attrs = k90_attrs,
};

/*
 * Driver functions
 */

static int k90_init_backlight(struct hid_device *dev)
{
	int ret;
	struct corsair_drvdata *drvdata = hid_get_drvdata(dev);
	size_t name_sz;
	char *name;

	drvdata->backlight = kzalloc(sizeof(struct k90_led), GFP_KERNEL);
	if (!drvdata->backlight) {
		ret = -ENOMEM;
		goto fail_backlight_alloc;
	}

	name_sz =
	    strlen(dev_name(&dev->dev)) + sizeof(K90_BACKLIGHT_LED_SUFFIX);
	name = kzalloc(name_sz, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto fail_name_alloc;
	}
	snprintf(name, name_sz, "%s" K90_BACKLIGHT_LED_SUFFIX,
		 dev_name(&dev->dev));
	drvdata->backlight->removed = false;
	drvdata->backlight->cdev.name = name;
	drvdata->backlight->cdev.max_brightness = 3;
	drvdata->backlight->cdev.brightness_set = k90_brightness_set;
	drvdata->backlight->cdev.brightness_get = k90_backlight_get;
	INIT_WORK(&drvdata->backlight->work, k90_backlight_work);
	ret = led_classdev_register(&dev->dev, &drvdata->backlight->cdev);
	if (ret != 0)
		goto fail_register_cdev;

	return 0;

fail_register_cdev:
	kfree(drvdata->backlight->cdev.name);
fail_name_alloc:
	kfree(drvdata->backlight);
	drvdata->backlight = NULL;
fail_backlight_alloc:
	return ret;
}

static int k90_init_macro_functions(struct hid_device *dev)
{
	int ret;
	struct corsair_drvdata *drvdata = hid_get_drvdata(dev);
	struct k90_drvdata *k90;
	size_t name_sz;
	char *name;

	k90 = kzalloc(sizeof(struct k90_drvdata), GFP_KERNEL);
	if (!k90) {
		ret = -ENOMEM;
		goto fail_drvdata;
	}
	drvdata->k90 = k90;

	/* Init LED device for record LED */
	name_sz = strlen(dev_name(&dev->dev)) + sizeof(K90_RECORD_LED_SUFFIX);
	name = kzalloc(name_sz, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto fail_record_led_alloc;
	}
	snprintf(name, name_sz, "%s" K90_RECORD_LED_SUFFIX,
		 dev_name(&dev->dev));
	k90->record_led.removed = false;
	k90->record_led.cdev.name = name;
	k90->record_led.cdev.max_brightness = 1;
	k90->record_led.cdev.brightness_set = k90_brightness_set;
	k90->record_led.cdev.brightness_get = k90_record_led_get;
	INIT_WORK(&k90->record_led.work, k90_record_led_work);
	k90->record_led.brightness = 0;
	ret = led_classdev_register(&dev->dev, &k90->record_led.cdev);
	if (ret != 0)
		goto fail_record_led;

	/* Init attributes */
	ret = sysfs_create_group(&dev->dev.kobj, &k90_attr_group);
	if (ret != 0)
		goto fail_sysfs;

	return 0;

fail_sysfs:
	k90->record_led.removed = true;
	led_classdev_unregister(&k90->record_led.cdev);
	cancel_work_sync(&k90->record_led.work);
fail_record_led:
	kfree(k90->record_led.cdev.name);
fail_record_led_alloc:
	kfree(k90);
fail_drvdata:
	drvdata->k90 = NULL;
	return ret;
}

static void k90_cleanup_backlight(struct hid_device *dev)
{
	struct corsair_drvdata *drvdata = hid_get_drvdata(dev);

	if (drvdata->backlight) {
		drvdata->backlight->removed = true;
		led_classdev_unregister(&drvdata->backlight->cdev);
		cancel_work_sync(&drvdata->backlight->work);
		kfree(drvdata->backlight->cdev.name);
		kfree(drvdata->backlight);
	}
}

static void k90_cleanup_macro_functions(struct hid_device *dev)
{
	struct corsair_drvdata *drvdata = hid_get_drvdata(dev);
	struct k90_drvdata *k90 = drvdata->k90;

	if (k90) {
		sysfs_remove_group(&dev->dev.kobj, &k90_attr_group);

		k90->record_led.removed = true;
		led_classdev_unregister(&k90->record_led.cdev);
		cancel_work_sync(&k90->record_led.work);
		kfree(k90->record_led.cdev.name);

		kfree(k90);
	}
}

static int corsair_probe(struct hid_device *dev, const struct hid_device_id *id)
{
	int ret;
	unsigned long quirks = id->driver_data;
	struct corsair_drvdata *drvdata;
	struct usb_interface *usbif = to_usb_interface(dev->dev.parent);

	drvdata = devm_kzalloc(&dev->dev, sizeof(struct corsair_drvdata),
			       GFP_KERNEL);
	if (drvdata == NULL)
		return -ENOMEM;
	drvdata->quirks = quirks;
	hid_set_drvdata(dev, drvdata);

	ret = hid_parse(dev);
	if (ret != 0) {
		hid_err(dev, "parse failed\n");
		return ret;
	}
	ret = hid_hw_start(dev, HID_CONNECT_DEFAULT);
	if (ret != 0) {
		hid_err(dev, "hw start failed\n");
		return ret;
	}

	if (usbif->cur_altsetting->desc.bInterfaceNumber == 0) {
		if (quirks & CORSAIR_USE_K90_MACRO) {
			ret = k90_init_macro_functions(dev);
			if (ret != 0)
				hid_warn(dev, "Failed to initialize K90 macro functions.\n");
		}
		if (quirks & CORSAIR_USE_K90_BACKLIGHT) {
			ret = k90_init_backlight(dev);
			if (ret != 0)
				hid_warn(dev, "Failed to initialize K90 backlight.\n");
		}
	}

	return 0;
}

static void corsair_remove(struct hid_device *dev)
{
	k90_cleanup_macro_functions(dev);
	k90_cleanup_backlight(dev);

	hid_hw_stop(dev);
}

static int corsair_event(struct hid_device *dev, struct hid_field *field,
			 struct hid_usage *usage, __s32 value)
{
	struct corsair_drvdata *drvdata = hid_get_drvdata(dev);

	if (!drvdata->k90)
		return 0;

	switch (usage->hid & HID_USAGE) {
	case CORSAIR_USAGE_MACRO_RECORD_START:
		drvdata->k90->record_led.brightness = 1;
		break;
	case CORSAIR_USAGE_MACRO_RECORD_STOP:
		drvdata->k90->record_led.brightness = 0;
		break;
	default:
		break;
	}

	return 0;
}

static int corsair_input_mapping(struct hid_device *dev,
				 struct hid_input *input,
				 struct hid_field *field,
				 struct hid_usage *usage, unsigned long **bit,
				 int *max)
{
	int gkey;

	gkey = corsair_usage_to_gkey(usage->hid & HID_USAGE);
	if (gkey != 0) {
		hid_map_usage_clear(input, usage, bit, max, EV_KEY,
				    corsair_gkey_map[gkey - 1]);
		return 1;
	}
	if ((usage->hid & HID_USAGE) >= CORSAIR_USAGE_SPECIAL_MIN &&
	    (usage->hid & HID_USAGE) <= CORSAIR_USAGE_SPECIAL_MAX) {
		switch (usage->hid & HID_USAGE) {
		case CORSAIR_USAGE_MACRO_RECORD_START:
			hid_map_usage_clear(input, usage, bit, max, EV_KEY,
					    corsair_record_keycodes[0]);
			return 1;

		case CORSAIR_USAGE_MACRO_RECORD_STOP:
			hid_map_usage_clear(input, usage, bit, max, EV_KEY,
					    corsair_record_keycodes[1]);
			return 1;

		case CORSAIR_USAGE_M1:
			hid_map_usage_clear(input, usage, bit, max, EV_KEY,
					    corsair_profile_keycodes[0]);
			return 1;

		case CORSAIR_USAGE_M2:
			hid_map_usage_clear(input, usage, bit, max, EV_KEY,
					    corsair_profile_keycodes[1]);
			return 1;

		case CORSAIR_USAGE_M3:
			hid_map_usage_clear(input, usage, bit, max, EV_KEY,
					    corsair_profile_keycodes[2]);
			return 1;

		default:
			return -1;
		}
	}

	return 0;
}

static const struct hid_device_id corsair_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, USB_DEVICE_ID_CORSAIR_K90),
		.driver_data = CORSAIR_USE_K90_MACRO |
			       CORSAIR_USE_K90_BACKLIGHT },
	{}
};

MODULE_DEVICE_TABLE(hid, corsair_devices);

static struct hid_driver corsair_driver = {
	.name = "corsair",
	.id_table = corsair_devices,
	.probe = corsair_probe,
	.event = corsair_event,
	.remove = corsair_remove,
	.input_mapping = corsair_input_mapping,
};

module_hid_driver(corsair_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clement Vuchener");
MODULE_DESCRIPTION("HID driver for Corsair devices");
