/*
 *  indycam.c - Silicon Graphics IndyCam digital camera driver
 *
 *  Copyright (C) 2003 Ladislav Michl <ladis@linux-mips.org>
 *  Copyright (C) 2004,2005 Mikael Nousiainen <tmnousia@cc.hut.fi>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>

/* IndyCam decodes stream of photons into digital image representation ;-) */
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <media/v4l2-common.h>
#include <media/v4l2-i2c-drv-legacy.h>

#include "indycam.h"

#define INDYCAM_MODULE_VERSION "0.0.5"

MODULE_DESCRIPTION("SGI IndyCam driver");
MODULE_VERSION(INDYCAM_MODULE_VERSION);
MODULE_AUTHOR("Mikael Nousiainen <tmnousia@cc.hut.fi>");
MODULE_LICENSE("GPL");

static unsigned short normal_i2c[] = { 0x56 >> 1, I2C_CLIENT_END };

I2C_CLIENT_INSMOD;

// #define INDYCAM_DEBUG

#ifdef INDYCAM_DEBUG
#define dprintk(x...) printk("IndyCam: " x);
#define indycam_regdump(client) indycam_regdump_debug(client)
#else
#define dprintk(x...)
#define indycam_regdump(client)
#endif

struct indycam {
	struct i2c_client *client;
	u8 version;
};

static const u8 initseq[] = {
	INDYCAM_CONTROL_AGCENA,		/* INDYCAM_CONTROL */
	INDYCAM_SHUTTER_60,		/* INDYCAM_SHUTTER */
	INDYCAM_GAIN_DEFAULT,		/* INDYCAM_GAIN */
	0x00,				/* INDYCAM_BRIGHTNESS (read-only) */
	INDYCAM_RED_BALANCE_DEFAULT,	/* INDYCAM_RED_BALANCE */
	INDYCAM_BLUE_BALANCE_DEFAULT,	/* INDYCAM_BLUE_BALANCE */
	INDYCAM_RED_SATURATION_DEFAULT,	/* INDYCAM_RED_SATURATION */
	INDYCAM_BLUE_SATURATION_DEFAULT,/* INDYCAM_BLUE_SATURATION */
};

/* IndyCam register handling */

static int indycam_read_reg(struct i2c_client *client, u8 reg, u8 *value)
{
	int ret;

	if (reg == INDYCAM_REG_RESET) {
		dprintk("indycam_read_reg(): "
			"skipping write-only register %d\n", reg);
		*value = 0;
		return 0;
	}

	ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0) {
		printk(KERN_ERR "IndyCam: indycam_read_reg(): read failed, "
		       "register = 0x%02x\n", reg);
		return ret;
	}

	*value = (u8)ret;

	return 0;
}

static int indycam_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int err;

	if ((reg == INDYCAM_REG_BRIGHTNESS)
	    || (reg == INDYCAM_REG_VERSION)) {
		dprintk("indycam_write_reg(): "
			"skipping read-only register %d\n", reg);
		return 0;
	}

	dprintk("Writing Reg %d = 0x%02x\n", reg, value);
	err = i2c_smbus_write_byte_data(client, reg, value);

	if (err) {
		printk(KERN_ERR "IndyCam: indycam_write_reg(): write failed, "
		       "register = 0x%02x, value = 0x%02x\n", reg, value);
	}
	return err;
}

static int indycam_write_block(struct i2c_client *client, u8 reg,
			       u8 length, u8 *data)
{
	int i, err;

	for (i = 0; i < length; i++) {
		err = indycam_write_reg(client, reg + i, data[i]);
		if (err)
			return err;
	}

	return 0;
}

/* Helper functions */

#ifdef INDYCAM_DEBUG
static void indycam_regdump_debug(struct i2c_client *client)
{
	int i;
	u8 val;

	for (i = 0; i < 9; i++) {
		indycam_read_reg(client, i, &val);
		dprintk("Reg %d = 0x%02x\n", i, val);
	}
}
#endif

