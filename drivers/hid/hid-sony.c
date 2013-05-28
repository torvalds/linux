/*
 *  HID driver for some sony "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2008 Jiri Slaby
 *  Copyright (c) 2006-2008 Jiri Kosina
 *  Copyright (c) 2013 Colin Leitner <colin.leitner@gmail.com>
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
#include <linux/usb.h>
#include <linux/leds.h>

#include "hid-ids.h"

#define VAIO_RDESC_CONSTANT     (1 << 0)
#define SIXAXIS_CONTROLLER_USB  (1 << 1)
#define SIXAXIS_CONTROLLER_BT   (1 << 2)
#define BUZZ_CONTROLLER         (1 << 3)

static const u8 sixaxis_rdesc_fixup[] = {
	0x95, 0x13, 0x09, 0x01, 0x81, 0x02, 0x95, 0x0C,
	0x81, 0x01, 0x75, 0x10, 0x95, 0x04, 0x26, 0xFF,
	0x03, 0x46, 0xFF, 0x03, 0x09, 0x01, 0x81, 0x02
};

static const u8 sixaxis_rdesc_fixup2[] = {
	0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xa1, 0x02,
	0x85, 0x01, 0x75, 0x08, 0x95, 0x01, 0x15, 0x00,
	0x26, 0xff, 0x00, 0x81, 0x03, 0x75, 0x01, 0x95,
	0x13, 0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45,
	0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x13, 0x81,
	0x02, 0x75, 0x01, 0x95, 0x0d, 0x06, 0x00, 0xff,
	0x81, 0x03, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05,
	0x01, 0x09, 0x01, 0xa1, 0x00, 0x75, 0x08, 0x95,
	0x04, 0x35, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30,
	0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
	0xc0, 0x05, 0x01, 0x95, 0x13, 0x09, 0x01, 0x81,
	0x02, 0x95, 0x0c, 0x81, 0x01, 0x75, 0x10, 0x95,
	0x04, 0x26, 0xff, 0x03, 0x46, 0xff, 0x03, 0x09,
	0x01, 0x81, 0x02, 0xc0, 0xa1, 0x02, 0x85, 0x02,
	0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xb1, 0x02,
	0xc0, 0xa1, 0x02, 0x85, 0xee, 0x75, 0x08, 0x95,
	0x30, 0x09, 0x01, 0xb1, 0x02, 0xc0, 0xa1, 0x02,
	0x85, 0xef, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01,
	0xb1, 0x02, 0xc0, 0xc0,
};

static const unsigned int buzz_keymap[] = {
	/* The controller has 4 remote buzzers, each with one LED and 5
	 * buttons.
	 * 
	 * We use the mapping chosen by the controller, which is:
	 *
	 * Key          Offset
	 * -------------------
	 * Buzz              1
	 * Blue              5
	 * Orange            4
	 * Green             3
	 * Yellow            2
	 *
	 * So, for example, the orange button on the third buzzer is mapped to
	 * BTN_TRIGGER_HAPPY14
	 */
	[ 1] = BTN_TRIGGER_HAPPY1,
	[ 2] = BTN_TRIGGER_HAPPY2,
	[ 3] = BTN_TRIGGER_HAPPY3,
	[ 4] = BTN_TRIGGER_HAPPY4,
	[ 5] = BTN_TRIGGER_HAPPY5,
	[ 6] = BTN_TRIGGER_HAPPY6,
	[ 7] = BTN_TRIGGER_HAPPY7,
	[ 8] = BTN_TRIGGER_HAPPY8,
	[ 9] = BTN_TRIGGER_HAPPY9,
	[10] = BTN_TRIGGER_HAPPY10,
	[11] = BTN_TRIGGER_HAPPY11,
	[12] = BTN_TRIGGER_HAPPY12,
	[13] = BTN_TRIGGER_HAPPY13,
	[14] = BTN_TRIGGER_HAPPY14,
	[15] = BTN_TRIGGER_HAPPY15,
	[16] = BTN_TRIGGER_HAPPY16,
	[17] = BTN_TRIGGER_HAPPY17,
	[18] = BTN_TRIGGER_HAPPY18,
	[19] = BTN_TRIGGER_HAPPY19,
	[20] = BTN_TRIGGER_HAPPY20,
};

struct sony_sc {
	unsigned long quirks;

	void *extra;
};

