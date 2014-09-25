/*
 *  HID driver for Lenovo:
 *  - ThinkPad USB Keyboard with TrackPoint (tpkbd)
 *  - ThinkPad Compact Bluetooth Keyboard with TrackPoint (cptkbd)
 *  - ThinkPad Compact USB Keyboard with TrackPoint (cptkbd)
 *
 *  Copyright (c) 2012 Bernhard Seibold
 *  Copyright (c) 2014 Jamie Lentin <jm@lentin.co.uk>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/leds.h>

#include "hid-ids.h"

struct lenovo_drvdata_tpkbd {
	int led_state;
	struct led_classdev led_mute;
	struct led_classdev led_micmute;
	int press_to_select;
	int dragging;
	int release_to_select;
	int select_right;
	int sensitivity;
	int press_speed;
};

struct lenovo_drvdata_cptkbd {
	bool fn_lock;
};

#define map_key_clear(c) hid_map_usage_clear(hi, usage, bit, max, EV_KEY, (c))

static int lenovo_input_mapping_tpkbd(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if (usage->hid == (HID_UP_BUTTON | 0x0010)) {
		/* This sub-device contains trackpoint, mark it */
		hid_set_drvdata(hdev, (void *)1);
		map_key_clear(KEY_MICMUTE);
		return 1;
	}
	return 0;
}

static int lenovo_input_mapping_cptkbd(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	/* HID_UP_LNVENDOR = USB, HID_UP_MSVENDOR = BT */
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_MSVENDOR ||
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_LNVENDOR) {
		set_bit(EV_REP, hi->input->evbit);
		switch (usage->hid & HID_USAGE) {
		case 0x00f1: /* Fn-F4: Mic mute */
			map_key_clear(KEY_MICMUTE);
			return 1;
		case 0x00f2: /* Fn-F5: Brightness down */
			map_key_clear(KEY_BRIGHTNESSDOWN);
			return 1;
		case 0x00f3: /* Fn-F6: Brightness up */
			map_key_clear(KEY_BRIGHTNESSUP);
			return 1;
		case 0x00f4: /* Fn-F7: External display (projector) */
			map_key_clear(KEY_SWITCHVIDEOMODE);
			return 1;
		case 0x00f5: /* Fn-F8: Wireless */
			map_key_clear(KEY_WLAN);
			return 1;
		case 0x00f6: /* Fn-F9: Control panel */
			map_key_clear(KEY_CONFIG);
			return 1;
		case 0x00f8: /* Fn-F11: View open applications (3 boxes) */
			map_key_clear(KEY_SCALE);
			return 1;
		case 0x00fa: /* Fn-Esc: Fn-lock toggle */
			map_key_clear(KEY_FN_ESC);
			return 1;
		case 0x00fb: /* Fn-F12: Open My computer (6 boxes) USB-only */
			/* NB: This mapping is invented in raw_event below */
			map_key_clear(KEY_FILE);
			return 1;
		}
	}

	return 0;
}

static int lenovo_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_TPKBD:
		return lenovo_input_mapping_tpkbd(hdev, hi, field,
							usage, bit, max);
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_CBTKBD:
		return lenovo_input_mapping_cptkbd(hdev, hi, field,
							usage, bit, max);
	default:
		return 0;
	}
}

#undef map_key_clear

/* Send a config command to the keyboard */
static int lenovo_send_cmd_cptkbd(struct hid_device *hdev,
			unsigned char byte2, unsigned char byte3)
{
	int ret;
	unsigned char buf[] = {0x18, byte2, byte3};

	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
		ret = hid_hw_raw_request(hdev, 0x13, buf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
		break;
	case USB_DEVICE_ID_LENOVO_CBTKBD:
		ret = hid_hw_output_report(hdev, buf, sizeof(buf));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret < 0 ? ret : 0; /* BT returns 0, USB returns sizeof(buf) */
}

static void lenovo_features_set_cptkbd(struct hid_device *hdev)
{
	int ret;
	struct lenovo_drvdata_cptkbd *cptkbd_data = hid_get_drvdata(hdev);

	ret = lenovo_send_cmd_cptkbd(hdev, 0x05, cptkbd_data->fn_lock);
	if (ret)
		hid_err(hdev, "Fn-lock setting failed: %d\n", ret);
}

static ssize_t attr_fn_lock_show_cptkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_cptkbd *cptkbd_data = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", cptkbd_data->fn_lock);
}