static int indycam_get_control(struct i2c_client *client,
			       struct v4l2_control *ctrl)
{
	struct indycam *camera = i2c_get_clientdata(client);
	u8 reg;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = indycam_read_reg(client, INDYCAM_REG_CONTROL, &reg);
		if (ret)
			return -EIO;
		if (ctrl->id == V4L2_CID_AUTOGAIN)
			ctrl->value = (reg & INDYCAM_CONTROL_AGCENA)
				? 1 : 0;
		else
			ctrl->value = (reg & INDYCAM_CONTROL_AWBCTL)
				? 1 : 0;
		break;
	case V4L2_CID_EXPOSURE:
		ret = indycam_read_reg(client, INDYCAM_REG_SHUTTER, &reg);
		if (ret)
			return -EIO;
		ctrl->value = ((s32)reg == 0x00) ? 0xff : ((s32)reg - 1);
		break;
	case V4L2_CID_GAIN:
		ret = indycam_read_reg(client, INDYCAM_REG_GAIN, &reg);
		if (ret)
			return -EIO;
		ctrl->value = (s32)reg;
		break;
	case V4L2_CID_RED_BALANCE:
		ret = indycam_read_reg(client, INDYCAM_REG_RED_BALANCE, &reg);
		if (ret)
			return -EIO;
		ctrl->value = (s32)reg;
		break;
	case V4L2_CID_BLUE_BALANCE:
		ret = indycam_read_reg(client, INDYCAM_REG_BLUE_BALANCE, &reg);
		if (ret)
			return -EIO;
		ctrl->value = (s32)reg;
		break;
	case INDYCAM_CONTROL_RED_SATURATION:
		ret = indycam_read_reg(client,
				       INDYCAM_REG_RED_SATURATION, &reg);
		if (ret)
			return -EIO;
		ctrl->value = (s32)reg;
		break;
	case INDYCAM_CONTROL_BLUE_SATURATION:
		ret = indycam_read_reg(client,
				       INDYCAM_REG_BLUE_SATURATION, &reg);
		if (ret)
			return -EIO;
		ctrl->value = (s32)reg;
		break;
	case V4L2_CID_GAMMA:
		if (camera->version == CAMERA_VERSION_MOOSE) {
			ret = indycam_read_reg(client,
					       INDYCAM_REG_GAMMA, &reg);
			if (ret)
				return -EIO;
			ctrl->value = (s32)reg;
		} else {
			ctrl->value = INDYCAM_GAMMA_DEFAULT;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int indycam_set_control(struct i2c_client *client,
			       struct v4l2_control *ctrl)
{
	struct indycam *camera = i2c_get_clientdata(client);
	u8 reg;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ret = indycam_read_reg(client, INDYCAM_REG_CONTROL, &reg);
		if (ret)
			break;

		if (ctrl->id == V4L2_CID_AUTOGAIN) {
			if (ctrl->value)
				reg |= INDYCAM_CONTROL_AGCENA;
			else
				reg &= ~INDYCAM_CONTROL_AGCENA;
		} else {
			if (ctrl->value)
				reg |= INDYCAM_CONTROL_AWBCTL;
			else
				reg &= ~INDYCAM_CONTROL_AWBCTL;
		}

		ret = indycam_write_reg(client, INDYCAM_REG_CONTROL, reg);
		break;
	case V4L2_CID_EXPOSURE:
		reg = (ctrl->value == 0xff) ? 0x00 : (ctrl->value + 1);
		ret = indycam_write_reg(client, INDYCAM_REG_SHUTTER, reg);
		break;
	case V4L2_CID_GAIN:
		ret = indycam_write_reg(client, INDYCAM_REG_GAIN, ctrl->value);
		break;
	case V4L2_CID_RED_BALANCE:
		ret = indycam_write_reg(client, INDYCAM_REG_RED_BALANCE,
					ctrl->value);
		break;
	case V4L2_CID_BLUE_BALANCE:
		ret = indycam_write_reg(client, INDYCAM_REG_BLUE_BALANCE,
					ctrl->value);
		break;
	case INDYCAM_CONTROL_RED_SATURATION:
		ret = indycam_write_reg(client, INDYCAM_REG_RED_SATURATION,
					ctrl->value);
		break;
	case INDYCAM_CONTROL_BLUE_SATURATION:
		ret = indycam_write_reg(client, INDYCAM_REG_BLUE_SATURATION,
					ctrl->value);
		break;
	case V4L2_CID_GAMMA:
		if (camera->version == CAMERA_VERSION_MOOSE) {
			ret = indycam_write_reg(client, INDYCAM_REG_GAMMA,
						ctrl->value);
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/* I2C-interface */

static int indycam_command(struct i2c_client *client, unsigned int cmd,
			   void *arg)
{
	/* The old video_decoder interface just isn't enough,
	 * so we'll use some custom commands. */
	switch (cmd) {
	case VIDIOC_G_CTRL:
		return indycam_get_control(client, arg);

	case VIDIOC_S_CTRL:
		return indycam_set_control(client, arg);

	default:
		return -EINVAL;
	}

	return 0;
}

static int indycam_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int err = 0;
	struct indycam *camera;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	camera = kzalloc(sizeof(struct indycam), GFP_KERNEL);
	if (!camera)
		return -ENOMEM;

	i2c_set_clientdata(client, camera);

	camera->client = client;

	camera->version = i2c_smbus_read_byte_data(client,
						   INDYCAM_REG_VERSION);
	if (camera->version != CAMERA_VERSION_INDY &&
	    camera->version != CAMERA_VERSION_MOOSE) {
		kfree(camera);
		return -ENODEV;
	}

	printk(KERN_INFO "IndyCam v%d.%d detected\n",
	       INDYCAM_VERSION_MAJOR(camera->version),
	       INDYCAM_VERSION_MINOR(camera->version));

	indycam_regdump(client);

	// initialize
	err = indycam_write_block(client, 0, sizeof(initseq), (u8 *)&initseq);
	if (err) {
		printk(KERN_ERR "IndyCam initialization failed\n");
		kfree(camera);
		return -EIO;
	}

	indycam_regdump(client);

	// white balance
	err = indycam_write_reg(client, INDYCAM_REG_CONTROL,
			  INDYCAM_CONTROL_AGCENA | INDYCAM_CONTROL_AWBCTL);
	if (err) {
		printk(KERN_ERR "IndyCam: White balancing camera failed\n");
		kfree(camera);
		return -EIO;
	}

	indycam_regdump(client);

	printk(KERN_INFO "IndyCam initialized\n");

	return 0;
}

static int indycam_remove(struct i2c_client *client)
{
	struct indycam *camera = i2c_get_clientdata(client);

	kfree(camera);
	return 0;
}

static int indycam_legacy_probe(struct i2c_adapter *adapter)
{
	return adapter->id == I2C_HW_SGI_VINO;
}

static const struct i2c_device_id indycam_id[] = {
	{ "indycam", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, indycam_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "indycam",
	.driverid = I2C_DRIVERID_INDYCAM,
	.command = indycam_command,
	.probe = indycam_probe,
	.remove = indycam_remove,
	.legacy_probe = indycam_legacy_probe,
	.id_table = indycam_id,
};