struct buzz_extra {
	int led_state;
	struct led_classdev *leds[4];
};

/* Sony Vaio VGX has wrongly mouse pointer declared as constant */
static __u8 *sony_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	/*
	 * Some Sony RF receivers wrongly declare the mouse pointer as a
	 * a constant non-data variable.
	 */
	if ((sc->quirks & VAIO_RDESC_CONSTANT) && *rsize >= 56 &&
	    /* usage page: generic desktop controls */
	    /* rdesc[0] == 0x05 && rdesc[1] == 0x01 && */
	    /* usage: mouse */
	    rdesc[2] == 0x09 && rdesc[3] == 0x02 &&
	    /* input (usage page for x,y axes): constant, variable, relative */
	    rdesc[54] == 0x81 && rdesc[55] == 0x07) {
		hid_info(hdev, "Fixing up Sony RF Receiver report descriptor\n");
		/* input: data, variable, relative */
		rdesc[55] = 0x06;
	}

	/* The HID descriptor exposed over BT has a trailing zero byte */
	if ((((sc->quirks & SIXAXIS_CONTROLLER_USB) && *rsize == 148) ||
			((sc->quirks & SIXAXIS_CONTROLLER_BT) && *rsize == 149)) &&
			rdesc[83] == 0x75) {
		hid_info(hdev, "Fixing up Sony Sixaxis report descriptor\n");
		memcpy((void *)&rdesc[83], (void *)&sixaxis_rdesc_fixup,
			sizeof(sixaxis_rdesc_fixup));
	} else if (sc->quirks & SIXAXIS_CONTROLLER_USB &&
		   *rsize > sizeof(sixaxis_rdesc_fixup2)) {
		hid_info(hdev, "Sony Sixaxis clone detected. Using original report descriptor (size: %d clone; %d new)\n",
			 *rsize, (int)sizeof(sixaxis_rdesc_fixup2));
		*rsize = sizeof(sixaxis_rdesc_fixup2);
		memcpy(rdesc, &sixaxis_rdesc_fixup2, *rsize);
	}
	return rdesc;
}

static int sony_raw_event(struct hid_device *hdev, struct hid_report *report,
		__u8 *rd, int size)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	/* Sixaxis HID report has acclerometers/gyro with MSByte first, this
	 * has to be BYTE_SWAPPED before passing up to joystick interface
	 */
	if ((sc->quirks & (SIXAXIS_CONTROLLER_USB | SIXAXIS_CONTROLLER_BT)) &&
			rd[0] == 0x01 && size == 49) {
		swap(rd[41], rd[42]);
		swap(rd[43], rd[44]);
		swap(rd[45], rd[46]);
		swap(rd[47], rd[48]);
	}

	return 0;
}

static int sony_mapping(struct hid_device *hdev, struct hid_input *hi,
			struct hid_field *field, struct hid_usage *usage,
			unsigned long **bit, int *max)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	if (sc->quirks & BUZZ_CONTROLLER) {
		unsigned int key = usage->hid & HID_USAGE;

		if ((usage->hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
			return -1;

		switch (usage->collection_index) {
		case 1:
			if (key >= ARRAY_SIZE(buzz_keymap))
				return -1;

			key = buzz_keymap[key];
			if (!key)
				return -1;
			break;
		default:
			return -1;
		}

		hid_map_usage_clear(hi, usage, bit, max, EV_KEY, key);
		return 1;
	}

	return -1;
}

/*
 * The Sony Sixaxis does not handle HID Output Reports on the Interrupt EP
 * like it should according to usbhid/hid-core.c::usbhid_output_raw_report()
 * so we need to override that forcing HID Output Reports on the Control EP.
 *
 * There is also another issue about HID Output Reports via USB, the Sixaxis
 * does not want the report_id as part of the data packet, so we have to
 * discard buf[0] when sending the actual control message, even for numbered
 * reports, humpf!
 */
static int sixaxis_usb_output_raw_report(struct hid_device *hid, __u8 *buf,
		size_t count, unsigned char report_type)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface = intf->cur_altsetting;
	int report_id = buf[0];
	int ret;

	if (report_type == HID_OUTPUT_REPORT) {
		/* Don't send the Report ID */
		buf++;
		count--;
	}

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		HID_REQ_SET_REPORT,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		((report_type + 1) << 8) | report_id,
		interface->desc.bInterfaceNumber, buf, count,
		USB_CTRL_SET_TIMEOUT);

	/* Count also the Report ID, in case of an Output report. */
	if (ret > 0 && report_type == HID_OUTPUT_REPORT)
		ret++;

	return ret;
}

