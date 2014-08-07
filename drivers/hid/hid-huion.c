/*
 *  HID driver for Huion devices not fully compliant with HID standard
 *
 *  Copyright (c) 2013 Martin Rusko
 *  Copyright (c) 2014 Nikolai Kondrashov
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
#include <asm/unaligned.h>
#include "usbhid/usbhid.h"

#include "hid-ids.h"

/* Report descriptor template placeholder head */
#define HUION_PH_HEAD	0xFE, 0xED, 0x1D

/* Report descriptor template placeholder IDs */
enum huion_ph_id {
	HUION_PH_ID_X_LM,
	HUION_PH_ID_X_PM,
	HUION_PH_ID_Y_LM,
	HUION_PH_ID_Y_PM,
	HUION_PH_ID_PRESSURE_LM,
	HUION_PH_ID_NUM
};

/* Report descriptor template placeholder */
#define HUION_PH(_ID) HUION_PH_HEAD, HUION_PH_ID_##_ID

/* Fixed report descriptor template */
static const __u8 huion_tablet_rdesc_template[] = {
	0x05, 0x0D,             /*  Usage Page (Digitizer),                 */
	0x09, 0x02,             /*  Usage (Pen),                            */
	0xA1, 0x01,             /*  Collection (Application),               */
	0x85, 0x07,             /*      Report ID (7),                      */
	0x09, 0x20,             /*      Usage (Stylus),                     */
	0xA0,                   /*      Collection (Physical),              */
	0x14,                   /*          Logical Minimum (0),            */
	0x25, 0x01,             /*          Logical Maximum (1),            */
	0x75, 0x01,             /*          Report Size (1),                */
	0x09, 0x42,             /*          Usage (Tip Switch),             */
	0x09, 0x44,             /*          Usage (Barrel Switch),          */
	0x09, 0x46,             /*          Usage (Tablet Pick),            */
	0x95, 0x03,             /*          Report Count (3),               */
	0x81, 0x02,             /*          Input (Variable),               */
	0x95, 0x03,             /*          Report Count (3),               */
	0x81, 0x03,             /*          Input (Constant, Variable),     */
	0x09, 0x32,             /*          Usage (In Range),               */
	0x95, 0x01,             /*          Report Count (1),               */
	0x81, 0x02,             /*          Input (Variable),               */
	0x95, 0x01,             /*          Report Count (1),               */
	0x81, 0x03,             /*          Input (Constant, Variable),     */
	0x75, 0x10,             /*          Report Size (16),               */
	0x95, 0x01,             /*          Report Count (1),               */
	0xA4,                   /*          Push,                           */
	0x05, 0x01,             /*          Usage Page (Desktop),           */
	0x65, 0x13,             /*          Unit (Inch),                    */
	0x55, 0xFD,             /*          Unit Exponent (-3),             */
	0x34,                   /*          Physical Minimum (0),           */
	0x09, 0x30,             /*          Usage (X),                      */
	0x27, HUION_PH(X_LM),   /*          Logical Maximum (PLACEHOLDER),  */
	0x47, HUION_PH(X_PM),   /*          Physical Maximum (PLACEHOLDER), */
	0x81, 0x02,             /*          Input (Variable),               */
	0x09, 0x31,             /*          Usage (Y),                      */
	0x27, HUION_PH(Y_LM),   /*          Logical Maximum (PLACEHOLDER),  */
	0x47, HUION_PH(Y_PM),   /*          Physical Maximum (PLACEHOLDER), */
	0x81, 0x02,             /*          Input (Variable),               */
	0xB4,                   /*          Pop,                            */
	0x09, 0x30,             /*          Usage (Tip Pressure),           */
	0x27,
	HUION_PH(PRESSURE_LM),  /*          Logical Maximum (PLACEHOLDER),  */
	0x81, 0x02,             /*          Input (Variable),               */
	0xC0,                   /*      End Collection,                     */
	0xC0                    /*  End Collection                          */
};

/* Driver data */
struct huion_drvdata {
	__u8 *rdesc;
	unsigned int rsize;
};

static __u8 *huion_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct huion_drvdata *drvdata = hid_get_drvdata(hdev);
	switch (hdev->product) {
	case USB_DEVICE_ID_HUION_TABLET:
		if (drvdata->rdesc != NULL) {
			rdesc = drvdata->rdesc;
			*rsize = drvdata->rsize;
		}
		break;
	}
	return rdesc;
}

