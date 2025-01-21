// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Hans de Goede <hdegoede@redhat.com>
 *
 * Driver for the LetSketch / VSON WP9620N drawing tablet.
 * This drawing tablet is also sold under other brand names such as Case U,
 * presumably this driver will work for all of them. But it has only been
 * tested with a LetSketch WP9620N model.
 *
 * These tablets also work without a special HID driver, but then only part
 * of the active area works and both the pad and stylus buttons are hardwired
 * to special key-combos. E.g. the 2 stylus buttons send right mouse clicks /
 * resp. "e" key presses.
 *
 * This device has 4 USB interfaces:
 *
 * Interface 0 EP 0x81 bootclass mouse, rdesc len 18, report id 0x08,
 *                                                    Application(ff00.0001)
 *  This interface sends raw event input reports in a custom format, but only
 *  after doing the special dance from letsketch_probe(). After enabling this
 *  interface the other 3 interfaces are disabled.
 *
 * Interface 1 EP 0x82 bootclass mouse, rdesc len 83, report id 0x0a, Tablet
 *  This interface sends absolute events for the pen, including pressure,
 *  but only for some part of the active area due to special "aspect ratio"
 *  correction and only half by default since it assumes it will be used
 *  with a phone in portraid mode, while using the tablet in landscape mode.
 *  Also stylus + pad button events are not reported here.
 *
 * Interface 2 EP 0x83 bootclass keybd, rdesc len 64, report id none, Std Kbd
 *  This interfaces send various hard-coded key-combos for the pad buttons
 *  and "e" keypresses for the 2nd stylus button
 *
 * Interface 3 EP 0x84 bootclass mouse, rdesc len 75, report id 0x01, Std Mouse
 *  This reports right-click mouse-button events for the 1st stylus button
 */
#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/usb.h>

#include <linux/unaligned.h>

#include "hid-ids.h"

#define LETSKETCH_RAW_IF		0

#define LETSKETCH_RAW_DATA_LEN		12
#define LETSKETCH_RAW_REPORT_ID		8

#define LETSKETCH_PAD_BUTTONS		5

#define LETSKETCH_INFO_STR_IDX_BEGIN	0xc8
#define LETSKETCH_INFO_STR_IDX_END	0xca

#define LETSKETCH_GET_STRING_RETRIES	5

struct letsketch_data {
	struct hid_device *hdev;
	struct input_dev *input_tablet;
	struct input_dev *input_tablet_pad;
	struct timer_list inrange_timer;
};

static int letsketch_open(struct input_dev *dev)
{
	struct letsketch_data *data = input_get_drvdata(dev);

	return hid_hw_open(data->hdev);
}

static void letsketch_close(struct input_dev *dev)
{
	struct letsketch_data *data = input_get_drvdata(dev);

	hid_hw_close(data->hdev);
}

static struct input_dev *letsketch_alloc_input_dev(struct letsketch_data *data)
{
	struct input_dev *input;

	input = devm_input_allocate_device(&data->hdev->dev);
	if (!input)
		return NULL;

	input->id.bustype = data->hdev->bus;
	input->id.vendor  = data->hdev->vendor;
	input->id.product = data->hdev->product;
	input->id.version = data->hdev->bus;
	input->phys = data->hdev->phys;
	input->uniq = data->hdev->uniq;
	input->open = letsketch_open;
	input->close = letsketch_close;

	input_set_drvdata(input, data);

	return input;
}

static int letsketch_setup_input_tablet(struct letsketch_data *data)
{
	struct input_dev *input;

	input = letsketch_alloc_input_dev(data);
	if (!input)
		return -ENOMEM;

	input_set_abs_params(input, ABS_X, 0, 50800, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, 31750, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, 8192, 0, 0);
	input_abs_set_res(input, ABS_X, 240);
	input_abs_set_res(input, ABS_Y, 225);
	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_capability(input, EV_KEY, BTN_TOOL_PEN);
	input_set_capability(input, EV_KEY, BTN_STYLUS);
	input_set_capability(input, EV_KEY, BTN_STYLUS2);

	/* All known brands selling this tablet use WP9620[N] as model name */
	input->name = "WP9620 Tablet";

	data->input_tablet = input;

	return input_register_device(data->input_tablet);
}

static int letsketch_setup_input_tablet_pad(struct letsketch_data *data)
{
	struct input_dev *input;
	int i;

	input = letsketch_alloc_input_dev(data);
	if (!input)
		return -ENOMEM;

	for (i = 0; i < LETSKETCH_PAD_BUTTONS; i++)
		input_set_capability(input, EV_KEY, BTN_0 + i);

	/*
	 * These are never send on the pad input_dev, but must be set
	 * on the Pad to make udev / libwacom happy.
	 */
	input_set_abs_params(input, ABS_X, 0, 1, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, 1, 0, 0);
	input_set_capability(input, EV_KEY, BTN_STYLUS);

	input->name = "WP9620 Pad";

	data->input_tablet_pad = input;

	return input_register_device(data->input_tablet_pad);
}

static void letsketch_inrange_timeout(struct timer_list *t)
{
	struct letsketch_data *data = from_timer(data, t, inrange_timer);
	struct input_dev *input = data->input_tablet;

	input_report_key(input, BTN_TOOL_PEN, 0);
	input_sync(input);
}

