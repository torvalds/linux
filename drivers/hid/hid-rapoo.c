// SPDX-License-Identifier: GPL-2.0
#include <asm-generic/errno-base.h>
#include <asm-generic/int-ll64.h>
#include <linux/bitops.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <linux/module.h>
#include <linux/hid.h>
#include <linux/usb.h>

#include "hid-ids.h"

#define RAPOO_BTN_BACK			0x08
#define RAPOO_BTN_FORWARD		0x10

static const struct hid_device_id rapoo_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_RAPOO, USB_DEVICE_ID_RAPOO_2_4G_RECEIVER) },
	{ }
};
MODULE_DEVICE_TABLE(hid, rapoo_devices);

static int rapoo_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct input_dev *input;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "start failed\n");
		return ret;
	}

	if (hdev->bus == BUS_USB) {
		struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

		if (intf->cur_altsetting->desc.bInterfaceNumber != 1)
			return 0;
	}

	input = devm_input_allocate_device(&hdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = "Rapoo 2.4G Wireless Mouse";
	input->phys = "rapoo/input1";
	input->id.bustype = hdev->bus;
	input->id.vendor = hdev->vendor;
	input->id.product = hdev->product;
	input->id.version = hdev->version;

	__set_bit(EV_KEY, input->evbit);
	__set_bit(KEY_BACK, input->keybit);
	__set_bit(KEY_FORWARD, input->keybit);

	ret = input_register_device(input);
	if (ret)
		return ret;

	hid_set_drvdata(hdev, input);

	return ret;
}

static int rapoo_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	struct input_dev *input = hid_get_drvdata(hdev);

	if (!input)
		return 0;

	if (report->id == 1 && size >= 2) {
		u8 btn = data[1];

		input_report_key(input, KEY_BACK, btn & RAPOO_BTN_BACK);
		input_report_key(input, KEY_FORWARD, btn & RAPOO_BTN_FORWARD);
		input_sync(input);
		return 1;
	}

	return 0;
}

static struct hid_driver rapoo_driver = {
	.name = "hid-rapoo",
	.id_table = rapoo_devices,
	.probe = rapoo_probe,
	.raw_event = rapoo_raw_event,
};

module_hid_driver(rapoo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nguyen Dinh Dang Duong <dangduong31205@gmail.com>");
MODULE_DESCRIPTION("RAPOO 2.4G Wireless Device Driver");

