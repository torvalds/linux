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
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ath3k_table);

#define USB_REQ_DFU_DNLOAD	1
#define BULK_SIZE		4096

struct ath3k_data {
	struct usb_device *udev;
	u8 *fw_data;
	u32 fw_size;
	u32 fw_sent;
};

static int ath3k_load_firmware(struct ath3k_data *data,
				unsigned char *firmware,
				int count)
{
	u8 *send_buf;
	int err, pipe, len, size, sent = 0;

	BT_DBG("ath3k %p udev %p", data, data->udev);

	pipe = usb_sndctrlpipe(data->udev, 0);

	if ((usb_control_msg(data->udev, pipe,
				USB_REQ_DFU_DNLOAD,
				USB_TYPE_VENDOR, 0, 0,
				firmware, 20, USB_CTRL_SET_TIMEOUT)) < 0) {
		BT_ERR("Can't change to loading configuration err");
		return -EBUSY;
	}
	sent += 20;
	count -= 20;

	send_buf = kmalloc(BULK_SIZE, GFP_ATOMIC);
	if (!send_buf) {
		BT_ERR("Can't allocate memory chunk for firmware");
		return -ENOMEM;
	}

	while (count) {
		size = min_t(uint, count, BULK_SIZE);
		pipe = usb_sndbulkpipe(data->udev, 0x02);
		memcpy(send_buf, firmware + sent, size);

		err = usb_bulk_msg(data->udev, pipe, send_buf, size,
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
	struct ath3k_data *data;
	int size;

	BT_DBG("intf %p id %p", intf, id);

	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->udev = udev;

	if (request_firmware(&firmware, "ath3k-1.fw", &udev->dev) < 0) {
		kfree(data);
		return -EIO;
	}

	size = max_t(uint, firmware->size, 4096);
	data->fw_data = kmalloc(size, GFP_KERNEL);
	if (!data->fw_data) {
		release_firmware(firmware);
		kfree(data);
		return -ENOMEM;
	}

	memcpy(data->fw_data, firmware->data, firmware->size);
	data->fw_size = firmware->size;
	data->fw_sent = 0;
	release_firmware(firmware);

	usb_set_intfdata(intf, data);
	if (ath3k_load_firmware(data, data->fw_data, data->fw_size)) {
		usb_set_intfdata(intf, NULL);
		kfree(data->fw_data);
		kfree(data);
		return -EIO;
	}

	return 0;
}

static void ath3k_disconnect(struct usb_interface *intf)
{
	struct ath3k_data *data = usb_get_intfdata(intf);

	BT_DBG("ath3k_disconnect intf %p", intf);

	kfree(data->fw_data);
	kfree(data);
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
