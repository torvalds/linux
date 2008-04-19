/*
 *  Driver for the Auvitek USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@hauppauge.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <linux/mutex.h>

#include "au0828.h"

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");

#define _err(fmt, arg...)\
	do {\
		printk(KERN_ERR DRIVER_NAME "/0: " fmt, ## arg);\
	} while (0)

#define _info(fmt, arg...)\
	do {\
		printk(KERN_INFO DRIVER_NAME "/0: " fmt, ## arg);\
	} while (0)

#define _dbg(level, fmt, arg...)\
	do {\
		if (debug >= level) \
			printk(KERN_DEBUG DRIVER_NAME "/0: " fmt, ## arg);\
	} while (0)

#define _AU0828_BULKPIPE 0x03
#define _BULKPIPESIZE 0xffff

static int send_control_msg(struct au0828_dev *dev, u16 request, u32 value,
	u16 index, unsigned char *cp, u16 size);
static int recv_control_msg(struct au0828_dev *dev, u16 request, u32 value,
	u16 index, unsigned char *cp, u16 size);

/* USB Direction */
#define CMD_REQUEST_IN		0x00
#define CMD_REQUEST_OUT		0x01

u32 au0828_readreg(struct au0828_dev *dev, u16 reg)
{
	recv_control_msg(dev, CMD_REQUEST_IN, 0, reg, dev->ctrlmsg, 1);
	_dbg(3,"%s(0x%x) = 0x%x\n", __FUNCTION__, reg, dev->ctrlmsg[0]);
	return dev->ctrlmsg[0];
}

u32 au0828_writereg(struct au0828_dev *dev, u16 reg, u32 val)
{
	_dbg(3,"%s(0x%x, 0x%x)\n", __FUNCTION__, reg, val);
	return send_control_msg(dev, CMD_REQUEST_OUT, val, reg, dev->ctrlmsg, 0);
}

static void cmd_msg_dump(struct au0828_dev *dev)
{
	int i;

	for (i = 0;i < sizeof(dev->ctrlmsg); i+=16)
		_dbg(1,"%s() %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x %02x %02x %02x %02x\n",
			__FUNCTION__,
			dev->ctrlmsg[i+0], dev->ctrlmsg[i+1],
			dev->ctrlmsg[i+2], dev->ctrlmsg[i+3],
			dev->ctrlmsg[i+4], dev->ctrlmsg[i+5],
			dev->ctrlmsg[i+6], dev->ctrlmsg[i+7],
			dev->ctrlmsg[i+8], dev->ctrlmsg[i+9],
			dev->ctrlmsg[i+10], dev->ctrlmsg[i+11],
			dev->ctrlmsg[i+12], dev->ctrlmsg[i+13],
			dev->ctrlmsg[i+14], dev->ctrlmsg[i+15]);
}

static int send_control_msg(struct au0828_dev *dev, u16 request, u32 value,
	u16 index, unsigned char *cp, u16 size)
{
	int status = -ENODEV;
	mutex_lock(&dev->mutex);
	if (dev->usbdev) {

		/* cp must be memory that has been allocated by kmalloc */
		status = usb_control_msg(dev->usbdev,
				usb_sndctrlpipe(dev->usbdev, 0),
				request,
				USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				value, index,
				cp, size, 1000);

		status = min(status, 0);

		if (status < 0) {
			_err("%s() Failed sending control message, error %d.\n",
				__FUNCTION__,
				status);
		}

	}
	mutex_unlock(&dev->mutex);
	return status;
}

static int recv_control_msg(struct au0828_dev *dev, u16 request, u32 value,
	u16 index, unsigned char *cp, u16 size)
{
	int status = -ENODEV;
	mutex_lock(&dev->mutex);
	if (dev->usbdev) {

		memset(dev->ctrlmsg, 0, sizeof(dev->ctrlmsg));

		/* cp must be memory that has been allocated by kmalloc */
		status = usb_control_msg(dev->usbdev,
				usb_rcvctrlpipe(dev->usbdev, 0),
				request,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				value, index,
				cp, size, 1000);

		status = min(status, 0);

		if (status < 0) {
			_err("%s() Failed receiving ctrl msg, error %d.\n",
				__FUNCTION__,
				status);
		}
		else
			if (debug > 4)
				cmd_msg_dump(dev);
	}
	mutex_unlock(&dev->mutex);
	return status;
}
static void au0828_usb_disconnect(struct usb_interface *interface)
{
	struct au0828_dev *dev = usb_get_intfdata(interface);

	_dbg(1,"%s()\n", __FUNCTION__);

	/* Digital TV */
	au0828_dvb_unregister(dev);

	/* I2C */
	au0828_i2c_unregister(dev);

	usb_set_intfdata(interface, NULL);

	mutex_lock(&dev->mutex);
	dev->usbdev = NULL;
	mutex_unlock(&dev->mutex);

	kfree(dev);

}

static int au0828_usb_probe (struct usb_interface *interface,
	const struct usb_device_id *id)
{
	int ifnum;
	struct au0828_dev *dev;
	struct usb_device *usbdev = interface_to_usbdev(interface);

	ifnum = interface->altsetting->desc.bInterfaceNumber;

	if (ifnum != 0)
		return -ENODEV;

	_dbg(1,"%s() vendor id 0x%x device id 0x%x ifnum:%d\n",
		__FUNCTION__,
		le16_to_cpu(usbdev->descriptor.idVendor),
		le16_to_cpu(usbdev->descriptor.idProduct),
		ifnum);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		_err("Unable to allocate memory\n");
		return -ENOMEM;
	}

	mutex_init(&dev->mutex);
	mutex_init(&dev->dvb.lock);
	dev->usbdev = usbdev;
	dev->board = id->driver_info;

	usb_set_intfdata(interface, dev);

	/* Power Up the bridge */
	au0828_write(dev, REG_600, 1 << 4);

	/* Bring up the GPIO's and supporting devices */
	au0828_gpio_setup(dev);

	/* I2C */
	au0828_i2c_register(dev);

	/* Digital TV */
	au0828_dvb_register(dev);

	_info("Registered device AU0828 [%s]\n",
		au0828_boards[dev->board].name);

	return 0;
}

static struct usb_driver au0828_usb_driver = {
	.name		= DRIVER_NAME,
	.probe		= au0828_usb_probe,
	.disconnect	= au0828_usb_disconnect,
	.id_table	= au0828_usb_id_table,
};

static int __init au0828_init(void)
{
	int ret;

	_info("au0828 driver loaded\n");

	ret = usb_register(&au0828_usb_driver);
	if (ret)
		_err("usb_register failed, error = %d\n", ret);

	return ret;
}

static void __exit au0828_exit(void)
{
	usb_deregister(&au0828_usb_driver);
}

module_init(au0828_init);
module_exit(au0828_exit);

MODULE_DESCRIPTION("Driver for Auvitek AU0828 based products");
MODULE_AUTHOR("Steven Toth <stoth@hauppauge.com>");
MODULE_LICENSE("GPL");
