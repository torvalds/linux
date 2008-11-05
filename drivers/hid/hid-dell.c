/*
 *  HID driver for some dell "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2007 Paul Walmsley
 *  Copyright (c) 2008 Jiri Slaby
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

#include "hid-ids.h"

static int dell_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

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

	usbhid_set_leds(hdev);

	return 0;
err_free:
	return ret;
}

static const struct hid_device_id dell_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_DELL, USB_DEVICE_ID_DELL_W7658) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_DELL, USB_DEVICE_ID_DELL_SK8115) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_GENERIC_13BA, USB_DEVICE_ID_GENERIC_13BA_KBD_MOUSE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, dell_devices);

static struct hid_driver dell_driver = {
	.name = "dell",
	.id_table = dell_devices,
	.probe = dell_probe,
};

static int dell_init(void)
{
	return hid_register_driver(&dell_driver);
}

static void dell_exit(void)
{
	hid_unregister_driver(&dell_driver);
}

module_init(dell_init);
module_exit(dell_exit);
MODULE_LICENSE("GPL");

HID_COMPAT_LOAD_DRIVER(dell);