static ssize_t attr_fn_lock_store_cptkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_cptkbd *cptkbd_data = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	cptkbd_data->fn_lock = !!value;
	lenovo_features_set_cptkbd(hdev);

	return count;
}

static struct device_attribute dev_attr_fn_lock_cptkbd =
	__ATTR(fn_lock, S_IWUSR | S_IRUGO,
			attr_fn_lock_show_cptkbd,
			attr_fn_lock_store_cptkbd);

static struct attribute *lenovo_attributes_cptkbd[] = {
	&dev_attr_fn_lock_cptkbd.attr,
	NULL
};

static const struct attribute_group lenovo_attr_group_cptkbd = {
	.attrs = lenovo_attributes_cptkbd,
};

static int lenovo_raw_event(struct hid_device *hdev,
			struct hid_report *report, u8 *data, int size)
{
	/*
	 * Compact USB keyboard's Fn-F12 report holds down many other keys, and
	 * its own key is outside the usage page range. Remove extra
	 * keypresses and remap to inside usage page.
	 */
	if (unlikely(hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD
			&& size == 3
			&& data[0] == 0x15
			&& data[1] == 0x94
			&& data[2] == 0x01)) {
		data[1] = 0x0;
		data[2] = 0x4;
	}

	return 0;
}

static int lenovo_features_set_tpkbd(struct hid_device *hdev)
{
	struct hid_report *report;
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);

	report = hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[4];

	report->field[0]->value[0]  = data_pointer->press_to_select   ? 0x01 : 0x02;
	report->field[0]->value[0] |= data_pointer->dragging          ? 0x04 : 0x08;
	report->field[0]->value[0] |= data_pointer->release_to_select ? 0x10 : 0x20;
	report->field[0]->value[0] |= data_pointer->select_right      ? 0x80 : 0x40;
	report->field[1]->value[0] = 0x03; // unknown setting, imitate windows driver
	report->field[2]->value[0] = data_pointer->sensitivity;
	report->field[3]->value[0] = data_pointer->press_speed;

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
	return 0;
}

static ssize_t attr_press_to_select_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->press_to_select);
}

static ssize_t attr_press_to_select_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->press_to_select = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_dragging_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->dragging);
}

static ssize_t attr_dragging_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->dragging = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_release_to_select_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->release_to_select);
}

static ssize_t attr_release_to_select_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->release_to_select = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_select_right_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->select_right);
}

static ssize_t attr_select_right_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->select_right = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_sensitivity_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data_pointer->sensitivity);
}

static ssize_t attr_sensitivity_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	data_pointer->sensitivity = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static ssize_t attr_press_speed_show_tpkbd(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data_pointer->press_speed);
}

static ssize_t attr_press_speed_store_tpkbd(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	data_pointer->press_speed = value;
	lenovo_features_set_tpkbd(hdev);

	return count;
}

static struct device_attribute dev_attr_press_to_select_tpkbd =
	__ATTR(press_to_select, S_IWUSR | S_IRUGO,
			attr_press_to_select_show_tpkbd,
			attr_press_to_select_store_tpkbd);

static struct device_attribute dev_attr_dragging_tpkbd =
	__ATTR(dragging, S_IWUSR | S_IRUGO,
			attr_dragging_show_tpkbd,
			attr_dragging_store_tpkbd);

static struct device_attribute dev_attr_release_to_select_tpkbd =
	__ATTR(release_to_select, S_IWUSR | S_IRUGO,
			attr_release_to_select_show_tpkbd,
			attr_release_to_select_store_tpkbd);

static struct device_attribute dev_attr_select_right_tpkbd =
	__ATTR(select_right, S_IWUSR | S_IRUGO,
			attr_select_right_show_tpkbd,
			attr_select_right_store_tpkbd);

