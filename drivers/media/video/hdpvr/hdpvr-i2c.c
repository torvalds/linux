
/*
 * Hauppauge HD PVR USB driver
 *
 * Copyright (C) 2008      Janne Grunau (j@jannau.net)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/i2c.h>
#include <linux/slab.h>

#include "hdpvr.h"

#define CTRL_READ_REQUEST	0xb8
#define CTRL_WRITE_REQUEST	0x38

#define REQTYPE_I2C_READ	0xb1
#define REQTYPE_I2C_WRITE	0xb0
#define REQTYPE_I2C_WRITE_STATT	0xd0

static int hdpvr_i2c_read(struct hdpvr_device *dev, unsigned char addr,
			  char *data, int len)
{
	int ret;
	char *buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = usb_control_msg(dev->udev,
			      usb_rcvctrlpipe(dev->udev, 0),
			      REQTYPE_I2C_READ, CTRL_READ_REQUEST,
			      0x100|addr, 0, buf, len, 1000);

	if (ret == len) {
		memcpy(data, buf, len);
		ret = 0;
	} else if (ret >= 0)
		ret = -EIO;

	kfree(buf);

	return ret;
}

static int hdpvr_i2c_write(struct hdpvr_device *dev, unsigned char addr,
			   char *data, int len)
{
	int ret;
	char *buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, data, len);
	ret = usb_control_msg(dev->udev,
			      usb_sndctrlpipe(dev->udev, 0),
			      REQTYPE_I2C_WRITE, CTRL_WRITE_REQUEST,
			      0x100|addr, 0, buf, len, 1000);

	if (ret < 0)
		goto error;

	ret = usb_control_msg(dev->udev,
			      usb_rcvctrlpipe(dev->udev, 0),
			      REQTYPE_I2C_WRITE_STATT, CTRL_READ_REQUEST,
			      0, 0, buf, 2, 1000);

	if (ret == 2)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

error:
	kfree(buf);
	return ret;
}

static int hdpvr_transfer(struct i2c_adapter *i2c_adapter, struct i2c_msg *msgs,
			  int num)
{
	struct hdpvr_device *dev = i2c_get_adapdata(i2c_adapter);
	int retval = 0, i, addr;

	if (num <= 0)
		return 0;

	mutex_lock(&dev->i2c_mutex);

	for (i = 0; i < num && !retval; i++) {
		addr = msgs[i].addr << 1;

		if (msgs[i].flags & I2C_M_RD)
			retval = hdpvr_i2c_read(dev, addr, msgs[i].buf,
						msgs[i].len);
		else
			retval = hdpvr_i2c_write(dev, addr, msgs[i].buf,
						 msgs[i].len);
	}

	mutex_unlock(&dev->i2c_mutex);

	return retval ? retval : num;
}

static u32 hdpvr_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm hdpvr_algo = {
	.master_xfer   = hdpvr_transfer,
	.functionality = hdpvr_functionality,
};

int hdpvr_register_i2c_adapter(struct hdpvr_device *dev)
{
	struct i2c_adapter *i2c_adap;
	int retval = -ENOMEM;

	i2c_adap = kzalloc(sizeof(struct i2c_adapter), GFP_KERNEL);
	if (i2c_adap == NULL)
		goto error;

	strlcpy(i2c_adap->name, "Hauppauge HD PVR I2C",
		sizeof(i2c_adap->name));
	i2c_adap->algo  = &hdpvr_algo;
	i2c_adap->owner = THIS_MODULE;
	i2c_adap->dev.parent = &dev->udev->dev;

	i2c_set_adapdata(i2c_adap, dev);

	retval = i2c_add_adapter(i2c_adap);

	if (!retval)
		dev->i2c_adapter = i2c_adap;
	else
		kfree(i2c_adap);

error:
	return retval;
}
