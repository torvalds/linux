/*
 *  Nano River Technologies viperboard i2c master driver
 *
 *  (C) 2012 by Lemonage GmbH
 *  Author: Lars Poeschel <poeschel@lemonage.de>
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>

#include <linux/usb.h>
#include <linux/i2c.h>

#include <linux/mfd/viperboard.h>

struct vprbrd_i2c {
	struct i2c_adapter i2c;
	u8 bus_freq_param;
};

/* i2c bus frequency module parameter */
static u8 i2c_bus_param;
static unsigned int i2c_bus_freq = 100;
module_param(i2c_bus_freq, int, 0);
MODULE_PARM_DESC(i2c_bus_freq,
	"i2c bus frequency in khz (default is 100) valid values: 10, 100, 200, 400, 1000, 3000, 6000");

static int vprbrd_i2c_status(struct i2c_adapter *i2c,
	struct vprbrd_i2c_status *status, bool prev_error)
{
	u16 bytes_xfer;
	int ret;
	struct vprbrd *vb = (struct vprbrd *)i2c->algo_data;

	/* check for protocol error */
	bytes_xfer = sizeof(struct vprbrd_i2c_status);

	ret = usb_control_msg(vb->usb_dev, usb_rcvctrlpipe(vb->usb_dev, 0),
		VPRBRD_USB_REQUEST_I2C, VPRBRD_USB_TYPE_IN, 0x0000, 0x0000,
		status, bytes_xfer, VPRBRD_USB_TIMEOUT_MS);

	if (ret != bytes_xfer)
		prev_error = true;

	if (prev_error) {
		dev_err(&i2c->dev, "failure in usb communication\n");
		return -EREMOTEIO;
	}

	dev_dbg(&i2c->dev, "  status = %d\n", status->status);
	if (status->status != 0x00) {
		dev_err(&i2c->dev, "failure: i2c protocol error\n");
		return -EPROTO;
	}
	return 0;
}

static int vprbrd_i2c_receive(struct usb_device *usb_dev,
	struct vprbrd_i2c_read_msg *rmsg, int bytes_xfer)
{
	int ret, bytes_actual;
	int error = 0;

	/* send the read request */
	ret = usb_bulk_msg(usb_dev,
		usb_sndbulkpipe(usb_dev, VPRBRD_EP_OUT), rmsg,
		sizeof(struct vprbrd_i2c_read_hdr), &bytes_actual,
		VPRBRD_USB_TIMEOUT_MS);

	if ((ret < 0)
		|| (bytes_actual != sizeof(struct vprbrd_i2c_read_hdr))) {
		dev_err(&usb_dev->dev, "failure transmitting usb\n");
		error = -EREMOTEIO;
	}

	/* read the actual data */
	ret = usb_bulk_msg(usb_dev,
		usb_rcvbulkpipe(usb_dev, VPRBRD_EP_IN), rmsg,
		bytes_xfer, &bytes_actual, VPRBRD_USB_TIMEOUT_MS);

	if ((ret < 0) || (bytes_xfer != bytes_actual)) {
		dev_err(&usb_dev->dev, "failure receiving usb\n");
		error = -EREMOTEIO;
	}
	return error;
}

static int vprbrd_i2c_addr(struct usb_device *usb_dev,
	struct vprbrd_i2c_addr_msg *amsg)
{
	int ret, bytes_actual;

	ret = usb_bulk_msg(usb_dev,
		usb_sndbulkpipe(usb_dev, VPRBRD_EP_OUT), amsg,
		sizeof(struct vprbrd_i2c_addr_msg), &bytes_actual,
		VPRBRD_USB_TIMEOUT_MS);

	if ((ret < 0) ||
			(sizeof(struct vprbrd_i2c_addr_msg) != bytes_actual)) {
		dev_err(&usb_dev->dev, "failure transmitting usb\n");
		return -EREMOTEIO;
	}
	return 0;
}

