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
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/videodev.h>
/* IndyCam decodes stream of photons into digital image representation ;-) */
#include <linux/video_decoder.h>
#include <linux/i2c.h>

#include "indycam.h"

//#define INDYCAM_DEBUG

#define INDYCAM_MODULE_VERSION "0.0.3"

MODULE_DESCRIPTION("SGI IndyCam driver");
MODULE_VERSION(INDYCAM_MODULE_VERSION);
MODULE_AUTHOR("Mikael Nousiainen <tmnousia@cc.hut.fi>");
MODULE_LICENSE("GPL");

#ifdef INDYCAM_DEBUG
#define dprintk(x...) printk("IndyCam: " x);
#define indycam_regdump(client) indycam_regdump_debug(client)
#else
#define dprintk(x...)
#define indycam_regdump(client)
#endif

struct indycam {
	struct i2c_client *client;
	int version;
};

static struct i2c_driver i2c_driver_indycam;

static const unsigned char initseq[] = {
	INDYCAM_CONTROL_AGCENA,		/* INDYCAM_CONTROL */
	INDYCAM_SHUTTER_DEFAULT,	/* INDYCAM_SHUTTER */
	INDYCAM_GAIN_DEFAULT,		/* INDYCAM_GAIN */
	0x00,				/* INDYCAM_BRIGHTNESS (read-only) */
	INDYCAM_RED_BALANCE_DEFAULT,	/* INDYCAM_RED_BALANCE */
	INDYCAM_BLUE_BALANCE_DEFAULT,	/* INDYCAM_BLUE_BALANCE */
	INDYCAM_RED_SATURATION_DEFAULT,	/* INDYCAM_RED_SATURATION */
	INDYCAM_BLUE_SATURATION_DEFAULT,/* INDYCAM_BLUE_SATURATION */
};

/* IndyCam register handling */