static struct device_attribute dev_attr_sensitivity_tpkbd =
	__ATTR(sensitivity, S_IWUSR | S_IRUGO,
			attr_sensitivity_show_tpkbd,
			attr_sensitivity_store_tpkbd);

static struct device_attribute dev_attr_press_speed_tpkbd =
	__ATTR(press_speed, S_IWUSR | S_IRUGO,
			attr_press_speed_show_tpkbd,
			attr_press_speed_store_tpkbd);

static struct attribute *lenovo_attributes_tpkbd[] = {
	&dev_attr_press_to_select_tpkbd.attr,
	&dev_attr_dragging_tpkbd.attr,
	&dev_attr_release_to_select_tpkbd.attr,
	&dev_attr_select_right_tpkbd.attr,
	&dev_attr_sensitivity_tpkbd.attr,
	&dev_attr_press_speed_tpkbd.attr,
	NULL
};

static const struct attribute_group lenovo_attr_group_tpkbd = {
	.attrs = lenovo_attributes_tpkbd,
};

static enum led_brightness lenovo_led_brightness_get_tpkbd(
			struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);
	int led_nr = 0;

	if (led_cdev == &data_pointer->led_micmute)
		led_nr = 1;

	return data_pointer->led_state & (1 << led_nr)
				? LED_FULL
				: LED_OFF;
}

static void lenovo_led_brightness_set_tpkbd(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);
	struct hid_report *report;
	int led_nr = 0;

	if (led_cdev == &data_pointer->led_micmute)
		led_nr = 1;

	if (value == LED_OFF)
		data_pointer->led_state &= ~(1 << led_nr);
	else
		data_pointer->led_state |= 1 << led_nr;

	report = hdev->report_enum[HID_OUTPUT_REPORT].report_id_hash[3];
	report->field[0]->value[0] = (data_pointer->led_state >> 0) & 1;
	report->field[0]->value[1] = (data_pointer->led_state >> 1) & 1;
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
}

static int lenovo_probe_tpkbd(struct hid_device *hdev)
{
	struct device *dev = &hdev->dev;
	struct lenovo_drvdata_tpkbd *data_pointer;
	size_t name_sz = strlen(dev_name(dev)) + 16;
	char *name_mute, *name_micmute;
	int i;
	int ret;

	/*
	 * Only register extra settings against subdevice where input_mapping
	 * set drvdata to 1, i.e. the trackpoint.
	 */
	if (!hid_get_drvdata(hdev))
		return 0;

	hid_set_drvdata(hdev, NULL);

	/* Validate required reports. */
	for (i = 0; i < 4; i++) {
		if (!hid_validate_values(hdev, HID_FEATURE_REPORT, 4, i, 1))
			return -ENODEV;
	}
	if (!hid_validate_values(hdev, HID_OUTPUT_REPORT, 3, 0, 2))
		return -ENODEV;

	ret = sysfs_create_group(&hdev->dev.kobj, &lenovo_attr_group_tpkbd);
	if (ret)
		hid_warn(hdev, "Could not create sysfs group: %d\n", ret);

	data_pointer = devm_kzalloc(&hdev->dev,
				    sizeof(struct lenovo_drvdata_tpkbd),
				    GFP_KERNEL);
	if (data_pointer == NULL) {
		hid_err(hdev, "Could not allocate memory for driver data\n");
		return -ENOMEM;
	}

	// set same default values as windows driver
	data_pointer->sensitivity = 0xa0;
	data_pointer->press_speed = 0x38;

	name_mute = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
	name_micmute = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
	if (name_mute == NULL || name_micmute == NULL) {
		hid_err(hdev, "Could not allocate memory for led data\n");
		return -ENOMEM;
	}
	snprintf(name_mute, name_sz, "%s:amber:mute", dev_name(dev));
	snprintf(name_micmute, name_sz, "%s:amber:micmute", dev_name(dev));

	hid_set_drvdata(hdev, data_pointer);

	data_pointer->led_mute.name = name_mute;
	data_pointer->led_mute.brightness_get = lenovo_led_brightness_get_tpkbd;
	data_pointer->led_mute.brightness_set = lenovo_led_brightness_set_tpkbd;
	data_pointer->led_mute.dev = dev;
	led_classdev_register(dev, &data_pointer->led_mute);

	data_pointer->led_micmute.name = name_micmute;
	data_pointer->led_micmute.brightness_get =
		lenovo_led_brightness_get_tpkbd;
	data_pointer->led_micmute.brightness_set =
		lenovo_led_brightness_set_tpkbd;
	data_pointer->led_micmute.dev = dev;
	led_classdev_register(dev, &data_pointer->led_micmute);

	lenovo_features_set_tpkbd(hdev);

	return 0;
}