static int vprbrd_i2c_read(struct vprbrd *vb, struct i2c_msg *msg)
{
	int ret;
	u16 remain_len, len1, len2, start = 0x0000;
	struct vprbrd_i2c_read_msg *rmsg =
		(struct vprbrd_i2c_read_msg *)vb->buf;

	remain_len = msg->len;
	rmsg->header.cmd = VPRBRD_I2C_CMD_READ;
	while (remain_len > 0) {
		rmsg->header.addr = cpu_to_le16(start + 0x4000);
		if (remain_len <= 255) {
			len1 = remain_len;
			len2 = 0x00;
			rmsg->header.len0 = remain_len;
			rmsg->header.len1 = 0x00;
			rmsg->header.len2 = 0x00;
			rmsg->header.len3 = 0x00;
			rmsg->header.len4 = 0x00;
			rmsg->header.len5 = 0x00;
			remain_len = 0;
		} else if (remain_len <= 510) {
			len1 = remain_len;
			len2 = 0x00;
			rmsg->header.len0 = remain_len - 255;
			rmsg->header.len1 = 0xff;
			rmsg->header.len2 = 0x00;
			rmsg->header.len3 = 0x00;
			rmsg->header.len4 = 0x00;
			rmsg->header.len5 = 0x00;
			remain_len = 0;
		} else if (remain_len <= 512) {
			len1 = remain_len;
			len2 = 0x00;
			rmsg->header.len0 = remain_len - 510;
			rmsg->header.len1 = 0xff;
			rmsg->header.len2 = 0xff;
			rmsg->header.len3 = 0x00;
			rmsg->header.len4 = 0x00;
			rmsg->header.len5 = 0x00;
			remain_len = 0;
		} else if (remain_len <= 767) {
			len1 = 512;
			len2 = remain_len - 512;
			rmsg->header.len0 = 0x02;
			rmsg->header.len1 = 0xff;
			rmsg->header.len2 = 0xff;
			rmsg->header.len3 = remain_len - 512;
			rmsg->header.len4 = 0x00;
			rmsg->header.len5 = 0x00;
			remain_len = 0;
		} else if (remain_len <= 1022) {
			len1 = 512;
			len2 = remain_len - 512;
			rmsg->header.len0 = 0x02;
			rmsg->header.len1 = 0xff;
			rmsg->header.len2 = 0xff;
			rmsg->header.len3 = remain_len - 767;
			rmsg->header.len4 = 0xff;
			rmsg->header.len5 = 0x00;
			remain_len = 0;
		} else if (remain_len <= 1024) {
			len1 = 512;
			len2 = remain_len - 512;
			rmsg->header.len0 = 0x02;
			rmsg->header.len1 = 0xff;
			rmsg->header.len2 = 0xff;
			rmsg->header.len3 = remain_len - 1022;
			rmsg->header.len4 = 0xff;
			rmsg->header.len5 = 0xff;
			remain_len = 0;
		} else {
			len1 = 512;
			len2 = 512;
			rmsg->header.len0 = 0x02;
			rmsg->header.len1 = 0xff;
			rmsg->header.len2 = 0xff;
			rmsg->header.len3 = 0x02;
			rmsg->header.len4 = 0xff;
			rmsg->header.len5 = 0xff;
			remain_len -= 1024;
			start += 1024;
		}
		rmsg->header.tf1 = cpu_to_le16(len1);
		rmsg->header.tf2 = cpu_to_le16(len2);

		/* first read transfer */
		ret = vprbrd_i2c_receive(vb->usb_dev, rmsg, len1);
		if (ret < 0)
			return ret;
		/* copy the received data */
		memcpy(msg->buf + start, rmsg, len1);

		/* second read transfer if neccessary */
		if (len2 > 0) {
			ret = vprbrd_i2c_receive(vb->usb_dev, rmsg, len2);
			if (ret < 0)
				return ret;
			/* copy the received data */
			memcpy(msg->buf + start + 512, rmsg, len2);
		}
	}
	return 0;
}

