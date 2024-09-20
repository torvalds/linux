// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HID driver for Holtek keyboard
 * Copyright (c) 2012 Tom Harwood
*/

/*
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "hid-ids.h"
#include "usbhid/usbhid.h"

/* Holtek based keyboards (USB ID 04d9:a055) have the following issues:
 * - The report descriptor specifies an excessively large number of consumer
 *   usages (2^15), which is more than HID_MAX_USAGES. This prevents proper
 *   parsing of the report descriptor.
 * - The report descriptor reports on caps/scroll/num lock key presses, but
 *   doesn't have an LED output usage block.
 *
 * The replacement descriptor below fixes the number of consumer usages,
 * and provides an LED output usage block. LED output events are redirected
 * to the boot interface.
 */

static __u8 holtek_kbd_rdesc_fixed[] = {
	/* Original report descriptor, with reduced number of consumer usages */
	0x05, 0x01,         /*  Usage Page (Desktop),                         */
	0x09, 0x80,         /*  Usage (Sys Control),                          */
	0xA1, 0x01,         /*  Collection (Application),                     */
	0x85, 0x01,         /*      Report ID (1),                            */
	0x19, 0x81,         /*      Usage Minimum (Sys Power Down),           */
	0x29, 0x83,         /*      Usage Maximum (Sys Wake Up),              */
	0x15, 0x00,         /*      Logical Minimum (0),                      */
	0x25, 0x01,         /*      Logical Maximum (1),                      */
	0x95, 0x03,         /*      Report Count (3),                         */
	0x75, 0x01,         /*      Report Size (1),                          */
	0x81, 0x02,         /*      Input (Variable),                         */
	0x95, 0x01,         /*      Report Count (1),                         */
	0x75, 0x05,         /*      Report Size (5),                          */
	0x81, 0x01,         /*      Input (Constant),                         */
	0xC0,               /*  End Collection,                               */
	0x05, 0x0C,         /*  Usage Page (Consumer),                        */
	0x09, 0x01,         /*  Usage (Consumer Control),                     */
	0xA1, 0x01,         /*  Collection (Application),                     */
	0x85, 0x02,         /*      Report ID (2),                            */
	0x19, 0x00,         /*      Usage Minimum (00h),                      */
	0x2A, 0xFF, 0x2F,   /*      Usage Maximum (0x2FFF), previously 0x7FFF */
	0x15, 0x00,         /*      Logical Minimum (0),                      */
	0x26, 0xFF, 0x2F,   /*      Logical Maximum (0x2FFF),previously 0x7FFF*/
	0x95, 0x01,         /*      Report Count (1),                         */
	0x75, 0x10,         /*      Report Size (16),                         */
	0x81, 0x00,         /*      Input,                                    */
	0xC0,               /*  End Collection,                               */
	0x05, 0x01,         /*  Usage Page (Desktop),                         */
	0x09, 0x06,         /*  Usage (Keyboard),                             */
	0xA1, 0x01,         /*  Collection (Application),                     */
	0x85, 0x03,         /*      Report ID (3),                            */
	0x95, 0x38,         /*      Report Count (56),                        */
	0x75, 0x01,         /*      Report Size (1),                          */
	0x15, 0x00,         /*      Logical Minimum (0),                      */
	0x25, 0x01,         /*      Logical Maximum (1),                      */
	0x05, 0x07,         /*      Usage Page (Keyboard),                    */
	0x19, 0xE0,         /*      Usage Minimum (KB Leftcontrol),           */
	0x29, 0xE7,         /*      Usage Maximum (KB Right GUI),             */
	0x19, 0x00,         /*      Usage Minimum (None),                     */
	0x29, 0x2F,         /*      Usage Maximum (KB Lboxbracket And Lbrace),*/
	0x81, 0x02,         /*      Input (Variable),                         */
	0xC0,               /*  End Collection,                               */
	0x05, 0x01,         /*  Usage Page (Desktop),                         */
	0x09, 0x06,         /*  Usage (Keyboard),                             */
	0xA1, 0x01,         /*  Collection (Application),                     */
	0x85, 0x04,         /*      Report ID (4),                            */
	0x95, 0x38,         /*      Report Count (56),                        */
	0x75, 0x01,         /*      Report Size (1),                          */
	0x15, 0x00,         /*      Logical Minimum (0),                      */
	0x25, 0x01,         /*      Logical Maximum (1),                      */
	0x05, 0x07,         /*      Usage Page (Keyboard),                    */
	0x19, 0x30,         /*      Usage Minimum (KB Rboxbracket And Rbrace),*/
	0x29, 0x67,         /*      Usage Maximum (KP Equals),                */
	0x81, 0x02,         /*      Input (Variable),                         */
	0xC0,               /*  End Collection                                */

	/* LED usage for the boot protocol interface */
	0x05, 0x01,         /*  Usage Page (Desktop),                         */
	0x09, 0x06,         /*  Usage (Keyboard),                             */
	0xA1, 0x01,         /*  Collection (Application),                     */
	0x05, 0x08,         /*      Usage Page (LED),                         */
	0x19, 0x01,         /*      Usage Minimum (01h),                      */
	0x29, 0x03,         /*      Usage Maximum (03h),                      */
	0x15, 0x00,         /*      Logical Minimum (0),                      */
	0x25, 0x01,         /*      Logical Maximum (1),                      */
	0x75, 0x01,         /*      Report Size (1),                          */
	0x95, 0x03,         /*      Report Count (3),                         */
	0x91, 0x02,         /*      Output (Variable),                        */
	0x95, 0x05,         /*      Report Count (5),                         */
	0x91, 0x01,         /*      Output (Constant),                        */
	0xC0,               /*  End Collection                                */
};