static int lenovo_probe_cptkbd(struct hid_device *hdev)
{
	int ret;
	struct lenovo_drvdata_cptkbd *cptkbd_data;

	/* All the custom action happens on the USBMOUSE device for USB */
	if (hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD
			&& hdev->type != HID_TYPE_USBMOUSE) {
		hid_dbg(hdev, "Ignoring keyboard half of device\n");
		return 0;
	}

	cptkbd_data = devm_kzalloc(&hdev->dev,
					sizeof(*cptkbd_data),
					GFP_KERNEL);
	if (cptkbd_data == NULL) {
		hid_err(hdev, "can't alloc keyboard descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, cptkbd_data);

	/*
	 * Tell the keyboard a driver understands it, and turn F7, F9, F11 into
	 * regular keys
	 */
	ret = lenovo_send_cmd_cptkbd(hdev, 0x01, 0x03);
	if (ret)
		hid_warn(hdev, "Failed to switch F7/9/11 mode: %d\n", ret);

	/* Turn Fn-Lock on by default */
	cptkbd_data->fn_lock = true;
	lenovo_features_set_cptkbd(hdev);

	ret = sysfs_create_group(&hdev->dev.kobj, &lenovo_attr_group_cptkbd);
	if (ret)
		hid_warn(hdev, "Could not create sysfs group: %d\n", ret);

	return 0;
}

static int lenovo_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid_parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hid_hw_start failed\n");
		goto err;
	}

	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_TPKBD:
		ret = lenovo_probe_tpkbd(hdev);
		break;
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_CBTKBD:
		ret = lenovo_probe_cptkbd(hdev);
		break;
	default:
		ret = 0;
		break;
	}
	if (ret)
		goto err_hid;

	return 0;
err_hid:
	hid_hw_stop(hdev);
err:
	return ret;
}

static void lenovo_remove_tpkbd(struct hid_device *hdev)
{
	struct lenovo_drvdata_tpkbd *data_pointer = hid_get_drvdata(hdev);

	/*
	 * Only the trackpoint half of the keyboard has drvdata and stuff that
	 * needs unregistering.
	 */
	if (data_pointer == NULL)
		return;

	sysfs_remove_group(&hdev->dev.kobj,
			&lenovo_attr_group_tpkbd);

	led_classdev_unregister(&data_pointer->led_micmute);
	led_classdev_unregister(&data_pointer->led_mute);

	hid_set_drvdata(hdev, NULL);
}

static void lenovo_remove_cptkbd(struct hid_device *hdev)
{
	sysfs_remove_group(&hdev->dev.kobj,
			&lenovo_attr_group_cptkbd);
}

static void lenovo_remove(struct hid_device *hdev)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_LENOVO_TPKBD:
		lenovo_remove_tpkbd(hdev);
		break;
	case USB_DEVICE_ID_LENOVO_CUSBKBD:
	case USB_DEVICE_ID_LENOVO_CBTKBD:
		lenovo_remove_cptkbd(hdev);
		break;
	}

	hid_hw_stop(hdev);
}

static const struct hid_device_id lenovo_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_TPKBD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_CUSBKBD) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_CBTKBD) },
	{ }
};

MODULE_DEVICE_TABLE(hid, lenovo_devices);

static struct hid_driver lenovo_driver = {
	.name = "lenovo",
	.id_table = lenovo_devices,
	.input_mapping = lenovo_input_mapping,
	.probe = lenovo_probe,
	.remove = lenovo_remove,
	.raw_event = lenovo_raw_event,
};
module_hid_driver(lenovo_driver);

MODULE_LICENSE("GPL");
