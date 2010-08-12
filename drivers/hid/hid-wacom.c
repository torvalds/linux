/*
 *  Bluetooth Wacom Tablet support
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2007 Paul Walmsley
 *  Copyright (c) 2008 Jiri Slaby <jirislaby@gmail.com>
 *  Copyright (c) 2006 Andrew Zabolotny <zap@homelink.ru>
 *  Copyright (c) 2009 Bastien Nocera <hadess@hadess.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#ifdef CONFIG_HID_WACOM_POWER_SUPPLY
#include <linux/power_supply.h>
#endif

#include "hid-ids.h"

struct wacom_data {
	__u16 tool;
	unsigned char butstate;
	unsigned char high_speed;
#ifdef CONFIG_HID_WACOM_POWER_SUPPLY
	int battery_capacity;
	struct power_supply battery;
	struct power_supply ac;
#endif
};

#ifdef CONFIG_HID_WACOM_POWER_SUPPLY
/*percent of battery capacity, 0 means AC online*/
static unsigned short batcap[8] = { 1, 15, 25, 35, 50, 70, 100, 0 };

static enum power_supply_property wacom_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY
};

static enum power_supply_property wacom_ac_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE
};

static int wacom_battery_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct wacom_data *wdata = container_of(psy,
					struct wacom_data, battery);
	int power_state = batcap[wdata->battery_capacity];
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		/* show 100% battery capacity when charging */
		if (power_state == 0)
			val->intval = 100;
		else
			val->intval = power_state;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int wacom_ac_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct wacom_data *wdata = container_of(psy, struct wacom_data, ac);
	int power_state = batcap[wdata->battery_capacity];
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		/* fall through */
	case POWER_SUPPLY_PROP_ONLINE:
		if (power_state == 0)
			val->intval = 1;
		else
			val->intval = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}
#endif

static void wacom_poke(struct hid_device *hdev, u8 speed)
{
	struct wacom_data *wdata = hid_get_drvdata(hdev);
	int limit, ret;
	char rep_data[2];

	rep_data[0] = 0x03 ; rep_data[1] = 0x00;
	limit = 3;
	do {
		ret = hdev->hid_output_raw_report(hdev, rep_data, 2,
				HID_FEATURE_REPORT);
	} while (ret < 0 && limit-- > 0);

	if (ret >= 0) {
		if (speed == 0)
			rep_data[0] = 0x05;
		else
			rep_data[0] = 0x06;

		rep_data[1] = 0x00;
		limit = 3;
		do {
			ret = hdev->hid_output_raw_report(hdev, rep_data, 2,
					HID_FEATURE_REPORT);
		} while (ret < 0 && limit-- > 0);

		if (ret >= 0) {
			wdata->high_speed = speed;
			return;
		}
	}

	/*
	 * Note that if the raw queries fail, it's not a hard failure and it
	 * is safe to continue
	 */
	dev_warn(&hdev->dev, "failed to poke device, command %d, err %d\n",
				rep_data[0], ret);
	return;
}

static ssize_t wacom_show_speed(struct device *dev,
				struct device_attribute
				*attr, char *buf)
{
	struct wacom_data *wdata = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%i\n", wdata->high_speed);
}

static ssize_t wacom_store_speed(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	int new_speed;

	if (sscanf(buf, "%1d", &new_speed ) != 1)
		return -EINVAL;

	if (new_speed == 0 || new_speed == 1) {
		wacom_poke(hdev, new_speed);
		return strnlen(buf, PAGE_SIZE);
	} else
		return -EINVAL;
}

static DEVICE_ATTR(speed, S_IRUGO | S_IWUGO,
		wacom_show_speed, wacom_store_speed);

