// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for PhoenixRC 8-axis flight controller
 *
 * Copyright (C) 2022 Marcus Folkesson <marcus.folkesson@gmail.com>
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

struct pxrc_priv {
	u8 slider;
	u8 dial;
	bool alternate;
};

static const __u8 pxrc_rdesc_fixed[] = {
	0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
	0x09, 0x04,        // Usage (Joystick)
	0xA1, 0x01,        // Collection (Application)
	0x09, 0x01,        //   Usage (Pointer)
	0xA1, 0x00,        //   Collection (Physical)
	0x09, 0x30,        //     Usage (X)
	0x09, 0x36,        //     Usage (Slider)
	0x09, 0x31,        //     Usage (Y)
	0x09, 0x32,        //     Usage (Z)
	0x09, 0x33,        //     Usage (Rx)
	0x09, 0x34,        //     Usage (Ry)
	0x09, 0x35,        //     Usage (Rz)
	0x09, 0x37,        //     Usage (Dial)
	0x15, 0x00,        //     Logical Minimum (0)
	0x26, 0xFF, 0x00,  //     Logical Maximum (255)
	0x35, 0x00,        //     Physical Minimum (0)
	0x46, 0xFF, 0x00,  //     Physical Maximum (255)
	0x75, 0x08,        //     Report Size (8)
	0x95, 0x08,        //     Report Count (8)
	0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0xC0,              //   End Collection
	0xC0,              // End Collection
};

static const __u8 *pxrc_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				     unsigned int *rsize)
{
	hid_info(hdev, "fixing up PXRC report descriptor\n");
	*rsize = sizeof(pxrc_rdesc_fixed);
	return pxrc_rdesc_fixed;
}

static int pxrc_raw_event(struct hid_device *hdev, struct hid_report *report,
	 u8 *data, int size)
{
	struct pxrc_priv *priv = hid_get_drvdata(hdev);

	if (priv->alternate)
		priv->slider = data[7];
	else
		priv->dial = data[7];

	data[1] = priv->slider;
	data[7] = priv->dial;

	priv->alternate = !priv->alternate;
	return 0;
}

static int pxrc_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct pxrc_priv *priv;

	priv = devm_kzalloc(&hdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	hid_set_drvdata(hdev, priv);

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

static const struct hid_device_id pxrc_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MULTIPLE_1781, USB_DEVICE_ID_PHOENIXRC) },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(hid, pxrc_devices);

static struct hid_driver pxrc_driver = {
	.name = "hid-pxrc",
	.id_table = pxrc_devices,
	.report_fixup = pxrc_report_fixup,
	.probe = pxrc_probe,
	.raw_event = pxrc_raw_event,
};
module_hid_driver(pxrc_driver);

MODULE_AUTHOR("Marcus Folkesson <marcus.folkesson@gmail.com>");
MODULE_DESCRIPTION("HID driver for PXRC 8-axis flight controller");
MODULE_LICENSE("GPL");