/**
 * Enable fully-functional tablet mode and determine device parameters.
 *
 * @hdev:	HID device
 */
static int huion_tablet_enable(struct hid_device *hdev)
{
	int rc;
	struct usb_device *usb_dev = hid_to_usb_dev(hdev);
	struct huion_drvdata *drvdata = hid_get_drvdata(hdev);
	__le16 buf[6];

	/*
	 * Read string descriptor containing tablet parameters. The specific
	 * string descriptor and data were discovered by sniffing the Windows
	 * driver traffic.
	 * NOTE: This enables fully-functional tablet mode.
	 */
	rc = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				(USB_DT_STRING << 8) + 0x64,
				0x0409, buf, sizeof(buf),
				USB_CTRL_GET_TIMEOUT);
	if (rc == -EPIPE)
		hid_warn(hdev, "device parameters not found\n");
	else if (rc < 0)
		hid_warn(hdev, "failed to get device parameters: %d\n", rc);
	else if (rc != sizeof(buf))
		hid_warn(hdev, "invalid device parameters\n");
	else {
		s32 params[HUION_PH_ID_NUM];
		s32 resolution;
		__u8 *p;
		s32 v;

		/* Extract device parameters */
		params[HUION_PH_ID_X_LM] = le16_to_cpu(buf[1]);
		params[HUION_PH_ID_Y_LM] = le16_to_cpu(buf[2]);
		params[HUION_PH_ID_PRESSURE_LM] = le16_to_cpu(buf[4]);
		resolution = le16_to_cpu(buf[5]);
		if (resolution == 0) {
			params[HUION_PH_ID_X_PM] = 0;
			params[HUION_PH_ID_Y_PM] = 0;
		} else {
			params[HUION_PH_ID_X_PM] = params[HUION_PH_ID_X_LM] *
							1000 / resolution;
			params[HUION_PH_ID_Y_PM] = params[HUION_PH_ID_Y_LM] *
							1000 / resolution;
		}

		/* Allocate fixed report descriptor */
		drvdata->rdesc = devm_kmalloc(&hdev->dev,
					sizeof(huion_tablet_rdesc_template),
					GFP_KERNEL);
		if (drvdata->rdesc == NULL) {
			hid_err(hdev, "failed to allocate fixed rdesc\n");
			return -ENOMEM;
		}
		drvdata->rsize = sizeof(huion_tablet_rdesc_template);

		/* Format fixed report descriptor */
		memcpy(drvdata->rdesc, huion_tablet_rdesc_template,
			drvdata->rsize);
		for (p = drvdata->rdesc;
		     p <= drvdata->rdesc + drvdata->rsize - 4;) {
			if (p[0] == 0xFE && p[1] == 0xED && p[2] == 0x1D &&
			    p[3] < sizeof(params)) {
				v = params[p[3]];
				put_unaligned(cpu_to_le32(v), (s32 *)p);
				p += 4;
			} else {
				p++;
			}
		}
	}

	return 0;
}

static int huion_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int rc;
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct huion_drvdata *drvdata;

	/* Allocate and assign driver data */
	drvdata = devm_kzalloc(&hdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (drvdata == NULL) {
		hid_err(hdev, "failed to allocate driver data\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, drvdata);

	switch (id->product) {
	case USB_DEVICE_ID_HUION_TABLET:
		/* If this is the pen interface */
		if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
			rc = huion_tablet_enable(hdev);
			if (rc) {
				hid_err(hdev, "tablet enabling failed\n");
				return rc;
			}
		}
		break;
	}

	rc = hid_parse(hdev);
	if (rc) {
		hid_err(hdev, "parse failed\n");
		return rc;
	}

	rc = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (rc) {
		hid_err(hdev, "hw start failed\n");
		return rc;
	}

	return 0;
}

static int huion_raw_event(struct hid_device *hdev, struct hid_report *report,
			u8 *data, int size)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	/* If this is a pen input report */
	if (intf->cur_altsetting->desc.bInterfaceNumber == 0 &&
	    report->type == HID_INPUT_REPORT &&
	    report->id == 0x07 && size >= 2)
		/* Invert the in-range bit */
		data[1] ^= 0x40;

	return 0;
}

static const struct hid_device_id huion_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_HUION, USB_DEVICE_ID_HUION_TABLET) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC, USB_DEVICE_ID_HUION_TABLET) },
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
