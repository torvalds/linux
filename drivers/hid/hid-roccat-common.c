/*
 * Roccat common functions for device specific drivers
 *
 * Copyright (c) 2011 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "hid-roccat-common.h"

static inline uint16_t roccat_common2_feature_report(uint8_t report_id)
{
	return 0x300 | report_id;
}

int roccat_common2_receive(struct usb_device *usb_dev, uint report_id,
		void *data, uint size)
{
	char *buf;
	int len;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			HID_REQ_GET_REPORT,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			roccat_common2_feature_report(report_id),
			0, buf, size, USB_CTRL_SET_TIMEOUT);

	memcpy(data, buf, size);
	kfree(buf);
	return ((len < 0) ? len : ((len != size) ? -EIO : 0));
}
EXPORT_SYMBOL_GPL(roccat_common2_receive);

int roccat_common2_send(struct usb_device *usb_dev, uint report_id,
		void const *data, uint size)
{
	char *buf;
	int len;

	buf = kmemdup(data, size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			HID_REQ_SET_REPORT,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
			roccat_common2_feature_report(report_id),
			0, buf, size, USB_CTRL_SET_TIMEOUT);

	kfree(buf);
	return ((len < 0) ? len : ((len != size) ? -EIO : 0));
}
EXPORT_SYMBOL_GPL(roccat_common2_send);

enum roccat_common2_control_states {
	ROCCAT_COMMON_CONTROL_STATUS_OVERLOAD = 0,
	ROCCAT_COMMON_CONTROL_STATUS_OK = 1,
	ROCCAT_COMMON_CONTROL_STATUS_INVALID = 2,
	ROCCAT_COMMON_CONTROL_STATUS_WAIT = 3,
};

static int roccat_common2_receive_control_status(struct usb_device *usb_dev)
{
	int retval;
	struct roccat_common2_control control;

	do {
		msleep(50);
		retval = roccat_common2_receive(usb_dev,
				ROCCAT_COMMON_COMMAND_CONTROL,
				&control, sizeof(struct roccat_common2_control));

		if (retval)
			return retval;

		switch (control.value) {
		case ROCCAT_COMMON_CONTROL_STATUS_OK:
			return 0;
		case ROCCAT_COMMON_CONTROL_STATUS_WAIT:
			msleep(500);
			continue;
		case ROCCAT_COMMON_CONTROL_STATUS_INVALID:

		case ROCCAT_COMMON_CONTROL_STATUS_OVERLOAD:
			/* seems to be critical - replug necessary */
			return -EINVAL;
		default:
			dev_err(&usb_dev->dev,
					"roccat_common2_receive_control_status: "
					"unknown response value 0x%x\n",
					control.value);
			return -EINVAL;
		}

	} while (1);
}

int roccat_common2_send_with_status(struct usb_device *usb_dev,
		uint command, void const *buf, uint size)
{
	int retval;

	retval = roccat_common2_send(usb_dev, command, buf, size);
	if (retval)
		return retval;

	msleep(100);

	return roccat_common2_receive_control_status(usb_dev);
}
EXPORT_SYMBOL_GPL(roccat_common2_send_with_status);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat common driver");
MODULE_LICENSE("GPL v2");
