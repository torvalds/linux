// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for loading USB isight firmware
 *
 * Copyright (C) 2008 Matthew Garrett <mjg@redhat.com>
 *
 * The USB isight cameras in recent Apples are roughly compatible with the USB
 * video class specification, and can be driven by uvcvideo. However, they
 * need firmware to be loaded beforehand. After firmware loading, the device
 * detaches from the USB bus and reattaches with a new device ID. It can then
 * be claimed by the uvc driver.
 *
 * The firmware is non-free and must be extracted by the user. Tools to do this
 * are available at http://bersace03.free.fr/ift/
 *
 * The isight firmware loading was reverse engineered by Johannes Berg
 * <johannes@sipsolutions.de>, and this driver is based on code by Ronald
 * Bultje <rbultje@ronald.bitfreak.net>
 */

#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>

static const struct usb_device_id id_table[] = {
	{USB_DEVICE(0x05ac, 0x8300)},
	{},
};

MODULE_DEVICE_TABLE(usb, id_table);

static int isight_firmware_load(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int llen, len, req, ret = 0;
	const struct firmware *firmware;
	unsigned char *buf = kmalloc(50, GFP_KERNEL);
	unsigned char data[4];
	const u8 *ptr;

	if (!buf)
		return -ENOMEM;

	if (request_firmware(&firmware, "isight.fw", &dev->dev) != 0) {
		printk(KERN_ERR "Unable to load isight firmware\n");
		ret = -ENODEV;
		goto out;
	}

	ptr = firmware->data;

	buf[0] = 0x01;
	if (usb_control_msg
	    (dev, usb_sndctrlpipe(dev, 0), 0xa0, 0x40, 0xe600, 0, buf, 1,
	     300) != 1) {
		printk(KERN_ERR
		       "Failed to initialise isight firmware loader\n");
		ret = -ENODEV;
		goto out;
	}

	while (ptr+4 <= firmware->data+firmware->size) {
		memcpy(data, ptr, 4);
		len = (data[0] << 8 | data[1]);
		req = (data[2] << 8 | data[3]);
		ptr += 4;

		if (len == 0x8001)
			break;	/* success */
		else if (len == 0)
			continue;

		for (; len > 0; req += 50) {
			llen = min(len, 50);
			len -= llen;
			if (ptr+llen > firmware->data+firmware->size) {
				printk(KERN_ERR
				       "Malformed isight firmware");
				ret = -ENODEV;
				goto out;
			}
			memcpy(buf, ptr, llen);

			ptr += llen;

			if (usb_control_msg
			    (dev, usb_sndctrlpipe(dev, 0), 0xa0, 0x40, req, 0,
			     buf, llen, 300) != llen) {
				printk(KERN_ERR
				       "Failed to load isight firmware\n");
				ret = -ENODEV;
				goto out;
			}

		}
	}

	buf[0] = 0x00;
	if (usb_control_msg
	    (dev, usb_sndctrlpipe(dev, 0), 0xa0, 0x40, 0xe600, 0, buf, 1,
	     300) != 1) {
		printk(KERN_ERR "isight firmware loading completion failed\n");
		ret = -ENODEV;
	}

out:
	kfree(buf);
	release_firmware(firmware);
	return ret;
}

MODULE_FIRMWARE("isight.fw");

static void isight_firmware_disconnect(struct usb_interface *intf)
{
}

static struct usb_driver isight_firmware_driver = {
	.name = "isight_firmware",
	.probe = isight_firmware_load,
	.disconnect = isight_firmware_disconnect,
	.id_table = id_table,
};

module_usb_driver(isight_firmware_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