/*
 * Sending HID_REQ_GET_REPORT changes the operation mode of the ps3 controller
 * to "operational".  Without this, the ps3 controller will not report any
 * events.
 */
static int sixaxis_set_operational_usb(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *dev = interface_to_usbdev(intf);
	__u16 ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	int ret;
	char *buf = kmalloc(18, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				 HID_REQ_GET_REPORT,
				 USB_DIR_IN | USB_TYPE_CLASS |
				 USB_RECIP_INTERFACE,
				 (3 << 8) | 0xf2, ifnum, buf, 17,
				 USB_CTRL_GET_TIMEOUT);
	if (ret < 0)
		hid_err(hdev, "can't set operational mode\n");

	kfree(buf);

	return ret;
}

static int sixaxis_set_operational_bt(struct hid_device *hdev)
{
	unsigned char buf[] = { 0xf4,  0x42, 0x03, 0x00, 0x00 };
	return hdev->hid_output_raw_report(hdev, buf, sizeof(buf), HID_FEATURE_REPORT);
}

static void buzz_set_leds(struct hid_device *hdev, int leds)
{
	struct list_head *report_list =
		&hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next,
		struct hid_report, list);
	__s32 *value = report->field[0]->value;

	value[0] = 0x00;
	value[1] = (leds & 1) ? 0xff : 0x00;
	value[2] = (leds & 2) ? 0xff : 0x00;
	value[3] = (leds & 4) ? 0xff : 0x00;
	value[4] = (leds & 8) ? 0xff : 0x00;
	value[5] = 0x00;
	value[6] = 0x00;
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
}

static void buzz_led_set_brightness(struct led_classdev *led,
				    enum led_brightness value)
{
	struct device *dev = led->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct sony_sc *drv_data;
	struct buzz_extra *buzz;

	int n;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data || !drv_data->extra) {
		hid_err(hdev, "No device data\n");
		return;
	}
	buzz = drv_data->extra;

	for (n = 0; n < 4; n++) {
		if (led == buzz->leds[n]) {
			int on = !! (buzz->led_state & (1 << n));
			if (value == LED_OFF && on) {
				buzz->led_state &= ~(1 << n);
				buzz_set_leds(hdev, buzz->led_state);
			} else if (value != LED_OFF && !on) {
				buzz->led_state |= (1 << n);
				buzz_set_leds(hdev, buzz->led_state);
			}
			break;
		}
	}
}

static enum led_brightness buzz_led_get_brightness(struct led_classdev *led)
{
	struct device *dev = led->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct sony_sc *drv_data;
	struct buzz_extra *buzz;

	int n;
	int on = 0;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data || !drv_data->extra) {
		hid_err(hdev, "No device data\n");
		return LED_OFF;
	}
	buzz = drv_data->extra;

	for (n = 0; n < 4; n++) {
		if (led == buzz->leds[n]) {
			on = !! (buzz->led_state & (1 << n));
			break;
		}
	}

	return on ? LED_FULL : LED_OFF;
}

static int buzz_init(struct hid_device *hdev)
{
	struct sony_sc *drv_data;
	struct buzz_extra *buzz;
	int n, ret = 0;
	struct led_classdev *led;
	size_t name_sz;
	char *name;

	drv_data = hid_get_drvdata(hdev);
	BUG_ON(!(drv_data->quirks & BUZZ_CONTROLLER));

	buzz = kzalloc(sizeof(*buzz), GFP_KERNEL);
	if (!buzz) {
		hid_err(hdev, "Insufficient memory, cannot allocate driver data\n");
		return -ENOMEM;
	}
	drv_data->extra = buzz;

	/* Clear LEDs as we have no way of reading their initial state. This is
	 * only relevant if the driver is loaded after somebody actively set the
	 * LEDs to on */
	buzz_set_leds(hdev, 0x00);

	name_sz = strlen(dev_name(&hdev->dev)) + strlen("::buzz#") + 1;

	for (n = 0; n < 4; n++) {
		led = kzalloc(sizeof(struct led_classdev) + name_sz, GFP_KERNEL);
		if (!led) {
			hid_err(hdev, "Couldn't allocate memory for LED %d\n", n);
			goto error_leds;
		}

		name = (void *)(&led[1]);
		snprintf(name, name_sz, "%s::buzz%d", dev_name(&hdev->dev), n + 1);
		led->name = name;
		led->brightness = 0;
		led->max_brightness = 1;
		led->brightness_get = buzz_led_get_brightness;
		led->brightness_set = buzz_led_set_brightness;

		if (led_classdev_register(&hdev->dev, led)) {
			hid_err(hdev, "Failed to register LED %d\n", n);
			kfree(led);
			goto error_leds;
		}

		buzz->leds[n] = led;
	}

	return ret;

error_leds:
	for (n = 0; n < 4; n++) {
		led = buzz->leds[n];
		buzz->leds[n] = NULL;
		if (!led)
			continue;
		led_classdev_unregister(led);
		kfree(led);
	}

	kfree(drv_data->extra);
	drv_data->extra = NULL;
	return ret;
}

