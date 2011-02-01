/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/usb.h>
#include <net/bluetooth/bluetooth.h>

#define VERSION "1.0"


static struct usb_device_id ath3k_table[] = {
	/* Atheros AR3011 */
	{ USB_DEVICE(0x0CF3, 0x3000) },

	/* Atheros AR3011 with sflash firmware*/
	{ USB_DEVICE(0x0CF3, 0x3002) },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ath3k_table);

#define USB_REQ_DFU_DNLOAD	1
#define BULK_SIZE		4096

static int ath3k_load_firmware(struct usb_device *udev,
				const struct firmware *firmware)
{
	u8 *send_buf;
	int err, pipe, len, size, sent = 0;
	int count = firmware->size;

	BT_DBG("udev %p", udev);

	pipe = usb_sndctrlpipe(udev, 0);

	send_buf = kmalloc(BULK_SIZE, GFP_ATOMIC);
	if (!send_buf) {
		BT_ERR("Can't allocate memory chunk for firmware");
		return -ENOMEM;
	}

	memcpy(send_buf, firmware->data, 20);
	if ((err = usb_control_msg(udev, pipe,
				USB_REQ_DFU_DNLOAD,
				USB_TYPE_VENDOR, 0, 0,
				send_buf, 20, USB_CTRL_SET_TIMEOUT)) < 0) {
		BT_ERR("Can't change to loading configuration err");
		goto error;
	}
	sent += 20;
	count -= 20;

	while (count) {
		size = min_t(uint, count, BULK_SIZE);
		pipe = usb_sndbulkpipe(udev, 0x02);
		memcpy(send_buf, firmware->data + sent, size);

		err = usb_bulk_msg(udev, pipe, send_buf, size,
					&len, 3000);

		if (err || (len != size)) {
			BT_ERR("Error in firmware loading err = %d,"
				"len = %d, size = %d", err, len, size);
			goto error;
		}

		sent  += size;
		count -= size;
	}

	kfree(send_buf);
	return 0;

error:
	kfree(send_buf);
	return err;
}

static int ath3k_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	const struct firmware *firmware;
	struct usb_device *udev = interface_to_usbdev(intf);

	BT_DBG("intf %p id %p", intf, id);

	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	if (request_firmware(&firmware, "ath3k-1.fw", &udev->dev) < 0) {
		return -EIO;
	}

	if (ath3k_load_firmware(udev, firmware)) {
		release_firmware(firmware);
		return -EIO;
	}
	release_firmware(firmware);

	return 0;
}

static void ath3k_disconnect(struct usb_interface *intf)
{
	BT_DBG("ath3k_disconnect intf %p", intf);
}

static struct usb_driver ath3k_driver = {
	.name		= "ath3k",
	.probe		= ath3k_probe,
	.disconnect	= ath3k_disconnect,
	.id_table	= ath3k_table,
};

static int __init ath3k_init(void)
{
	BT_INFO("Atheros AR30xx firmware driver ver %s", VERSION);
	return usb_register(&ath3k_driver);
}

static void __exit ath3k_exit(void)
{
	usb_deregister(&ath3k_driver);
}

module_init(ath3k_init);
module_exit(ath3k_exit);

MODULE_AUTHOR("Atheros Communications");
MODULE_DESCRIPTION("Atheros AR30xx firmware driver");
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("ath3k-1.fw");
