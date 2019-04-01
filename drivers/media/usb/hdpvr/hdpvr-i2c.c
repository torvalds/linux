
/*
 * Hauppauge HD PVR USB driver
 *
 * Copyright (C) 2008      Janne Grunau (j@jannau.net)
 *
 * IR device registration code is
 * Copyright (C) 2010	Andy Walls <awalls@md.metrocast.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#if IS_ENABLED(CONFIG_I2C)

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/export.h>

#include "hdpvr.h"

#define CTRL_READ_REQUEST	0xb8
#define CTRL_WRITE_REQUEST	0x38

#define REQTYPE_I2C_READ	0xb1
#define REQTYPE_I2C_WRITE	0xb0
#define REQTYPE_I2C_WRITE_STATT	0xd0

#define Z8F0811_IR_TX_I2C_ADDR	0x70
#define Z8F0811_IR_RX_I2C_ADDR	0x71


struct i2c_client *hdpvr_register_ir_i2c(struct hdpvr_device *dev)
{
	struct IR_i2c_init_data *init_data = &dev->ir_i2c_init_data;
	struct i2c_board_info info = {
		I2C_BOARD_INFO("ir_z8f0811_hdpvr", Z8F0811_IR_RX_I2C_ADDR),
	};

	/* Our default information for ir-kbd-i2c.c to use */
	init_data->ir_codes = RC_MAP_HAUPPAUGE;
	init_data->internal_get_key_func = IR_KBD_GET_KEY_HAUP_XVR;
	init_data->type = RC_PROTO_BIT_RC5 | RC_PROTO_BIT_RC6_MCE |
			  RC_PROTO_BIT_RC6_6A_32;
	init_data->name = "HD-PVR";
	init_data->polling_interval = 405; /* ms, duplicated from Windows */
	info.platform_data = init_data;

	return i2c_new_device(&dev->i2c_adapter, &info);
}

static int hdpvr_i2c_read(struct hdpvr_device *dev, int bus,
			  unsigned char addr, char *wdata, int wlen,
			  char *data, int len)
{
	int ret;

	if ((len > sizeof(dev->i2c_buf)) || (wlen > sizeof(dev->i2c_buf)))
		return -EINVAL;

	if (wlen) {
		memcpy(dev->i2c_buf, wdata, wlen);
		ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
				      REQTYPE_I2C_WRITE, CTRL_WRITE_REQUEST,
				      (bus << 8) | addr, 0, dev->i2c_buf,
				      wlen, 1000);
		if (ret < 0)
			return ret;
	}

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
			      REQTYPE_I2C_READ, CTRL_READ_REQUEST,
			      (bus << 8) | addr, 0, dev->i2c_buf, len, 1000);

	if (ret == len) {
		memcpy(data, dev->i2c_buf, len);
		ret = 0;
	} else if (ret >= 0)
		ret = -EIO;

	return ret;
}

static int hdpvr_i2c_write(struct hdpvr_device *dev, int bus,
			   unsigned char addr, char *data, int len)
{
	int ret;

	if (len > sizeof(dev->i2c_buf))
		return -EINVAL;

	memcpy(dev->i2c_buf, data, len);
	ret = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
			      REQTYPE_I2C_WRITE, CTRL_WRITE_REQUEST,
			      (bus << 8) | addr, 0, dev->i2c_buf, len, 1000);

	if (ret < 0)
		return ret;

	ret = usb_control_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
			      REQTYPE_I2C_WRITE_STATT, CTRL_READ_REQUEST,
			      0, 0, dev->i2c_buf, 2, 1000);

	if ((ret == 2) && (dev->i2c_buf[1] == (len - 1)))
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	return ret;
}

static int hdpvr_transfer(struct i2c_adapter *i2c_adapter, struct i2c_msg *msgs,
			  int num)
{
	struct hdpvr_device *dev = i2c_get_adapdata(i2c_adapter);
	int retval = 0, addr;

	mutex_lock(&dev->i2c_mutex);

	addr = msgs[0].addr << 1;

	if (num == 1) {
		if (msgs[0].flags & I2C_M_RD)
			retval = hdpvr_i2c_read(dev, 1, addr, NULL, 0,
						msgs[0].buf, msgs[0].len);
		else
			retval = hdpvr_i2c_write(dev, 1, addr, msgs[0].buf,
						 msgs[0].len);
	} else if (num == 2) {
		if (msgs[0].addr != msgs[1].addr) {
			v4l2_warn(&dev->v4l2_dev, "refusing 2-phase i2c xfer with conflicting target addresses\n");
			retval = -EINVAL;
			goto out;
		}

		if ((msgs[0].flags & I2C_M_RD) || !(msgs[1].flags & I2C_M_RD)) {
			v4l2_warn(&dev->v4l2_dev, "refusing complex xfer with r0=%d, r1=%d\n",
				  msgs[0].flags & I2C_M_RD,
				  msgs[1].flags & I2C_M_RD);
			retval = -EINVAL;
			goto out;
		}

		/*
		 * Write followed by atomic read is the only complex xfer that
		 * we actually support here.
		 */
		retval = hdpvr_i2c_read(dev, 1, addr, msgs[0].buf, msgs[0].len,
					msgs[1].buf, msgs[1].len);
	} else {
		v4l2_warn(&dev->v4l2_dev, "refusing %d-phase i2c xfer\n", num);
	}

out:
	mutex_unlock(&dev->i2c_mutex);

	return retval ? retval : num;
}

static u32 hdpvr_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm hdpvr_algo = {
	.master_xfer   = hdpvr_transfer,
	.functionality = hdpvr_functionality,
};

static const struct i2c_adapter hdpvr_i2c_adapter_template = {
	.name   = "Hauppauge HD PVR I2C",
	.owner  = THIS_MODULE,
	.algo   = &hdpvr_algo,
};

static int hdpvr_activate_ir(struct hdpvr_device *dev)
{
	char buffer[2];

	mutex_lock(&dev->i2c_mutex);

	hdpvr_i2c_read(dev, 0, 0x54, NULL, 0, buffer, 1);

	buffer[0] = 0;
	buffer[1] = 0x8;
	hdpvr_i2c_write(dev, 1, 0x54, buffer, 2);

	buffer[1] = 0x18;
	hdpvr_i2c_write(dev, 1, 0x54, buffer, 2);

	mutex_unlock(&dev->i2c_mutex);

	return 0;
}

int hdpvr_register_i2c_adapter(struct hdpvr_device *dev)
{
	int retval = -ENOMEM;

	hdpvr_activate_ir(dev);

	dev->i2c_adapter = hdpvr_i2c_adapter_template;
	dev->i2c_adapter.dev.parent = &dev->udev->dev;

	i2c_set_adapdata(&dev->i2c_adapter, dev);

	retval = i2c_add_adapter(&dev->i2c_adapter);

	return retval;
}

#endif