static int wacom_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *raw_data, int size)
{
	struct wacom_data *wdata = hid_get_drvdata(hdev);
	struct hid_input *hidinput;
	struct input_dev *input;
	unsigned char *data = (unsigned char *) raw_data;
	int tool, x, y, rw;

	if (!(hdev->claimed & HID_CLAIMED_INPUT))
		return 0;

	tool = 0;
	hidinput = list_entry(hdev->inputs.next, struct hid_input, list);
	input = hidinput->input;

	/* Check if this is a tablet report */
	if (data[0] != 0x03)
		return 0;

	/* Get X & Y positions */
	x = le16_to_cpu(*(__le16 *) &data[2]);
	y = le16_to_cpu(*(__le16 *) &data[4]);

	/* Get current tool identifier */
	if (data[1] & 0x90) { /* If pen is in the in/active area */
		switch ((data[1] >> 5) & 3) {
		case 0:	/* Pen */
			tool = BTN_TOOL_PEN;
			break;

		case 1: /* Rubber */
			tool = BTN_TOOL_RUBBER;
			break;

		case 2: /* Mouse with wheel */
		case 3: /* Mouse without wheel */
			tool = BTN_TOOL_MOUSE;
			break;
		}

		/* Reset tool if out of active tablet area */
		if (!(data[1] & 0x10))
			tool = 0;
	}

	/* If tool changed, notify input subsystem */
	if (wdata->tool != tool) {
		if (wdata->tool) {
			/* Completely reset old tool state */
			if (wdata->tool == BTN_TOOL_MOUSE) {
				input_report_key(input, BTN_LEFT, 0);
				input_report_key(input, BTN_RIGHT, 0);
				input_report_key(input, BTN_MIDDLE, 0);
				input_report_abs(input, ABS_DISTANCE,
					input_abs_get_max(input, ABS_DISTANCE));
			} else {
				input_report_key(input, BTN_TOUCH, 0);
				input_report_key(input, BTN_STYLUS, 0);
				input_report_key(input, BTN_STYLUS2, 0);
				input_report_abs(input, ABS_PRESSURE, 0);
			}
			input_report_key(input, wdata->tool, 0);
			input_sync(input);
		}
		wdata->tool = tool;
		if (tool)
			input_report_key(input, tool, 1);
	}

	if (tool) {
		input_report_abs(input, ABS_X, x);
		input_report_abs(input, ABS_Y, y);

		switch ((data[1] >> 5) & 3) {
		case 2: /* Mouse with wheel */
			input_report_key(input, BTN_MIDDLE, data[1] & 0x04);
			rw = (data[6] & 0x01) ? -1 :
				(data[6] & 0x02) ? 1 : 0;
			input_report_rel(input, REL_WHEEL, rw);
			/* fall through */

		case 3: /* Mouse without wheel */
			input_report_key(input, BTN_LEFT, data[1] & 0x01);
			input_report_key(input, BTN_RIGHT, data[1] & 0x02);
			/* Compute distance between mouse and tablet */
			rw = 44 - (data[6] >> 2);
			if (rw < 0)
				rw = 0;
			else if (rw > 31)
				rw = 31;
			input_report_abs(input, ABS_DISTANCE, rw);
			break;

		default:
			input_report_abs(input, ABS_PRESSURE,
					data[6] | (((__u16) (data[1] & 0x08)) << 5));
			input_report_key(input, BTN_TOUCH, data[1] & 0x01);
			input_report_key(input, BTN_STYLUS, data[1] & 0x02);
			input_report_key(input, BTN_STYLUS2, (tool == BTN_TOOL_PEN) && data[1] & 0x04);
			break;
		}

		input_sync(input);
	}

	/* Report the state of the two buttons at the top of the tablet
	 * as two extra fingerpad keys (buttons 4 & 5). */
	rw = data[7] & 0x03;
	if (rw != wdata->butstate) {
		wdata->butstate = rw;
		input_report_key(input, BTN_0, rw & 0x02);
		input_report_key(input, BTN_1, rw & 0x01);
		input_report_key(input, BTN_TOOL_FINGER, 0xf0);
		input_event(input, EV_MSC, MSC_SERIAL, 0xf0);
		input_sync(input);
	}

#ifdef CONFIG_HID_WACOM_POWER_SUPPLY
	/* Store current battery capacity */
	rw = (data[7] >> 2 & 0x07);
	if (rw != wdata->battery_capacity)
		wdata->battery_capacity = rw;
#endif
	return 1;
}

static int wacom_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	struct hid_input *hidinput;
	struct input_dev *input;
	struct wacom_data *wdata;
	int ret;

	wdata = kzalloc(sizeof(*wdata), GFP_KERNEL);
	if (wdata == NULL) {
		dev_err(&hdev->dev, "can't alloc wacom descriptor\n");
		return -ENOMEM;
	}

	hid_set_drvdata(hdev, wdata);

	/* Parse the HID report now */
	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		dev_err(&hdev->dev, "hw start failed\n");
		goto err_free;
	}

	ret = device_create_file(&hdev->dev, &dev_attr_speed);
	if (ret)
		dev_warn(&hdev->dev,
			"can't create sysfs speed attribute err: %d\n", ret);

	/* Set Wacom mode 2 with high reporting speed */
	wacom_poke(hdev, 1);