static int vprbrd_i2c_write(struct vprbrd *vb, struct i2c_msg *msg)
{
	int ret, bytes_actual;
	u16 remain_len, bytes_xfer,
		start = 0x0000;
	struct vprbrd_i2c_write_msg *wmsg =
		(struct vprbrd_i2c_write_msg *)vb->buf;

	remain_len = msg->len;
	wmsg->header.cmd = VPRBRD_I2C_CMD_WRITE;
	wmsg->header.last = 0x00;
	wmsg->header.chan = 0x00;
	wmsg->header.spi = 0x0000;
	while (remain_len > 0) {
		wmsg->header.addr = cpu_to_le16(start + 0x4000);
		if (remain_len > 503) {
			wmsg->header.len1 = 0xff;
			wmsg->header.len2 = 0xf8;
			remain_len -= 503;
			bytes_xfer = 503 + sizeof(struct vprbrd_i2c_write_hdr);
			start += 503;
		} else if (remain_len > 255) {
			wmsg->header.len1 = 0xff;
			wmsg->header.len2 = (remain_len - 255);
			bytes_xfer = remain_len +
				sizeof(struct vprbrd_i2c_write_hdr);
			remain_len = 0;
		} else {
			wmsg->header.len1 = remain_len;
			wmsg->header.len2 = 0x00;
			bytes_xfer = remain_len +
				sizeof(struct vprbrd_i2c_write_hdr);
			remain_len = 0;
		}
		memcpy(wmsg->data, msg->buf + start,
			bytes_xfer - sizeof(struct vprbrd_i2c_write_hdr));

		ret = usb_bulk_msg(vb->usb_dev,
			usb_sndbulkpipe(vb->usb_dev,
			VPRBRD_EP_OUT), wmsg,
			bytes_xfer, &bytes_actual, VPRBRD_USB_TIMEOUT_MS);
		if ((ret < 0) || (bytes_xfer != bytes_actual))
			return -EREMOTEIO;
	}
	return 0;
}

static int vprbrd_i2c_xfer(struct i2c_adapter *i2c, struct i2c_msg *msgs,
		int num)
{
	struct i2c_msg *pmsg;
	int i, ret,
		error = 0;
	struct vprbrd *vb = (struct vprbrd *)i2c->algo_data;
	struct vprbrd_i2c_addr_msg *amsg =
		(struct vprbrd_i2c_addr_msg *)vb->buf;
	struct vprbrd_i2c_status *smsg = (struct vprbrd_i2c_status *)vb->buf;

	dev_dbg(&i2c->dev, "master xfer %d messages:\n", num);

	for (i = 0 ; i < num ; i++) {
		pmsg = &msgs[i];

		dev_dbg(&i2c->dev,
			"  %d: %s (flags %d) %d bytes to 0x%02x\n",
			i, pmsg->flags & I2C_M_RD ? "read" : "write",
			pmsg->flags, pmsg->len, pmsg->addr);

		mutex_lock(&vb->lock);
		/* directly send the message */
		if (pmsg->flags & I2C_M_RD) {
			/* read data */
			amsg->cmd = VPRBRD_I2C_CMD_ADDR;
			amsg->unknown2 = 0x00;
			amsg->unknown3 = 0x00;
			amsg->addr = pmsg->addr;
			amsg->unknown1 = 0x01;
			amsg->len = cpu_to_le16(pmsg->len);
			/* send the addr and len, we're interested to board */
			ret = vprbrd_i2c_addr(vb->usb_dev, amsg);
			if (ret < 0)
				error = ret;

			ret = vprbrd_i2c_read(vb, pmsg);
			if (ret < 0)
				error = ret;

			ret = vprbrd_i2c_status(i2c, smsg, error);
			if (ret < 0)
				error = ret;
			/* in case of protocol error, return the error */
			if (error < 0)
				goto error;
		} else {
			/* write data */
			ret = vprbrd_i2c_write(vb, pmsg);

			amsg->cmd = VPRBRD_I2C_CMD_ADDR;
			amsg->unknown2 = 0x00;
			amsg->unknown3 = 0x00;
			amsg->addr = pmsg->addr;
			amsg->unknown1 = 0x00;
			amsg->len = cpu_to_le16(pmsg->len);
			/* send the addr, the data goes to to board */
			ret = vprbrd_i2c_addr(vb->usb_dev, amsg);
			if (ret < 0)
				error = ret;

			ret = vprbrd_i2c_status(i2c, smsg, error);
			if (ret < 0)
				error = ret;

			if (error < 0)
				goto error;
		}
		mutex_unlock(&vb->lock);
	}
	return 0;
error:
	mutex_unlock(&vb->lock);
	return error;
}

static u32 vprbrd_i2c_func(struct i2c_adapter *i2c)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

/* This is the actual algorithm we define */
static const struct i2c_algorithm vprbrd_algorithm = {
	.master_xfer	= vprbrd_i2c_xfer,
	.functionality	= vprbrd_i2c_func,
};

static struct i2c_adapter_quirks vprbrd_quirks = {
	.max_read_len = 2048,
	.max_write_len = 2048,
};