static int letsketch_raw_event(struct hid_device *hdev,
			       struct hid_report *report,
			       u8 *raw_data, int size)
{
	struct letsketch_data *data = hid_get_drvdata(hdev);
	struct input_dev *input;
	int i;

	if (size != LETSKETCH_RAW_DATA_LEN || raw_data[0] != LETSKETCH_RAW_REPORT_ID)
		return 0;

	switch (raw_data[1] & 0xf0) {
	case 0x80: /* Pen data */
		input = data->input_tablet;
		input_report_key(input, BTN_TOOL_PEN, 1);
		input_report_key(input, BTN_TOUCH, raw_data[1] & 0x01);
		input_report_key(input, BTN_STYLUS, raw_data[1] & 0x02);
		input_report_key(input, BTN_STYLUS2, raw_data[1] & 0x04);
		input_report_abs(input, ABS_X,
				 get_unaligned_le16(raw_data + 2));
		input_report_abs(input, ABS_Y,
				 get_unaligned_le16(raw_data + 4));
		input_report_abs(input, ABS_PRESSURE,
				 get_unaligned_le16(raw_data + 6));
		/*
		 * There is no out of range event, so use a timer for this
		 * when in range we get an event approx. every 8 ms.
		 */
		mod_timer(&data->inrange_timer, jiffies + msecs_to_jiffies(100));
		break;
	case 0xe0: /* Pad data */
		input = data->input_tablet_pad;
		for (i = 0; i < LETSKETCH_PAD_BUTTONS; i++)
			input_report_key(input, BTN_0 + i, raw_data[4] == (i + 1));
		break;
	default:
		hid_warn(data->hdev, "Warning unknown data header: 0x%02x\n",
			 raw_data[0]);
		return 0;
	}

	input_sync(input);
	return 0;
}

/*
 * The tablets magic handshake to put it in raw mode relies on getting
 * string descriptors. But the firmware is buggy and does not like it if
 * we do this too fast. Even if we go slow sometimes the usb_string() call
 * fails. Ignore errors and retry it a couple of times if necessary.
 */
static int letsketch_get_string(struct usb_device *udev, int index, char *buf, int size)
{
	int i, ret;

	for (i = 0; i < LETSKETCH_GET_STRING_RETRIES; i++) {
		usleep_range(5000, 7000);
		ret = usb_string(udev, index, buf, size);
		if (ret > 0)
			return 0;
	}

	dev_err(&udev->dev, "Max retries (%d) exceeded reading string descriptor %d\n",
		LETSKETCH_GET_STRING_RETRIES, index);
	return ret ? ret : -EIO;
}

static int letsketch_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct device *dev = &hdev->dev;
	struct letsketch_data *data;
	struct usb_interface *intf;
	struct usb_device *udev;
	char buf[256];
	int i, ret;

	if (!hid_is_usb(hdev))
		return -ENODEV;

	intf = to_usb_interface(hdev->dev.parent);
	if (intf->altsetting->desc.bInterfaceNumber != LETSKETCH_RAW_IF)
		return -ENODEV; /* Ignore the other interfaces */

	udev = interface_to_usbdev(intf);

	/*
	 * Instead of using a set-feature request, or even a custom USB ctrl
	 * message the tablet needs this elaborate magic reading of USB
	 * string descriptors to kick it into raw mode. This is what the
	 * Windows drivers are seen doing in an USB trace under Windows.
	 */
	for (i = LETSKETCH_INFO_STR_IDX_BEGIN; i <= LETSKETCH_INFO_STR_IDX_END; i++) {
		ret = letsketch_get_string(udev, i, buf, sizeof(buf));
		if (ret)
			return ret;

		hid_info(hdev, "Device info: %s\n", buf);
	}

	for (i = 1; i <= 250; i++) {
		ret = letsketch_get_string(udev, i, buf, sizeof(buf));
		if (ret)
			return ret;
	}

	ret = letsketch_get_string(udev, 0x64, buf, sizeof(buf));
	if (ret)
		return ret;

	ret = letsketch_get_string(udev, LETSKETCH_INFO_STR_IDX_BEGIN, buf, sizeof(buf));
	if (ret)
		return ret;

	/*
	 * The tablet should be in raw mode now, end with a final delay before
	 * doing further IO to the device.
	 */
	usleep_range(5000, 7000);

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->hdev = hdev;
	timer_setup(&data->inrange_timer, letsketch_inrange_timeout, 0);
	hid_set_drvdata(hdev, data);

	ret = letsketch_setup_input_tablet(data);
	if (ret)
		return ret;

	ret = letsketch_setup_input_tablet_pad(data);
	if (ret)
		return ret;

	return hid_hw_start(hdev, HID_CONNECT_HIDRAW);
}

static const struct hid_device_id letsketch_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LETSKETCH, USB_DEVICE_ID_WP9620N) },
	{ }
};
MODULE_DEVICE_TABLE(hid, letsketch_devices);

static struct hid_driver letsketch_driver = {
	.name = "letsketch",
	.id_table = letsketch_devices,
	.probe = letsketch_probe,
	.raw_event = letsketch_raw_event,
};
module_hid_driver(letsketch_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Driver for the LetSketch / VSON WP9620N drawing tablet");
MODULE_LICENSE("GPL");
