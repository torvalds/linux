/*
 *  HID driver for Huion devices not fully compliant with HID standard
 *
 *  Copyright (c) 2013 Martin Rusko
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
#include <linux/usb.h>
#include "usbhid/usbhid.h"

#include "hid-ids.h"

/* Original Huion 580 report descriptor size */
#define HUION_580_RDESC_ORIG_SIZE	177

/* Fixed Huion 580 report descriptor */
static __u8 huion_580_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x07,         /*      Report ID (7),                  */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x09, 0x32,         /*          Usage (In Range),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x40, 0x1F,   /*          Physical Maximum (8000),    */
	0x26, 0x00, 0x7D,   /*          Logical Maximum (32000),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0x88, 0x13,   /*          Physical Maximum (5000),    */
	0x26, 0x20, 0x4E,   /*          Logical Maximum (20000),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x07,   /*          Logical Maximum (2047),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

static __u8 *huion_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_HUION_580:
		if (*rsize == HUION_580_RDESC_ORIG_SIZE) {
			rdesc = huion_580_rdesc_fixed;
			*rsize = sizeof(huion_580_rdesc_fixed);
		}
		break;
	}
	return rdesc;
}

/**
 * Enable fully-functional tablet mode by reading special string
 * descriptor.
 *
 * @hdev:	HID device
 *
 * The specific string descriptor and data were discovered by sniffing
 * the Windows driver traffic.
 */
static int huion_tablet_enable(struct hid_device *hdev)
{
	int rc;
	char buf[22];

	rc = usb_string(hid_to_usb_dev(hdev), 0x64, buf, sizeof(buf));
	if (rc < 0)
		return rc;

	return 0;
}

static int huion_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	/* Ignore interfaces 1 (mouse) and 2 (keyboard) for Huion 580 tablet,
	 * as they are not used
	 */
	switch (id->product) {
	case USB_DEVICE_ID_HUION_580:
		if (intf->cur_altsetting->desc.bInterfaceNumber != 0x00)
			return -ENODEV;
		break;
	}

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err;
	}

	switch (id->product) {
	case USB_DEVICE_ID_HUION_580:
		ret = huion_tablet_enable(hdev);
		if (ret) {
			hid_err(hdev, "tablet enabling failed\n");
			goto enabling_err;
		}
		break;
	}

	return 0;
enabling_err:
	hid_hw_stop(hdev);
err:
	return ret;
}

static int huion_raw_event(struct hid_device *hdev, struct hid_report *report,
			u8 *data, int size)
{
	/* If this is a pen input report then invert the in-range bit */
	if (report->type == HID_INPUT_REPORT && report->id == 0x07 && size >= 2)
		data[1] ^= 0x40;

	return 0;
}

static const struct hid_device_id huion_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_HUION, USB_DEVICE_ID_HUION_580) },
	{ }
};
MODULE_DEVICE_TABLE(hid, huion_devices);

static struct hid_driver huion_driver = {
	.name = "huion",
	.id_table = huion_devices,
	.probe = huion_probe,
	.report_fixup = huion_report_fixup,
	.raw_event = huion_raw_event,
};
module_hid_driver(huion_driver);

MODULE_AUTHOR("Martin Rusko");
MODULE_DESCRIPTION("Huion HID driver");
MODULE_LICENSE("GPL");