static int indycam_read_reg(struct i2c_client *client, unsigned char reg,
			     unsigned char *value)
{
	int ret;

	if (reg == INDYCAM_RESET) {
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

	*value = (unsigned char)ret;

	return 0;
}

static int indycam_write_reg(struct i2c_client *client, unsigned char reg,
			     unsigned char value)
{
	int err;

	if ((reg == INDYCAM_BRIGHTNESS)
	    || (reg == INDYCAM_VERSION)) {
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

static int indycam_write_block(struct i2c_client *client, unsigned char reg,
				unsigned char length, unsigned char *data)
{
	unsigned char i;
	int err;

	for (i = reg; i < length; i++) {
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
	unsigned char val;

	for (i = 0; i < 9; i++) {
		indycam_read_reg(client, i, &val);
		dprintk("Reg %d = 0x%02x\n", i, val);
	}
}
#endif

static int indycam_get_controls(struct i2c_client *client,
				struct indycam_control *ctrl)
{
	unsigned char ctrl_reg;

	indycam_read_reg(client, INDYCAM_CONTROL, &ctrl_reg);
	ctrl->agc = (ctrl_reg & INDYCAM_CONTROL_AGCENA)
		? INDYCAM_VALUE_ENABLED
		: INDYCAM_VALUE_DISABLED;
	ctrl->awb = (ctrl_reg & INDYCAM_CONTROL_AWBCTL)
		? INDYCAM_VALUE_ENABLED
		: INDYCAM_VALUE_DISABLED;
	indycam_read_reg(client, INDYCAM_SHUTTER,
			 (unsigned char *)&ctrl->shutter);
	indycam_read_reg(client, INDYCAM_GAIN,
			 (unsigned char *)&ctrl->gain);
	indycam_read_reg(client, INDYCAM_RED_BALANCE,
			 (unsigned char *)&ctrl->red_balance);
	indycam_read_reg(client, INDYCAM_BLUE_BALANCE,
			 (unsigned char *)&ctrl->blue_balance);
	indycam_read_reg(client, INDYCAM_RED_SATURATION,
			 (unsigned char *)&ctrl->red_saturation);
	indycam_read_reg(client, INDYCAM_BLUE_SATURATION,
			 (unsigned char *)&ctrl->blue_saturation);
	indycam_read_reg(client, INDYCAM_GAMMA,
			 (unsigned char *)&ctrl->gamma);

	return 0;
}

static int indycam_set_controls(struct i2c_client *client,
				struct indycam_control *ctrl)
{
	unsigned char ctrl_reg;

	indycam_read_reg(client, INDYCAM_CONTROL, &ctrl_reg);
	if (ctrl->agc != INDYCAM_VALUE_UNCHANGED) {
		if (ctrl->agc)
			ctrl_reg |= INDYCAM_CONTROL_AGCENA;
		else
			ctrl_reg &= ~INDYCAM_CONTROL_AGCENA;
	}
	if (ctrl->awb != INDYCAM_VALUE_UNCHANGED) {
		if (ctrl->awb)
			ctrl_reg |= INDYCAM_CONTROL_AWBCTL;
		else
			ctrl_reg &= ~INDYCAM_CONTROL_AWBCTL;
	}
	indycam_write_reg(client, INDYCAM_CONTROL, ctrl_reg);

	if (ctrl->shutter >= 0)
		indycam_write_reg(client, INDYCAM_SHUTTER, ctrl->shutter);
	if (ctrl->gain >= 0)
		indycam_write_reg(client, INDYCAM_GAIN, ctrl->gain);
	if (ctrl->red_balance >= 0)
		indycam_write_reg(client, INDYCAM_RED_BALANCE,
				  ctrl->red_balance);
	if (ctrl->blue_balance >= 0)
		indycam_write_reg(client, INDYCAM_BLUE_BALANCE,
				  ctrl->blue_balance);
	if (ctrl->red_saturation >= 0)
		indycam_write_reg(client, INDYCAM_RED_SATURATION,
				  ctrl->red_saturation);
	if (ctrl->blue_saturation >= 0)
		indycam_write_reg(client, INDYCAM_BLUE_SATURATION,
				  ctrl->blue_saturation);
	if (ctrl->gamma >= 0)
		indycam_write_reg(client, INDYCAM_GAMMA, ctrl->gamma);

	return 0;
}

/* I2C-interface */

static int indycam_attach(struct i2c_adapter *adap, int addr, int kind)
{
	int err = 0;
	struct indycam *camera;
	struct i2c_client *client;

	printk(KERN_INFO "SGI IndyCam driver version %s\n",
	       INDYCAM_MODULE_VERSION);

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	camera = kmalloc(sizeof(struct indycam), GFP_KERNEL);
	if (!camera) {
		err = -ENOMEM;
		goto out_free_client;
	}

	memset(client, 0, sizeof(struct i2c_client));
	memset(camera, 0, sizeof(struct indycam));

	client->addr = addr;
	client->adapter = adap;
	client->driver = &i2c_driver_indycam;
	client->flags = 0;
	strcpy(client->name, "IndyCam client");
	i2c_set_clientdata(client, camera);

	camera->client = client;

	err = i2c_attach_client(client);
	if (err)
		goto out_free_camera;

	camera->version = i2c_smbus_read_byte_data(client, INDYCAM_VERSION);
	if (camera->version != CAMERA_VERSION_INDY &&
	    camera->version != CAMERA_VERSION_MOOSE) {
		err = -ENODEV;
		goto out_detach_client;
	}
	printk(KERN_INFO "IndyCam v%d.%d detected\n",
	       INDYCAM_VERSION_MAJOR(camera->version),
	       INDYCAM_VERSION_MINOR(camera->version));

	indycam_regdump(client);

	// initialize
	err = indycam_write_block(client, 0, sizeof(initseq),
				  (unsigned char *)&initseq);
	if (err) {
		printk(KERN_ERR "IndyCam initalization failed\n");
		err = -EIO;
		goto out_detach_client;
	}

	indycam_regdump(client);

	// white balance
	err = indycam_write_reg(client, INDYCAM_CONTROL,
			  INDYCAM_CONTROL_AGCENA | INDYCAM_CONTROL_AWBCTL);
	if (err) {
		printk(KERN_ERR "IndyCam white balance "
		       "initialization failed\n");
		err = -EIO;
		goto out_detach_client;
	}

	indycam_regdump(client);

	printk(KERN_INFO "IndyCam initialized\n");

	return 0;

out_detach_client:
	i2c_detach_client(client);
out_free_camera:
	kfree(camera);
out_free_client:
	kfree(client);
	return err;
}

static int indycam_probe(struct i2c_adapter *adap)
{
	/* Indy specific crap */
	if (adap->id == I2C_HW_SGI_VINO)
		return indycam_attach(adap, INDYCAM_ADDR, 0);
	/* Feel free to add probe here :-) */
	return -ENODEV;
}

static int indycam_detach(struct i2c_client *client)
{
	struct indycam *camera = i2c_get_clientdata(client);

	i2c_detach_client(client);
	kfree(camera);
	kfree(client);
	return 0;
}

static int indycam_command(struct i2c_client *client, unsigned int cmd,
			   void *arg)
{
	// struct indycam *camera = i2c_get_clientdata(client);

	/* The old video_decoder interface just isn't enough,
	 * so we'll use some custom commands. */
	switch (cmd) {
	case DECODER_GET_CAPABILITIES: {
		struct video_decoder_capability *cap = arg;

		cap->flags  = VIDEO_DECODER_NTSC;
		cap->inputs = 1;
		cap->outputs = 1;
		break;
	}
	case DECODER_GET_STATUS: {
		int *iarg = arg;

		*iarg = DECODER_STATUS_GOOD | DECODER_STATUS_NTSC |
			DECODER_STATUS_COLOR;
		break;
	}
	case DECODER_SET_NORM: {
		int *iarg = arg;

		switch (*iarg) {
		case VIDEO_MODE_NTSC:
			break;
		default:
			return -EINVAL;
		}
		break;
	}
	case DECODER_SET_INPUT:	{
		int *iarg = arg;

		if (*iarg != 0)
			return -EINVAL;
		break;
	}
	case DECODER_SET_OUTPUT: {
		int *iarg = arg;

		if (*iarg != 0)
			return -EINVAL;
		break;
	}
	case DECODER_ENABLE_OUTPUT: {
		/* Always enabled */
		break;
	}
	case DECODER_SET_PICTURE: {
		// struct video_picture *pic = arg;
		/* TODO: convert values for indycam_set_controls() */
		break;
	}
	case DECODER_INDYCAM_GET_CONTROLS: {
		struct indycam_control *ctrl = arg;
		indycam_get_controls(client, ctrl);
	}
	case DECODER_INDYCAM_SET_CONTROLS: {
		struct indycam_control *ctrl = arg;
		indycam_set_controls(client, ctrl);
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static struct i2c_driver i2c_driver_indycam = {
	.owner		= THIS_MODULE,
	.name 		= "indycam",
	.id 		= I2C_DRIVERID_INDYCAM,
	.flags 		= I2C_DF_NOTIFY,
	.attach_adapter = indycam_probe,
	.detach_client 	= indycam_detach,
	.command 	= indycam_command,
};

static int __init indycam_init(void)
{
	return i2c_add_driver(&i2c_driver_indycam);
}

static void __exit indycam_exit(void)
{
	i2c_del_driver(&i2c_driver_indycam);
}

module_init(indycam_init);
module_exit(indycam_exit);
