/*
 * HID driver for Nintendo Wiimote devices
 * Copyright (c) 2011 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/module.h>
#include "hid-ids.h"

#define WIIMOTE_VERSION "0.1"
#define WIIMOTE_NAME "Nintendo Wii Remote"

static int wiimote_hid_event(struct hid_device *hdev, struct hid_report *report,
							u8 *raw_data, int size)
{
	if (size < 1)
		return -EINVAL;

	return 0;
}

static int wiimote_hid_probe(struct hid_device *hdev,
				const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "HID parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "HW start failed\n");
		return ret;
	}

	hid_info(hdev, "New device registered\n");
	return 0;
}

static void wiimote_hid_remove(struct hid_device *hdev)
{
	hid_info(hdev, "Device removed\n");
	hid_hw_stop(hdev);
}

static const struct hid_device_id wiimote_hid_devices[] = {
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_NINTENDO,
				USB_DEVICE_ID_NINTENDO_WIIMOTE) },
	{ }
};
MODULE_DEVICE_TABLE(hid, wiimote_hid_devices);

static struct hid_driver wiimote_hid_driver = {
	.name = "wiimote",
	.id_table = wiimote_hid_devices,
	.probe = wiimote_hid_probe,
	.remove = wiimote_hid_remove,
	.raw_event = wiimote_hid_event,
};

static int __init wiimote_init(void)
{
	int ret;

	ret = hid_register_driver(&wiimote_hid_driver);
	if (ret)
		pr_err("Can't register wiimote hid driver\n");

	return ret;
}

static void __exit wiimote_exit(void)
{
	hid_unregister_driver(&wiimote_hid_driver);
}

module_init(wiimote_init);
module_exit(wiimote_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Herrmann <dh.herrmann@gmail.com>");
MODULE_DESCRIPTION(WIIMOTE_NAME " Device Driver");
MODULE_VERSION(WIIMOTE_VERSION);
