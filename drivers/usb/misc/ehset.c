// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
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

extern const struct usb_device_id *usb_device_match_id(struct usb_device *udev,
						const struct usb_device_id *id);

/*
 * A list of USB hubs which requires to disable the power
 * to the port before starting the testing procedures.
 */
static const struct usb_device_id ehset_hub_list[] = {
	{ USB_DEVICE(0x0424, 0x4502) },
	{ USB_DEVICE(0x0424, 0x4913) },
	{ USB_DEVICE(0x0451, 0x8027) },
	{ }
};

static int ehset_prepare_port_for_testing(struct usb_device *hub_udev, u16 portnum)
{
	int ret = 0;

	/*
	 * The USB2.0 spec chapter 11.24.2.13 says that the USB port which is
	 * going under test needs to be put in suspend before sending the
	 * test command. Most hubs don't enforce this precondition, but there
	 * are some hubs which needs to disable the power to the port before
	 * starting the test.
	 */
	if (usb_device_match_id(hub_udev, ehset_hub_list)) {
		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_CLEAR_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_ENABLE,
					   portnum, NULL, 0, 1000, GFP_KERNEL);
		/*
		 * Wait for the port to be disabled. It's an arbitrary value
		 * which worked every time.
		 */
		msleep(100);
	} else {
		/*
		 * For the hubs which are compliant with the spec,
		 * put the port in SUSPEND.
		 */
		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_SET_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_SUSPEND,
					   portnum, NULL, 0, 1000, GFP_KERNEL);
	}
	return ret;
}

static int ehset_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	int ret = -EINVAL;
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_device *hub_udev = dev->parent;
	struct usb_device_descriptor buf;
	u8 portnum = dev->portnum;
	u16 test_pid = le16_to_cpu(dev->descriptor.idProduct);

	switch (test_pid) {
	case TEST_SE0_NAK_PID:
		ret = ehset_prepare_port_for_testing(hub_udev, portnum);
		if (!ret)
			break;
		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_SET_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_TEST,
					   (USB_TEST_SE0_NAK << 8) | portnum,
					   NULL, 0, 1000, GFP_KERNEL);
		break;
	case TEST_J_PID:
		ret = ehset_prepare_port_for_testing(hub_udev, portnum);
		if (!ret)
			break;
		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_SET_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_TEST,
					   (USB_TEST_J << 8) | portnum, NULL, 0,
					   1000, GFP_KERNEL);
		break;
	case TEST_K_PID:
		ret = ehset_prepare_port_for_testing(hub_udev, portnum);
		if (!ret)
			break;
		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_SET_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_TEST,
					   (USB_TEST_K << 8) | portnum, NULL, 0,
					   1000, GFP_KERNEL);
		break;
	case TEST_PACKET_PID:
		ret = ehset_prepare_port_for_testing(hub_udev, portnum);
		if (!ret)
			break;
		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_SET_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_TEST,
					   (USB_TEST_PACKET << 8) | portnum,
					   NULL, 0, 1000, GFP_KERNEL);
		break;
	case TEST_HS_HOST_PORT_SUSPEND_RESUME:
		/* Test: wait for 15secs -> suspend -> 15secs delay -> resume */
		msleep(15 * 1000);
		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_SET_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_SUSPEND,
					   portnum, NULL, 0, 1000, GFP_KERNEL);
		if (ret < 0)
			break;

		msleep(15 * 1000);
		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_CLEAR_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_SUSPEND,
					   portnum, NULL, 0, 1000, GFP_KERNEL);
		break;
	case TEST_SINGLE_STEP_GET_DEV_DESC:
		/* Test: wait for 15secs -> GetDescriptor request */
		msleep(15 * 1000);

		ret = usb_control_msg_recv(dev, 0, USB_REQ_GET_DESCRIPTOR,
					   USB_DIR_IN, USB_DT_DEVICE << 8, 0,
					   &buf, USB_DT_DEVICE_SIZE,
					   USB_CTRL_GET_TIMEOUT, GFP_KERNEL);
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

		ret = usb_control_msg_send(hub_udev, 0, USB_REQ_SET_FEATURE,
					   USB_RT_PORT, USB_PORT_FEAT_TEST,
					   (6 << 8) | portnum, NULL, 0,
					   60 * 1000, GFP_KERNEL);

		break;
	default:
		dev_err(&intf->dev, "%s: unsupported PID: 0x%x\n",
			__func__, test_pid);
	}

	return ret;
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
