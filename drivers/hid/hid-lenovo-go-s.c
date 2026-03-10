// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Lenovo Legion Go S devices.
 *
 *  Copyright (c) 2026 Derek J. Clark <derekjohn.clark@gmail.com>
 *  Copyright (c) 2026 Valve Corporation
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <linux/workqueue_types.h>

#include "hid-ids.h"

#define GO_S_CFG_INTF_IN	0x84
#define GO_S_PACKET_SIZE	64

static struct hid_gos_cfg {
	struct delayed_work gos_cfg_setup;
	struct completion send_cmd_complete;
	struct hid_device *hdev;
	struct mutex cfg_mutex; /*ensure single synchronous output report*/
} drvdata;

struct command_report {
	u8 cmd;
	u8 sub_cmd;
	u8 data[63];
} __packed;

struct version_report {
	u8 cmd;
	u32 version;
	u8 reserved[59];
} __packed;

enum mcu_command_index {
	GET_VERSION = 0x01,
	GET_MCU_ID,
	GET_GAMEPAD_CFG,
	SET_GAMEPAD_CFG,
	GET_TP_PARAM,
	SET_TP_PARAM,
	GET_RGB_CFG = 0x0f,
	SET_RGB_CFG,
	GET_PL_TEST = 0xdf,
};

#define FEATURE_NONE 0x00

static int hid_gos_version_event(u8 *data)
{
	struct version_report *ver_rep = (struct version_report *)data;

	drvdata.hdev->firmware_version = get_unaligned_le32(&ver_rep->version);
	return 0;
}

static int get_endpoint_address(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_host_endpoint *ep;

	if (intf) {
		ep = intf->cur_altsetting->endpoint;
		if (ep)
			return ep->desc.bEndpointAddress;
	}

	return -ENODEV;
}

static int hid_gos_raw_event(struct hid_device *hdev, struct hid_report *report,
			     u8 *data, int size)
{
	struct command_report *cmd_rep;
	int ep, ret;

	ep = get_endpoint_address(hdev);
	if (ep != GO_S_CFG_INTF_IN)
		return 0;

	if (size != GO_S_PACKET_SIZE)
		return -EINVAL;

	cmd_rep = (struct command_report *)data;

	switch (cmd_rep->cmd) {
	case GET_VERSION:
		ret = hid_gos_version_event(data);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	dev_dbg(&hdev->dev, "Rx data as raw input report: [%*ph]\n",
		GO_S_PACKET_SIZE, data);

	complete(&drvdata.send_cmd_complete);
	return ret;
}

static int mcu_property_out(struct hid_device *hdev, u8 command, u8 index,
			    u8 *data, size_t len)
{
	unsigned char *dmabuf __free(kfree) = NULL;
	u8 header[] = { command, index };
	size_t header_size = ARRAY_SIZE(header);
	int timeout, ret;

	if (header_size + len > GO_S_PACKET_SIZE)
		return -EINVAL;

	guard(mutex)(&drvdata.cfg_mutex);
	/* We can't use a devm_alloc reusable buffer without side effects during suspend */
	dmabuf = kzalloc(GO_S_PACKET_SIZE, GFP_KERNEL);
	if (!dmabuf)
		return -ENOMEM;

	memcpy(dmabuf, header, header_size);
	memcpy(dmabuf + header_size, data, len);

	dev_dbg(&hdev->dev, "Send data as raw output report: [%*ph]\n",
		GO_S_PACKET_SIZE, dmabuf);

	ret = hid_hw_output_report(hdev, dmabuf, GO_S_PACKET_SIZE);
	if (ret < 0)
		return ret;

	ret = ret == GO_S_PACKET_SIZE ? 0 : -EINVAL;
	if (ret)
		return ret;

	/* PL_TEST commands can take longer because they go out to another device */
	timeout = (command == GET_PL_TEST) ? 200 : 5;
	ret = wait_for_completion_interruptible_timeout(&drvdata.send_cmd_complete,
							msecs_to_jiffies(timeout));

	if (ret == 0) /* timeout occurred */
		ret = -EBUSY;

	reinit_completion(&drvdata.send_cmd_complete);
	return 0;
}

static void cfg_setup(struct work_struct *work)
{
	int ret;

	ret = mcu_property_out(drvdata.hdev, GET_VERSION, FEATURE_NONE, NULL, 0);
	if (ret) {
		dev_err(&drvdata.hdev->dev, "Failed to retrieve MCU Version: %i\n", ret);
		return;
	}
}

static int hid_gos_cfg_probe(struct hid_device *hdev,
			     const struct hid_device_id *_id)
{
	int ret;

	hid_set_drvdata(hdev, &drvdata);
	drvdata.hdev = hdev;
	mutex_init(&drvdata.cfg_mutex);

	init_completion(&drvdata.send_cmd_complete);

	/* Executing calls prior to returning from probe will lock the MCU. Schedule
	 * initial data call after probe has completed and MCU can accept calls.
	 */
	INIT_DELAYED_WORK(&drvdata.gos_cfg_setup, &cfg_setup);
	ret = schedule_delayed_work(&drvdata.gos_cfg_setup, msecs_to_jiffies(2));
	if (!ret) {
		dev_err(&hdev->dev, "Failed to schedule startup delayed work\n");
		return -ENODEV;
	}

	return 0;
}

static void hid_gos_cfg_remove(struct hid_device *hdev)
{
	guard(mutex)(&drvdata.cfg_mutex);
	cancel_delayed_work_sync(&drvdata.gos_cfg_setup);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	hid_set_drvdata(hdev, NULL);
}

static int hid_gos_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	int ret, ep;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "Failed to start HID device\n");
		return ret;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "Failed to open HID device\n");
		hid_hw_stop(hdev);
		return ret;
	}

	ep = get_endpoint_address(hdev);
	if (ep != GO_S_CFG_INTF_IN) {
		dev_dbg(&hdev->dev, "Started interface %x as generic HID device.\n", ep);
		return 0;
	}

	ret = hid_gos_cfg_probe(hdev, id);
	if (ret)
		dev_err_probe(&hdev->dev, ret, "Failed to start configuration interface");

	dev_dbg(&hdev->dev, "Started interface %x as Go S configuration interface\n", ep);
	return ret;
}

static void hid_gos_remove(struct hid_device *hdev)
{
	int ep = get_endpoint_address(hdev);

	switch (ep) {
	case GO_S_CFG_INTF_IN:
		hid_gos_cfg_remove(hdev);
		break;
	default:
		hid_hw_close(hdev);
		hid_hw_stop(hdev);

		break;
	}
}

static const struct hid_device_id hid_gos_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_QHE,
			 USB_DEVICE_ID_LENOVO_LEGION_GO_S_XINPUT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_QHE,
			 USB_DEVICE_ID_LENOVO_LEGION_GO_S_DINPUT) },
	{}
};

MODULE_DEVICE_TABLE(hid, hid_gos_devices);
static struct hid_driver hid_lenovo_go_s = {
	.name = "hid-lenovo-go-s",
	.id_table = hid_gos_devices,
	.probe = hid_gos_probe,
	.remove = hid_gos_remove,
	.raw_event = hid_gos_raw_event,
};
module_hid_driver(hid_lenovo_go_s);

MODULE_AUTHOR("Derek J. Clark");
MODULE_DESCRIPTION("HID Driver for Lenovo Legion Go S Series gamepad.");
MODULE_LICENSE("GPL");
