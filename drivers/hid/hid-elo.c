/*
 * HID driver for ELO usb touchscreen 4000/4500
 *
 * Copyright (c) 2013 Jiri Slaby
 *
 * Data parsing taken from elousb driver by Vojtech Pavlik.
 *
 * This driver is licensed under the terms of GPLv2.
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>

#include "hid-ids.h"

static void elo_input_configured(struct hid_device *hdev,
		struct hid_input *hidinput)
{
	struct input_dev *input = hidinput->input;

	set_bit(BTN_TOUCH, input->keybit);
	set_bit(ABS_PRESSURE, input->absbit);
	input_set_abs_params(input, ABS_PRESSURE, 0, 256, 0, 0);
}

static void elo_process_data(struct input_dev *input, const u8 *data, int size)
{
	int press;

	input_report_abs(input, ABS_X, (data[3] << 8) | data[2]);
	input_report_abs(input, ABS_Y, (data[5] << 8) | data[4]);

	press = 0;
	if (data[1] & 0x80)
		press = (data[7] << 8) | data[6];
	input_report_abs(input, ABS_PRESSURE, press);

	if (data[1] & 0x03) {
		input_report_key(input, BTN_TOUCH, 1);
		input_sync(input);
	}

	if (data[1] & 0x04)
		input_report_key(input, BTN_TOUCH, 0);

	input_sync(input);
}

static int elo_raw_event(struct hid_device *hdev, struct hid_report *report,
	 u8 *data, int size)
{
	struct hid_input *hidinput;

	if (!(hdev->claimed & HID_CLAIMED_INPUT) || list_empty(&hdev->inputs))
		return 0;

	hidinput = list_first_entry(&hdev->inputs, struct hid_input, list);

	switch (report->id) {
	case 0:
		if (data[0] == 'T') {	/* Mandatory ELO packet marker */
			elo_process_data(hidinput->input, data, size);
			return 1;
		}
		break;
	default:	/* unknown report */
		/* Unknown report type; pass upstream */
		hid_info(hdev, "unknown report type %d\n", report->id);
		break;
	}

	return 0;
}

static int elo_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	return 0;
err_free:
	return ret;
}

static void elo_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
}

static const struct hid_device_id elo_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELO, 0x0009), },
	{ HID_USB_DEVICE(USB_VENDOR_ID_ELO, 0x0030), },
	{ }
};
MODULE_DEVICE_TABLE(hid, elo_devices);

static struct hid_driver elo_driver = {
	.name = "elo",
	.id_table = elo_devices,
	.probe = elo_probe,
	.remove = elo_remove,
	.raw_event = elo_raw_event,
	.input_configured = elo_input_configured,
};

static int __init elo_driver_init(void)
{
	return hid_register_driver(&elo_driver);
}
module_init(elo_driver_init);

static void __exit elo_driver_exit(void)
{
	hid_unregister_driver(&elo_driver);
}
module_exit(elo_driver_exit);

MODULE_AUTHOR("Jiri Slaby <jslaby@suse.cz>");
MODULE_LICENSE("GPL");