static int vprbrd_i2c_probe(struct platform_device *pdev)
{
	struct vprbrd *vb = dev_get_drvdata(pdev->dev.parent);
	struct vprbrd_i2c *vb_i2c;
	int ret;
	int pipe;

	vb_i2c = devm_kzalloc(&pdev->dev, sizeof(*vb_i2c), GFP_KERNEL);
	if (vb_i2c == NULL)
		return -ENOMEM;

	/* setup i2c adapter description */
	vb_i2c->i2c.owner = THIS_MODULE;
	vb_i2c->i2c.class = I2C_CLASS_HWMON;
	vb_i2c->i2c.algo = &vprbrd_algorithm;
	vb_i2c->i2c.quirks = &vprbrd_quirks;
	vb_i2c->i2c.algo_data = vb;
	/* save the param in usb capabable memory */
	vb_i2c->bus_freq_param = i2c_bus_param;

	snprintf(vb_i2c->i2c.name, sizeof(vb_i2c->i2c.name),
		 "viperboard at bus %03d device %03d",
		 vb->usb_dev->bus->busnum, vb->usb_dev->devnum);

	/* setting the bus frequency */
	if ((i2c_bus_param <= VPRBRD_I2C_FREQ_10KHZ)
		&& (i2c_bus_param >= VPRBRD_I2C_FREQ_6MHZ)) {
		pipe = usb_sndctrlpipe(vb->usb_dev, 0);
		ret = usb_control_msg(vb->usb_dev, pipe,
			VPRBRD_USB_REQUEST_I2C_FREQ, VPRBRD_USB_TYPE_OUT,
			0x0000, 0x0000, &vb_i2c->bus_freq_param, 1,
			VPRBRD_USB_TIMEOUT_MS);
		if (ret != 1) {
			dev_err(&pdev->dev, "failure setting i2c_bus_freq to %d\n",
				i2c_bus_freq);
			return -EIO;
		}
	} else {
		dev_err(&pdev->dev,
			"invalid i2c_bus_freq setting:%d\n", i2c_bus_freq);
		return -EIO;
	}

	vb_i2c->i2c.dev.parent = &pdev->dev;

	/* attach to i2c layer */
	i2c_add_adapter(&vb_i2c->i2c);

	platform_set_drvdata(pdev, vb_i2c);

	return 0;
}

static int vprbrd_i2c_remove(struct platform_device *pdev)
{
	struct vprbrd_i2c *vb_i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&vb_i2c->i2c);

	return 0;
}

static struct platform_driver vprbrd_i2c_driver = {
	.driver.name	= "viperboard-i2c",
	.driver.owner	= THIS_MODULE,
	.probe		= vprbrd_i2c_probe,
	.remove		= vprbrd_i2c_remove,
};

static int __init vprbrd_i2c_init(void)
{
	switch (i2c_bus_freq) {
	case 6000:
		i2c_bus_param = VPRBRD_I2C_FREQ_6MHZ;
		break;
	case 3000:
		i2c_bus_param = VPRBRD_I2C_FREQ_3MHZ;
		break;
	case 1000:
		i2c_bus_param = VPRBRD_I2C_FREQ_1MHZ;
		break;
	case 400:
		i2c_bus_param = VPRBRD_I2C_FREQ_400KHZ;
		break;
	case 200:
		i2c_bus_param = VPRBRD_I2C_FREQ_200KHZ;
		break;
	case 100:
		i2c_bus_param = VPRBRD_I2C_FREQ_100KHZ;
		break;
	case 10:
		i2c_bus_param = VPRBRD_I2C_FREQ_10KHZ;
		break;
	default:
		pr_warn("invalid i2c_bus_freq (%d)\n", i2c_bus_freq);
		i2c_bus_param = VPRBRD_I2C_FREQ_100KHZ;
	}

	return platform_driver_register(&vprbrd_i2c_driver);
}
subsys_initcall(vprbrd_i2c_init);

static void __exit vprbrd_i2c_exit(void)
{
	platform_driver_unregister(&vprbrd_i2c_driver);
}
module_exit(vprbrd_i2c_exit);

MODULE_AUTHOR("Lars Poeschel <poeschel@lemonage.de>");
MODULE_DESCRIPTION("I2C master driver for Nano River Techs Viperboard");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:viperboard-i2c");
