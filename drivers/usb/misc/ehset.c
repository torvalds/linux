/*
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/ch11.h>

#define TEST_SE0_NAK_PID			0x0101
#define TEST_J_PID				0x0102
#define TEST_K_PID				0x0103
#define TEST_PACKET_PID				0x0104
#define TEST_HS_HOST_PORT_SUSPEND_RESUME	0x0106
#define TEST_SINGLE_STEP_GET_DEV_DESC		0x0107
#define TEST_SINGLE_STEP_SET_FEATURE		0x0108

static int ehset_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	int ret = -EINVAL;
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_device *hub_udev = dev->parent;
	struct usb_device_descriptor *buf;
	u8 portnum = dev->portnum;
	u16 test_pid = le16_to_cpu(dev->descriptor.idProduct);

	switch (test_pid) {
	case TEST_SE0_NAK_PID:
		ret = usb_control_msg(hub_udev, usb_sndctrlpipe(hub_udev, 0),
					USB_REQ_SET_FEATURE, USB_RT_PORT,
					USB_PORT_FEAT_TEST,
					(TEST_SE0_NAK << 8) | portnum,
					NULL, 0, 1000);
		break;
	case TEST_J_PID:
		ret = usb_control_msg(hub_udev, usb_sndctrlpipe(hub_udev, 0),
					USB_REQ_SET_FEATURE, USB_RT_PORT,
					USB_PORT_FEAT_TEST,
					(TEST_J << 8) | portnum,
					NULL, 0, 1000);
		break;
	case TEST_K_PID:
		ret = usb_control_msg(hub_udev, usb_sndctrlpipe(hub_udev, 0),
					USB_REQ_SET_FEATURE, USB_RT_PORT,
					USB_PORT_FEAT_TEST,
					(TEST_K << 8) | portnum,
					NULL, 0, 1000);
		break;
	case TEST_PACKET_PID:
		ret = usb_control_msg(hub_udev, usb_sndctrlpipe(hub_udev, 0),
					USB_REQ_SET_FEATURE, USB_RT_PORT,
					USB_PORT_FEAT_TEST,
					(TEST_PACKET << 8) | portnum,
					NULL, 0, 1000);
		break;
	case TEST_HS_HOST_PORT_SUSPEND_RESUME:
		/* Test: wait for 15secs -> suspend -> 15secs delay -> resume */
		msleep(15 * 1000);
		ret = usb_control_msg(hub_udev, usb_sndctrlpipe(hub_udev, 0),
					USB_REQ_SET_FEATURE, USB_RT_PORT,
					USB_PORT_FEAT_SUSPEND, portnum,
					NULL, 0, 1000);
		if (ret < 0)
			break;

		msleep(15 * 1000);
		ret = usb_control_msg(hub_udev, usb_sndctrlpipe(hub_udev, 0),
					USB_REQ_CLEAR_FEATURE, USB_RT_PORT,
					USB_PORT_FEAT_SUSPEND, portnum,
					NULL, 0, 1000);
		break;
	case TEST_SINGLE_STEP_GET_DEV_DESC:
		/* Test: wait for 15secs -> GetDescriptor request */
		msleep(15 * 1000);
		buf = kmalloc(USB_DT_DEVICE_SIZE, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
					USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
					USB_DT_DEVICE << 8, 0,
					buf, USB_DT_DEVICE_SIZE,
					USB_CTRL_GET_TIMEOUT);
		kfree(buf);
		break;
	case TEST_SINGLE_STEP_SET_FEATURE:
		/*
		 * GetDescriptor SETUP request -> 15secs delay -> IN & STATUS
		 *
		 * Note, this test is only supported on root hubs since the
		 * SetPortFeature handling can only be done inside the HCD's
		 * hub_control callback function.
		 */
		if (hub_udev != dev->bus->root_hub) {
			dev_err(&intf->dev, "SINGLE_STEP_SET_FEATURE test only supported on root hub\n");
			break;
		}

		ret = usb_control_msg(hub_udev, usb_sndctrlpipe(hub_udev, 0),
					USB_REQ_SET_FEATURE, USB_RT_PORT,
					USB_PORT_FEAT_TEST,
					(6 << 8) | portnum,
					NULL, 0, 60 * 1000);

		break;
	default:
		dev_err(&intf->dev, "%s: unsupported PID: 0x%x\n",
			__func__, test_pid);
	}

	return (ret < 0) ? ret : 0;
}

static void ehset_disconnect(struct usb_interface *intf)
{
}

static const struct usb_device_id ehset_id_table[] = {
	{ USB_DEVICE(0x1a0a, TEST_SE0_NAK_PID) },
	{ USB_DEVICE(0x1a0a, TEST_J_PID) },
	{ USB_DEVICE(0x1a0a, TEST_K_PID) },
	{ USB_DEVICE(0x1a0a, TEST_PACKET_PID) },
	{ USB_DEVICE(0x1a0a, TEST_HS_HOST_PORT_SUSPEND_RESUME) },
	{ USB_DEVICE(0x1a0a, TEST_SINGLE_STEP_GET_DEV_DESC) },
	{ USB_DEVICE(0x1a0a, TEST_SINGLE_STEP_SET_FEATURE) },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, ehset_id_table);

static struct usb_driver ehset_driver = {
	.name =		"usb_ehset_test",
	.probe =	ehset_probe,
	.disconnect =	ehset_disconnect,
	.id_table =	ehset_id_table,
};

module_usb_driver(ehset_driver);

MODULE_DESCRIPTION("USB Driver for EHSET Test Fixture");
MODULE_LICENSE("GPL v2");
