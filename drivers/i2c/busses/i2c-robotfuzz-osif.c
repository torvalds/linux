// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for RobotFuzz OSIF
 *
 * Copyright (c) 2013 Andrew Lunn <andrew@lunn.ch>
 * Copyright (c) 2007 Barry Carter <Barry.Carter@robotfuzz.com>
 *
 * Based on the i2c-tiny-usb by
 *
 * Copyright (C) 2006 Til Harbaum (Till@Harbaum.org)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/usb.h>

#define OSIFI2C_READ		20
#define OSIFI2C_WRITE		21
#define OSIFI2C_STOP		22
#define OSIFI2C_STATUS		23
#define OSIFI2C_SET_BIT_RATE	24

#define STATUS_ADDRESS_ACK	0
#define STATUS_ADDRESS_NAK	2

struct osif_priv {
	struct usb_device *usb_dev;
	struct usb_interface *interface;
	struct i2c_adapter adapter;
	unsigned char status;
};

static int osif_usb_read(struct i2c_adapter *adapter, int cmd,
			 int value, int index, void *data, int len)
{
	struct osif_priv *priv = adapter->algo_data;

	return usb_control_msg(priv->usb_dev, usb_rcvctrlpipe(priv->usb_dev, 0),
			       cmd, USB_TYPE_VENDOR | USB_RECIP_INTERFACE |
			       USB_DIR_IN, value, index, data, len, 2000);
}

static int osif_usb_write(struct i2c_adapter *adapter, int cmd,
			  int value, int index, void *data, int len)
{

	struct osif_priv *priv = adapter->algo_data;

	return usb_control_msg(priv->usb_dev, usb_sndctrlpipe(priv->usb_dev, 0),
			       cmd, USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			       value, index, data, len, 2000);
}

static int osif_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			 int num)
{
	struct osif_priv *priv = adapter->algo_data;
	struct i2c_msg *pmsg;
	int ret;
	int i;

	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];

		if (pmsg->flags & I2C_M_RD) {
			ret = osif_usb_read(adapter, OSIFI2C_READ,
					    pmsg->flags, pmsg->addr,
					    pmsg->buf, pmsg->len);
			if (ret != pmsg->len) {
				dev_err(&adapter->dev, "failure reading data\n");
				return -EREMOTEIO;
			}
		} else {
			ret = osif_usb_write(adapter, OSIFI2C_WRITE,
					     pmsg->flags, pmsg->addr,
					     pmsg->buf, pmsg->len);
			if (ret != pmsg->len) {
				dev_err(&adapter->dev, "failure writing data\n");
				return -EREMOTEIO;
			}
		}

		ret = osif_usb_write(adapter, OSIFI2C_STOP, 0, 0, NULL, 0);
		if (ret) {
			dev_err(&adapter->dev, "failure sending STOP\n");
			return -EREMOTEIO;
		}

		/* read status */
		ret = osif_usb_read(adapter, OSIFI2C_STATUS, 0, 0,
				    &priv->status, 1);
		if (ret != 1) {
			dev_err(&adapter->dev, "failure reading status\n");
			return -EREMOTEIO;
		}

		if (priv->status != STATUS_ADDRESS_ACK) {
			dev_dbg(&adapter->dev, "status = %d\n", priv->status);
			return -EREMOTEIO;
		}
	}

	return i;
}

static u32 osif_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm osif_algorithm = {
	.master_xfer	= osif_xfer,
	.functionality	= osif_func,
};

#define USB_OSIF_VENDOR_ID	0x1964
#define USB_OSIF_PRODUCT_ID	0x0001

static const struct usb_device_id osif_table[] = {
	{ USB_DEVICE(USB_OSIF_VENDOR_ID, USB_OSIF_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, osif_table);

static int osif_probe(struct usb_interface *interface,
			     const struct usb_device_id *id)
{
	int ret;
	struct osif_priv *priv;
	u16 version;

	priv = devm_kzalloc(&interface->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->usb_dev = usb_get_dev(interface_to_usbdev(interface));
	priv->interface = interface;

	usb_set_intfdata(interface, priv);

	priv->adapter.owner = THIS_MODULE;
	priv->adapter.class = I2C_CLASS_HWMON;
	priv->adapter.algo = &osif_algorithm;
	priv->adapter.algo_data = priv;
	snprintf(priv->adapter.name, sizeof(priv->adapter.name),
		 "OSIF at bus %03d device %03d",
		 priv->usb_dev->bus->busnum, priv->usb_dev->devnum);

	/*
	 * Set bus frequency. The frequency is:
	 * 120,000,000 / ( 16 + 2 * div * 4^prescale).
	 * Using dev = 52, prescale = 0 give 100KHz */
	ret = osif_usb_write(&priv->adapter, OSIFI2C_SET_BIT_RATE, 52, 0,
			    NULL, 0);
	if (ret) {
		dev_err(&interface->dev, "failure sending bit rate");
		usb_put_dev(priv->usb_dev);
		return ret;
	}

	i2c_add_adapter(&(priv->adapter));

	version = le16_to_cpu(priv->usb_dev->descriptor.bcdDevice);
	dev_info(&interface->dev,
		 "version %x.%02x found at bus %03d address %03d",
		 version >> 8, version & 0xff,
		 priv->usb_dev->bus->busnum, priv->usb_dev->devnum);

	return 0;
}

static void osif_disconnect(struct usb_interface *interface)
{
	struct osif_priv *priv = usb_get_intfdata(interface);

	i2c_del_adapter(&(priv->adapter));
	usb_set_intfdata(interface, NULL);
	usb_put_dev(priv->usb_dev);
}

static struct usb_driver osif_driver = {
	.name		= "RobotFuzz Open Source InterFace, OSIF",
	.probe		= osif_probe,
	.disconnect	= osif_disconnect,
	.id_table	= osif_table,
};

module_usb_driver(osif_driver);

MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_AUTHOR("Barry Carter <barry.carter@robotfuzz.com>");
MODULE_DESCRIPTION("RobotFuzz OSIF driver");
MODULE_LICENSE("GPL v2");
