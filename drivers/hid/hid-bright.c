/*
 *  HID driver for some bright "special" devices
 *
 *  Copyright (c) 2008 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * Based on hid-dell driver
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

static int bright_probe(struct hid_device *hdev, const struct hid_device_id *id)
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

static const struct hid_device_id bright_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_BRIGHT, USB_DEVICE_ID_BRIGHT_ABNT2) },
	{ }
};
MODULE_DEVICE_TABLE(hid, bright_devices);

static struct hid_driver bright_driver = {
	.name = "bright",
	.id_table = bright_devices,
	.probe = bright_probe,
};

static int bright_init(void)
{
	return hid_register_driver(&bright_driver);
}

static void bright_exit(void)
{
	hid_unregister_driver(&bright_driver);
}

module_init(bright_init);
module_exit(bright_exit);
MODULE_LICENSE("GPL");

HID_COMPAT_LOAD_DRIVER(bright);
