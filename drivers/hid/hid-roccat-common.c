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
#include "hid-roccat-common.h"

static inline uint16_t roccat_common_feature_report(uint8_t report_id)
{
	return 0x300 | report_id;
}

int roccat_common_receive(struct usb_device *usb_dev, uint report_id,
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
			roccat_common_feature_report(report_id),
			0, buf, size, USB_CTRL_SET_TIMEOUT);

	memcpy(data, buf, size);
	kfree(buf);
	return ((len < 0) ? len : ((len != size) ? -EIO : 0));
}
EXPORT_SYMBOL_GPL(roccat_common_receive);

int roccat_common_send(struct usb_device *usb_dev, uint report_id,
		void const *data, uint size)
{
	char *buf;
	int len;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	memcpy(buf, data, size);

	len = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			HID_REQ_SET_REPORT,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
			roccat_common_feature_report(report_id),
			0, buf, size, USB_CTRL_SET_TIMEOUT);

	kfree(buf);
	return ((len < 0) ? len : ((len != size) ? -EIO : 0));
}
EXPORT_SYMBOL_GPL(roccat_common_send);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat common driver");
MODULE_LICENSE("GPL v2");