#ifdef CONFIG_HID_WACOM_POWER_SUPPLY
	wdata->battery.properties = wacom_battery_props;
	wdata->battery.num_properties = ARRAY_SIZE(wacom_battery_props);
	wdata->battery.get_property = wacom_battery_get_property;
	wdata->battery.name = "wacom_battery";
	wdata->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	wdata->battery.use_for_apm = 0;

	ret = power_supply_register(&hdev->dev, &wdata->battery);
	if (ret) {
		dev_warn(&hdev->dev,
			"can't create sysfs battery attribute, err: %d\n", ret);
		/*
		 * battery attribute is not critical for the tablet, but if it
		 * failed then there is no need to create ac attribute
		 */
		goto move_on;
	}

	wdata->ac.properties = wacom_ac_props;
	wdata->ac.num_properties = ARRAY_SIZE(wacom_ac_props);
	wdata->ac.get_property = wacom_ac_get_property;
	wdata->ac.name = "wacom_ac";
	wdata->ac.type = POWER_SUPPLY_TYPE_MAINS;
	wdata->ac.use_for_apm = 0;

	ret = power_supply_register(&hdev->dev, &wdata->ac);
	if (ret) {
		dev_warn(&hdev->dev,
			"can't create ac battery attribute, err: %d\n", ret);
		/*
		 * ac attribute is not critical for the tablet, but if it
		 * failed then we don't want to battery attribute to exist
		 */
		power_supply_unregister(&wdata->battery);
	}

move_on:
#endif
	hidinput = list_entry(hdev->inputs.next, struct hid_input, list);
	input = hidinput->input;

	/* Basics */
	input->evbit[0] |= BIT(EV_KEY) | BIT(EV_ABS) | BIT(EV_REL);

	__set_bit(REL_WHEEL, input->relbit);

	__set_bit(BTN_TOOL_PEN, input->keybit);
	__set_bit(BTN_TOUCH, input->keybit);
	__set_bit(BTN_STYLUS, input->keybit);
	__set_bit(BTN_STYLUS2, input->keybit);
	__set_bit(BTN_LEFT, input->keybit);
	__set_bit(BTN_RIGHT, input->keybit);
	__set_bit(BTN_MIDDLE, input->keybit);

	/* Pad */
	input->evbit[0] |= BIT(EV_MSC);

	__set_bit(MSC_SERIAL, input->mscbit);

	__set_bit(BTN_0, input->keybit);
	__set_bit(BTN_1, input->keybit);
	__set_bit(BTN_TOOL_FINGER, input->keybit);

	/* Distance, rubber and mouse */
	__set_bit(BTN_TOOL_RUBBER, input->keybit);
	__set_bit(BTN_TOOL_MOUSE, input->keybit);

	input_set_abs_params(input, ABS_X, 0, 16704, 4, 0);
	input_set_abs_params(input, ABS_Y, 0, 12064, 4, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 511, 0, 0);
	input_set_abs_params(input, ABS_DISTANCE, 0, 32, 0, 0);

	return 0;

err_free:
	kfree(wdata);
	return ret;
}

static void wacom_remove(struct hid_device *hdev)
{
#ifdef CONFIG_HID_WACOM_POWER_SUPPLY
	struct wacom_data *wdata = hid_get_drvdata(hdev);
#endif
	hid_hw_stop(hdev);

#ifdef CONFIG_HID_WACOM_POWER_SUPPLY
	power_supply_unregister(&wdata->battery);
	power_supply_unregister(&wdata->ac);
#endif
	kfree(hid_get_drvdata(hdev));
}

static const struct hid_device_id wacom_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_WACOM, USB_DEVICE_ID_WACOM_GRAPHIRE_BLUETOOTH) },

	{ }
};
MODULE_DEVICE_TABLE(hid, wacom_devices);

static struct hid_driver wacom_driver = {
	.name = "wacom",
	.id_table = wacom_devices,
	.probe = wacom_probe,
	.remove = wacom_remove,
	.raw_event = wacom_raw_event,
};

static int __init wacom_init(void)
{
	int ret;

	ret = hid_register_driver(&wacom_driver);
	if (ret)
		printk(KERN_ERR "can't register wacom driver\n");
	return ret;
}

static void __exit wacom_exit(void)
{
	hid_unregister_driver(&wacom_driver);
}

module_init(wacom_init);
module_exit(wacom_exit);
MODULE_LICENSE("GPL");