static void buzz_remove(struct hid_device *hdev)
{
	struct sony_sc *drv_data;
	struct buzz_extra *buzz;
	struct led_classdev *led;
	int n;

	drv_data = hid_get_drvdata(hdev);
	BUG_ON(!(drv_data->quirks & BUZZ_CONTROLLER));

	buzz = drv_data->extra;

	for (n = 0; n < 4; n++) {
		led = buzz->leds[n];
		buzz->leds[n] = NULL;
		if (!led)
			continue;
		led_classdev_unregister(led);
		kfree(led);
	}

	kfree(drv_data->extra);
	drv_data->extra = NULL;
}

static int sony_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	unsigned long quirks = id->driver_data;
	struct sony_sc *sc;
	unsigned int connect_mask = HID_CONNECT_DEFAULT;

	sc = kzalloc(sizeof(*sc), GFP_KERNEL);
	if (sc == NULL) {
		hid_err(hdev, "can't alloc sony descriptor\n");
		return -ENOMEM;
	}

	sc->quirks = quirks;
	hid_set_drvdata(hdev, sc);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	if (sc->quirks & VAIO_RDESC_CONSTANT)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;
	else if (sc->quirks & SIXAXIS_CONTROLLER_USB)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;
	else if (sc->quirks & SIXAXIS_CONTROLLER_BT)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;

	ret = hid_hw_start(hdev, connect_mask);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	if (sc->quirks & SIXAXIS_CONTROLLER_USB) {
		hdev->hid_output_raw_report = sixaxis_usb_output_raw_report;
		ret = sixaxis_set_operational_usb(hdev);
	}
	else if (sc->quirks & SIXAXIS_CONTROLLER_BT)
		ret = sixaxis_set_operational_bt(hdev);
	else if (sc->quirks & BUZZ_CONTROLLER)
		ret = buzz_init(hdev);
	else
		ret = 0;

	if (ret < 0)
		goto err_stop;

	return 0;
err_stop:
	hid_hw_stop(hdev);
err_free:
	kfree(sc);
	return ret;
}

static void sony_remove(struct hid_device *hdev)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	if (sc->quirks & BUZZ_CONTROLLER)
		buzz_remove(hdev);

	hid_hw_stop(hdev);
	kfree(sc);
}

static const struct hid_device_id sony_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_USB },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_NAVIGATION_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_USB },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_BT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_VAIO_VGX_MOUSE),
		.driver_data = VAIO_RDESC_CONSTANT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_VAIO_VGP_MOUSE),
		.driver_data = VAIO_RDESC_CONSTANT },
	/* Wired Buzz Controller. Reported as Sony Hub from its USB ID and as
	 * Logitech joystick from the device descriptor. */
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_BUZZ_CONTROLLER),
		.driver_data = BUZZ_CONTROLLER },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_WIRELESS_BUZZ_CONTROLLER),
		.driver_data = BUZZ_CONTROLLER },
	{ }
};
MODULE_DEVICE_TABLE(hid, sony_devices);

static struct hid_driver sony_driver = {
	.name          = "sony",
	.id_table      = sony_devices,
	.input_mapping = sony_mapping,
	.probe         = sony_probe,
	.remove        = sony_remove,
	.report_fixup  = sony_report_fixup,
	.raw_event     = sony_raw_event
};
module_hid_driver(sony_driver);

MODULE_LICENSE("GPL");