static __u8 *holtek_kbd_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);

	if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
		rdesc = holtek_kbd_rdesc_fixed;
		*rsize = sizeof(holtek_kbd_rdesc_fixed);
	}
	return rdesc;
}

static int holtek_kbd_input_event(struct input_dev *dev, unsigned int type,
		unsigned int code,
		int value)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct usb_device *usb_dev = hid_to_usb_dev(hid);

	/* Locate the boot interface, to receive the LED change events */
	struct usb_interface *boot_interface = usb_ifnum_to_if(usb_dev, 0);
	struct hid_device *boot_hid;
	struct hid_input *boot_hid_input;

	if (unlikely(boot_interface == NULL))
		return -ENODEV;

	boot_hid = usb_get_intfdata(boot_interface);
	if (list_empty(&boot_hid->inputs)) {
		hid_err(hid, "no inputs found\n");
		return -ENODEV;
	}
	boot_hid_input = list_first_entry(&boot_hid->inputs,
		struct hid_input, list);

	return boot_hid_input->input->event(boot_hid_input->input, type, code,
			value);
}

static int holtek_kbd_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	struct usb_interface *intf;
	int ret;

	if (!hid_is_usb(hdev))
		return -EINVAL;

	ret = hid_parse(hdev);
	if (!ret)
		ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	intf = to_usb_interface(hdev->dev.parent);
	if (!ret && intf->cur_altsetting->desc.bInterfaceNumber == 1) {
		struct hid_input *hidinput;
		list_for_each_entry(hidinput, &hdev->inputs, list) {
			hidinput->input->event = holtek_kbd_input_event;
		}
	}

	return ret;
}

static const struct hid_device_id holtek_kbd_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_HOLTEK_ALT,
			USB_DEVICE_ID_HOLTEK_ALT_KEYBOARD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, holtek_kbd_devices);

static struct hid_driver holtek_kbd_driver = {
	.name = "holtek_kbd",
	.id_table = holtek_kbd_devices,
	.report_fixup = holtek_kbd_report_fixup,
	.probe = holtek_kbd_probe
};
module_hid_driver(holtek_kbd_driver);

MODULE_DESCRIPTION("HID driver for Holtek keyboard");
MODULE_LICENSE("GPL");
